#include <stdio.h>
#include <unistd.h>

#include "lthread.h"

void *
run(void *data)
{
    (void) data;
    for (int iters = 0; iters < 10; iters++) {
        printf("Thread doing things!\n");
        for (int ii = 0; ii < 1000000000; ii++) {
            ;
        }
    }
    return NULL;
}

int
main(int argc, char *argv[])
{
    struct lthread t;
    (void)argc;
    (void)argv;
    lthread_init();

    lthread_create(&t, NULL, run);

    for (;;) {
        printf("Main Thread doing things!\n");
        for (int ii = 0; ii < 1000000000; ii++) {
            ;
        }
    }
    return 0;
}
