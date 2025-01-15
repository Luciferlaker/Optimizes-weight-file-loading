#include <fcntl.h>  
#include <unistd.h>  
#include <sys/mman.h>  
#include <stdio.h>
#include <string.h>
#include<errno.h>

#include <unistd.h>  // 必须包含头文件

#define FILE_NAME "inode.c"
#define DEVICE_FILENAME "/dev/shm_dev" 
#define FILE_SIZE 38543  // 50MB in bytes 

#define Zero_QUEUE_SIZE 1024
#define Zero_MSG_SIZE 1

#define Clean_QUEUE_SIZE 1024
#define Clean_MSG_SIZE 1

#define START_CLEAN_PTE 7890
#define START_ZERO_PAGE 7891

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


void delay(unsigned int seconds) {
    sleep(seconds);  // 延时指定的秒数
}
  
void print_memory(const void *ptr, size_t size) {
    unsigned char *byte_ptr = (unsigned char *)ptr;
    for (size_t i = 0; i < size; i++) {
        printf("%02x ", byte_ptr[i]);  // 按十六进制打印每个字节
        if ((i + 1) % 16 == 0) {
            printf("\n");  // 每 16 字节换行，便于阅读
        }
    }
    printf("\n");
}

int main()  
{  
    int fd;  
    int ret;
    struct shm_area *p = NULL;
  
    // Get the current process ID
    pid_t pid = getpid();

    // Print the process ID
    printf("Current process PID: %d\n", pid);

    fd = open(DEVICE_FILENAME, O_RDWR|O_NDELAY);  
  
    if(fd >= 0) {
        p = (struct shm_area *)mmap(0,  
                sizeof(struct shm_area),  
                PROT_READ | PROT_WRITE,  
                MAP_SHARED,  
                fd,  
                0);
    }

    int file_fd;
    void *mapped_memory;
    
    // Open the file
    file_fd = open(FILE_NAME, O_RDONLY);
    if (file_fd == -1) {
        perror("Failed to open the file");
        return 1;
    }


    // Map the file into memory (read-only mapping)
    mapped_memory = mmap(NULL,FILE_SIZE, PROT_READ, MAP_PRIVATE, file_fd, 0);
    if (mapped_memory == MAP_FAILED) {
        perror("mmap failed");
        close(file_fd);
        return 1;
    }

    p->zero_pgae_quene.pid = pid ;
    for(int i = 0;i < 9;i ++)
    {
        p->zero_pgae_quene.address[i] = (unsigned long)mapped_memory + i * 4096;
        p->zero_pgae_quene.write_pos ++;
    }
    
    ioctl(fd,START_ZERO_PAGE);
    int input = 0;
    while (1) {
        printf("请输入1继续执行，其他数字退出: ");
        scanf("%d", &input);

        if (input == 1) {
            printf("输入为1，继续执行...\n");
            // 在这里执行后续的逻辑
            break;  // 输入1后，跳出循环，继续执行后面的代码
        } 
    }

    // 打印文件内容
    //printf("%.*s\n",20480, mapped_memory);  // 使用 %.*s 打印指定大小的字符串
    // 打印映射的内存内容
    printf("Mapped memory contents:\n");
    print_memory(mapped_memory, 20480);

    p->clean_pte_quene.pid = pid ;
    for(int i = 0;i < 9;i ++)
    {
        p->clean_pte_quene.address[i] = (unsigned long)mapped_memory + i * 4096;
            p->clean_pte_quene.write_pos ++;
    }
    
    ioctl(fd,START_CLEAN_PTE);
    while (1) {
        printf("请输入1继续执行，其他数字退出: ");
        scanf("%d", &input);
        if (input == 1) {
            printf("输入为1，继续执行...\n");
            // 在这里执行后续的逻辑
            break;  // 输入1后，跳出循环，继续执行后面的代码
        } 
    }
    // 打印文件内容
    printf("%.*s\n",20480, mapped_memory);  // 使用 %.*s 打印指定大小的字符串
    return ret;  

}  