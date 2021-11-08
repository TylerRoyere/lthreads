#include <stdio.h>

#include "lthread.h"

#define ADD_TIMES (5000000)
#define REPEATS (20)
#define NUM_THREADS (25)

void *
add_things(void *data)
{
    int increment = *(int *)data;
    int *retval = malloc(sizeof(int));

    for (int itr = 0; itr < REPEATS; itr++) {
        *retval = 0;
        for (int ii = 0; ii < ADD_TIMES; ii++) {
            *retval += increment;
        }
    }

    return retval;
}

int main(int argc, char *argv[])
{
    (void) argc, (void) argv;
    lthread threads[NUM_THREADS];
    int increments[NUM_THREADS];
    int *temp;

    lthread_init();

    for (int ii = 0; ii < NUM_THREADS; ii++) {
        increments[ii] = ii;
        lthread_create(threads + ii, add_things, increments + ii);
    }

    for (int ii = 0; ii < NUM_THREADS; ii++) {
        int expected = (ii * ADD_TIMES);
        lthread_join(threads[ii], (void**)&temp);
        if (*temp != expected) {
            printf("[%d] %d != %d\n",
                    ii, *temp, expected);
            return 1;
        }
        else {
            printf("[%d] = %d\n", ii, *temp);
        }
    }

    return 0;
}
