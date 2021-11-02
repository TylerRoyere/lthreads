#ifndef LTHREAD_H
#define LTHREAD_H 

#include <setjmp.h>

enum lthread_status {
    CREATED = 0,
    RUNNING,
    READY,
    BLOCKED,
    DONE,
};

struct lthread {
    void *(*start_routine)(void *data);
    void *data;
    enum lthread_status status;
    sigjmp_buf context;
    void *stack;
    int id;
    struct lthread *next;
};


int lthread_init(void);
int lthread_create(struct lthread *t, void *data, void *(*start_routine)(void *data));
int lthread_destroy(struct lthread *t, void **retval);

#endif
