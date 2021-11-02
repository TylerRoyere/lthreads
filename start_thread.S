.globl start_thread
start_thread:
   /* movq %rcx, %rbp */
    movq %rcx, %rsp
    call lthread_run
