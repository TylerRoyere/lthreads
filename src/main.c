#include <stdio.h>
#include <unistd.h>

#include "lthread.h"

void *
run(void *data)
{
    (void)data;
    char buffer[128];
    fflush(stdin);
    for (int ii = 0; ii < 5; ii++) {
        scanf("%s", buffer);
        buffer[127] = '\0';
        printf("%s\n", buffer);
    }
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
