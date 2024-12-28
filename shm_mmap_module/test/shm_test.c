#include <fcntl.h>  
#include <unistd.h>  
#include <sys/mman.h>  
#include <stdio.h>
#include <string.h>
#include<errno.h>
  
#define QUEUE_SIZE 1024
#define MSG_SIZE 256

// 共享内存队列结构
struct shm_queue {
    unsigned int read_pos;
    unsigned int write_pos;
    unsigned int count;
    unsigned long data[QUEUE_SIZE * MSG_SIZE];
};


#define DEVICE_FILENAME "/dev/shm_dev"  
  
int main()  
{  
    int fd;  
    int ret;
    struct shm_queue *p = NULL;
    //char buff[64];    
  
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

        p->read_pos =2;

        printf("p->read_pos = %d\n", p->read_pos);
        printf("p->write_pos = %d\n", p->write_pos);

        munmap(p,sizeof(struct shm_queue));  

        close(fd);  
    }  

 /*   fd = open(DEVICE_FILENAME, O_RDWR|O_NDELAY);
    ret = write(fd, "abcd", strlen("abcd"));
    if (ret < 0) {
        printf("write error!\n");
        ret = errno;
        goto out;
    }

    ret = read(fd, buff, 64);
    if (ret < 0) {
        printf("read error!\n");
        ret = errno;
        goto out;
    }

    printf("read: %s\n", buff);
out:*/
    return ret;  
}  