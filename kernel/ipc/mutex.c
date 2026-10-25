#include <m5os/mutex.h>
#include <m5os/arch.h>
#include <m5os/kernel.h>
#include <m5os/hal_time.h>
#include "../scheduler/sched_internal.h"

#define IDLE_PID  1u

void mutex_init(mutex_t *m) { m->owner = NULL; m->next_held = NULL; }

/* Every owned mutex is linked into its owner's `held` list, so that killing a
 * task can release them (otherwise a task killed while holding e.g. the
 * console lock would freeze every later kprintf). */
static void held_push(task_t *t, mutex_t *m) { m->owner = t; m->next_held = t->held; t->held = m; }

static void held_remove(task_t *t, mutex_t *m)
{
    for (mutex_t **pp = &t->held; *pp; pp = &(*pp)->next_held) {
        if (*pp == m) { *pp = m->next_held; break; }
    }
    m->next_held = NULL;
}

/* TASK_MAX is small: a bounded fixed-point calculation also handles transitive
 * donation, multiple held locks and cycles without recursion or allocation. */
void mutex_recompute_priorities(void)
{
    for(int i=0;i<TASK_MAX;i++) g_tasks[i].priority=g_tasks[i].base_priority;
    for(int pass=0;pass<TASK_MAX;pass++) {
        bool changed=false;
        for(int i=0;i<TASK_MAX;i++) {
            task_t *waiter=&g_tasks[i];
            if(waiter->state!=TASK_BLOCKED || !waiter->wait_mutex) continue;
            task_t *owner=waiter->wait_mutex->owner;
            if(owner && owner->state!=TASK_UNUSED && owner->state!=TASK_TERMINATED &&
                owner->priority<waiter->priority) {
                owner->priority=waiter->priority; changed=true;
            }
        }
        if(!changed) break;
    }
}

/* Give `m` (owned by `owner`) to the best waiter, or free it.  Returns the task
 * that received it (NULL if none).  Interrupts must be masked. */
static task_t *release_locked(mutex_t *m, task_t *owner)
{
    held_remove(owner, m);
    task_t *best = NULL;
    for (int i = 0; i < TASK_MAX; i++) {
        task_t *t = &g_tasks[i];
        if (t->state == TASK_BLOCKED && t->wait_obj == m && (best == NULL || t->priority > best->priority)) best = t;
    }
    if (best) {
        held_push(best, m);
        if (best->mutex_granted_us != UINT64_MAX)
            best->mutex_granted_us = hal_time_now_us();
        best->wait_obj = NULL;
        best->wait_mutex = NULL;
        best->state = TASK_RUNNABLE;
    } else {
        m->owner = NULL;
    }
    mutex_recompute_priorities();
    return best;
}

bool mutex_trylock(mutex_t *m)
{
    uint32_t irq = arch_irq_disable();
    bool got = (m->owner == NULL);
    if (got) held_push(task_current(), m);
    arch_irq_restore(irq);
    return got;
}

static void lock_impl(mutex_t *m, mutex_wait_info_t *out)
{
    task_t *self = task_current();
    if (out) *out = (mutex_wait_info_t){0};

    if (self->id == IDLE_PID) {                 /* idle may not block: poll */
        while (!mutex_trylock(m)) task_yield();
        return;
    }

    uint32_t irq = arch_irq_disable();
    self->mutex_granted_us = out ? 0 : UINT64_MAX;
    if (out) { out->profiled = true; out->entered_us = hal_time_now_us(); }
    if (m->owner == self) kernel_panic("mutex_lock: recursive lock");
    if (m->owner == NULL) {
        held_push(self, m);
        if (out) self->mutex_granted_us = hal_time_now_us();
    } else {
        if (out) { out->contended = true; out->owner_pid = m->owner->id; }
        self->wait_obj = m;
        self->wait_mutex = m;
        self->state = TASK_BLOCKED;
        mutex_recompute_priorities();
        sched_reschedule();                     /* returns once unlock() handed the mutex to us */
        if (m->owner != self) kernel_panic("mutex_lock: woken without ownership");
    }
    if (out) { out->granted_us = self->mutex_granted_us; out->resumed_us = hal_time_now_us(); }
    self->mutex_granted_us = UINT64_MAX;
    arch_irq_restore(irq);
}

void mutex_lock(mutex_t *m) { lock_impl(m, NULL); }
void mutex_lock_timed(mutex_t *m, mutex_wait_info_t *out) { lock_impl(m, out); }

void mutex_unlock(mutex_t *m)
{
    uint32_t irq = arch_irq_disable();
    task_t *self = task_current();
    if (m->owner != self) kernel_panic("mutex_unlock: not the owner");
    task_t *next = release_locked(m, self);
    bool preempt = next && next->priority > self->priority;
    arch_irq_restore(irq);

    if (preempt) task_yield();                  /* do not wait for the next tick */
}

void mutex_release_all(task_t *t)
{
    t->wait_mutex=NULL;
    t->mutex_granted_us=UINT64_MAX;
    while (t->held) release_locked(t->held, t);
    mutex_recompute_priorities();
}
