#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>

#define DEFAULT_LOOP 100000

typedef struct {
    pthread_mutex_t mutex;
    int counter;
} shared_data_t;

static void print_pagemap_entry(void *addr)
{
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        perror("sysconf");
        return;
    }

    uintptr_t virtual_addr = (uintptr_t)addr;
    uint64_t virtual_page_index = virtual_addr / page_size;
    off_t offset = virtual_page_index * sizeof(uint64_t);

    int fd = open("/proc/self/pagemap", O_RDONLY);
    if (fd < 0) {
        perror("open /proc/self/pagemap");
        return;
    }

    uint64_t entry = 0;

    ssize_t n = pread(fd, &entry, sizeof(entry), offset);
    if (n != sizeof(entry)) {
        perror("pread /proc/self/pagemap");
        close(fd);
        return;
    }

    close(fd);

    int present = (entry >> 63) & 1;
    int swapped = (entry >> 62) & 1;
    int file_or_shared_anon = (entry >> 61) & 1;
    int uffd_wp = (entry >> 57) & 1;
    int exclusive = (entry >> 56) & 1;
    int soft_dirty = (entry >> 55) & 1;

    uint64_t pfn = entry & ((1ULL << 55) - 1);

    printf("\n========== pagemap information ==========\n");
    printf("Virtual address          : %p\n", addr);
    printf("Page size                : %ld bytes\n", page_size);
    printf("Virtual page index       : 0x%llx\n",
           (unsigned long long)virtual_page_index);
    printf("Offset in pagemap        : 0x%llx\n",
           (unsigned long long)offset);
    printf("Raw pagemap entry        : 0x%016llx\n",
           (unsigned long long)entry);

    printf("Bit 63 present           : %d\n", present);
    printf("Bit 62 swapped           : %d\n", swapped);
    printf("Bit 61 file/shared-anon  : %d\n", file_or_shared_anon);
    printf("Bit 57 uffd-wp           : %d\n", uffd_wp);
    printf("Bit 56 exclusive         : %d\n", exclusive);
    printf("Bit 55 soft-dirty        : %d\n", soft_dirty);

    if (present) {
        printf("PFN bits 0-54            : 0x%llx\n",
               (unsigned long long)pfn);

        if (pfn != 0) {
            uint64_t physical_addr = pfn * page_size +
                                     (virtual_addr % page_size);
            printf("Physical address         : 0x%llx\n",
                   (unsigned long long)physical_addr);
        } else {
            printf("PFN is 0. On many Linux systems, PFN is hidden unless running as root or with CAP_SYS_ADMIN.\n");
        }
    } else if (swapped) {
        printf("This page is swapped out. Bits 0-54 contain swap information.\n");
    } else {
        printf("This page is not currently present in physical memory.\n");
    }

    printf("=========================================\n\n");
}

int main(int argc, char *argv[])
{
    int loop = DEFAULT_LOOP;

    if (argc >= 2) {
        loop = atoi(argv[1]);
        if (loop <= 0) {
            fprintf(stderr, "Invalid loop count.\n");
            return 1;
        }
    }

    int shmid = shmget(IPC_PRIVATE, sizeof(shared_data_t), IPC_CREAT | 0666);
    if (shmid == -1) {
        perror("shmget");
        return 1;
    }

    shared_data_t *shared = (shared_data_t *)shmat(shmid, NULL, 0);
    if (shared == (void *)-1) {
        perror("shmat");
        shmctl(shmid, IPC_RMID, NULL);
        return 1;
    }

    /*
     * 访问共享内存，确保该虚拟页真正建立映射。
     * Linux 中共享内存页通常是按需分配的，
     * 如果不访问，pagemap 中可能看不到 present 状态。
     */
    shared->counter = 0;

    pthread_mutexattr_t attr;
    if (pthread_mutexattr_init(&attr) != 0) {
        perror("pthread_mutexattr_init");
        shmdt(shared);
        shmctl(shmid, IPC_RMID, NULL);
        return 1;
    }

    if (pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED) != 0) {
        perror("pthread_mutexattr_setpshared");
        pthread_mutexattr_destroy(&attr);
        shmdt(shared);
        shmctl(shmid, IPC_RMID, NULL);
        return 1;
    }

    if (pthread_mutex_init(&shared->mutex, &attr) != 0) {
        perror("pthread_mutex_init");
        pthread_mutexattr_destroy(&attr);
        shmdt(shared);
        shmctl(shmid, IPC_RMID, NULL);
        return 1;
    }

    pthread_mutexattr_destroy(&attr);

    printf("Shared memory id: %d\n", shmid);
    printf("Shared memory start address: %p\n", (void *)shared);

    /*
     * 打印共享内存第一个页面对应的 pagemap 表项。
     * shared 指向共享内存起始位置，因此它所在页就是共享内存的第一个虚拟页面。
     */
    print_pagemap_entry((void *)shared);

    printf("Initial counter = %d\n", shared->counter);
    printf("Each child process will add counter %d times.\n\n", loop);

    pid_t pid1 = fork();

    if (pid1 < 0) {
        perror("fork pid1");
        pthread_mutex_destroy(&shared->mutex);
        shmdt(shared);
        shmctl(shmid, IPC_RMID, NULL);
        return 1;
    }

    if (pid1 == 0) {
        for (int i = 0; i < loop; i++) {
            pthread_mutex_lock(&shared->mutex);
            shared->counter++;
            pthread_mutex_unlock(&shared->mutex);
        }

        printf("Child process 1 finished, pid = %d\n", getpid());
        shmdt(shared);
        exit(0);
    }

    pid_t pid2 = fork();

    if (pid2 < 0) {
        perror("fork pid2");
        wait(NULL);
        pthread_mutex_destroy(&shared->mutex);
        shmdt(shared);
        shmctl(shmid, IPC_RMID, NULL);
        return 1;
    }

    if (pid2 == 0) {
        for (int i = 0; i < loop; i++) {
            pthread_mutex_lock(&shared->mutex);
            shared->counter++;
            pthread_mutex_unlock(&shared->mutex);
        }

        printf("Child process 2 finished, pid = %d\n", getpid());
        shmdt(shared);
        exit(0);
    }

    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);

    printf("\nBoth child processes finished.\n");
    printf("Final counter = %d\n", shared->counter);
    printf("Expected counter = %d\n", loop * 2);

    if (shared->counter == loop * 2) {
        printf("Result: correct. Mutex successfully protected shared memory.\n");
    } else {
        printf("Result: wrong. Race condition happened.\n");
    }

    pthread_mutex_destroy(&shared->mutex);

    shmdt(shared);

    shmctl(shmid, IPC_RMID, NULL);

    return 0;
}