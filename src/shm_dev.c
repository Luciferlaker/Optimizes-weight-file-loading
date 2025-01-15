#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/init.h> 
#include <linux/fs.h> 
#include <linux/mm.h> 
#include <asm/uaccess.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <asm/io.h>
#include <linux/proc_fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>   
#include <linux/spinlock.h>    
#include <linux/mmu_context.h> 
#include <asm/pgtable.h>     
#include <linux/mempolicy.h>  
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/mm_types.h>
#include <linux/pgtable.h>
#include <asm/tlbflush.h>
#include <linux/delay.h>
#include <linux/ktime.h>  // For ktime_get()
#include <linux/timekeeping.h>  // For timekeeping functions

#define DEVICE_NAME "shm_dev"
#define CLASS_NAME "shmqueue_class"

static struct class*  class;
static struct device*  device;
static int major;


#define Zero_QUEUE_SIZE 1024
#define Zero_MSG_SIZE 1

#define Clean_QUEUE_SIZE 1024
#define Clean_MSG_SIZE 1

#define START_CLEAN_PTE 7890
#define START_ZERO_PAGE 7891

static DEFINE_MUTEX(shm_mutex);

wait_queue_head_t clean_quene;

wait_queue_head_t zero_quene;

// zero_page
struct zero_page_queue {
    unsigned int read_pos;
    unsigned int write_pos;
    unsigned int count;
    int pid;
    unsigned long address[Zero_MSG_SIZE * Zero_QUEUE_SIZE];
};

// clean-pte
struct clean_pte_quene{
    unsigned int read_pos;
    unsigned int write_pos;
    unsigned int count;
    int pid;
    unsigned long address[Clean_MSG_SIZE*Clean_QUEUE_SIZE];
};

static struct shm_area{
    struct zero_page_queue zero_pgae_quene;
    struct clean_pte_quene clean_pte_quene;
};

static struct shm_area *sh_mem = NULL;



//clean pte function
static void clear_pte_by_address(struct mm_struct *mm, unsigned long address)
{
    struct vm_area_struct *vma;
    pte_t *ptep;
    spinlock_t *ptl;
    
    // 1. 先获取mmap_read_lock，因为要访问VMA
    mmap_read_lock(mm);
    
    // 2. 查找address所在的VMA
    vma = find_vma(mm, address);
    if (!vma || address < vma->vm_start) {
        mmap_read_unlock(mm);
        printk("no find mmap address \n");
        return;  // 地址不在任何VMA范围内
    }
    printk("find mmap address \n");
    // 3. 通过地址获取对应的页表项指针
    ptep = pte_offset_map_lock(mm, pmd_offset(pud_offset(p4d_offset(pgd_offset(mm, address), 
              address), address), address), address, &ptl);
    if (!ptep) {
        mmap_read_unlock(mm);
        return;  // 页表项不存在
    }
    //printk("find pte valind");
    // 4. 检查PTE是否有效
    if (pte_present(*ptep)) {
            printk("clean the pte\n");
            // 使用 ptep_get_and_clear 清除页表项
            pte_t old_pte = ptep_get_and_clear(vma->vm_mm, address, ptep);
           // printk("Old pte: %lx\n", old_pte);  // 打印原始页表项
           //printk("New pte: %lx\n", *ptep);   // 打印清空后的页表项，应该是0或无效状态
             // 刷新 TLB
           //flush_tlb_page(vma, address);
           __flush_tlb_all();
    }
    if (!pte_present(*ptep)) {
             printk("pte is none\n");
     }
    
    // 6. 按照获取的相反顺序释放锁
    pte_unmap_unlock(ptep, ptl);  // 释放页表锁并解除映射
    mmap_read_unlock(mm);         // 释放mmap锁
}

