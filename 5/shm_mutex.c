#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
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
    printf("Shared memory address: %p\n", (void *)shared);
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