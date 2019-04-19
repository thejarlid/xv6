#include <cdefs.h>
#include <defs.h>
#include <param.h>
#include <memlayout.h>
#include <mmu.h>
#include <proc.h>
#include <x86_64.h>
#include <syscall.h>
#include <trap.h>
#include <sysinfo.h>

// User code makes a system call with INT T_SYSCALL.
// System call number in %eax.
// Arguments on the stack, from the user call to the C
// library system call function. The saved user %esp points
// to a saved program counter, and then the first argument.

// Fetch the int at addr from the current process.
int
fetchint_helper(uint64_t addr, int *ip, struct mem_region* region) {
  if ((addr >= (uint64_t)region->start) && (addr + 4 < (uint64_t)region->start + region->size)) {
    *ip = *(int*)(addr);
    return 0;
  }
  return -1;
}

int
fetchint(uint64_t addr, int *ip)
{
  int i;
  i = fetchint_helper(addr, ip, &myproc()->mem_regions[CODE]);
  if (i == 0) {
    return 0;
  }
  i = fetchint_helper(addr, ip, &myproc()->mem_regions[HEAP]);
  if (i == 0) {
    return 0;
  }
  i = fetchint_helper(addr, ip, &myproc()->mem_regions[USTACK]);
  if (i == 0) {
    return 0;
  }
  return -1;
}

int
fetchint64_helper(uint64_t addr, int64_t *ip, struct mem_region* region) {
  if ((addr >= (uint64_t)region->start) && (addr + 8 < (uint64_t)region->start + region->size)) {
    *ip = *(int64_t*)(addr);
    return 0;
  }
  return -1;
}

int
fetchint64(uint64_t addr, int64_t *ip)
{
  int i;
  i = fetchint64_helper(addr, ip, &myproc()->mem_regions[CODE]);
  if (i == 0) {
    return 0;
  }
  i = fetchint64_helper(addr, ip, &myproc()->mem_regions[HEAP]);
  if (i == 0) {
    return 0;
  }
  i = fetchint64_helper(addr, ip, &myproc()->mem_regions[USTACK]);
  if (i == 0) {
    return 0;
  }
  return -1;
}

int
fetchstr_helper(uint64_t addr, char **pp, struct mem_region *region)
{
  char *s, *ep;

  if ((addr >= (uint64_t)region->start) && (addr < (uint64_t)region->start + region->size)) {
    *pp = (char*)addr;
    ep = region->start + region->size;
    for(s = *pp; s < ep; s++)
      if(*s == 0)
        return s - *pp;
    return -1;
  }
  return -1;
}

// Fetch the nul-terminated string at addr from the current process.
// Doesn't actually copy the string - just sets *pp to point at it.
// Returns length of string, not including nul.
int
fetchstr(uint64_t addr, char **pp)
{
  int i;

  i = fetchstr_helper(addr, pp, &myproc()->mem_regions[CODE]);
  if (i >=0) {
    return i;
  }

  i = fetchstr_helper(addr, pp, &myproc()->mem_regions[HEAP]);
  if (i >=0) {
    return i;
  }

  i = fetchstr_helper(addr, pp, &myproc()->mem_regions[USTACK]);
  if (i >=0) {
    return i;
  }

  return -1;
}

static uint64_t
fetcharg(int n)
{
  switch (n) {
  case 0: return myproc()->tf->rdi;
  case 1: return myproc()->tf->rsi;
  case 2: return myproc()->tf->rdx;
  case 3: return myproc()->tf->rcx;
  case 4: return myproc()->tf->r8;
  case 5: return myproc()->tf->r9;
  }
  panic("more than 6 arguments for a syscall");
}

// Fetch the nth 32-bit system call argument.
int
argint(int n, int *ip)
{
  *ip = fetcharg(n);
  return 0;
}


// Fetch the nth 64-bit system call argument.
int
argint64(int n, int64_t *ip)
{
  *ip = fetcharg(n);
  return 0;
}

