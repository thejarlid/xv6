# Lab 3: Address Space Management

## Introduction
In this lab, we are going to cover address space management. With it, you will
be able to run xk's shell. We also ask you to implement some common techniques to save memory.

### Setting up the init process

At this point, you probably noticed that binary `initcode` contains the test code `user/lab2test.c`. `kernel/initcode.S` is the starting point of user-level application and it calls `main` in `user/lab2test.c`. Now you need to change `kerne/initcode.S` so that it actually called `exec` that you have implemented in lab2 to start the shell.

To do this, Let's look at `kernel/Makefrag`, you will find this section
```
$(O)/initcode : kernel/initcode.S user/lab2test.c $(ULIB)
	$(CC) -nostdinc -I inc -c kernel/initcode.S -o $(O)/initcode.o
	$(CC) -ffreestanding -MD -MP -mno-sse -I inc -c user/lab2test.c -o $(O)/lab2test.o
	$(LD) $(LDFLAGS) -N -e start -Ttext 0 -o $(O)/initcode.out $(O)/initcode.o $(O)/lab2test.o $(ULIB)
	$(OBJCOPY) -S -O binary $(O)/initcode.out $(O)/initcode
	$(OBJDUMP) -S $(O)/initcode.out > $(O)/initcode.asm
```

To do that, first remove the dependency of `user/lab2test.c` in initcode by replacing the part in `kernel/Makefrag` that builds `initcode` to the following:
```
$(O)/initcode : kernel/initcode.S
	$(CC) -nostdinc -I inc -c kernel/initcode.S -o $(O)/initcode.o
	$(LD) $(LDFLAGS) -N -e start -Ttext 0 -o $(O)/initcode.out $(O)/initcode.o
	$(OBJCOPY) -S -O binary $(O)/initcode.out $(O)/initcode
	$(OBJDUMP) -S $(O)/initcode.o > $(O)/initcode.asm
```

Also change the content of `kernel/initcode.S` to
```
#include <syscall.h>
#include <trap.h>

.globl start
start:
  mov $init, %rdi
  mov $argv, %rsi
  mov $SYS_exec, %rax
  int $TRAP_SYSCALL

exit:
  mov $SYS_exit, %rax
  int $TRAP_SYSCALL
  jmp exit

init:
  .string "/lab3init\0"

.p2align 2
argv:
  .quad init
  .quad 0
```

After you change the parts above, xk will start `kernel/initcode.S` as the first program. `kernel/initcode.S` will load `user/lab3init.c` on the disk, this will open the console and exec `user/lab3test.c`. Later on we will change this to load into a shell!

## Part1: Create a user-level heap
A process that needs more memory at runtime can call `sbrk` to grow its
heap data memory. In normal usage, the user library routine
malloc (in C) or new (in C++) calls sbrk whenever the application
asks to allocate a data region that cannot fit on the current heap
(e.g., if the heap is completely allocated due to prior calls to malloc).

For example, if a user application wants to increase the
heap size by `n` bytes, it calls `sbrk(n)`. `sbrk(n)` returns the (virtual)
memory address of the new n bytes. The user application can also decrease
the amount of heap by passing negative values to `sbrk`, although that
is less frequently used.  Generally, the library asks sbrk to provide
more space than immediately needed, to reduce the number of system calls.

As a special case, when a user application is loaded via `exec`,
the user program is initialized to have a zero-size heap
(i.e., `mem_region[HEAP].size = 0`), and so the first call to malloc
always calls sbrk.

