#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <sched.h>

#define DEFAULT_LOOP 100000

typedef struct {
    volatile int turn;          // strict alternation 的核心变量
    volatile int in_cs[2];      // 用来检测是否有两个进程同时进入临界区
    volatile int violation;     // 是否发生互斥错误
    int counter;                // 两个进程共享访问的计数器
} shared_data_t;

void strict_alternation_process(shared_data_t *shared, int id, int loop)
{
    int other = 1 - id;

    for (int i = 0; i < loop; i++) {

        /*
         * Entry section:
         * 如果当前 turn 不是自己，就一直等待。
         * 这就是 strict alternation 的忙等待部分。
         */
        while (shared->turn != id) {
            sched_yield();
        }

        /*
         * Critical section:
         * 当前进程进入临界区。
         * 理论上同一时刻只能有一个进程进入这里。
         */
        shared->in_cs[id] = 1;

        if (shared->in_cs[other] == 1) {
            shared->violation = 1;
            printf("Mutual exclusion violation! process %d and process %d are both in critical section.\n",
                   id, other);
        }

        shared->counter++;

        if (i < 5) {
            printf("Process %d enters critical section, i = %d, counter = %d\n",
                   id, i, shared->counter);
        }

        shared->in_cs[id] = 0;

        /*
         * Exit section:
         * 当前进程退出临界区后，把 turn 交给另一个进程。
         */
        shared->turn = other;
    }

    printf("Process %d finished, pid = %d\n", id, getpid());
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

    /*
     * 关闭 stdout 缓冲，避免 fork 后输出缓冲导致显示混乱。
     */
    setbuf(stdout, NULL);

    /*
     * 使用 shmget 创建 System V 共享内存。
     */
    int shmid = shmget(IPC_PRIVATE, sizeof(shared_data_t), IPC_CREAT | 0666);
    if (shmid == -1) {
        perror("shmget");
        return 1;
    }

    /*
     * 使用 shmat 将共享内存映射到当前进程地址空间。
     */
    shared_data_t *shared = (shared_data_t *)shmat(shmid, NULL, 0);
    if (shared == (void *)-1) {
        perror("shmat");
        shmctl(shmid, IPC_RMID, NULL);
        return 1;
    }

    /*
     * 初始化共享变量。
     * turn = 0 表示一开始允许 0 号进程先进入临界区。
     */
    shared->turn = 0;
    shared->in_cs[0] = 0;
    shared->in_cs[1] = 0;
    shared->violation = 0;
    shared->counter = 0;

    printf("Shared memory id      : %d\n", shmid);
    printf("Shared memory address : %p\n", (void *)shared);
    printf("Initial turn          : %d\n", shared->turn);
    printf("Each process loops    : %d times\n\n", loop);

    /*
     * 创建第一个子进程，编号为 0。
     */
    pid_t pid0 = fork();

    if (pid0 < 0) {
        perror("fork pid0");
        shmdt(shared);
        shmctl(shmid, IPC_RMID, NULL);
        return 1;
    }

    if (pid0 == 0) {
        strict_alternation_process(shared, 0, loop);
        shmdt(shared);
        exit(0);
    }

    /*
     * 创建第二个子进程，编号为 1。
     */
    pid_t pid1 = fork();

    if (pid1 < 0) {
        perror("fork pid1");
        waitpid(pid0, NULL, 0);
        shmdt(shared);
        shmctl(shmid, IPC_RMID, NULL);
        return 1;
    }

    if (pid1 == 0) {
        strict_alternation_process(shared, 1, loop);
        shmdt(shared);
        exit(0);
    }

    /*
     * 父进程等待两个子进程结束。
     */
    waitpid(pid0, NULL, 0);
    waitpid(pid1, NULL, 0);

    printf("\nBoth child processes finished.\n");
    printf("Final counter   : %d\n", shared->counter);
    printf("Expected value  : %d\n", loop * 2);
    printf("Violation flag  : %d\n", shared->violation);

    if (shared->counter == loop * 2 && shared->violation == 0) {
        printf("Result          : correct. Strict alternation ensures mutual exclusion.\n");
    } else {
        printf("Result          : wrong. Mutual exclusion failed.\n");
    }

    /*
     * 解除映射并删除共享内存。
     */
    shmdt(shared);
    shmctl(shmid, IPC_RMID, NULL);

    return 0;
}