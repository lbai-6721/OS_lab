#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <string.h>

#define DEFAULT_ROUNDS 50000000LL

static long long time_diff_ns(struct timespec start, struct timespec end)
{
    return (end.tv_sec - start.tv_sec) * 1000000000LL +
           (end.tv_nsec - start.tv_nsec);
}

int main(int argc, char *argv[])
{
    long long rounds = DEFAULT_ROUNDS;

    if (argc >= 2) {
        rounds = atoll(argv[1]);
        if (rounds <= 0) {
            fprintf(stderr, "Invalid rounds: %s\n", argv[1]);
            return 1;
        }
    }

    pthread_mutex_t mutex;
    int ret = pthread_mutex_init(&mutex, NULL);
    if (ret != 0) {
        fprintf(stderr, "pthread_mutex_init failed: %s\n", strerror(ret));
        return 1;
    }

    struct timespec start, end;

    clock_gettime(CLOCK_MONOTONIC, &start);

    for (long long i = 0; i < rounds; i++) {
        pthread_mutex_lock(&mutex);
        pthread_mutex_unlock(&mutex);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    long long total_ns = time_diff_ns(start, end);
    double avg_ns = (double)total_ns / rounds;

    printf("Test object : pthread_mutex\n");
    printf("Rounds      : %lld\n", rounds);
    printf("Total time  : %lld ns\n", total_ns);
    printf("Average     : %.2f ns per lock/unlock pair\n", avg_ns);

    pthread_mutex_destroy(&mutex);

    return 0;
}