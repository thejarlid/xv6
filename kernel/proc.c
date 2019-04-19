#include <cdefs.h>
#include <defs.h>
#include <param.h>
#include <memlayout.h>
#include <mmu.h>
#include <x86_64.h>
#include <proc.h>
#include <spinlock.h>
#include <trap.h>
#include <fs.h>
#include <file.h>

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void reboot(void)
{
    uint8_t good = 0x02;
    while (good & 0x02)
        good = inb(0x64);
    outb(0x64, 0xFE);
loop:
    asm volatile ("hlt");
    goto loop;
}

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trap_frame*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 8;
  *(uint64_t*)sp = (uint64_t)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->rip = (uint64_t)forkret;



  return p;
}

struct proc*
getProcessAtIndex(int index) {
  if (index < 0 || index >= NPROC) {
    return 0;
  }
  struct proc* p;
  acquire(&ptable.lock);
  p = &ptable.proc[index];
  release(&ptable.lock);
  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_out_initcode_start[], _binary_out_initcode_size[];

  p = allocproc();

  initproc = p;
  if((p->pml4 = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pml4, _binary_out_initcode_start, (int64_t)_binary_out_initcode_size, p->pid);
  p->mem_regions[CODE].start = 0;
  p->mem_regions[CODE].size = PGROUNDUP((int64_t)_binary_out_initcode_size);
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ss = (SEG_UDATA << 3) | DPL_USER;
  p->tf->rflags = FLAGS_IF;
  p->tf->rip = 0;  // beginning of initcode.S
  p->tf->rsp = PGROUNDUP((uint64_t)_binary_out_initcode_size) + 2 * PGSIZE;
  p->mem_regions[USTACK].start = (char*)PGROUNDUP((uint64_t)_binary_out_initcode_size) + PGSIZE;
  p->mem_regions[USTACK].size = PGSIZE;

  safestrcpy(p->name, "initcode", sizeof(p->name));

  // // this assignment to p->state lets other cores
  // // run this process. the acquire forces the above
  // // writes to be visible, and the lock is also needed
  // // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}


// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  // your code here
  struct proc *newProcess = allocproc();
  struct proc *currentProcess = myproc();

  if (newProcess == 0) {
    // failed to allocate space for new process
    return -1;
  }

  // make new page table
  // pml4e_t* pgtbl = copyuvm(currentProcess->pml4, currentProcess->mem_regions, newProcess->pid);
  // if (pgtbl == 0) {
  //   // failed to copy user memory
  //   newProcess->state = UNUSED;
  //   kfree(newProcess->kstack);
  //   return -1;
  //  }
  pml4e_t *pgtbl;
  if ((pgtbl = setupkvm()) == 0) {
    newProcess->state = UNUSED;
    kfree(newProcess->kstack);
    return -1;
  }

  // loop through every region for the parent process excluding the mmaped I/O region
  for (int region = 0; region < 3; region++) {
    // for each region get the starting virtual address and size
    uint64_t region_start = (uint64_t)currentProcess->mem_regions[region].start;
    uint64_t region_size = currentProcess->mem_regions[region].size;

    newProcess->mem_regions[region].start = (char*)region_start;
    newProcess->mem_regions[region].size = region_size;

    // for the given region we need to loop through every page associated with it from
    // the start to start + size
    for (uint64_t pgVAddr = region_start; pgVAddr < (region_start + region_size); pgVAddr += PGSIZE) {

      // get the pate table entry for the parent's process page
      pte_t *pte;
      if ((pte = walkpml4(currentProcess->pml4, (char *)pgVAddr, 0)) == 0) {
        newProcess->state = UNUSED;
        kfree(newProcess->kstack);
        return -1;
      }

      // set the write bit of the parent process to off
      // turn the read only bit on
      *pte &= ~(PTE_W);
      *pte |= PTE_RO;
      *pte |= PTE_U;

      // set up mapping of virtual address to physical address for the child process
      uint64_t pa = PTE_ADDR(*pte);
      pte_t *childPTE;
      if ((childPTE = walkpml4(pgtbl, (char*)pgVAddr, 1)) == 0) {
        newProcess->state = UNUSED;
        kfree(newProcess->kstack);
        return -1;
      }
      *childPTE = *pte;

      inrementPageRefCount(pa);
      switchuvm(currentProcess);
    }
  }



  // need to copy memory mapped region
  if (currentProcess->mem_regions[MMAP].start != 0) {

    // get page table entry
    pte_t *pte = walkpml4(currentProcess->pml4, currentProcess->mem_regions[MMAP].start, 0);
    if (pte == 0) {
      return -1;
    }

    // set region mapping
    newProcess->mem_regions[MMAP].start = currentProcess->mem_regions[MMAP].start;
    newProcess->mem_regions[MMAP].size = currentProcess->mem_regions[MMAP].size;
    newProcess->mapped_file = currentProcess->mapped_file;

    // need to map the mmaped region to point to the same physical address
    uint64_t pa = PTE_ADDR(*pte);
    if (mappages(pgtbl, ((uint64_t)currentProcess->mem_regions[MMAP].start) >> PT_SHIFT, 1, pa >> PT_SHIFT, PTE_W | PTE_U, newProcess->pid) < 0) {
      newProcess->state = UNUSED;
      kfree(newProcess->kstack);
      return -1;
    }

    // increase ref count for the physical page being mapped
    struct core_map_entry *cme = pa2page(pa);
    cme->refCount++;
  }

  newProcess->pml4 = pgtbl;
  newProcess->parent = currentProcess;


  // copy memory region descriptor in the new process entry in ptable
  safestrcpy(newProcess->name, currentProcess->name, sizeof(currentProcess->name));
  for (int i = 0; i < 3; i++) {
    newProcess->mem_regions[i].start = currentProcess->mem_regions[i].start;
    newProcess->mem_regions[i].size = currentProcess->mem_regions[i].size;
  }

  // duplicate the trap frame in the new process
  *(newProcess->tf) = *(currentProcess->tf);

  // duplicate all the open files in the new process
  for (int i = 0; i < NOFILE; i++) {
    if (currentProcess->oft[i] != 0) {
      newProcess->oft[i] = currentProcess->oft[i];
      if (dupFile(newProcess->oft[i]) == -1) {
        newProcess->state = UNUSED;
        kfree(newProcess->kstack);
        return -1;
      }
    }
  }

  // set the return register of fork to be different in both the child
  // and the parent process.
  newProcess->tf->rax = 0;

  // set the state of the new process to RUNNABLE
  // // this assignment to p->state lets other cores
  // // run this process. the acquire forces the above
  // // writes to be visible, and the lock is also needed
  // // because the assignment might not be atomic.
  acquire(&ptable.lock);
  newProcess->state = RUNNABLE;
  release(&ptable.lock);

  return newProcess->pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  // your code here
  struct proc *currentProcess = myproc();
  // wake up parent waiting on this process
  wakeup(currentProcess->parent);

  acquire(&ptable.lock);
  // if this process has any children set the parent of that
  // to initproc.
  for (int i = 0; i < NPROC; i++) {

    // check every open process if the parent is equal to the currentProcess
    if (ptable.proc[i].state != UNUSED && ptable.proc[i].parent == currentProcess) {

      // let the parent become initproc
      ptable.proc[i].parent = initproc;

      // the edge case if we have a child process that exited but
      // we didn't wait for it then when the initproc adopts it
      // we have to wakeup initproc
      if (ptable.proc[i].state == ZOMBIE) {
        release(&ptable.lock);
        wakeup(initproc);
        acquire(&ptable.lock);
      }
    }
  }

  // close all files for the current process
  for (int i = 0; i < NOFILE; i++) {
    if (currentProcess->oft[i] != 0) {
      release(&ptable.lock);
      closeFile(currentProcess->oft[i]);
      currentProcess->oft[i] = 0;
      acquire(&ptable.lock);
    }
  }

  // set state of current process to zombie so that it won't get timesliced back in
  currentProcess->state = ZOMBIE;
  // sched requires lock being held
  sched(); // get a new process to run we don't return from exit
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  // your code here
  // Scan through table looking for exited children.
  acquire(&ptable.lock);

  struct proc *currentProcess = myproc();
  // we have to loop indefinitly until we find a child that exits
  // if there are no children we can just return
  while (1) {
    int numKids = 0;
    for (int i = 0; i < NPROC; i++) {
      // check every open process if the parent is equal to the currentProcess
      if (ptable.proc[i].state != UNUSED && ptable.proc[i].parent == currentProcess) {
        numKids++;
        struct proc *child = &ptable.proc[i];

        // error never enters this if statement
        // even if there is a child that exited
        if (child->state == ZOMBIE) {
          int childPID = child->pid;

          // need to clean up memeory for child
          // includes:
          // 1. kstack
          // 2. page table (freevm())
          // 3. zero out struct

          // free page table and stack
          kfree(child->kstack);
          freevm(child->pml4, childPID);

          // zero out struct
          child->state = UNUSED;
          child->pid = 0;
          child->parent = 0;
          child->tf = 0;
          child->context = 0;
          child->chan = 0;
          child->killed = 0;
          release(&ptable.lock);
          return childPID;
        }
      }
    }

    // if no kids we would wait forever so return
    if (numKids == 0) {
      release(&ptable.lock);
      return -1;
    }

    // we didn't find any zombie children so go back to sleep
    sleep(currentProcess, &ptable.lock);
  }
  return -1;
}

/*
  increments the program's data space by increment bytes. Returns -1 on error and
  on success returns the address of the newly allocated space
*/
char*
sbrk(int incr) {
  // get current process, its page table, and the heap's start address and the current size
  struct proc* currentProcess = myproc();
  pml4e_t* pageTable = currentProcess->pml4;
  uint64_t newSize = currentProcess->mem_regions[HEAP].size + incr;
  char* heapStart  = currentProcess->mem_regions[HEAP].start;

  // if the increment is negative then we want to deallocate memory so we call deallocuvm
  // which will then round to the page size and continue to remove pages of memory from
  // our page table mapping.
  // if positive we do the same but call allocuvm
  char* brk;
  int updatedSize;
  if (incr < 0) {
    updatedSize = deallocuvm(pageTable, heapStart, currentProcess->mem_regions[HEAP].size, newSize, currentProcess->pid);
  } else {
    updatedSize = allocuvm(pageTable, heapStart, currentProcess->mem_regions[HEAP].size, newSize, currentProcess->pid);
  }

  // if either deallocuvm or allocuvm failed return an error
  if (updatedSize < 0) {
    return (char*)-1;
  }

  // get the new break value we want to return for the client and update the currentProcess's heap size
  brk = heapStart + (updatedSize - incr);
  currentProcess->mem_regions[HEAP].size = updatedSize;
  switchuvm(currentProcess);
  return brk;
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;

  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      mycpu()->proc = p;
      switchuvm(p);
      p->state = RUNNING;
      swtch(&mycpu()->scheduler, p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      mycpu()->proc = 0;
    }
    release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1) {
    cprintf("pid : %d\n", myproc()->pid);
    cprintf("ncli : %d\n", mycpu()->ncli);
    cprintf("intena : %d\n", mycpu()->intena);

    panic("sched locks");
  }
  if(myproc()->state == RUNNING)
    panic("sched running");
  if(readeflags()&FLAGS_IF)
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&myproc()->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}


// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}


// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  if(myproc() == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }

  // Go to sleep.
  myproc()->chan = chan;
  myproc()->state = SLEEPING;
  sched();

  // Tidy up.
  myproc()->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    = "unused",
  [EMBRYO]    = "embryo",
  [SLEEPING]  = "sleep ",
  [RUNNABLE]  = "runble",
  [RUNNING]   = "run   ",
  [ZOMBIE]    = "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint64_t pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state != 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint64_t*)p->context->rbp, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

struct proc *
findproc(int pid) {
  struct proc *p;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if (p->pid == pid)
      return p;
  }
  return 0;
}
