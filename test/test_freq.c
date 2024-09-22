#include <stdio.h>
#include "lthread.h"

#define N 1000
#define M 1000

int main(int argc, char *argv[])
{
    (void)argc, (void)argv;

    lthread_init();

    for (int ii = 0; ii < N; ii++) {
        for (int jj = 0; jj < M; jj++) {
            lthread_yield();
        }
    }

    return 0;
}
