#include <cdefs.h>
#include <defs.h>
#include <param.h>
#include <memlayout.h>
#include <mmu.h>
#include <proc.h>
#include <x86_64.h>
#include <trap.h>
#include <spinlock.h>

// Interrupt descriptor table (shared by all CPUs).
struct gate_desc idt[256];
extern void* vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

int num_page_faults = 0;

void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    set_gate_desc(&idt[i], 0, SEG_KCODE<<3, vectors[i], KERNEL_PL);
  set_gate_desc(&idt[TRAP_SYSCALL], 1, SEG_KCODE<<3, vectors[TRAP_SYSCALL], USER_PL);

  initlock(&tickslock, "time");
}

void
idtinit(void)
{
  lidt((void *)idt, sizeof(idt));
}

//PAGEBREAK: 41
void
trap(struct trap_frame *tf)
{
  uint64_t addr;

  if(tf->trapno == TRAP_SYSCALL){
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if(myproc()->killed)
      exit();
    return;
  }

  switch(tf->trapno){
  case TRAP_IRQ0 + IRQ_TIMER:
    if(cpunum() == 0){
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
    }
    lapiceoi();
    break;
  case TRAP_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case TRAP_IRQ0 + IRQ_IDE+1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case TRAP_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case TRAP_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case TRAP_IRQ0 + 7:
  case TRAP_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpunum(), tf->cs, tf->rip);
    lapiceoi();
    break;

  //PAGEBREAK: 13
  default:
    addr = rcr2();
    struct proc *currentProcess = myproc();

    // page fault trap handler
    if (tf->trapno == TRAP_PF) {
      num_page_faults += 1;

      // handle page swapping
      // get the current page table entry for the process
      pte_t *pte;
      if ((pte = walkpml4(currentProcess->pml4, (char *)addr, 0)) == 0) {

        panic("invalid page");
      }
      if (!(*pte & PTE_P) && (*pte & PTE_DSK)) {
        // this is where we handle a page swap
        // 1. evict a page
        // 2. pull page needed in
        if (evictPage() == -1) {
          panic("unable to evict page");
        }
        if (swapPageIn(addr, pte) == -1) {
          panic("can't swap page in");
        }
        return;
      }

      // need to check that the address being accessed is within
      // mem_regions[USTACK].start + (10 * PGSIZE) else its an actual
      // trap exception.
      uint64_t stackStart = (uint64_t)currentProcess->mem_regions[USTACK].start;
      uint64_t oldStackSize = currentProcess->mem_regions[USTACK].size;

      if (addr < stackStart && addr >= (SZ_2G - 10 * PGSIZE)) {
        // allocate the new pages up to the "top" of the stack
        // then adjust the mem_region stack and return
        uint64_t newPGAddr = PGROUNDDOWN(addr);
        int updatedSize = allocuvm(currentProcess->pml4, (char*)newPGAddr, 0, stackStart - newPGAddr, currentProcess->pid);
        if (updatedSize < 0) {
          panic("trap -- allocuvm failed");
        }
        currentProcess->mem_regions[USTACK].size = updatedSize + oldStackSize;
        currentProcess->mem_regions[USTACK].start = (char*)(stackStart - updatedSize);
        return;
      }

      // check that the process has the RO bit set and the W bit off
      if (!(*pte & PTE_W) && (*pte & PTE_RO)) {
        if (addr >= SZ_2G) {
          panic("Trying to write to memory above 2Gs");
        }

        // get the core map entry for the page
        uint64_t pa = PTE_ADDR(*pte);
        struct core_map_entry *cme = pa2page(pa);

        if (cme->refCount > 1) {
          // allocate a new page
          char *mem = kalloc();
          if (mem == 0) {
            panic("kalloc couldn't make a new page");
          }
          decrementPageRefCount(pa);
          memmove(mem, (char*)P2V(pa), PGSIZE);
          *pte = PTE(V2P(mem), PTE_FLAGS(*pte) | PTE_W | PTE_P | PTE_U);
          *pte &= ~(PTE_RO);
          add_phy_mem_map(currentProcess->pid, PTE_ADDR(PGROUNDDOWN(addr)), PTE_ADDR(*pte));
          switchuvm(currentProcess);
          return;
        } else if (cme->refCount == 1) {
          *pte = PTE(*pte, PTE_FLAGS(*pte) | PTE_W | PTE_P | PTE_U);
          *pte &= ~(PTE_RO);
          switchuvm(currentProcess);
          return;
        } else {
          panic("illegal core map entry");
        }
      }

      if(myproc() == 0 || (tf->cs&3) == 0) {
        // In kernel, it must be our mistake.
        cprintf("unexpected trap %d from cpu %d rip %lx (cr2=0x%x)\n",
                tf->trapno, cpunum(), tf->rip, addr);
        panic("trap");
      }
    }

    // Assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "rip 0x%lx addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno, tf->err, cpunum(), tf->rip,
            addr);
    myproc()->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if(myproc() && myproc()->state == RUNNING && tf->trapno == TRAP_IRQ0+IRQ_TIMER)
    yield();

  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}
