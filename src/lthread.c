#include "lthread.h"

#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <time.h>

#include <unistd.h>
#include <signal.h>

#include <sys/mman.h>
#include <sys/time.h>

#ifndef LTHREAD_ALARM_INTERVAL
#define LTHREAD_ALARM_INTERVAL (999999999)
#endif 

#define NSEC_PER_SEC (1000000000)
#define LTHREAD_ALARM_INTERVAL_PART_NS ((LTHREAD_ALARM_INTERVAL) % NSEC_PER_SEC)
#define LTHREAD_ALARM_INTERVAL_PART_S ((LTHREAD_ALARM_INTERVAL) / NSEC_PER_SEC)

#ifndef LTHREAD_STACK_SIZE
#define LTHREAD_STACK_SIZE (2 * 1024 * 1024) /* 2MB */
#endif

#ifndef LTHREAD_INITIAL_LTHREADS
#define LTHREAD_INITIAL_LTHREADS 4
#endif

#ifndef LTHREAD_CLOCKID
#define LTHREAD_CLOCKID CLOCK_REALTIME
#endif

#ifndef LTHREAD_SIG
#define LTHREAD_SIG (SIGRTMIN)
#endif

#ifdef LTHREAD_DEBUG_SIGNAL_BLOCKING

#define UNBLOCK_SIGNAL() do { \
    sigprocmask(SIG_UNBLOCK, &lthread_sig_mask, NULL); \
    fprintf(stdout, "Unblocking signal %d:%d\n", __FILE__, __LINE__); \
} while (0)
#define BLOCK_SIGNAL() do { \
    sigprocmask(SIG_BLOCK, &lthread_sig_mask, NULL); \
    fprintf(stdout, "Blocking signal %d:%d\n", __FILE__, __LINE__); \
} while (0)

#else 

#define UNBLOCK_SIGNAL() sigprocmask(SIG_UNBLOCK, &lthread_sig_mask, NULL)
#define BLOCK_SIGNAL() sigprocmask(SIG_BLOCK, &lthread_sig_mask, NULL)

#endif


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

/* Timer used for signals */
static timer_t lthread_timer;

/* Mask containing scheduling signal */
static sigset_t lthread_sig_mask;

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
    me->data = start_routine(data);
    me->status = DONE;
    printf("Thread finished\n");
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
change_alarm(int turn_on)
{
    /* The on structure starts with the default scheduling parameters */
    static struct itimerspec on = {
        .it_interval = {
            .tv_sec = LTHREAD_ALARM_INTERVAL_PART_S,
            .tv_nsec = LTHREAD_ALARM_INTERVAL_PART_NS,
        },
        .it_value = {
            .tv_sec = LTHREAD_ALARM_INTERVAL_PART_S,
            .tv_nsec = LTHREAD_ALARM_INTERVAL_PART_NS,
        },
    };
    /* The off structure is just all 0s */
    static struct itimerspec off = {
        .it_interval = {
            .tv_sec = 0,
            .tv_nsec = 0,
        },
        .it_value = {
            .tv_sec = 0,
            .tv_nsec = 0,
        },
    };

    if (turn_on) {
        /* Turn the timer on */
        timer_settime(lthread_timer, 0, &on, &off);
    }
    else {
        /* Turn the timer off */
        timer_settime(lthread_timer, 0, &off, &on);
    }
}


static void
lthread_alarm_handler(int num)
{
    BLOCK_SIGNAL();
    printf("LTHREAD_SIG %d sent!\n", num);

    /* save current thread execution */
    if (head->status == DONE) {
        fprintf(stderr, "TODO: handle return value\n");
        pop_queue();
    }
    else {
        head->status = READY;

        if (sigsetjmp(head->context, 1) == 1) {
            UNBLOCK_SIGNAL();
            return;
        }

        bump_queue();
    }

    while (head->status != READY) {
        switch (head->status) {
            case CREATED:
               puts("Creating thread");
                UNBLOCK_SIGNAL();
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
    /* Action to perform on LTHREAD_SIG */
    struct sigaction act = {
        .sa_handler = lthread_alarm_handler, /* Scheduling handler */
        .sa_flags = 0/*SA_NODEFER*/, /* Can be interrupted within scheduler
                                -- Maybe this shouldn't be the case? */
    };
    struct sigevent event = {
        .sigev_notify = SIGEV_SIGNAL, /* Call handler on signal */
        .sigev_signo = LTHREAD_SIG, /* Signal number, based on SIGRTALRM */
        .sigev_value.sival_ptr = &lthread_timer, /* Timer to use if necessary */
    };

    /* Allocate thread storage */
    lthreads = calloc(LTHREAD_INITIAL_LTHREADS, sizeof(*lthreads));
    nlthreads = LTHREAD_INITIAL_LTHREADS;

    /* Setup signal mask */
    sigemptyset(&lthread_sig_mask);
    sigaddset(&lthread_sig_mask, LTHREAD_SIG);

    /* Set LTHREAD_SIG signal handler */
    sigemptyset(&act.sa_mask);
    if (sigaction(LTHREAD_SIG , &act, NULL)) {
        fprintf(stderr, "Failed to set LTHREAD_SIG handler\n");
        exit(EXIT_FAILURE);
    }

    /* block lthread signal */
    BLOCK_SIGNAL();

    /* Create timer */
    if (timer_create(LTHREAD_CLOCKID, &event, &lthread_timer) == -1) {
        perror("Failed to create timer");
        exit(EXIT_FAILURE);
    }

    /* Setup main thread context */
    new_thread = malloc(sizeof(*new_thread));
    new_thread->status = RUNNING;
    sigsetjmp(new_thread->context, 1);
    init_queue(new_thread);

    /* Start scheduled signals */
    change_alarm(1);

    /* Unblock lthread signal */
    UNBLOCK_SIGNAL();

    return 0;
}

int
lthread_create(struct lthread *t, void *data, void *(*start_routine)(void *data))
{
    void *stack;
    struct lthread *new_thread;

    BLOCK_SIGNAL();

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

    UNBLOCK_SIGNAL();

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