static int clean_pte(void *data)
{
    while (!kthread_should_stop()) {
        printk("read_pos = %d \n",sh_mem->clean_pte_quene.read_pos);
        printk("write_pos = %d \n",sh_mem->clean_pte_quene.write_pos);
        wait_event_interruptible(clean_quene, (sh_mem->clean_pte_quene.write_pos!= sh_mem->clean_pte_quene.read_pos));
        printk("pte_clean_quene.address = %lu\n",sh_mem->clean_pte_quene.address[sh_mem->clean_pte_quene.read_pos]);
        printk("current->pid = %d \n",current->pid);
        printk("shm->pid = %d \n",sh_mem->clean_pte_quene.pid);

        struct pid *pid_struct;
        struct task_struct *task;

        // 查找 pid 结构体
        pid_struct = find_get_pid(sh_mem->clean_pte_quene.pid);

        // 使用 get_pid_task 获取 task_struct
        task = get_pid_task(pid_struct, PIDTYPE_PID);

        // 判断是否找到
        if (task != NULL) {
            printk(KERN_INFO "Found task with PID %d, name: %s\n", sh_mem->clean_pte_quene.pid, task->comm);
        } else {
            printk(KERN_INFO "No task found with PID %d\n", sh_mem->clean_pte_quene.pid);
        }
        struct mm_struct *mm = task -> mm;
        unsigned long address = sh_mem->clean_pte_quene.address[sh_mem->clean_pte_quene.read_pos];

        // Measure time taken by clear_pte_by_address
        //ktime_t start_time, end_time;
        //s64 duration_ns;

        //start_time = ktime_get();  // Record start time
        clear_pte_by_address(mm, address);
        //end_time = ktime_get();    // Record end time

        //duration_ns = ktime_to_ns(ktime_sub(end_time, start_time));  // Calculate duration in nanoseconds

        //printk(KERN_INFO "Time taken by clear_pte_by_address: %lld ns\n", duration_ns);

        sh_mem->clean_pte_quene.read_pos ++;
    }

    return 0;
}


//zero_page
int handle_zero_page(struct mm_struct *mm, unsigned long address)
{
    pgd_t *pgd;
    p4d_t *p4d;
    pud_t *pud;
    pmd_t *pmd;
    pte_t *pte;
    struct page *page;
    vm_fault_t ret = 0;
    pte_t entry;
    spinlock_t *ptl;

    /* Lock the mmap for reading */
    mmap_read_lock(mm);

    /* Find the VMA for the given address */
    struct vm_area_struct *vma = find_vma(mm, address);
    if (unlikely(!vma)) {
        printk(KERN_ERR "Cannot find VMA for address 0x%lx\n", address);
        mmap_read_unlock(mm);
        return VM_FAULT_SIGSEGV;
    }

    /* Check if the VMA allows the required operation */
    if (!(vma->vm_flags & (VM_READ | VM_WRITE))) {
        printk(KERN_ERR "VMA at address 0x%lx does not support required permissions\n", address);
        mmap_read_unlock(mm);
        return VM_FAULT_SIGSEGV;
    }

    /* Traverse or allocate page table entries */
    pgd = pgd_offset(mm, address);

    if (!pgd || pgd_none(*pgd)) {
        printk(KERN_INFO "Address 0x%lx does not have a valid PGD\n", address);
    }

    p4d = p4d_offset(pgd,address);
    if (p4d && !p4d_none(*p4d)) {
        printk(KERN_INFO "Address 0x%lx already has a valid P4D\n", address);
    } else {
        p4d = p4d_alloc(mm, pgd, address);
        if (!p4d) {
            ret = VM_FAULT_OOM;
            goto out_unlock;
        }
    }

    pud = pud_offset(p4d,address);
    if (pud && !pud_none(*pud)) {
        printk(KERN_INFO "Address 0x%lx already has a valid PUD\n", address);
    } else {
        pud = pud_alloc(mm, p4d, address);
        if (!pud) {
            ret = VM_FAULT_OOM;
            goto out_unlock;
        }
    } 

    pmd = pmd_offset(pud, address);
    if (pmd && !pmd_none(*pmd)) {
        printk(KERN_INFO "Address 0x%lx already has a valid PMD\n", address);
    } else {
        pmd = pmd_alloc(mm, pud, address);
        if (!pmd) {
            ret = VM_FAULT_OOM;
            goto out_unlock;
        }
    }


    /* Check if the address already has a PTE */
    pte = pte_offset_map(pmd, address);
    if (pte && !pte_none(*pte)) {
        printk(KERN_INFO "Address 0x%lx already has a valid PTE\n", address);
        pte_unmap(pte);
        mmap_read_unlock(mm);
        return 0;
    }

    if (pte_alloc(mm, pmd)) {
        ret = VM_FAULT_OOM;
        goto out_unlock;
    }

    /* Prepare the zero page entry */
    entry = pte_mkspecial(pfn_pte(my_zero_pfn(address), vma->vm_page_prot));

    /* Map the PTE and lock */
    pte = pte_offset_map_lock(mm, pmd, address, &ptl);
    if (!pte_none(*pte)) {
        /* Handle the case where the PTE is already populated */
        update_mmu_tlb(vma, address, pte);
        ret = 0;
        goto unlock_pte;
    }
    /* Set the PTE entry */
    set_pte_at(mm, address, pte, entry);

    /* Update MMU cache */
    update_mmu_tlb(vma, address, pte);

    ret = 0;
    printk(KERN_INFO "handle zero page successful");

unlock_pte:
    pte_unmap_unlock(pte, ptl);

out_unlock:
    mmap_read_unlock(mm);
    return ret;
}

