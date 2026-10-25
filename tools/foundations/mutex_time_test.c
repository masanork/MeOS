#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <m5os/mutex.h>
#include <m5os/arch.h>
#include "../../kernel/scheduler/sched_internal.h"

task_t g_tasks[TASK_MAX];
static task_t *current,*owner,*waiter;
static mutex_t *waiting;
static uint64_t now;
static unsigned depth,reads,mode;
static jmp_buf cancelled;
task_t *task_current(void){return current;}
uint32_t arch_irq_disable(void){return depth++;}
void arch_irq_restore(uint32_t old){assert(depth==old+1);depth=old;}
uint64_t hal_time_now_us(void){assert(depth);reads++;return now;}
void kernel_panic(const char *s){fprintf(stderr,"unexpected panic %s\n",s);abort();}
void task_yield(void){}
void sched_reschedule(void){
    assert(depth==1&&current==waiter&&waiter->state==TASK_BLOCKED);
    assert(owner->priority==waiter->priority);
    if(mode==2){
        waiter->state=TASK_TERMINATED;mutex_release_all(waiter);
        assert(waiter->mutex_granted_us==UINT64_MAX&&!waiter->wait_mutex);
        longjmp(cancelled,1);
    }
    /* Model the owner running, granting at 110, then the recipient only
     * resuming at 170. Keep the real mutex handoff and donation code. */
    depth=0;current=owner;now=110;
    if(mode==1){
        uint32_t irq=arch_irq_disable();owner->state=TASK_TERMINATED;
        mutex_release_all(owner);arch_irq_restore(irq);
    }
    else mutex_unlock(waiting);
    assert(waiting->owner==waiter&&waiter->state==TASK_RUNNABLE);
    assert(waiter->mutex_granted_us==110&&owner->priority==owner->base_priority);
    now=170;current=waiter;waiter->state=TASK_RUNNING;depth=1;
}
static void init(task_t *t,unsigned id,unsigned prio){
    *t=(task_t){.id=id,.priority=prio,.base_priority=prio,.state=TASK_RUNNABLE,
                .mutex_granted_us=UINT64_MAX};
}
static void prepare(mutex_t *m){
    init(owner,2,1);init(waiter,3,7);mutex_init(m);waiting=m;
    current=owner;mutex_lock(m);current=waiter;now=100;
}
int main(void){
    owner=&g_tasks[1];waiter=&g_tasks[2];mutex_t m=MUTEX_INIT;
    init(owner,2,1);current=owner;
    mutex_lock(&m);mutex_unlock(&m);assert(!reads);
    mutex_wait_info_t info;
    mutex_lock_timed(&m,&info);
    assert(info.profiled&&!info.contended&&!info.owner_pid);
    assert(!info.entered_us&&!info.granted_us&&!info.resumed_us);
    assert(owner->mutex_granted_us==UINT64_MAX);mutex_unlock(&m);
    for(mode=0;mode<2;mode++){
        prepare(&m);mutex_lock_timed(&m,&info);
        assert(info.profiled&&info.contended&&info.owner_pid==2);
        assert(info.entered_us==100&&info.granted_us==110&&info.resumed_us==170);
        assert(m.owner==waiter&&waiter->mutex_granted_us==UINT64_MAX);
        mutex_unlock(&m);
    }
    mode=2;prepare(&m);
    if(!setjmp(cancelled)){mutex_lock_timed(&m,&info);assert(0);}
    depth=0;current=owner;assert(owner->priority==1);mutex_unlock(&m);
    init(owner,1,0);current=owner;unsigned before=reads;
    mutex_lock_timed(&m,&info);assert(!info.profiled&&reads==before);mutex_unlock(&m);
    assert(!depth);
    puts("mutex timing: zero-time/uncontended, blocked vs granted-not-running, killed owner/waiter, idle and no clock reads for ordinary locks PASS");
}
