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

#define DEVICE_NAME "shm_dev"
#define CLASS_NAME "shmqueue_class"

#define QUEUE_SIZE 1024
#define MSG_SIZE 256

#define Wake_up_Sign 1234

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
static struct timer_list my_timer;

static DEFINE_MUTEX(mchar_mutex);


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
    // 注意：这里使用pte_offset_map_lock而不是普通的pte_offset_map
    // 因为我们需要同时获取pte和对应的spinlock
    ptep = pte_offset_map_lock(mm, pmd_offset(pud_offset(p4d_offset(pgd_offset(mm, address), 
              address), address), address), address, &ptl);
    if (!ptep) {
        mmap_read_unlock(mm);
        return;  // 页表项不存在
    }
    printk("find pte valind");
    // 4. 检查PTE是否有效
    if (pte_present(*ptep)) {
            printk("clean the pte\n");
            // 使用 ptep_get_and_clear 清除页表项
            pte_t old_pte = ptep_get_and_clear(vma->vm_mm, address, ptep);
            printk("Old pte: %lx\n", old_pte);  // 打印原始页表项
            printk("New pte: %lx\n", *ptep);   // 打印清空后的页表项，应该是0或无效状态
             // 刷新 TLB
           //flush_tlb_page(vma, address);
           __flush_tlb_all();
    }
    
    // 6. 按照获取的相反顺序释放锁
    pte_unmap_unlock(ptep, ptl);  // 释放页表锁并解除映射
    mmap_read_unlock(mm);         // 释放mmap锁
}

static int clean_pte(void *data)
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
        clear_pte_by_address(mm, address);
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
    sh_mem->address[0] = 1111;
    init_waitqueue_head(&clean_quene);

    struct task_struct *clean_pte_thread = kthread_run(clean_pte, NULL, "clean_pte_thread");
    

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