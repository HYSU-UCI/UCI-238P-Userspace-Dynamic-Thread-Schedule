/**
 * Tony Givargis
 * Copyright (C), 2023
 * University of California, Irvine
 *
 * CS 238P - Operating Systems
 * scheduler.c
 */

#undef _FORTIFY_SOURCE

#include <unistd.h>
#include <signal.h>
#include <setjmp.h>
#include "system.h"
#include "scheduler.h"

/**
 * Needs:
 *   setjmp()
 *   longjmp()
 */

/* research the above Needed API and design accordingly */

#define SZ_STACK 4096

struct thread {

    jmp_buf ctx;
    void *arg;
    scheduler_fnc_t fnc;
    struct {
        char *memory;
        char *memory_; 
    } stack;

    enum {
        INIT, 
        RUNNING, 
        SLEEPING, 
        TERMINATED
    } status;

    struct thread *next;
};
static struct {
        struct thread *head;
        struct thread *current;
        jmp_buf ctx;
} thread_link;

/**
 * Creates a new user thread.
 *
 * fnc: the start function of the user thread (see scheduler_fnc_t)
 * arg: a pass-through pointer defining the context of the user thread
 *
 * return: 0 on success, otherwise error
 */

int scheduler_create(scheduler_fnc_t fnc, void *arg) {

    size_t PAGE_SIZE = page_size();

    struct thread *new_thread = malloc(sizeof(struct thread));
    if (!new_thread) {
        TRACE("thread malloc failed");
        return -1;
    }

    new_thread->status = INIT;
    new_thread->fnc = fnc;
    new_thread->arg = arg;

    new_thread->stack.memory_ = malloc(SZ_STACK + PAGE_SIZE);
    if (!new_thread->stack.memory_) {
        TRACE("stack memory malloc failed");
        return -1;
    }
    new_thread->stack.memory = memory_align(new_thread->stack.memory_, PAGE_SIZE);

    /* add the new thread behind current thread */
    /* make the link a loop so it is convenient traverse the whole link */
    if (thread_link.head == NULL) {
        thread_link.head = new_thread;
        thread_link.current = new_thread;
        new_thread->next = new_thread;
    }
    else {
        struct thread *curr = thread_link.current; 
        new_thread->next = curr->next;     
        curr->next = new_thread;        
    }

    return 0;
}

struct thread *choose_candidate(void) {

    struct thread *curr = thread_link.current->next;
    
    while (curr != thread_link.current) {
        if (curr->status == INIT || curr->status == SLEEPING) {
            return curr;
        }
        curr = curr->next;
    }

    return NULL;
}

void schedule(void) {

    struct thread *candidate = choose_candidate();
    if (!candidate) {
        return;
    }

    thread_link.current = candidate;
    if (thread_link.current->status == INIT) {
        uint64_t rsp = (uint64_t) thread_link.current->stack.memory + SZ_STACK;
        __asm__ volatile ("mov %[rs], %%rsp" : [rs] "+r"(rsp) ::);

        thread_link.current->status = RUNNING;
        thread_link.current->fnc(thread_link.current->arg);
        thread_link.current->status = TERMINATED;
        longjmp(thread_link.ctx, 1);       
    }
    else {
        thread_link.current->status = RUNNING;
        longjmp(thread_link.current->ctx, 1);
    }  
}

void destroy(void) {

    struct thread *curr = thread_link.head;
    if (!curr) {
        return;
    }

    curr = curr->next;
    while (curr != thread_link.head) {
        struct thread *next = curr->next;
        free(curr->stack.memory_);
        free(curr);
        curr = next;
    }

    thread_link.head = NULL;
    thread_link.current = NULL;
    free(thread_link.head);
    free(thread_link.current);
}

/*  alarm_handler will be called when SIGALRM is triggered every second. 
    It yield the current thread to allow a context switch. */

void alarm_handler(int signum) {

    /* (void)signum; */
    if (signum == SIGALRM) {
        scheduler_yield();
    }    
}

void set_alarm(void) {

    signal(SIGALRM, alarm_handler);
    alarm(1);
}

void clear_alarm(void) {
    
    signal(SIGALRM, SIG_DFL);
    alarm(0);
}

/**
 * Called to execute the user threads previously created by calling
 * scheduler_create().
 *
 * Notes:
 *   * This function should be called after a sequence of 0 or more
 *     scheduler_create() calls.
 *   * This function returns after all user threads (previously created)
 *     have terminated.
 *   * This function is not re-enterant.
 */

void scheduler_execute(void) {

    setjmp(thread_link.ctx);
    /* extra credits */
    set_alarm();
    schedule();   
    clear_alarm();
    destroy();
}

/**
 * Called from within a user thread to yield the CPU to another user thread.
 */

void scheduler_yield(void) {

    if (!setjmp(thread_link.current->ctx)) {
        thread_link.current->status = SLEEPING;
        longjmp(thread_link.ctx, 1);
    }
}

