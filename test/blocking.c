#include <stdio.h>

#include "lthread.h"

#define NUM_THREADS (10)
#define NUM_ADDS (20000)
size_t sum = 0;

void *
add(void *data)
{
    (void) data;
    for (size_t ii = 0; ii < NUM_ADDS; ii++) {
        lthread_block();
        sum++;
        sum--;
        sum++;
        sum--;
        sum++;
        sum--;
        sum++;
        sum--;
        sum++;
        sum--;
        sum++;
        sum--;
        sum++;
        sum--;
        sum++;
        sum--;
        sum++;
        sum--;
        sum++;
        sum--;
        sum++;
        sum--;
        sum++;
        sum--;
        sum++;
        lthread_unblock();
    }

    return NULL;
}

int main(int argc, char *argv[])
{
    (void) argc, (void) argv;

    lthread threads[NUM_THREADS];

    lthread_init();

    for (int ii = 0; ii < NUM_THREADS; ii++) {
        lthread_create(threads + ii, add, NULL);
    }

    for (int ii = 0; ii < NUM_THREADS; ii++) {
        lthread_join(threads[ii], NULL);
    }

    if (sum != (NUM_ADDS * NUM_THREADS)) {
        printf("Sum %zu doesn't match expected %zu\n", 
                sum, (size_t)NUM_ADDS * NUM_THREADS);
        return 1;
    }
    else {
        printf("Sum = %zu\n", sum);
    }

    return 0;
}
