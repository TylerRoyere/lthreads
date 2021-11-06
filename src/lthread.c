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
#include <ucontext.h>

#include <sys/mman.h>
#include <sys/time.h>

#ifdef LTHREAD_DEBUG
#include <valgrind/valgrind.h>
#endif

#ifndef LTHREAD_ALARM_INTERVAL_NS
#define LTHREAD_ALARM_INTERVAL_NS 50000/*(500000)*/
#endif 

#define NSEC_PER_SEC (1000000000)
#define LTHREAD_ALARM_INTERVAL_PART_NS ((LTHREAD_ALARM_INTERVAL_NS) % NSEC_PER_SEC)
#define LTHREAD_ALARM_INTERVAL_PART_S ((LTHREAD_ALARM_INTERVAL_NS) / NSEC_PER_SEC)

#ifndef LTHREAD_STACK_SIZE
#define LTHREAD_STACK_SIZE (2 * 1024 * 1024) /* 2MB */
#endif

#ifndef LTHREAD_MAIN_THREAD
#define LTHREAD_MAIN_THREAD 1000000
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

/* Places thread 't' at the front of the queue */
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

/* Initializes the queue with a main thread */
static void
init_queue(struct lthread *main_thread)
{
    /* Main thread is always running, and initializes queue */
    head = tail = main_thread->next = main_thread;
}

/* Removes the first element from the queue */
static void
pop_queue(void)
{
    /* Use same assumption as push_queue(), the main thread
     * should always be running
     */
    tail->next = head->next;
    head = head->next;
}

/* Bumps tail and start forward 1, if 'rem' is non-zero
 * it will first remove the front element
 */
static void
bump_queue(int rem)
{
    if (!rem) {
        /* Move tail and head forward one entry */
        tail = head;
        head = head->next;
    }
    else {
        pop_queue();
    }
}

/* Gets an index that can be used to store the value of
 * the lthread structure for a thread so it may persist
 * after being de-scheduled
 */
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


/* Uses the provided index to "free" the entry used so it 
 * may be used for another thread
 */
static void
deallocate_lthread(size_t id)
{
    lthreads[id] = NULL;
}

/* Entry point for new thread
 */
extern void
lthread_run(int id)
{
#ifdef LTHREAD_DEBUG
    printf("LTHREAD: Starting lthread!\n");
#endif
    UNBLOCK_SIGNAL();
    struct lthread *me = lthreads[id];
    me->status = RUNNING;
    me->data = me->start_routine(me->data);
    me->status = DONE;
#ifdef LTHREAD_DEBUG
    printf("LTHREAD: Thread finished\n");
#endif
    for (;;) raise(LTHREAD_SIG);
}

/* Handles freeing resources held by thread
 */
static void
free_lthread(struct lthread *t)
{
#ifdef LTHREAD_DEBUG
    VALGRIND_STACK_DEREGISTER(t->stack_reg);
#endif
    munmap(t->stack, LTHREAD_STACK_SIZE);
    free(t);
}

/* Returns the pointer to the start of the upwards growing stack
 * for thread 't'
 */
static void *
lthread_stack_start(struct lthread *t)
{
    /* Everyone knows stacks grow upward :) */
    return (void*)( ((char*)t->stack) + LTHREAD_STACK_SIZE );
}

/* Conditionally starts and stops the timer whose expiration
 * sends a signal to the process therebye invoking the scheduler
 */
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

/* LTHREAD_SIG signal handler, used to handle the scheduling of
 * threads
 */
