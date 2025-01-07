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
#include <linux/oom.h>

#define DEVICE_NAME "shm_dev"
#define CLASS_NAME "shmqueue_class"

#define QUEUE_SIZE 1024
#define MSG_SIZE 256

#define Wake_up_Sign 4567

static struct class*  class;
static struct device*  device;
static int major;

wait_queue_head_t clean_quene;

// 共享内存队列结构
struct shm_queue {
    unsigned int read_pos;
    unsigned int write_pos;
    unsigned int count;
    int pid;
    unsigned long address[QUEUE_SIZE * MSG_SIZE];
};

static struct shm_queue *sh_mem = NULL;

static DEFINE_MUTEX(mchar_mutex);



// int handle_zero_page(struct mm_struct *mm, unsigned long address)
// {
//     pgd_t *pgd;
// 	p4d_t *p4d;
//     pud_t *pud;
//     pmd_t *pmd;
//     pte_t *pte;
//     struct page *page;
//     vm_fault_t ret = 0;
//     pte_t entry;
//     spinlock_t *ptl;
//     /*lock mm*/
// 	mmap_read_lock(mm);

//     /*find the vma*/
//     struct vm_area_struct *vma = find_vma(mm, address);
// 	if (unlikely(!vma)) {
// 		printk("cant find vma\n");
// 		return 0;
// 	}

//     /*create the pud、pgd*/
// 	pgd = pgd_offset(mm, address);
// 	p4d = p4d_alloc(mm, pgd, address);
// 	if (!p4d)
// 		return VM_FAULT_OOM;

// 	pud = pud_alloc(mm, p4d, address);
// 	if (!pud)
// 		return VM_FAULT_OOM;

// 	pmd = pmd_alloc(mm, pud, address);
// 	if (!pmd)
// 		return VM_FAULT_OOM;


//     if (pte_alloc(mm, pmd))
//         return VM_FAULT_OOM;

//     entry = pte_mkspecial(pfn_pte(my_zero_pfn(address),
//                     vma->vm_page_prot));
//     pte = pte_offset_map_lock(mm, pmd,
//             address, &ptl);
//     if (!pte_none(*pte)) {
//         update_mmu_tlb(vma, address, pte);
//         goto unlock;
//     }
//     ret = check_stable_address_space(mm);
//     if (ret)
//         goto unlock;
//     goto setpte;

// setpte:
// 	set_pte_at(mm, address, pte, entry);

// 	/* No need to invalidate - it was non-present before */
// 	update_mmu_cache(vma, address, pte);
// unlock:
// 	pte_unmap_unlock(pte, ptl);


//     mmap_read_unlock(mm);

//     return 0;
// }

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
        printk("read_pos = %d \n",sh_mem->read_pos);
        printk("read_pos = %d \n",sh_mem->write_pos);
        wait_event_interruptible(clean_quene, (sh_mem->write_pos!= sh_mem->read_pos));
        printk("pte_clean_quene.address = %lu\n",sh_mem->address[sh_mem->read_pos]);
        printk("current->pid = %d \n",current->pid);
        printk("shm->pid = %d \n",sh_mem->pid);

        struct pid *pid_struct;
        struct task_struct *task;

        // 查找 pid 结构体
        pid_struct = find_get_pid(sh_mem->pid);

        // 使用 get_pid_task 获取 task_struct
        task = get_pid_task(pid_struct, PIDTYPE_PID);

        // 判断是否找到
        if (task != NULL) {
            printk(KERN_INFO "Found task with PID %d, name: %s\n", sh_mem->pid, task->comm);
        } else {
            printk(KERN_INFO "No task found with PID %d\n", sh_mem->pid);
        }
        struct mm_struct *mm = task -> mm;
        unsigned long address = sh_mem->address[sh_mem->read_pos];
        handle_zero_page(mm, address);
        sh_mem->read_pos ++;
    }

    return 0;
}


/*  executed once the device is closed or releaseed by userspace
 *  @param inodep: pointer to struct inode
 *  @param filep: pointer to struct file 
 */
static int mchar_release(struct inode *inodep, struct file *filep)
{    
    mutex_unlock(&mchar_mutex);
    pr_info("mchar: Device successfully closed\n");

    return 0;
}

/* executed once the device is opened.
 *
 */
static int mchar_open(struct inode *inodep, struct file *filep)
{
    int ret = 0; 

    if(!mutex_trylock(&mchar_mutex)) {
        pr_alert("mchar: device busy!\n");
        ret = -EBUSY;
        goto out;
    }
 
    pr_info("mchar: Device opened\n");

out:
    return ret;
}

/*  mmap handler to map kernel space to user space  
 *
 */
static int mchar_mmap(struct file *filp, struct vm_area_struct *vma)
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

static ssize_t mchar_read(struct file *filep, char *buffer, size_t len, loff_t *offset)
{
    int ret;
    if (copy_to_user(buffer, sh_mem, len) == 0) {
        pr_info("mchar: copy %u char to the user\n", len);
        ret = len;
    } else {
        ret =  -EFAULT;   
    } 

out:
    return ret;
}

static ssize_t mchar_write(struct file *filep, const char *buffer, size_t len, loff_t *offset)
{
    int ret;
 
    if (copy_from_user(sh_mem, buffer, len)) {
        pr_err("mchar: write fault!\n");
        ret = -EFAULT;
        goto out;
    }
    pr_info("mchar: copy %d char from the user\n", len);
    ret = len;

out:
    return ret;
}


long mchar_ioctl(struct file *filep, unsigned int cmd, unsigned long arg)
{
    printk("entry ioctl\n");
    printk("cmd = %d \n",cmd);
    if(cmd == Wake_up_Sign)
    {
        wake_up_interruptible(&clean_quene);
        printk("wake up \n");
    }
	return 0;
}
 

static const struct file_operations mchar_fops = {
    .open = mchar_open,
    .read = mchar_read,
    .write = mchar_write,
    .release = mchar_release,
    .mmap = mchar_mmap,
    .unlocked_ioctl = mchar_ioctl,
    .owner = THIS_MODULE,
};

static int __init mchar_init(void)
{
    int ret = 0;    
    major = register_chrdev(0, DEVICE_NAME, &mchar_fops);

    if (major < 0) {
        pr_info("mchar: fail to register major number!");
        ret = major;
        goto out;
    }

    class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(class)){ 
        unregister_chrdev(major, DEVICE_NAME);
        pr_info("mchar: failed to register device class");
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
    sh_mem = kmalloc(sizeof(struct shm_queue), GFP_KERNEL); 
    //sh_mem = &shm_quene;
    if (sh_mem == NULL) {
        ret = -ENOMEM; 
        goto out;
    }
    mutex_init(&mchar_mutex);

    sh_mem->read_pos = 0;
    sh_mem->write_pos = 0;
    init_waitqueue_head(&clean_quene);

    struct task_struct *clean_pte_thread = kthread_run(zero_page, NULL, "clean_pte_thread");
    

out: 
    return ret;
}

static void __exit mchar_exit(void)
{
    mutex_destroy(&mchar_mutex); 
    device_destroy(class, MKDEV(major, 0));  
    class_unregister(class);
    class_destroy(class); 
    unregister_chrdev(major, DEVICE_NAME);
    
    pr_info("mchar: unregistered!");
}

module_init(mchar_init);
module_exit(mchar_exit);
MODULE_LICENSE("GPL");