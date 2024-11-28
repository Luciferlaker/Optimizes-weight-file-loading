#include <linux/module.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <asm/io.h>
#include <linux/cdev.h>
#include <linux/proc_fs.h>
#include <linux/mm.h>
#include <linux/slab.h>

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>

#define DEVICE_NAME "shm_dev"
#define CLASS_NAME "shmqueue_class"
#define QUEUE_SIZE 1024
#define MSG_SIZE 256

// 共享内存队列结构
struct shm_queue {
    unsigned int read_pos;
    unsigned int write_pos;
    unsigned int count;
    char data[QUEUE_SIZE * MSG_SIZE];
};

static struct {
    struct cdev cdev;
    dev_t dev_num;
    struct class *device_class;
    struct device *device;
    struct shm_queue *queue;
} shmqueue_dev;

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

    shmqueue_dev.queue = kmalloc(sizeof(struct shm_queue), GFP_KERNEL);
    if (!shmqueue_dev.queue) {
        cdev_del(&shmqueue_dev.cdev);
        device_destroy(shmqueue_dev.device_class, shmqueue_dev.dev_num);
        class_destroy(shmqueue_dev.device_class);
        unregister_chrdev_region(shmqueue_dev.dev_num, 1);
        return -ENOMEM;
    }

    memset(shmqueue_dev.queue, 0, sizeof(struct shm_queue));

    printk(KERN_INFO "SHM Queue module initialized\n");
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