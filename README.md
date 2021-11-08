# lthreads
"Lightweight" Userspace Threads for C with preemptive scheduling!

## Motivation
Preemptive scheduling is a tool typically used by Operating Systems to schedule processes. I thought it would be interesting to do the same but in userspace. But, the reality is preemptive userspace threading implementations have few applications in real programming. These restrictions are a result of the many async-signal-unsafe functions that most programs use that need to be handled carefully when signal based preeemption for thread scheduling is desired.

## Building Tests
Building the test executables is relatively easy, assuming `make` and `gcc` are already installed it is as simple as

```
$ make tests
```

## Using lthreads
To start using lthreads the program must first call `lthread_init()` so the lthread implementation may setup it's environment. This setup includes establishing a timer and signal handler to preempt the execution of threads for scheduling purposes. Then lthreads may be created in a similar fashion to commonly used pthreads. The main utilities of interest are:

1. `int lthread_create(lthread *t, void *(*start_routine)(void *), void *data);` - Initializes a new lthread `t` that will start execution at `start_routine` and will pass that entry point the value of `data`.
2. `int lthread_join(lthread t, void **retval);` - Waits for the specified lthread 't' to finish if it hasn't finished already. Once 't' is finished the value returned by 't' will be assigned to `*retval` so the program can obtain the threads return value. Afterwards the corresponding thread is destroyed, and all resources are no-longer associated with that thread.
3. `int lthread_yield(void);` - Stops the current thread of execution and gives the scheduler a chance to run a different thread.
4. `int lthread_sleep(size_t milliseconds);` - Sleeps the currently executing lthread for at least 'milliseconds' milliseconds. The actual time spent sleeping may be much larger than the specified time, but never smaller.
5. `int lthread_block(void);` and `int lthread_unblock(void);` - Stops and starts the preemption of the currently executing lthread respectively. These are useful when trying to modify globally shared resources which need to be synchronized (similar in vein to `pthread_mutex_t`'s `pthread_mutex_lock()` and `pthread_mutex_unlock()`). 
6. `LTHREAD_SAFE` - Following this macro a new block is created that will execute the contents of the block in preemption blocked context like those created by wrapping the code in `lthread_block();` and `lthread_unblock();` calls. If `CODE_BLOCK` expanded to the contents of the block after the `THREAD_SAFE` declaration, this is equivalent to:
    ```
    lthread_block();
    CODE_BLOCK
    lthread_unblock();
    ```
    
## A note on `signal-safety(7)`
This implementation of preemptive userspace threading utilizes posix timers to send scheduling signals that result in `setcontext();` calls to change thread execution. This scheduling architecture makes it possible that an async-signal-unsafe function is interrupted to execute a different lthread. Calling any other async-signal-unsafe function after a new lthread is scheduled to run will likely result in undefined behavior. Behavior that should be avoided whenever possible. As a result, if having well defined behavior is of any importantance calling async-signal-unsafe functions after `lthread_init();` must be done with care to avoid causing problems. This is why `LTHREAD_SAFE` was created. It can create a sufficiently safe environment to call these async-signal-unsafe functions from without causing possibly undefined behavior. 

All that being said this limits the usefulness of lthreads because performing many operations requires the lthread in question block all other lthreads from executing, defeating the purpose of using preemptive userspace thread scheduling. This may or may not be that tradgic depending on what you want to accomplish, or if you are willing to use many posix defined IO operations that are async-signal-safe. Regardless, special care needs to be made to ensure program behavior is defined beyond that of even pthread programs.
