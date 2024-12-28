#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/init.h> 
#include <linux/fs.h> 
#include <linux/mm.h> 
#include <asm/uaccess.h>
#include <linux/slab.h>

#define DEVICE_NAME "shm_dev"
#define CLASS_NAME "shmqueue_class"

#define QUEUE_SIZE 1024
#define MSG_SIZE 256

static struct class*  class;
static struct device*  device;
static int major;

static struct timer_list my_timer;



// 共享内存队列结构
struct shm_queue {
    unsigned int read_pos;
    unsigned int write_pos;
    unsigned int count;
    unsigned long data[QUEUE_SIZE * MSG_SIZE];
};

static struct shm_queue *sh_mem = NULL; 

static DEFINE_MUTEX(mchar_mutex);

// 定时器回调函数
static void timer_callback(struct timer_list *timer)
{
    pr_info("Periodic read: sh_mem->write_pos: %d\n", sh_mem->write_pos);
    pr_info("Periodic read: sh_mem->read_pos: %d\n", sh_mem->read_pos);
    // 重新启动定时器，周期性运行
    mod_timer(&my_timer, jiffies + HZ);  // 每隔 1 秒触发一次 (HZ 是每秒的时钟周期数)
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

static const struct file_operations mchar_fops = {
    .open = mchar_open,
    .read = mchar_read,
    .write = mchar_write,
    .release = mchar_release,
    .mmap = mchar_mmap,
    /*.unlocked_ioctl = mchar_ioctl,*/
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
    if (sh_mem == NULL) {
        ret = -ENOMEM; 
        goto out;
    }
    sh_mem->read_pos = 1;
    sh_mem->write_pos = 0;
    mutex_init(&mchar_mutex);

            // 初始化定时器
    timer_setup(&my_timer, timer_callback, 0);

    // 启动定时器，1 秒后触发
    mod_timer(&my_timer, jiffies + HZ);
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