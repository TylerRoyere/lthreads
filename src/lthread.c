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
#define LTHREAD_ALARM_INTERVAL_NS 500000/*(500000)*/
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

#ifdef LTHREAD_DEBUG
static size_t signal_handler_inst;
static struct timespec lthread_start;
static struct timespec lthread_end;

void lthread_debug_print_stats(void)
{
    double time;
    struct timespec diff = (struct timespec) {
        .tv_sec = lthread_end.tv_sec - lthread_start.tv_sec,
        .tv_nsec = lthread_end.tv_nsec - lthread_start.tv_nsec,
    };
    if (diff.tv_nsec < 0) {
        diff.tv_sec -= 1;
        diff.tv_nsec += 1000000000;
    }
    time = (double)diff.tv_sec + ((double)diff.tv_nsec / 1000000000);
    printf("Signal handler called %lf times per second\n",
            ((double)signal_handler_inst) / (time));
}
#endif


void start_thread(
        struct lthread_info *me,
        void *(*start_routine)(void *),
        void *data,
        void *stack
        );

/* Queue used for scheduling */
static struct lthread_info *head = NULL;
static struct lthread_info *tail = NULL;

/* Array used to store threads for return codes */
static struct lthread_info **lthreads = NULL;
static size_t nlthreads = 0;

/* Timer used for signals */
static timer_t lthread_timer;

/* Mask containing scheduling signal */
static sigset_t lthread_sig_mask;

/* Places thread 't' at the front of the queue */
static void
push_queue(struct lthread_info *t)
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
init_queue(struct lthread_info *main_thread)
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
    nlthreads *= 2;
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
    struct lthread_info *me = lthreads[id];
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
free_lthread(struct lthread_info *t)
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
lthread_stack_start(struct lthread_info *t)
{
    /* Everyone knows stacks grow upward :) */
    return (void*)( ((char*)t->stack) + LTHREAD_STACK_SIZE );
}

/* Returns non-zero if the specified thread's wake time is
 * before the current time
 */
