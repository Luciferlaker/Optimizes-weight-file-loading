#include <linux/module.h>
#include <linux/types.h>
#include <asm/io.h>
#include <linux/proc_fs.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/cdev.h>
#include <linux/device.h>
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

#define DEVICE_NAME "shm_dev"
#define CLASS_NAME "shmqueue_class"
#define QUEUE_SIZE 1024
#define MSG_SIZE 256

// 共享内存队列结构
struct shm_queue {
    unsigned int read_pos;
    unsigned int write_pos;
    unsigned int count;
    unsigned long address[QUEUE_SIZE * MSG_SIZE];
};

struct shm_queue pte_clean_quene;

static struct {
    struct cdev cdev;
    dev_t dev_num;
    struct class *device_class;
    struct device *device;
    struct shm_queue *queue;
} shmqueue_dev;

wait_queue_head_t clean_quene;

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
        return;  // 地址不在任何VMA范围内
    }
    
    // 3. 通过地址获取对应的页表项指针
    // 注意：这里使用pte_offset_map_lock而不是普通的pte_offset_map
    // 因为我们需要同时获取pte和对应的spinlock
    ptep = pte_offset_map_lock(mm, pmd_offset(pud_offset(p4d_offset(pgd_offset(mm, address), 
              address), address), address), address, &ptl);
    if (!ptep) {
        mmap_read_unlock(mm);
        return;  // 页表项不存在
    }
    
    // 4. 检查PTE是否有效
    if (pte_present(*ptep)) {
            // 使用 ptep_get_and_clear 清除页表项
            pte_t old_pte = ptep_get_and_clear(vma->vm_mm, address, ptep);
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
        wait_event_interruptible(clean_quene, (pte_clean_quene.read_pos != pte_clean_quene.write_pos));
        struct mm_struct *mm = current -> mm;
        unsigned long address = pte_clean_quene.address[pte_clean_quene.read_pos];
        clear_pte_by_address(mm, address);
        pte_clean_quene.read_pos ++;
    }

    return 0;
}

static int mmap_handler(struct file *filp, struct vm_area_struct *vma)
{
    unsigned long size = vma->vm_end - vma->vm_start;
    unsigned long pfn = virt_to_phys(shmqueue_dev.queue) >> PAGE_SHIFT;

    if (remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot)) {
        return -EAGAIN;
    }
    
    return 0;
}

static const struct file_operations shmqueue_fops = {
    .owner = THIS_MODULE,
    .mmap = mmap_handler,
};

static int __init shmqueue_init(void)
{
    int ret;

    ret = alloc_chrdev_region(&shmqueue_dev.dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        printk(KERN_ERR "Failed to allocate device number\n");
        return ret;
    }

    shmqueue_dev.device_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(shmqueue_dev.device_class)) {
        unregister_chrdev_region(shmqueue_dev.dev_num, 1);
        return PTR_ERR(shmqueue_dev.device_class);
    }


    shmqueue_dev.device = device_create(shmqueue_dev.device_class, NULL,
                                      shmqueue_dev.dev_num, NULL, DEVICE_NAME);
    if (IS_ERR(shmqueue_dev.device)) {
        class_destroy(shmqueue_dev.device_class);
        unregister_chrdev_region(shmqueue_dev.dev_num, 1);
        return PTR_ERR(shmqueue_dev.device);
    }

    cdev_init(&shmqueue_dev.cdev, &shmqueue_fops);
    ret = cdev_add(&shmqueue_dev.cdev, shmqueue_dev.dev_num, 1);
    if (ret < 0) {
        device_destroy(shmqueue_dev.device_class, shmqueue_dev.dev_num);
        class_destroy(shmqueue_dev.device_class);
        unregister_chrdev_region(shmqueue_dev.dev_num, 1);
        return ret;
    }

    //shmqueue_dev.queue = kmalloc(sizeof(struct shm_queue), GFP_KERNEL);
    pte_clean_quene.read_pos=0;
    pte_clean_quene.write_pos=0;
    shmqueue_dev.queue = &pte_clean_quene;


    if (!shmqueue_dev.queue) {
        cdev_del(&shmqueue_dev.cdev);
        device_destroy(shmqueue_dev.device_class, shmqueue_dev.dev_num);
        class_destroy(shmqueue_dev.device_class);
        unregister_chrdev_region(shmqueue_dev.dev_num, 1);
        return -ENOMEM;
    }

    memset(shmqueue_dev.queue, 0, sizeof(struct shm_queue));

    //DECLARE_WAIT_QUEUE_HEAD(clean_quene);
    init_waitqueue_head(&clean_quene);

    struct task_struct *clean_pte_thread = kthread_run(clean_pte, NULL, "clean_pte_thread");

    printk(KERN_INFO "SHM Queue module initialized\n");
    
    //struct task_struct *clean_pte_thread = kthread_run(clean_pte, NULL, "clean_pte_thread");

    return 0;
}

static void __exit shmqueue_exit(void)
{
    kfree(shmqueue_dev.queue);
    cdev_del(&shmqueue_dev.cdev);
    device_destroy(shmqueue_dev.device_class, shmqueue_dev.dev_num);
    class_destroy(shmqueue_dev.device_class);
    unregister_chrdev_region(shmqueue_dev.dev_num, 1);
    printk(KERN_INFO "SHM Queue module unloaded\n");
}

module_init(shmqueue_init);
module_exit(shmqueue_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("lwk");
MODULE_DESCRIPTION("Kernel-User Space Shared Memory Queue Module");