static void
lthread_alarm_handler(int num)
{
    BLOCK_SIGNAL();
    int remove_front = 0; /* Indicated if the first entry should be removed */
    (void)num;

    /* save current thread execution */
    if (head->status == DONE) {
        /* If the entry is done, remove it from scheduling */
        remove_front = 1;
    }
    else {
        /* Otherwise, save status for later */
        head->status = READY;

        if (getcontext(&head->context)) {
            perror("Failed to get context");
            exit(EXIT_FAILURE);
        }

        /* getcontext doesn't work like setjmp, need to use
         * status flag to ensure this thread only leaves when it is chosen */
        if (head->status == RUNNING) {
            UNBLOCK_SIGNAL();
            return;
        }
        remove_front = 0;
    }

    /* Find next runnable thread in the queue */
    do {
        /* Move the list forward one (removing first element if necessary */
        bump_queue(remove_front);
        remove_front = 0;
        switch (head->status) {
            case CREATED:
                UNBLOCK_SIGNAL();
                start_thread(head, head->start_routine, head->data, lthread_stack_start(head));
                break;
            case RUNNING:
                fprintf(stderr, "Thread marked running when it shouldn't be!\n");
                break;
            case READY:
                break;
            case DONE:
                remove_front = 1;
                break;
            case BLOCKED:
                break;
            default:
                fprintf(stderr, "Invalid state %d\n", head->status);
        }
    } while (head->status != READY);
    /* Setup thread and swap to its context */
    head->status = RUNNING;
    setcontext(&head->context);
}

/* Cleans up the environment when exiting */
void
lthread_cleanup(void)
{
    /* Delete timer */
    timer_delete(lthread_timer);
    /* Free lthreads array */
    free(lthreads);
    /* Free main thread information */
    free(head);
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
    new_thread->id = LTHREAD_MAIN_THREAD;

    /* Setup main threads context as current context */
    if (getcontext(&new_thread->context)) {
        perror("Failed to get context");
        exit(EXIT_FAILURE);
    }

    init_queue(new_thread);

    /* Add cleanup function run at exit() */
    atexit(lthread_cleanup);

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

    /* Stop interrupting me! */
    BLOCK_SIGNAL();

    /* Allocate space for new thread stack */
    stack = mmap(NULL, LTHREAD_STACK_SIZE,
            PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_STACK, -1, 0);
    if (stack == MAP_FAILED) {
        perror("Failed to mmap stack space for new thread: ");
        exit(EXIT_FAILURE);
    }
    /* Setup thread parameters */
#ifdef LTHREAD_DEBUG
    t->stack_reg = VALGRIND_STACK_REGISTER(stack, stack + LTHREAD_STACK_SIZE);
#endif
    /* setup structure */
    t->stack = stack;
    t->start_routine = start_routine;
    t->data = data;
    t->status = READY;
    t->id = allocate_lthread(); /* allocate handle for thread */

    /* Use current context as starting context */
    if (getcontext(&t->context)) {
        perror("Failed to get context");
        exit(EXIT_FAILURE);
    }

    /* Update current context with desired context for thread start */
    t->context.uc_stack.ss_sp = stack;
    t->context.uc_stack.ss_size = LTHREAD_STACK_SIZE;
    t->context.uc_link = &head->context;
    makecontext(&t->context, (void(*)(void))lthread_run, 1, t->id);

    /* Copy structure to internal one, this indicated that maybe
     * the interface needs to be a little different */
    new_thread = malloc(sizeof(*new_thread));
    memcpy(new_thread, t, sizeof(*new_thread));
    lthreads[t->id] = new_thread;
    push_queue(new_thread); /* Add thread to end of scheduling queue */

    /* OK Now I'm done */
    UNBLOCK_SIGNAL();

    return 0;
}

/* Destroys the thread corresponding to 't'
 * so that it is no longer scheduled, just like
 * it no longer exists
 */
void
lthread_destroy(struct lthread *t)
{
    BLOCK_SIGNAL();
    struct lthread *thread = lthreads[t->id];
    thread->status = DONE;
    UNBLOCK_SIGNAL();
    lthread_join(thread, NULL);
}

/* Similar to pthread_join(), wait for the specified 
 * thread 't' to finish working. The value returned
 * by that thread will be placed in 'retval'
 */
int
lthread_join(struct lthread *t, void **retval)
{
    /* Check that this is a valid thread */
    if (t->id >= nlthreads) {
        return 1;
    }

    struct lthread *thread = lthreads[t->id];
    if (thread == NULL) {
        return 1;
    }

    /* Wait for the  thread to complete naturally */
    while (thread->status != DONE) {
        raise(LTHREAD_SIG);
    }

    /* Save return value and deallocat resources */
    BLOCK_SIGNAL();
    if (retval != NULL) *retval = thread->data;
    deallocate_lthread(thread->id);
    free_lthread(thread);
    UNBLOCK_SIGNAL();


    return 0;
}
