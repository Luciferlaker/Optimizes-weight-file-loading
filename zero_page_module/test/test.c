#include <fcntl.h>  
#include <unistd.h>  
#include <sys/mman.h>  
#include <stdio.h>
#include <string.h>
#include<errno.h>

#include <unistd.h>  // 必须包含头文件

#define QUEUE_SIZE 1024
#define MSG_SIZE 256

#define Wake_up_Sign 4567
#define FILE_NAME "inode.c"
#define DEVICE_FILENAME "/dev/shm_dev" 
#define FILE_SIZE 38543  // 50MB in bytes 

// 共享内存队列结构
struct shm_queue {
    unsigned int read_pos;
    unsigned int write_pos;
    unsigned int count;
    int pid;
    unsigned long data[QUEUE_SIZE * MSG_SIZE];
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
    struct shm_queue *p = NULL;
  
    // Get the current process ID
    pid_t pid = getpid();

    // Print the process ID
    printf("Current process PID: %d\n", pid);

    fd = open(DEVICE_FILENAME, O_RDWR|O_NDELAY);  
  
    if(fd >= 0) {
        p = (struct shm_queue *)mmap(0,  
                sizeof(struct shm_queue),  
                PROT_READ | PROT_WRITE,  
                MAP_SHARED,  
                fd,  
                0);  
        printf("p->read_pos = %d\n", p->read_pos);
        printf("p->write_pos = %d\n", p->write_pos); 
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

    p->pid = pid ;
    for(int i = 0;i < 9;i ++)
    {
        p->data[i] = (unsigned long)mapped_memory + i * 4096;
            p->write_pos ++;
    }
    
    ioctl(fd,Wake_up_Sign);
    printf("p->read_pos = %d\n", p->read_pos);
    printf("p->write_pos = %d\n", p->write_pos);

    sleep(2);

    // 打印文件内容
    //printf("%.*s\n",20480, mapped_memory);  // 使用 %.*s 打印指定大小的字符串
    // 打印映射的内存内容
    printf("Mapped memory contents:\n");
    print_memory(mapped_memory, 20480);
    return ret;  

}  