static int zero_page(void *data)
{
    while (!kthread_should_stop()) {
        printk("read_pos = %d \n",sh_mem->zero_pgae_quene.read_pos);
        printk("read_pos = %d \n",sh_mem->zero_pgae_quene.write_pos);
        wait_event_interruptible(zero_quene, (sh_mem->zero_pgae_quene.write_pos!= sh_mem->zero_pgae_quene.read_pos));
        printk("pte_clean_quene.address = %lu\n",sh_mem->zero_pgae_quene.address[sh_mem->zero_pgae_quene.read_pos]);
        printk("current->pid = %d \n",current->pid);
        printk("shm->pid = %d \n",sh_mem->zero_pgae_quene.pid);

        struct pid *pid_struct;
        struct task_struct *task;

        // 查找 pid 结构体
        pid_struct = find_get_pid(sh_mem->zero_pgae_quene.pid);

        // 使用 get_pid_task 获取 task_struct
        task = get_pid_task(pid_struct, PIDTYPE_PID);

        // 判断是否找到
        if (task != NULL) {
            printk(KERN_INFO "Found task with PID %d, name: %s\n", sh_mem->zero_pgae_quene.pid, task->comm);
        } else {
            printk(KERN_INFO "No task found with PID %d\n", sh_mem->zero_pgae_quene.pid);
        }
        struct mm_struct *mm = task -> mm;
        unsigned long address = sh_mem->zero_pgae_quene.address[sh_mem->zero_pgae_quene.read_pos];
        handle_zero_page(mm, address);
        sh_mem->zero_pgae_quene.read_pos ++;
    }

    return 0;
}



/*  executed once the device is closed or releaseed by userspace
 *  @param inodep: pointer to struct inode
 *  @param filep: pointer to struct file 
 */
static int shm_release(struct inode *inodep, struct file *filep)
{    
    mutex_unlock(&shm_mutex);
    pr_info("shm_dev: Device successfully closed\n");

    return 0;
}

/* executed once the device is opened.
 *
 */
static int shm_open(struct inode *inodep, struct file *filep)
{
    int ret = 0; 

    if(!mutex_trylock(&shm_mutex)) {
        pr_alert("shm_dev: device busy!\n");
        ret = -EBUSY;
        goto out;
    }
 
    pr_info("shm_dev: Device opened\n");

out:
    return ret;
}

/*  mmap handler to map kernel space to user space  
 *
 */
static int shm_mmap(struct file *filp, struct vm_area_struct *vma)
{
    int ret = 0;
    struct page *page = NULL;
    unsigned long size = (unsigned long)(vma->vm_end - vma->vm_start);
    page = virt_to_page((unsigned long)sh_mem + (vma->vm_pgoff << PAGE_SHIFT)); 
    ret = remap_pfn_range(vma, vma->vm_start, page_to_pfn(page), size, vma->vm_page_prot);
    if (ret != 0) {
        goto out;
    }   

out:
    return ret;
}

