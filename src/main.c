#include <stdio.h>
#include <unistd.h>

#include <time.h>

#include "lthread.h"

void *
run(void *data)
{
    (void)data;
    struct timespec start, end, diff;
    clock_gettime(CLOCK_REALTIME, &start);
    lthread_sleep(100/*ms*/);
    clock_gettime(CLOCK_REALTIME, &end);
    diff = (struct timespec) {
        .tv_sec = end.tv_sec - start.tv_sec,
        .tv_nsec = end.tv_nsec - start.tv_nsec,
    };
    if (diff.tv_nsec < 0) {
        diff.tv_sec -= 1;
        diff.tv_nsec += 1000000000;
    }
    printf("elapsed time: %lu.%09li\n", diff.tv_sec, diff.tv_nsec);
    return NULL;
}

int
main(int argc, char *argv[])
{
    lthread t1;
    unsigned int values[] = {1, 2};
    (void)argc;
    (void)argv;
    lthread_init();

    lthread_create(&t1, run, values + 0);

    printf("Main Thread doing things!\n");

    for (int ii = 0; ii < 5; ii++) {
        for (int jj = 0; jj < 1000000000; jj++) {
        }
        printf("Doing work\n");
    }

    lthread_join(t1, NULL);
    
    return 0;
}
