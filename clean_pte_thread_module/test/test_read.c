#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/stat.h>

#define PAGE_SIZE 4096   // 每页大小为 4KB
#define NUM_PAGES 64     // 文件大小是 64 页
#define FILE_SIZE (PAGE_SIZE * NUM_PAGES) // 文件总大小

int main() {
    const char *filename = "testfile";
    int fd;
    void *map;

    // 打开文件
    fd = open(filename, O_RDONLY);
    if (fd == -1) {
        perror("Error opening file");
        return EXIT_FAILURE;
    }

    // 检查文件大小是否符合预期
    struct stat st;
    if (fstat(fd, &st) == -1) {
        perror("Error getting file size");
        close(fd);
        return EXIT_FAILURE;
    }

    if (st.st_size != FILE_SIZE) {
        fprintf(stderr, "Error: File size is not %d bytes\n", FILE_SIZE);
        close(fd);
        return EXIT_FAILURE;
    }

    // 使用 mmap 映射文件到内存
    map = mmap(NULL, FILE_SIZE, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) {
        perror("Error mapping file");
        close(fd);
        return EXIT_FAILURE;
    }

    // 读取文件内容并输出前 100 个字节（或者任意需要的内容）
    char *data = (char *)map;
    printf("First 100 bytes of the file:\n");
    for (int i = 0; i < 100; i++) {
        printf("%c", data[i]);
    }
    printf("\n");

    // 解除映射并关闭文件
    if (munmap(map, FILE_SIZE) == -1) {
        perror("Error unmapping file");
    }

    close(fd);
    return EXIT_SUCCESS;
}