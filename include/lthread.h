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
    SLEEPING,
};

struct lthread_info {
    void *(*start_routine)(void *data); /* Thread entry point */
    void *data; /* Data passed to entry point, return value */
    enum lthread_status status; /* Scheduling status of thread */
    ucontext_t context; /* Stored context of thread */
    void *stack; /* Pointer to end of the stack */
    size_t id; /* Allocated ID for the thread */
    struct timespec wake_time; /* Time to awake thread from SLEEPING */
    struct lthread_info *next; /* Next thread in the queue */
#ifdef LTHREAD_DEBUG
    /* Debug information to valgrind stops complaining */
    unsigned int stack_reg;
#endif
};

/* Thread handle will be its ID */
typedef size_t lthread;

/* Start scheduling lthreads */
int lthread_init(void);

/* Create an lthread 't' whose execution will start at 
 * the specified entry point 'start_routine'
 *
 * The start routing will be passed 'data' as the one
 * and only parameter to 'start_routine'
 *
 * When 'start_routine' completes, the return value will
 * be saved for retrieval by a subsquent call to lthread_join
 */
int lthread_create(lthread *t, void *(*start_routine)(void *data), void *data);

/* Waits for a thread 't' to complete execution. The return value of
 * that instance of 'start_routine' will be saved in 'retval' if
 * 'retval' is not NULL
 */
int lthread_join(lthread t, void **retval);

/* Stops a thread of executing in a more desructive fashion, the return
 * value is not recorded
 */
void lthread_destroy(lthread t);

/* Sleeps the currently executing thread for at least 'count' milliseconds.
 *
 * return value is zero on success, non-zero otherwise
 */
int lthread_sleep(size_t milliseconds);

/* Yeilds the execution of the current lthread so that another lthread
 * may begin execution. returns non-zero on failure
 */
int lthread_yield(void);

#endif
