#include <fcntl.h>  
#include <unistd.h>  
#include <sys/mman.h>  
#include <stdio.h>
#include <string.h>
#include<errno.h>
  
#define QUEUE_SIZE 1024
#define MSG_SIZE 256

#define Wake_up_Sign 1234
#define FILE_NAME "Makefile"
#define DEVICE_FILENAME "/dev/shm_dev" 
#define FILE_SIZE 64780  // 50MB in bytes 

// 共享内存队列结构
struct shm_queue {
    unsigned int read_pos;
    unsigned int write_pos;
    unsigned int count;
    int pid;
    unsigned long data[QUEUE_SIZE * MSG_SIZE];
};
 
  
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

    // 使用 madvise 来告诉内核这是随机访问
    if (madvise(mapped_memory, FILE_SIZE, MADV_RANDOM) != 0) {
        perror("madvise failed");
        munmap(mapped_memory, FILE_SIZE);
        close(file_fd);
        return 1;
    }

     // 打印文件内容
    printf("%.*s\n",40960,mapped_memory);  // 使用 %.*s 打印指定大小的字符串
    // Print the initial address of the mapped memory
    printf("Mapped memory address: %lu\n", (unsigned long)mapped_memory);


    p->pid = pid ;
    for(int i = 0;i < 16;i ++)
    {
        p->data[i] = (unsigned long)mapped_memory + i * 4096;
            p->write_pos ++;
    }
    
    ioctl(fd,Wake_up_Sign);
    printf("p->read_pos = %d\n", p->read_pos);
    printf("p->write_pos = %d\n", p->write_pos);

    sleep(2);

    //printf("%.*s\n",40960 ,mapped_memory);  // 使用 %.*s 打印指定大小的字符串
    return ret;  

}  