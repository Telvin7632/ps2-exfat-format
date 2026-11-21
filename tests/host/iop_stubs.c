/*
 * Host implementations of the IOP services imported by iop/main.c
 * (see iop/imports.lst), with hooks the tests use to inspect what happened.
 */
#include <stdlib.h>
#include "irx_imports.h"
#include "kerr.h"
#include "iop_stubs.h"

/* ---------------------------------------------------------------- BDM */

struct block_device *stub_bd_list[STUB_MAX_BD];

void bdm_get_bd(struct block_device **pbd, unsigned int count)
{
    unsigned int i;

    /* Same contract as PS2SDK bdm.c: fill up to min(count, 20) slots */
    for (i = 0; i < count && i < STUB_MAX_BD; i++)
        pbd[i] = stub_bd_list[i];
}

/* ---------------------------------------------------------------- sysmem */

int stub_live_allocs;

void *AllocSysMemory(int mode, int size, void *ptr)
{
    void *p;
    (void)mode;
    (void)ptr;
    p = malloc((size_t)size);
    if (p != NULL)
        stub_live_allocs++;
    return p;
}

int FreeSysMemory(void *ptr)
{
    if (ptr != NULL)
        stub_live_allocs--;
    free(ptr);
    return 0;
}

/* ---------------------------------------------------------------- threads */

static void (*thread_fn)(void *);
int stub_live_threads;
int stub_not_dormant_left; /* DeleteThread() answers KE_NOT_DORMANT this many times */
int stub_delete_calls;

int CreateThread(iop_thread_t *thread)
{
    if (stub_live_threads != 0)
        return -1;
    thread_fn = thread->thread;
    stub_live_threads = 1;
    return 1;
}

/* Runs the thread body synchronously: by the time _start waits on the
   semaphore, the worker has already signalled it. */
int StartThread(int thid, void *arg)
{
    if (thid != 1 || thread_fn == NULL)
        return -1;
    thread_fn(arg);
    return 0;
}

/* Simulates a worker that is still on its way out of worker() for a while. */
int DeleteThread(int thid)
{
    stub_delete_calls++;
    if (thid != 1 || stub_live_threads != 1)
        return -1;
    if (stub_not_dormant_left > 0) {
        stub_not_dormant_left--;
        return KE_NOT_DORMANT;
    }
    stub_live_threads = 0;
    thread_fn = NULL;
    return 0;
}

int DelayThread(int usec)
{
    (void)usec;
    return 0;
}

/* ---------------------------------------------------------------- semaphores */

static int sema_count;
int stub_live_semas;

int CreateSema(iop_sema_t *sema)
{
    if (stub_live_semas != 0)
        return -1;
    sema_count = sema->initial;
    stub_live_semas = 1;
    return 1;
}

int DeleteSema(int semid)
{
    if (semid != 1 || stub_live_semas != 1)
        return -1;
    stub_live_semas = 0;
    return 0;
}

int SignalSema(int semid)
{
    if (semid != 1)
        return -1;
    sema_count++;
    return 0;
}

int WaitSema(int semid)
{
    if (semid != 1 || sema_count <= 0) {
        /* Single-threaded host: waiting here would never end */
        fprintf(stderr, "WaitSema() would block forever\n");
        abort();
    }
    sema_count--;
    return 0;
}
