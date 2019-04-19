#include <cdefs.h>
#include <param.h>
#include <memlayout.h>
#include <mmu.h>
#include <proc.h>
#include <defs.h>
#include <x86_64.h>
#include <elf.h>
#include <trap.h>

int load_program_from_disk(pml4e_t *pml4, char *path, uint64_t *rip) {
  struct inode *ip;
  struct proghdr ph;
  int off;
  uint64_t sz;
  struct elfhdr elf;
  int i;

  if((ip = namei(path)) == 0){
    return 0;
  }

  iload(ip);

  // Check ELF header
  if(readi(ip, (char*)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto elf_failure;
  if(elf.magic != ELF_MAGIC)
    goto elf_failure;

  // Load program into memory.
  sz = 0;
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    if(readi(ip, (char*)&ph, off, sizeof(ph)) != sizeof(ph))
      goto elf_failure;
    if(ph.type != ELF_PROG_LOAD)
      continue;
    if(ph.memsz < ph.filesz)
      goto elf_failure;
    if(ph.vaddr + ph.memsz < ph.vaddr)
      goto elf_failure;


    if((sz = allocuvm(pml4, 0, sz, ph.vaddr + ph.memsz, myproc()->pid)) == -1)
     goto elf_failure;
    if(ph.vaddr % PGSIZE != 0)
      goto elf_failure;


    if(loaduvm(pml4, (char*)ph.vaddr, ip, ph.off, ph.filesz) < 0)
     goto elf_failure;
  }
  iput(ip);
  *rip = elf.entry;
  return sz;
elf_failure:
  if(ip){
    iput(ip);
  }
  return 0;
}

int
exec(char *path, char **argv)
{
  // your code here
  struct proc *process = myproc();

  // set up new page table
  pml4e_t *pml4 = setupkvm();
  uint64_t rip;
  if (pml4 == 0) {
    return -1;
  }

  // load program
  int sz = load_program_from_disk(pml4, path, &rip);
  if (sz == 0) {
    // loading program failed
    freevm(pml4, process->pid);
    return -1;
  }

  // similar to userinit method in proc.c
  process->mem_regions[CODE].start = 0;
  process->mem_regions[CODE].size = sz;

  // heap starts at page aligned position above sz
  process->mem_regions[HEAP].start = (char *)PGROUNDUP((int64_t) sz);
  process->mem_regions[HEAP].size = 0;

  // zero out MMAP regions
  process->mem_regions[MMAP].start = (char *)0;
  process->mem_regions[MMAP].size = 0;

  // initialize user's stack
  initustack(pml4, &process->mem_regions[USTACK], process->pid);


  // now set up USTACK arguments
  // start at bottom of the mem_regions[USTACK]
  uint64_t endOfStack = (uint64_t)(process->mem_regions[USTACK].start + process->mem_regions[USTACK].size);

  // start adding arguments from argv and count them with argc
  int argc = 0;
  while(argv[argc] != 0) {
    // continue adding arguments while there are arguments to add
    endOfStack -= strlen(argv[argc]) + 1;  // move stack pointer down by string length
    endOfStack = (((endOfStack)) & ~(7));  // round down to word align

    // copy data in argv[argc] to the area in the stack we just opened
    if (copyout(pml4, endOfStack, argv[argc], strlen(argv[argc]) + 1) == -1) {
      freevm(pml4, process->pid);
      return -1;
    }

    // replace argv[argc] with the address of the string we just copied and increment argc
    argv[argc] = (char *)endOfStack;
    argc++;
  }

  // push array of pointers to the strings onto the stack.
  // pointers are 8 bytes, make space for the number of elements in the array plus an additional space for a null termination character, then round down to the nearest word alignment
  endOfStack -= 8 * (argc + 1);
  endOfStack = (((endOfStack)) & ~(7));

  // copy the array of pointers which is argv since we replaced all the data in that with the addresses of the strings and the final element is a null terminator
  if (copyout(pml4, endOfStack, argv, 8 * (argc + 1)) == -1) {
    freevm(pml4, process->pid);
    return -1;
  }

  // need to put argc in rdi and argv ptr into rsi
  process->tf->rdi = argc;
  process->tf->rsi = endOfStack;

  // set up rip and rsp
  process->tf->rip = rip;
  process->tf->rsp = endOfStack;

  // replace page table
  pml4e_t *oldpml4 = process->pml4;
  process->pml4 = pml4;
  switchuvm(process);
  freevm(oldpml4, process->pid);
  return 0;
}