static int
lthread_done_sleeping(struct lthread_info *t)
{
    struct timespec curr;
    int done;
    if (clock_gettime(LTHREAD_CLOCKID, &curr)) {
        perror("Failed to get current clock time");
        exit(EXIT_FAILURE);
    }
    done = (curr.tv_sec > t->wake_time.tv_sec) ||
        ( (curr.tv_sec == t->wake_time.tv_sec) && 
          (curr.tv_nsec >= t->wake_time.tv_nsec) );
    return done;
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

#ifdef LTHREAD_DEBUG
    signal_handler_inst++;
#endif

    /* save current thread execution */
    if (head->status == DONE) {
        /* If the entry is done, remove it from scheduling */
        remove_front = 1;
    }
    else {
        /* Otherwise, save status for later */
        if (head->status == RUNNING) {
            head->status = READY;
        }

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
            case SLEEPING:
                if (lthread_done_sleeping(head)) {
                    memset(&head->wake_time, 0, sizeof(head->wake_time));
                    head->status = READY;
                }
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
    BLOCK_SIGNAL();
    /* Delete timer */
    timer_delete(lthread_timer);
    /* Free lthreads array */
    free(lthreads);
    /* Free main thread information */
    free(head);
#ifdef LTHREAD_DEBUG
    clock_gettime(LTHREAD_CLOCKID, &lthread_end);
    lthread_debug_print_stats();
#endif
}

int lthread_init(void)
{
    struct lthread_info *new_thread;
    /* Action to perform on LTHREAD_SIG */
    struct sigaction act = {
        .sa_handler = lthread_alarm_handler, /* Scheduling handler */
        .sa_flags = SA_RESTART/*0SA_NODEFER*/, /* Can be interrupted within scheduler
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

#ifdef LTHREAD_DEBUG
    clock_gettime(LTHREAD_CLOCKID, &lthread_start);
#endif

    /* Unblock lthread signal */
    UNBLOCK_SIGNAL();

    return 0;
}

int
lthread_create(lthread *t, void *(*start_routine)(void *data), void *data)
{
    void *stack;
    struct lthread_info *new_thread;

    /* Stop interrupting me! */
    BLOCK_SIGNAL();

    /* Allocate space for new thread stack */
    stack = mmap(NULL, LTHREAD_STACK_SIZE,
            PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE | MAP_STACK, -1, 0);
    if (stack == MAP_FAILED) {
        perror("Failed to mmap stack space for new thread: ");
        exit(EXIT_FAILURE);
    }

    /* Allocate lthread storage */
    new_thread = malloc(sizeof(*new_thread));
    new_thread->id = allocate_lthread();
    lthreads[new_thread->id] = new_thread;
    *t = new_thread->id;

    /* Setup thread parameters */
#ifdef LTHREAD_DEBUG
    new_thread->stack_reg = VALGRIND_STACK_REGISTER(stack, stack + LTHREAD_STACK_SIZE);
#endif
    /* setup structure */
    new_thread->stack = stack;
    new_thread->start_routine = start_routine;
    new_thread->data = data;
    new_thread->status = READY;

    /* Use current context as starting context */
    if (getcontext(&new_thread->context)) {
        perror("Failed to get context");
        exit(EXIT_FAILURE);
    }

    /* Update current context with desired context for thread start */
    new_thread->context.uc_stack.ss_sp = stack;
    new_thread->context.uc_stack.ss_size = LTHREAD_STACK_SIZE;
    new_thread->context.uc_link = &head->context;
    makecontext(&new_thread->context, (void(*)(void))lthread_run, 1, new_thread->id);

    /* Add thread to end of scheduling queue */
    push_queue(new_thread);

    /* OK Now I'm done */
    UNBLOCK_SIGNAL();

    return 0;
}

/* Destroys the thread corresponding to 't'
 * so that it is no longer scheduled, just like
 * it no longer exists
 */
void
lthread_destroy(lthread t)
{
    BLOCK_SIGNAL();
    /* TODO: Check if valid ID before using */
    struct lthread_info *thread = lthreads[t];
    thread->status = DONE;
    UNBLOCK_SIGNAL();
    lthread_join(t, NULL);
}

/* Similar to pthread_join(), wait for the specified 
 * thread 't' to finish working. The value returned
 * by that thread will be placed in 'retval'
 */
int
lthread_join(lthread t, void **retval)
{
    /* Check that this is a valid thread */
    if (t >= nlthreads) {
        return 1;
    }

    struct lthread_info *thread = lthreads[t];
    if (thread == NULL) {
        return 1;
    }

    /* Wait for the  thread to complete naturally */
    while (thread->status != DONE) {
        raise(LTHREAD_SIG);
    }

    /* Save return value and deallocate resources */
    BLOCK_SIGNAL();
    if (retval != NULL) *retval = thread->data;
    deallocate_lthread(thread->id);
    free_lthread(thread);
    UNBLOCK_SIGNAL();


    return 0;
}

int
lthread_sleep(size_t milliseconds)
{
    const size_t nanoseconds = milliseconds * 1000 * 1000;
    /* Put the current time as the sleep time */
    if (clock_gettime(LTHREAD_CLOCKID, &head->wake_time)) {
        perror("Failed to get current clock time");
        exit(EXIT_FAILURE);
    }

    /* Add the time to wait to current time */
    head->wake_time = (struct timespec) {
        .tv_sec = head->wake_time.tv_sec + (long) nanoseconds / NSEC_PER_SEC,
        .tv_nsec = head->wake_time.tv_nsec + (long) nanoseconds % NSEC_PER_SEC,
    };

    /* Make sure nanoseconds value is less than 1000000000 */
    if (head->wake_time.tv_nsec > NSEC_PER_SEC) {
        head->wake_time.tv_sec++;
        head->wake_time.tv_nsec -= NSEC_PER_SEC;
    }

    /* This thread is now sleeping */
    head->status = SLEEPING;

    /* Scheduler, come and take me! */
    raise(LTHREAD_SIG);

    return 0;
}

int
lthread_yield(void)
{
    return raise(LTHREAD_SIG);
}

int
lthread_block(void)
{
    return BLOCK_SIGNAL();
    return 0;
}

int
lthread_unblock(void)
{
    return UNBLOCK_SIGNAL();
}
