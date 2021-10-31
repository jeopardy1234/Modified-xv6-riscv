#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

struct Queue mlfq[1000];

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.

void push(struct Queue *q, struct proc *el)
{
    // if(q->sz >= NPROC)
    // {
    //     panic("Too many processes stacked!!");
    // }
    q->qarr[q->tail] = el;
    q->tail = (q->tail + 1) % NPROC;
    q->sz++;
}

void pop(struct Queue *q)
{
    if (q->sz == 0)
    {
        panic("Queue is empty!!");
    }
    q->head = (q->head + 1) % NPROC;
    q->sz--;
}

struct proc *front(struct Queue *q)
{
    if (q->sz == 0)
    {
        return 0;
    }
    return q->qarr[q->head];
}

void eraseq(struct Queue *q, int pid)
{
    for (int curr = q->head; curr != q->tail; curr = (curr + 1) % (NPROC + 5))
    {
        if (q->qarr[curr]->pid == pid)
        {
            struct proc *temp = q->qarr[curr];
            q->qarr[curr] = q->qarr[(curr + 1) % (NPROC + 1)];
            q->qarr[(curr + 1) % (NPROC + 1)] = temp;
        }
    }
    q->tail--;
    q->sz--;
    if (q->tail < 0)
    {
        q->tail = NPROC;
    }
}

void InitQueue()
{
    for (int i = 0; i < NMLFQ; i++)
    {
        mlfq[i].head = 0;
        mlfq[i].tail = 0;
        mlfq[i].sz = 0;
    }
}

void proc_mapstacks(pagetable_t kpgtbl)
{
    struct proc *p;

    for (p = proc; p < &proc[NPROC]; p++)
    {
        char *pa = kalloc();
        if (pa == 0)
            panic("kalloc");
        uint64 va = KSTACK((int)(p - proc));
        kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
    }
}

