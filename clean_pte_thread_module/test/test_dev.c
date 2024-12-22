#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#define QUEUE_SIZE 1024
#define MSG_SIZE 256

struct shm_queue {
    unsigned int read_pos;
    unsigned int write_pos;
    unsigned int count;
    unsigned long address[QUEUE_SIZE * MSG_SIZE];
};

// 入队 写入信息
static int enqueue(struct shm_queue *queue, unsigned long *msg)
{
    if (queue->count >= QUEUE_SIZE) {
        return -1; 
    }
    queue->address[queue->write_pos]=msg;
    queue->write_pos = (queue->write_pos + 1) % QUEUE_SIZE;
    queue->count++;
    return 0;
}

// 出队 读取信息
static int dequeue(struct shm_queue *queue, unsigned long *msg)
{
    if (queue->count == 0) {
        return -1;
    }

    msg = queue->address[queue->read_pos];
    queue->read_pos = (queue->read_pos + 1) % QUEUE_SIZE;
    queue->count--;
    return 0;
}

int main()
{
    int fd;
    struct shm_queue *queue;
    char message[MSG_SIZE];

    // 打开设备文件
    fd = open("/dev/shm_dev", O_RDWR);
    if (fd < 0) {
        perror("Failed to open device");
        return -1;
    }

    // 映射共享内存
    queue = mmap(NULL, sizeof(struct shm_queue), PROT_READ | PROT_WRITE,
                MAP_SHARED, fd, 0);
    if (queue == MAP_FAILED) {
        perror("mmap failed");
        close(fd);
        return -1;
    }
    printf("File mapped to virtual memory address: %p\n", queue);

    //写入消息
    unsigned long address = 111111;
    if (enqueue(queue, address) == 0) {
        printf("Message enqueued: %ld\n", address);
    } else {
        printf("Queue is full\n");
    }

    // 读取消息
    if (dequeue(queue, address) == 0) {
        printf("Message dequeued: %ld\n", address);
    } else {
        printf("Queue is empty\n");
    }

    munmap(queue, sizeof(struct shm_queue));
    close(fd);
    return 0;
}