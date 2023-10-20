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

#define SZ_STACK (1024*1024)

struct thread {
    jmp_buf env;
    enum {
        STATUS_,
        STATUS_RUNNING,
        STATUS_SLEEPING,
        STATUS_TERMINATED
    } status;
    struct {
        void *memory_; /* where it's located at */
        void *memory; /* the one we use */
    } stack;
    scheduler_fnc_t fnc;
    void *arg;
    struct thread *next;
};

static struct {
    struct thread *head;
    struct thread *current;
    jmp_buf env;
} scheduler;

struct thread *thread_candidate(void) {
    struct thread *curr = scheduler.current->next;
    while (curr != scheduler.current) {
        if (curr->status != STATUS_TERMINATED) {
            return curr;
        }
        curr = curr->next;
    }
    return NULL;
}

int scheduler_create(scheduler_fnc_t fnc, void *arg) {
    size_t PAGE_SIZE = page_size();

    struct thread *thread = malloc(sizeof(struct thread));
    if (!thread) {
        TRACE("malloc failed\n");
        return -1;
    }

    thread->stack.memory_ = malloc(SZ_STACK + PAGE_SIZE);
    if (!thread->stack.memory_) {
        TRACE("malloc failed\n");
        free(thread);
        return -1;
    }

    thread->stack.memory = memory_align(thread->stack.memory_, PAGE_SIZE);
    if (!thread->stack.memory) {
        TRACE("malloc failed\n");
        free(thread->stack.memory_);
        free(thread);
        return -1;
    }

    thread->status = STATUS_;
    thread->fnc = fnc;
    thread->arg = arg;
    thread->next = NULL;

    if (scheduler.head == NULL) {
        scheduler.head = thread;
        scheduler.current = thread;
        thread->next = thread;
    } else {
        struct thread *current = scheduler.current;
        struct thread *next = scheduler.current->next;
        current->next = thread;
        thread->next = next;
    }

    return 0;
}

void schedule(void) {
    struct thread *candidate = thread_candidate();
    if (candidate == NULL) {
        return;
    }

    scheduler.current = candidate;

    if (scheduler.current->status == STATUS_) {
        uint64_t rsp = (uint64_t) scheduler.current->stack.memory + SZ_STACK;
        __asm__ volatile ("mov %[rs], %%rsp \n" : [rs] "+r"(rsp)
        ::);

        scheduler.current->fnc(scheduler.current->arg);

        scheduler.current->status = STATUS_TERMINATED;
        longjmp(scheduler.env, 1);
    } else {
        scheduler.current->status = STATUS_RUNNING;
        longjmp(scheduler.current->env, 1);
    }
}

void destroy(void) {
    struct thread *curr = scheduler.current->next;
    while (curr != scheduler.current) {
        if (curr->status == STATUS_TERMINATED) {
            struct thread *next = curr->next;
            free(curr->stack.memory_);
            free(curr);
            curr = next;
        } else {
            curr = curr->next;
        }
    }

    if (scheduler.current->status == STATUS_TERMINATED) {
        free(scheduler.current->stack.memory_);
        free(scheduler.current);
        scheduler.current = NULL;
    }

    if (scheduler.current == NULL) {
        scheduler.head = NULL;
    }
}

void scheduler_execute(void) {
    setjmp(scheduler.env);
    schedule();
    destroy();
}

void scheduler_yield(void) {
    if (setjmp(scheduler.current->env) == 0) {
        scheduler.current->status = STATUS_SLEEPING;
        longjmp(scheduler.env, 1);
    }
}