static ssize_t shm_read(struct file *filep, char *buffer, size_t len, loff_t *offset)
{
    int ret;
    if (copy_to_user(buffer, sh_mem, len) == 0) {
        pr_info("shm: copy %u char to the user\n", len);
        ret = len;
    } else {
        ret =  -EFAULT;   
    } 

out:
    return ret;
}

static ssize_t shm_write(struct file *filep, const char *buffer, size_t len, loff_t *offset)
{
    int ret;
 
    if (copy_from_user(sh_mem, buffer, len)) {
        pr_err("shm: write fault!\n");
        ret = -EFAULT;
        goto out;
    }
    pr_info("shm: copy %d char from the user\n", len);
    ret = len;

out:
    return ret;
}


long shm_ioctl(struct file *filep, unsigned int cmd, unsigned long arg)
{
    printk("entry ioctl\n");
    printk("cmd = %d \n",cmd);

    // switch(cmd){
    //     case START_CLEAN_PTE:
    //         printk("wake_up_interruptible(&clean_quene);");
    //         wake_up_interruptible(&clean_quene);
    //         break;
    //     case START_ZERO_PAGE:
    //         printk("wake_up_interruptible(&zero_quene);");
    //         wake_up_interruptible(&zero_quene);
    //         break;
    //     default:
    //         printk("invaild message\n");
    // }
    if (cmd == START_CLEAN_PTE) {
        printk(KERN_INFO "Waking up clean_quene\n");
        wake_up_interruptible(&clean_quene);
    } else if (cmd == START_ZERO_PAGE) {
        printk(KERN_INFO "Waking up zero_quene\n");
        wake_up_interruptible(&zero_quene);
    } else {
        printk(KERN_ERR "Invalid message: cmd = %d\n", cmd);
    }
    
	return 0;
}
 

static const struct file_operations mchar_fops = {
    .open = shm_open,
    .read = shm_read,
    .write = shm_write,
    .release = shm_release,
    .mmap = shm_mmap,
    .unlocked_ioctl = shm_ioctl,
    .owner = THIS_MODULE,
};

static int __init shm_init(void)
{
    int ret = 0;    
    major = register_chrdev(0, DEVICE_NAME, &mchar_fops);

    if (major < 0) {
        pr_info("shm: fail to register major number!");
        ret = major;
        goto out;
    }

    class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(class)){ 
        unregister_chrdev(major, DEVICE_NAME);
        pr_info("shm: failed to register device class");
        ret = PTR_ERR(class);
        goto out;
    }

    device = device_create(class, NULL, MKDEV(major, 0), NULL, DEVICE_NAME);
    if (IS_ERR(device)) {
        class_destroy(class);
        unregister_chrdev(major, DEVICE_NAME);
        ret = PTR_ERR(device);
        goto out;
    }


    /* init this mmap area */
    sh_mem = kmalloc(sizeof(struct shm_area), GFP_KERNEL); 
    //sh_mem = &shm_quene;
    if (sh_mem == NULL) {
        ret = -ENOMEM; 
        goto out;
    }
    mutex_init(&shm_mutex);

    init_waitqueue_head(&clean_quene);
    init_waitqueue_head(&zero_quene);

    struct task_struct *clean_pte_thread = kthread_run(clean_pte, NULL, "clean_pte_thread");
    struct task_struct *zero_page_thread = kthread_run(zero_page, NULL, "zero_page_thread");


out: 
    return ret;
}

static void __exit shm_exit(void)
{
    mutex_destroy(&shm_mutex); 
    device_destroy(class, MKDEV(major, 0));  
    class_unregister(class);
    class_destroy(class); 
    unregister_chrdev(major, DEVICE_NAME);
    
    pr_info("mchar: unregistered!");
}

module_init(shm_init);
module_exit(shm_exit);
MODULE_LICENSE("GPL");