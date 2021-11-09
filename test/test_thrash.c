#include <stdio.h>

#include "lthread.h"

#define NUM_THREADS (20)
#define NUM_ITERS (10000)

void *
run(void *data)
{
    (void) data;
    return NULL;
}

int main(int argc, char *argv[])
{
    (void)argc, (void)argv;

    lthread threads[NUM_THREADS];

    lthread_init();

    for (int ii = 0; ii < NUM_ITERS; ii++) {
        for (int jj = 0; jj < NUM_THREADS; jj++) {
            lthread_create(threads + jj, run, NULL);
        }
        for (int jj = 0; jj < NUM_THREADS; jj++) {
            lthread_join(threads[jj], NULL);
        }
    }

    return 0;
}
