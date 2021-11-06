#include <stdio.h>
#include <unistd.h>

#include "lthread.h"

void *
run(void *data)
{
    void *ret = malloc(sizeof(unsigned int));
    printf("Thread doing things!\n");
    for (int ii = 0; ii < 1000000; ii++) {
        ;
    }
    printf("Thread done\n");
    *(unsigned int*)ret= 0xDEAD0000 ^ *(unsigned int*)data;
    return ret;
}

int
main(int argc, char *argv[])
{
    struct lthread t1, t2;
    void *retval;
    unsigned int values[] = {1, 2};
    (void)argc;
    (void)argv;
    lthread_init();

    lthread_create(&t1, values + 0, run);
    lthread_create(&t2, values + 1, run);

    printf("Main Thread doing things!\n");

    lthread_join(&t1, &retval);
    printf("Thread returned 0x%08X\n", *(unsigned int*)retval);
    free(retval);
    lthread_join(&t2, &retval);
    printf("Thread returned 0x%08X\n", *(unsigned int*)retval);
    free(retval);

    
    return 0;
}