Inherently, xk has to track how much memory in heap that is already allocated to each process (via `mem_region[HEAP]`). xk also has to allocate memory and free memory (using `kalloc` and `kfree`) on behalf of the user. Remember, xk allocates and frees memory at page granularity (i.e., 4096 bytes) but `sbrk` need to support allocating/deallocating memory at byte-level.  (Why might the user
library allocate memory in less than a page size?  To be portable, an
application can't depend on the machine adopting a specific page size.)

After the kernel allocates the memory on behalf of user, kernel has to
map those memory into user's address space
(using `mappage` in `kernel/vm.c`) in the heap memory region.

In user space, we have provided an implementation of `malloc` and `free` (in `user/umalloc.c`) that is going to use `sbrk`. After the implementation if `sbrk` is done, user-level application should be able to call `malloc` and `free`.

### Exercise
Implement `sbrk`.

### Question #1
Why might an application prefer using `malloc` and `free` instead of using `sbrk` directly?

## Part2: Starting shell
A shell or terminal is a typical user interface for operating systems. A shell is already implemented (in `user/sh.c`) for you and it is your task to load it after xk boots. Previously in lab1 and lab2, the test code (`user/lab1test.c`, `user/lab2test.c`) are directly linked to the kernel binary and thus did not need `exec` to run. From this lab on, all the test code and other user applications need to be loaded from disk.

In order to run the shell, all that needs to be done is to change:
```
init:
  .string "/lab3init\0"
```
to
```
init:
  .string "/init\0"
```

After you change the parts above, xk will start `kernel/initcode.S` again, but will load `user/init.c` instead of `user/lab3test.c`. `user/init.c` will fork into two processes. One will load `user/sh.c`, the other will wait for zombie processes to reap. After these changes, when you boot xk, you should see the following:

```
xk...
CPU: QEMU Virtual CPU version 2.5+
  fpu de pse tsc msr pae mce cx8 apic sep mtrr pge mca cmov pat pse36 clflush mmx fxsr sse sse2
  sse3 cx16 hypervisor
  syscall nx lm
  lahf_lm svm
E820: physical memory map [mem 0x9000-0x908f]
  [mem 0x0-0x9fbff] available
  [mem 0x9fc00-0x9ffff] reserved
  [mem 0xf0000-0xfffff] reserved
  [mem 0x100000-0xfdffff] available
  [mem 0xfe0000-0xffffff] reserved
  [mem 0xfffc0000-0xffffffff] reserved

cpu0: starting xk

free pages: 3354
cpu0: starting
sb: size 100000 nblocks 67205 bmap start ...
init: starting sh
$
```

### Exercise
Allow xk to boot into the shell. Try a set of commands like `cat`, `echo`, `grep`, `ls`, `wc` in combination of `|` (i.e. pipe) in the shell to see if it works properly.

### Question #2:
Explain how file descriptors of `ls` and `wc` change when user typed `ls | wc` in xk's shell.

## Part3: Memory-mapped I/O
Up to this point, all I/O in xk uses read/write system calls
on file descriptors.
Another way to do I/O is to map a file's content into a user memory region.
In this way, a user can read/write the file using regular machine instructions
on the mapped memory.

If two processes map the same file, then the memory region acts as shared
memory between the two processes; each process will see the other process's
writes to the memory region, and will need to use read-modify-write
instructions to ensure mutual exclusion and data race freedom.

For now, because files are the disk are read-only, if the mapped memory
changes, any change should be discarded when the region is unmapped.
(We'll change this in lab 5 to write changes back to the file system.)
For simplicity, you only need to support mapping regular files (e.g.,
not pipes or I/O devices), and any single process only maps one file at a time.

Because we only mmap one file per process, the interface is very simple;
the kernel assumes the file is already open for reading, and places the
mmap region in a pre-defined location that does not overlap
any other region (e.g., code, stack, or heap). In UNIX, applications can
ask the OS to put an mmap region in a specific location.
We will only ask you to map page aligned regions.

In this lab, you can put the mapped file at [2G, 2G + file size) in the user memory.
Remember that user stack grows from 2G so the mapped file will not interfere with the user stack.

```c
/*
 * arg0: int [file descriptor of the mapped region]
 *
 * Given a file descriptor,
 * map the bytes of the file
 * at the virtual address [2G, 2G + file size).
 * The address region must not overlap any other region.
 *
 * returns the size of the file when mmap succeeds
 * return -1 on error
 */
int
mmap(void)
```

```c
/*
 * arg0: int [file descriptor of the mapped region]
 *
 * Remove the memory mapped region
 *
 * returns -1 on error
 */
int
munmap(void)
```

### Exercise
Implement mmap and unmap.

## Part4: Grow user stack on-demand
In the rest of lab3, we study how to reduce memory consumption of xk. The first technique is to grow the user stack on-demand.
In your implementation of `exec`, the user stack size if fixed and is allocated before the user application starts.
However, we can change that to allocate only the memory that is needed
at run-time. Whenever a user application issues an instruction that
reads or writes the user stack (e.g., creating a stack frame, accessing local
variables), we grow the stack as needed.

When the user process starts, you should set up the user stack with
an initial page to store application arguments.

To implement grow-stack-on-demand, you will need to understand how to
handle page faults.
A page fault is a hardware exception that occurs when a program accesses
a virtual memory page without a valid page table entry, or with a valid
entry, but where the program does not have permission to perform the
operation.

On the hardware exception, control will trap into the kernel; the exception
handler will add memory to the stack region and resume execution.  

In this lab, you can assume user stack is never more than 10 pages.

### Question #3:
When a syscall completes, user-level execution resumes with the instruction immediately after the syscall; when a page fault exception completes, where does user-level execution resume?

### Question #4:
How should the kernel decide whether an unmapped
reference is a normal stack operation versus a stray pointer dereference that
should cause the application to halt? What should happen, for example, if
an application calls a procedure with a local variable that is an array
of a million integers?  

### Exercise
Implement growing the user stack on-demand. Note that our test code
uses the system call `sysinfo` to figure out how much memory is used.

### Question #5:
Is it possible to reduce the user stack size at
run-time (i.e., to deallocate the user stack when a procedure with a
large number of local variables goes out of scope)?  If so, sketch how that
might work.

## Part5: Copy-on-write fork
The next optimization improves the performance of fork by
using copy-on-write. Currently, `fork` duplicates every page
of user memory in the parent process.  Depending on the size
of the parent process, this can consume a lot of memory
and can potentially take a long time.  All this work is thrown away
if the child process immediately calls exec.

Here, we reduce the cost of fork by allowing multiple processes
to share the same physical memory, while at a logical level
still behaving as if the memory was copied.  As long as neither process
modifies the memory, it can stay shared; if either process changes
a page, a copy of that page will be made at that point
(that is, copy-on-write).

In `fork` returns, the child process is given a page table that
points to the same memory pages as the parent.  No additional memory
is allocated for the new process, other than to hold the page table.
However, the page tables of both processes will need to
be changed to be read-only.  That way, if either process tries
to alter the content of their memory, a trap to the kernel will occur,
and the kernel can make a copy of the memory page at that point,
before resuming the user code.

Note that synchronization will be needed, as both the child and the parent
could concurrently attempt to write to the same page.

To make a memory page read-only, we have provided a bit, `PTE_RO`, in the page table entry (in `inc/mmu.h`). Setting/unsetting this bit does not change the behavior of xk, but it can be used to keep track of whether the page is a copy-on-write page. When a page is set to be read-only, `PTE_W` must be unset and replaced with `PTE_RO`. Once the user tried to write (generating a fault) you will need to make a deep copy of the copy-on-write page, setting the `PTE_W` bit and unsetting the `PTE_RO` bit.

If a memory page's page table entry has `PTE_W` off, an instruction
that reads the memory page will work but a write will trigger a page fault.
Note that after making the copy, you will want to reset the page
table entries in both the parent and child to allow write access
to that page.

A tricky part of the assignment is that, of course, a child process
with a set of copy on write pages can fork another child.
Thus, any physical memory page can be shared by many processes.  
There are various possible ways of keeping track of which pages
are cached; we do not specify how. Instead, we will only test for
functional correctness -- that the number of allocated pages is
small and that the child processes execute as if they received a
complete copy of the parent's memory.

### Question #6:
The TLB caches the page table entries of recently referenced
pages.  When you modify the page table entry to allow write access,
how does xk ensure that the TLB does not have a stale version of the cache?

### Exercise
Implement copy-on-write fork.

## Testing and hand-in
After you implement the system calls described above. The kernel should be able to print `lab3 tests passed!`. You should also use `sysinfo` in the shell to detect potential memory leak when running `lab3test`. If your implementation is correct, `pages_in_use` should be kept the same before and after running `lab3test`.

### Question #7
For each member of the project team, how many hours did you
spend on this lab?

Create a `lab3.txt` file in the top-level xk directory with
your answers to the questions listed above.

Zip your source code and and upload via dropbox.
