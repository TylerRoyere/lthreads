#ifndef LTHREAD_H
#define LTHREAD_H 

#include <stdlib.h>
#include <setjmp.h>
#include <ucontext.h>

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
    ucontext_t context;
    void *stack;
    size_t id;
    struct lthread *next;
#ifdef LTHREAD_DEBUG
    unsigned int stack_reg;
#endif
};


int lthread_init(void);
int lthread_create(struct lthread *t, void *data, void *(*start_routine)(void *data));
int lthread_join(struct lthread *t, void **retval);
void lthread_destroy(struct lthread *t);

#endif