// initialize the proc table at boot time.
void procinit(void)
{
    struct proc *p;

    initlock(&pid_lock, "nextpid");
    initlock(&wait_lock, "wait_lock");
    for (p = proc; p < &proc[NPROC]; p++)
    {
        initlock(&p->lock, "proc");
        p->kstack = KSTACK((int)(p - proc));
    }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int cpuid()
{
    int id = r_tp();
    return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu *
mycpu(void)
{
    int id = cpuid();
    struct cpu *c = &cpus[id];
    return c;
}

// Return the current struct proc *, or zero if none.
struct proc *
myproc(void)
{
    push_off();
    struct cpu *c = mycpu();
    struct proc *p = c->proc;
    pop_off();
    return p;
}

int allocpid()
{
    int pid;

    acquire(&pid_lock);
    pid = nextpid;
    nextpid = nextpid + 1;
    release(&pid_lock);

    return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc *
allocproc(void)
{
    struct proc *p;
    for (p = proc; p < &proc[NPROC]; p++)
    {
        acquire(&p->lock);
        if (p->state == UNUSED)
        {
            goto found;
        }
        else
        {
            release(&p->lock);
        }
    }
    return 0;

found:
    p->pid = allocpid();
    p->create_time = ticks;
    p->state = USED;
    p->static_priority = STATIC_PRIORITY;
    p->times_chosen = 0;
    p->in_queue = 0;
    p->queue_stage = 0;
    p->tick_counter = 0;
    p->curr_thresh = 1;
    p->time_spent_currq = 0;
    p->wtime1 = 0;
    p->runnable_time = 0;
    p->level_enter = ticks;
    for(int i=0; i<NMLFQ; i++)
        p->level_times[i] = 0;
    // Allocate a trapframe page.
    if ((p->trapframe = (struct trapframe *)kalloc()) == 0)
    {
        freeproc(p);
        release(&p->lock);
        return 0;
    }

    // An empty user page table.
    p->pagetable = proc_pagetable(p);
    if (p->pagetable == 0)
    {
        freeproc(p);
        release(&p->lock);
        return 0;
    }

    // Set up new context to start executing at forkret,
    // which returns to user space.
    memset(&p->context, 0, sizeof(p->context));
    p->context.ra = (uint64)forkret;
    p->context.sp = p->kstack + PGSIZE;
    p->rtime = 0;
    p->etime = 0;
    p->ctime = ticks;
    p->time_stopped = 0;
    p->time_stopped_temp = 0;

    return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
    if (p->trapframe)
        kfree((void *)p->trapframe);
    p->trapframe = 0;
    if (p->pagetable)
        proc_freepagetable(p->pagetable, p->sz);
    p->pagetable = 0;
    p->sz = 0;
    p->pid = 0;
    p->parent = 0;
    p->name[0] = 0;
    p->chan = 0;
    p->killed = 0;
    p->xstate = 0;
    p->state = UNUSED;
}

// Create a user page table for a given process,
// with no user memory, but with trampoline pages.
pagetable_t
proc_pagetable(struct proc *p)
{
    pagetable_t pagetable;

    // An empty page table.
    pagetable = uvmcreate();
    if (pagetable == 0)
        return 0;

    // map the trampoline code (for system call return)
    // at the highest user virtual address.
    // only the supervisor uses it, on the way
    // to/from user space, so not PTE_U.
    if (mappages(pagetable, TRAMPOLINE, PGSIZE,
                 (uint64)trampoline, PTE_R | PTE_X) < 0)
    {
        uvmfree(pagetable, 0);
        return 0;
    }

    // map the trapframe just below TRAMPOLINE, for trampoline.S.
    if (mappages(pagetable, TRAPFRAME, PGSIZE,
                 (uint64)(p->trapframe), PTE_R | PTE_W) < 0)
    {
        uvmunmap(pagetable, TRAMPOLINE, 1, 0);
        uvmfree(pagetable, 0);
        return 0;
    }

    return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmunmap(pagetable, TRAPFRAME, 1, 0);
    uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// od -t xC initcode
uchar initcode[] = {
    0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
    0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
    0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
    0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
    0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
    0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00};

// Set up first user process.
void userinit(void)
{
    struct proc *p;

    p = allocproc();
    initproc = p;

    // allocate one user page and copy init's instructions
    // and data into it.
    uvminit(p->pagetable, initcode, sizeof(initcode));
    p->sz = PGSIZE;

    // prepare for the very first "return" from kernel to user.
    p->trapframe->epc = 0;     // user program counter
    p->trapframe->sp = PGSIZE; // user stack pointer

    safestrcpy(p->name, "initcode", sizeof(p->name));
    p->cwd = namei("/");

    p->state = RUNNABLE;
    p->runnable_time = ticks;
    release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int growproc(int n)
{
    uint sz;
    struct proc *p = myproc();

    sz = p->sz;
    if (n > 0)
    {
        if ((sz = uvmalloc(p->pagetable, sz, sz + n)) == 0)
        {
            return -1;
        }
    }
    else if (n < 0)
    {
        sz = uvmdealloc(p->pagetable, sz, sz + n);
    }
    p->sz = sz;
    return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int fork(void)
{
    int i, pid;
    struct proc *np;
    struct proc *p = myproc();

    // Allocate process.
    if ((np = allocproc()) == 0)
    {
        return -1;
    }

    // Copy user memory from parent to child.
    if (uvmcopy(p->pagetable, np->pagetable, p->sz) < 0)
    {
        freeproc(np);
        release(&np->lock);
        return -1;
    }
    np->sz = p->sz;
    np->mask = p->mask;

    // copy saved user registers.
    *(np->trapframe) = *(p->trapframe);

    // Cause fork to return 0 in the child.
    np->trapframe->a0 = 0;

    // increment reference counts on open file descriptors.
    for (i = 0; i < NOFILE; i++)
        if (p->ofile[i])
            np->ofile[i] = filedup(p->ofile[i]);
    np->cwd = idup(p->cwd);

    safestrcpy(np->name, p->name, sizeof(p->name));

    pid = np->pid;

    release(&np->lock);

    acquire(&wait_lock);
    np->parent = p;
    release(&wait_lock);

    acquire(&np->lock);
    np->state = RUNNABLE;
    np->runnable_time = ticks;
    release(&np->lock);

    return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void reparent(struct proc *p)
{
    struct proc *pp;

    for (pp = proc; pp < &proc[NPROC]; pp++)
    {
        if (pp->parent == p)
        {
            pp->parent = initproc;
            wakeup(initproc);
        }
    }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void exit(int status)
{
    struct proc *p = myproc();

    if (p == initproc)
        panic("init exiting");

    // Close all open files.
    for (int fd = 0; fd < NOFILE; fd++)
    {
        if (p->ofile[fd])
        {
            struct file *f = p->ofile[fd];
            fileclose(f);
            p->ofile[fd] = 0;
        }
    }

    begin_op();
    iput(p->cwd);
    end_op();
    p->cwd = 0;

    acquire(&wait_lock);

    // Give any children to init.
    reparent(p);

    // Parent might be sleeping in wait().
    wakeup(p->parent);

    acquire(&p->lock);

    if (p->state == RUNNABLE)
        p->wtime1 += (ticks - p->runnable_time);

    p->xstate = status;
    p->state = ZOMBIE;
    p->etime = ticks;

    release(&wait_lock);

    // Jump into the scheduler, never to return.
    sched();
    panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int wait(uint64 addr)
{
    struct proc *np;
    int havekids, pid;
    struct proc *p = myproc();

    acquire(&wait_lock);

    for (;;)
    {
        // Scan through table looking for exited children.
        havekids = 0;
        for (np = proc; np < &proc[NPROC]; np++)
        {
            if (np->parent == p)
            {
                // make sure the child isn't still in exit() or swtch().
                acquire(&np->lock);

                havekids = 1;
                if (np->state == ZOMBIE)
                {
                    // Found one.
                    pid = np->pid;
                    if (addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
                                             sizeof(np->xstate)) < 0)
                    {
                        release(&np->lock);
                        release(&wait_lock);
                        return -1;
                    }
                    freeproc(np);
                    release(&np->lock);
                    release(&wait_lock);
                    return pid;
                }
                release(&np->lock);
            }
        }

        // No point waiting if we don't have any children.
        if (!havekids || p->killed)
        {
            release(&wait_lock);
            return -1;
        }

        // Wait for a child to exit.
        sleep(p, &wait_lock); //DOC: wait-sleep
    }
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int waitx(uint64 addr, uint *rtime, uint *wtime)
{
    struct proc *np;
    int havekids, pid;
    struct proc *p = myproc();

    acquire(&wait_lock);

    for (;;)
    {
        // Scan through table looking for exited children.
        havekids = 0;
        for (np = proc; np < &proc[NPROC]; np++)
        {
            if (np->parent == p)
            {
                // make sure the child isn't still in exit() or swtch().
                acquire(&np->lock);

                havekids = 1;
                if (np->state == ZOMBIE)
                {
                    // Found one.
                    pid = np->pid;
                    *rtime = np->rtime;
                    *wtime = np->etime - np->ctime - np->rtime;
                    if (addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
                                             sizeof(np->xstate)) < 0)
                    {
                        release(&np->lock);
                        release(&wait_lock);
                        return -1;
                    }
                    freeproc(np);
                    release(&np->lock);
                    release(&wait_lock);
                    return pid;
                }
                release(&np->lock);
            }
        }

        // No point waiting if we don't have any children.
        if (!havekids || p->killed)
        {
            release(&wait_lock);
            return -1;
        }

        // Wait for a child to exit.
        sleep(p, &wait_lock); //DOC: wait-sleep
    }
}

void update_time()
{
    struct proc *p;
    for (p = proc; p < &proc[NPROC]; p++)
    {
        acquire(&p->lock);
        if (p->state == RUNNING)
        {
            p->level_times[p->queue_stage]++;
            p->rtime++;
            p->time_spent_currq++;
        }
        release(&p->lock);
    }
}

int setpriority(int newp, int pid)
{
    struct proc *p;
    int oldp;
    for (p = proc; p < &proc[NPROC]; p++)
    {
        acquire(&p->lock);

        if (p->pid == pid)
        {
            oldp = p->dynamic_priority;
            p->dynamic_priority = newp;
            release(&p->lock);
            return oldp;
        }
        release(&p->lock);
    }
    return -1;
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.

void ageing(void)
{
    for (struct proc *p = proc; p < &proc[NPROC]; p++)
    {
        if (p->state == RUNNABLE && ticks - p->level_enter >= 128)
        {
            if (p->in_queue)
            {
                eraseq(&mlfq[p->queue_stage], p->pid);
                p->in_queue = 0;
            }
            if (p->queue_stage != 0)
                p->queue_stage--;
            p->level_enter = ticks;
        }
    }
}

struct proc *MLFQ_Schedule(void)
{
    ageing();
    for (int i = 0; i < NMLFQ - 1; i++)
    {
        while (mlfq[i].sz)
        {
            struct proc *p = front(&mlfq[i]);
            pop(&mlfq[i]);
            p->in_queue = 0;
            if (p->state == RUNNABLE)
            {
                p->level_enter = ticks;
                return p;
            }
        }
    }
    return 0;
}
void scheduler(void)
{
    struct proc *p;
    struct cpu *c = mycpu();

    c->proc = 0;

#ifdef FCFS
    for (;;)
    {
        intr_on();
        struct proc *TempProc = 0;
        for (p = proc; p < &proc[NPROC]; p++)
        {
            acquire(&p->lock);
            if (p->state == RUNNABLE)
            {
                if (TempProc == 0 || TempProc->create_time > p->create_time)
                {
                    TempProc = p;
                    //acquire(&TempProc->lock);
                }
            }
            release(&p->lock);
        }
        if (TempProc)
        {
            acquire(&TempProc->lock);
            if (TempProc->state == RUNNABLE)
            {

                TempProc->state = RUNNING;
                TempProc->level_enter = ticks;
                c->proc = TempProc;
                p->wtime1 += (ticks - p->runnable_time);
                swtch(&c->context, &TempProc->context);
                c->proc = 0;
                p->level_enter = ticks;
            }
            release(&TempProc->lock);
        }
    }
#endif

#ifdef RR
    for (;;)
    {
        // Avoid deadlock by ensuring that devices can interrupt.
        intr_on();

        for (p = proc; p < &proc[NPROC]; p++)
        {
            acquire(&p->lock);
            if (p->state == RUNNABLE)
            {
                // Switch to chosen process.  It is the process's job
                // to release its lock and then reacquire it
                // before jumping back to us.
                p->state = RUNNING;
                c->proc = p;
                p->wtime1 += (ticks - p->runnable_time);
                swtch(&c->context, &p->context);

                // Process is done running for now.
                // It should have changed its p->state before coming back.
                c->proc = 0;
            }
            release(&p->lock);
        }
    }
#endif

#ifdef PBS
    for (;;)
    {
        intr_on();
        struct proc *TempProc = 0;
        for (p = proc; p < &proc[NPROC]; p++)
        {
            if (p->rtime == 0 && p->time_stopped == 0)
                p->niceness = 0;
            else
                p->niceness = 10 * (p->time_stopped) / (p->time_stopped + p->rtime);
            p->dynamic_priority = p->static_priority - p->niceness + 5;
            p->dynamic_priority = p->dynamic_priority > 100 ? 100 : p->dynamic_priority;
            p->dynamic_priority = p->dynamic_priority < 0 ? 0 : p->dynamic_priority;
            acquire(&p->lock);
            if (p->state == RUNNABLE)
            {
                if (TempProc == 0)
                {
                    TempProc = p;
                }
                else if (TempProc->dynamic_priority > p->dynamic_priority)
                {
                    TempProc = p;
                }
                else if (TempProc->dynamic_priority == p->dynamic_priority)
                {
                    if (TempProc->times_chosen < p->times_chosen)
                    {
                        TempProc = p;
                    }
                    else if (TempProc->times_chosen == p->times_chosen)
                    {
                        if (TempProc->create_time >= p->create_time)
                            TempProc = p;
                    }
                }
            }
            release(&p->lock);
        }
        if (TempProc)
        {
            acquire(&TempProc->lock);
            if (TempProc->state == RUNNABLE)
            {
                TempProc->state = RUNNING;
                TempProc->times_chosen++;
                c->proc = TempProc;
                p->wtime1 += (ticks - p->runnable_time);
                swtch(&c->context, &TempProc->context);
                c->proc = 0;
            }
            release(&TempProc->lock);
        }
    }
#endif

#ifdef MLFQ
    for (;;)
    {
        intr_on();
        for (p = proc; p < &proc[NPROC]; p++)
        {
            acquire(&p->lock);
            if (p->state == RUNNABLE)
            {
                if (!(p->in_queue))
                {
                    push(&mlfq[p->queue_stage], p);
                }
            }
            release(&p->lock);
        }

        struct proc *schedule_this = MLFQ_Schedule();
        if (schedule_this)
        {
            //printf("Queue 0 size: %d %d %d %s\n", mlfq[0].sz, mlfq[0].head, mlfq[0].tail, schedule_this->name);
            acquire(&schedule_this->lock);
            if (schedule_this->state == RUNNABLE)
            {
                schedule_this->times_chosen++;
                schedule_this->state = RUNNING;
                c->proc = schedule_this;
                p->wtime1 += (ticks - p->runnable_time);
                swtch(&c->context, &schedule_this->context);
                c->proc = 0;
            }
            release(&schedule_this->lock);
        }
    }
#endif
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void sched(void)
{
    int intena;
    struct proc *p = myproc();

    if (!holding(&p->lock))
        panic("sched p->lock");
    if (mycpu()->noff != 1)
        panic("sched locks");
    if (p->state == RUNNING)
        panic("sched running");
    if (intr_get())
        panic("sched interruptible");

    intena = mycpu()->intena;
    swtch(&p->context, &mycpu()->context);
    mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void yield(void)
{
    struct proc *p = myproc();
    acquire(&p->lock);
    p->state = RUNNABLE;
    p->runnable_time = ticks;
    sched();
    release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void forkret(void)
{
    static int first = 1;

    // Still holding p->lock from scheduler.
    release(&myproc()->lock);

    if (first)
    {
        // File system initialization must be run in the context of a
        // regular process (e.g., because it calls sleep), and thus cannot
        // be run from main().
        first = 0;
        fsinit(ROOTDEV);
    }

    usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void sleep(void *chan, struct spinlock *lk)
{
    struct proc *p = myproc();

    // Must acquire p->lock in order to
    // change p->state and then call sched.
    // Once we hold p->lock, we can be
    // guaranteed that we won't miss any wakeup
    // (wakeup locks p->lock),
    // so it's okay to release lk.

    acquire(&p->lock); //DOC: sleeplock1

    release(lk);
    if (p->state == RUNNABLE)
        p->wtime1 += (ticks - p->runnable_time);
    // Go to sleep.
    p->chan = chan;
    p->state = SLEEPING;
    p->time_stopped_temp = ticks;

    sched();

    // Tidy up.
    p->chan = 0;

    // Reacquire original lock.
    release(&p->lock);

    acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void wakeup(void *chan)
{
    struct proc *p;

    for (p = proc; p < &proc[NPROC]; p++)
    {
        if (p != myproc())
        {
            acquire(&p->lock);
            if (p->state == SLEEPING && p->chan == chan)
            {
                p->state = RUNNABLE;
                p->runnable_time = ticks;
                p->time_stopped += ticks - p->time_stopped_temp;
            }
            release(&p->lock);
        }
    }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int kill(int pid)
{
    struct proc *p;

    for (p = proc; p < &proc[NPROC]; p++)
    {
        acquire(&p->lock);
        if (p->pid == pid)
        {
            p->killed = 1;
            if (p->state == SLEEPING)
            {
                // Wake process from sleep().
                p->state = RUNNABLE;
            }
            release(&p->lock);
            return 0;
        }
        release(&p->lock);
    }
    return -1;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
    struct proc *p = myproc();
    if (user_dst)
    {
        return copyout(p->pagetable, dst, src, len);
    }
    else
    {
        memmove((char *)dst, src, len);
        return 0;
    }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
    struct proc *p = myproc();
    if (user_src)
    {
        return copyin(p->pagetable, dst, src, len);
    }
    else
    {
        memmove(dst, (char *)src, len);
        return 0;
    }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void procdump(void)
{
    static char *states[] = {
        [UNUSED] "unused",
        [SLEEPING] "sleep ",
        [RUNNABLE] "runble",
        [RUNNING] "run   ",
        [ZOMBIE] "zombie"};
    struct proc *p;
    char *state;

    printf("\n");
#ifndef MLFQ
#ifndef PBS
    printf("PID \t State \t Process Name\n");
#endif
#endif

#ifdef MLFQ
    printf("PID \t Priority \t State \t\t rtime \t wtime \t nrun \t q0 \t q1 \t q2 \t q3 \t q4\n");
#endif
#ifdef PBS
    printf("PID \t Priority \t State \t\t rtime \t wtime \t nrun\n");
#endif
    for (p = proc; p < &proc[NPROC]; p++)
    {
        if (p->state == UNUSED)
            continue;
        if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
            state = states[p->state];
        else
            state = "???";
#ifndef PBS
#ifndef MLFQ
        printf("%d \t %s \t %s ", p->pid, state, p->name);
#endif
#endif

#ifdef PBS
        printf("%d \t %d \t\t %s \t %d \t %d \t %d \t", p->pid, p->dynamic_priority, state, p->rtime, p->etime - p->ctime - p->rtime, p->times_chosen);
#endif
#ifdef MLFQ
        printf("%d \t %d \t\t %s \t %d \t %d \t %d \t %d \t %d \t %d \t %d \t %d", p->pid, p->queue_stage, state, p->rtime, p->etime - p->ctime - p->rtime, p->times_chosen, p->level_times[0],p->level_times[1], p->level_times[2], p->level_times[3], p->level_times[4]);
#endif

    printf("\n");
    }
}