// Fetch the nth word-sized system call argument as a pointer
// to a block of memory of size bytes.  Check that the pointer
// lies within the process address space.
int
argptr(int n, char **pp, int size)
{
  int64_t i;

  if(argint64(n, &i) < 0)
    return -1;
  if(size < 0)
    return -1;
  if (((uint64_t)i >= (uint64_t)myproc()->mem_regions[CODE].start) && ((uint64_t)i + size <= (uint64_t)myproc()->mem_regions[CODE].start + myproc()->mem_regions[CODE].size)) {
      *pp = (char*)i;
      return 0;
    }
  if (((uint64_t)i >= (uint64_t)myproc()->mem_regions[HEAP].start) && ((uint64_t)i + size <= (uint64_t)myproc()->mem_regions[HEAP].start + myproc()->mem_regions[HEAP].size)) {
    *pp = (char*)i;
    return 0;
  }
  if (((uint64_t)i >= (uint64_t)myproc()->mem_regions[USTACK].start) && ((uint64_t)i + size <= (uint64_t)myproc()->mem_regions[USTACK].start + myproc()->mem_regions[USTACK].size)) {
    *pp = (char*)i;
    return 0;
  }
  return -1;
}

// Fetch the nth word-sized system call argument as a string pointer.
// Check that the pointer is valid and the string is nul-terminated.
// (There is no shared writable memory, so the string can't change
// between this check and being used by the kernel.)
int
argstr(int n, char **pp)
{
  int addr;
  if(argint(n, &addr) < 0)
    return -1;
  return fetchstr(addr, pp);
}

extern int sys_chdir(void);
extern int sys_close(void);
extern int sys_dup(void);
extern int sys_exec(void);
extern int sys_exit(void);
extern int sys_fork(void);
extern int sys_fstat(void);
extern int sys_getpid(void);
extern int sys_kill(void);
extern int sys_link(void);
extern int sys_mkdir(void);
extern int sys_mknod(void);
extern int sys_open(void);
extern int sys_pipe(void);
extern int sys_read(void);
extern int sys_sbrk(void);
extern int sys_sleep(void);
extern int sys_unlink(void);
extern int sys_wait(void);
extern int sys_write(void);
extern int sys_uptime(void);
extern int sys_sysinfo(void);
extern int sys_mmap(void);
extern int sys_munmap(void);
extern int sys_crashn(void);

static int (*syscalls[])(void) = {
[SYS_fork]    = sys_fork,
[SYS_exit]    = sys_exit,
[SYS_wait]    = sys_wait,
[SYS_pipe]    = sys_pipe,
[SYS_read]    = sys_read,
[SYS_kill]    = sys_kill,
[SYS_exec]    = sys_exec,
[SYS_fstat]   = sys_fstat,
[SYS_chdir]   = sys_chdir,
[SYS_dup]     = sys_dup,
[SYS_getpid]  = sys_getpid,
[SYS_sbrk]    = sys_sbrk,
[SYS_sleep]   = sys_sleep,
[SYS_uptime]  = sys_uptime,
[SYS_open]    = sys_open,
[SYS_write]   = sys_write,
[SYS_mknod]   = sys_mknod,
[SYS_unlink]  = sys_unlink,
[SYS_link]    = sys_link,
[SYS_mkdir]   = sys_mkdir,
[SYS_close]   = sys_close,
[SYS_sysinfo] = sys_sysinfo,
[SYS_mmap]    = sys_mmap,
[SYS_munmap]  = sys_munmap,
[SYS_crashn]  = sys_crashn,
};

void
syscall(void)
{
  int num;

  num = myproc()->tf->rax;
  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    myproc()->tf->rax = syscalls[num]();
  } else {
    cprintf("%d %s: unknown sys call %d\n",
            myproc()->pid, myproc()->name, num);
    myproc()->tf->rax = -1;
  }
}

int
sys_sysinfo(void)
{
  struct sys_info *info;

  if(argptr(0, (void*)&info, sizeof(info)) < 0)
    return -1;

  info->pages_in_use = pages_in_use;
  info->pages_in_swap = pages_in_swap;
  info->free_pages = free_pages;
  info->num_page_faults = num_page_faults;
  info->num_disk_reads = num_disk_reads;

  return 0;
}
