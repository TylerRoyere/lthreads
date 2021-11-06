#include "lthread.h"

#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>

#include <unistd.h>
#include <signal.h>

#include <sys/mman.h>

#ifndef LTHREAD_ALARM_INTERVAL
#define LTHREAD_ALARM_INTERVAL (999999)
#endif 

#ifndef LTHREAD_STACK_SIZE
#define LTHREAD_STACK_SIZE (2 * 1024 * 1024) /* 2MB */
#endif

#ifndef LTHREAD_INITIAL_LTHREADS
#define LTHREAD_INITIAL_LTHREADS 4
#endif


#define BLOCK_SIGNAL()
#define UNBLOCK_SIGNAL()


void start_thread(
        struct lthread *me,
        void *(*start_routine)(void *),
        void *data,
        void *stack
        );

/* Queue used for scheduling */
static struct lthread *head = NULL;
static struct lthread *tail = NULL;

/* Array used to store threads for return codes */
static struct lthread **lthreads = NULL;
static size_t nlthreads = 0;

static void
push_queue(struct lthread *t)
{
    /* We can safely assume the main 
     * thread is always running after lthread_init()
     */
    t->next = head;
    tail->next = t;
    tail = t;
}

static void
init_queue(struct lthread *main_thread)
{
    /* Main thread is always running, and initializes queue */
    head = tail = main_thread->next = main_thread;
}

static void
bump_queue(void)
{
    /* Move tail and head forward one entry */
    tail = head;
    head = head->next;
}

static void
pop_queue(void)
{
    /* Use same assumption as push_queue(), the main thread
     * should always be running
     */
    tail->next = head->next;
    head = head->next;
}

static size_t
allocate_lthread(void)
{
    size_t old_size, ii;
    if (lthreads == NULL) {
        fprintf(stderr, "No lthread storage, need to call lthread_init() first\n");
        exit(EXIT_FAILURE);
    }

    /* Search for open return code slot */
    for (ii = 0; ii < nlthreads; ii++) {
        if (lthreads[ii] == NULL) {
            return ii;
        }
    }

    /* None were found, we need to allocate more */
    old_size = nlthreads;
    nlthreads += 2;
    lthreads = realloc(lthreads, sizeof(*lthreads) * nlthreads);

    /* Clear newly allocated return codes since they may not be 0 */
    memset(lthreads + old_size, 0, old_size * sizeof(*lthreads));

    return old_size;
}

static void
deallocate_lthread(size_t id)
{
    lthreads[id] = NULL;
}

extern void
lthread_run(
        struct lthread *me,
        void *(*start_routine)(void *),
        void *data
        )
{
    printf("Starting lthread!\n");
    me->status = RUNNING;
    void *ret;
    ret = start_routine(data);
    me->data = ret;
    me->status = DONE;
    printf("Thread finished\n");
    ualarm(1, LTHREAD_ALARM_INTERVAL);
    for (;;);
}

static void
free_lthread(struct lthread *t)
{
#ifdef LTHREAD_DEBUG
    VALGRIND_STACK_DEREGISTER(t->stack_reg);
#endif
    munmap(t->stack, LTHREAD_STACK_SIZE);
    free(t);
}

static void *
lthread_stack_start(struct lthread *t)
{
    /* Everyone knows stacks grow upward :) */
    return (void*)( ((char*)t->stack) + LTHREAD_STACK_SIZE );
}


static void
enable_alarm(void)
{
    sigset_t mask;
    /* Unblock SIGALRM */
    sigemptyset(&mask);
    sigaddset(&mask, SIGALRM);
    if (sigprocmask(SIG_UNBLOCK, &mask, NULL)) {
        perror("Failed to unblock SIGALRM");
        exit(EXIT_FAILURE);
    }
}

static void
disable_alarm(void)
{
    sigset_t mask;
    /* Block SIGALRM */
    sigemptyset(&mask);
    sigaddset(&mask, SIGALRM);
    if (sigprocmask(SIG_BLOCK, &mask, NULL)) {
        perror("Failed to block SIGALRM");
        exit(EXIT_FAILURE);
    }
}


static void
lthread_alarm_handler(int num)
{
    printf("SIGALRM %d sent!\n", num);

    /* save current thread execution */
    if (head->status == DONE) {
        fprintf(stderr, "TODO: handle return value\n");
        pop_queue();
    }
    else {
        head->status = READY;

        if (sigsetjmp(head->context, 1) == 1)
            return;

        bump_queue();
    }

    while (head->status != READY) {
        switch (head->status) {
            case CREATED:
                puts("Creating thread");
                start_thread(head, head->start_routine, head->data, lthread_stack_start(head));
                break;
            case RUNNING:
                fprintf(stderr, "Thread marked running when it shouldn't be!\n");
                break;
            case READY:
                break;
            case DONE:
                break;
            case BLOCKED:
                break;
            default:
                fprintf(stderr, "Invalid state %d\n", head->status);
        }
        bump_queue();
    }
    siglongjmp(head->context, 1);
}

int lthread_init(void)
{
    struct lthread *new_thread;
    struct sigaction act = {
        .sa_handler = lthread_alarm_handler,
        .sa_flags = SA_NODEFER,
    };

    /* Set SIGALRM signal handler */
    sigemptyset(&act.sa_mask);
    if (sigaction(SIGALRM, &act, NULL)) {
        fprintf(stderr, "Failed to set SIGALRM handler\n");
        exit(EXIT_FAILURE);
    }

    /* Allocate thread storage */
    lthreads = calloc(LTHREAD_INITIAL_LTHREADS, sizeof(*lthreads));
    nlthreads = LTHREAD_INITIAL_LTHREADS;

    /* Unblock SIGALRM */
    enable_alarm();

    /* Setup main thread context */
    new_thread = malloc(sizeof(*new_thread));
    new_thread->status = RUNNING;
    sigsetjmp(new_thread->context, 1);
    init_queue(new_thread);

    /* Start alarming */
    ualarm(1, LTHREAD_ALARM_INTERVAL);

    return 0;
}

int
lthread_create(struct lthread *t, void *data, void *(*start_routine)(void *data))
{
    void *stack;
    struct lthread *new_thread;

    stack = mmap(NULL, LTHREAD_STACK_SIZE,
            PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_STACK, -1, 0);
    if (stack == MAP_FAILED) {
        perror("Failed to mmap stack space for new thread: ");
        exit(EXIT_FAILURE);
    }
#ifdef LTHRAED_DEBUG
    t->stack_reg = VALGRIND_STACK_REGISTER(stack, stack + LTHREAD_STACK_SIZE);
#endif
    /* setup structure */
    t->stack = stack;
    t->start_routine = start_routine;
    t->data = data;
    t->status = CREATED;
    t->id = allocate_lthread(); /* allocate handle for thread */

    /* Copy structure to internal one, this indicated that maybe
     * the interface needs to be a little different */
    new_thread = malloc(sizeof(*new_thread));
    memcpy(new_thread, t, sizeof(*new_thread));
    lthreads[t->id] = new_thread;
    push_queue(new_thread); /* Add thread to end of scheduling queue */

    return 0;
}

void
lthread_destroy(struct lthread *t)
{
    BLOCK_SIGNAL();
    struct lthread *thread = lthreads[t->id];
    thread->status = DONE;
    UNBLOCK_SIGNAL();
    lthread_join(thread, NULL);
}

int
lthread_join(struct lthread *t, void **retval)
{
    if (t->id >= nlthreads) {
        return 1;
    }

    struct lthread *thread = lthreads[t->id];
    if (thread == NULL) {
        return 1;
    }

    while (thread->status != DONE) {
        //raise(LTHREAD_SIG)
    }

    BLOCK_SIGNAL();
    if (retval != NULL) *retval = thread->data;
    deallocate_lthread(thread->id);
    free_lthread(thread);
    UNBLOCK_SIGNAL();


    return 0;
}
