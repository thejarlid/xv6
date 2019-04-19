# Lab 1: Interrupts and System Calls

## Introduction
All of our labs are based on the Experimental Kernel (i.e., xk). xk is a new experimental
operating system kernel for teaching the principles and practice of operating systems.
xk's baseline code is a complete, bootable operating system. It provides some simple system
calls that it is capable of running a “hello world” program. Your task in each lab is to make
xk complete and also add a few functionalities.

## Installing xk
The files you will need for this and subsequent lab assignments in
this course are distributed using the Git version control system.
To learn more about Git, take a look at the
[Git user's manual](http://www.kernel.org/pub/software/scm/git/docs/user-manual.html),
or, if you are already familiar with other version control systems,
you may find this
[CS-oriented overview of Git](http://eagain.net/articles/git-for-computer-scientists/)
useful.

The course Git repository is on the [CSE GitLab](//www.cs.washington.edu/lab/gitlab).
Follow the instructions there to set up your ssh keys if you haven't.

You need to clone the course repository, by running the commands below. You need to use either a x86_64 linux machine or a mac. You can also use attu by logging into it remotely (`ssh attu.cs.washington.edu`).
```
$ git clone git@gitlab.cs.washington.edu:xk-public/au17.git xk
Cloning into xk...
$ cd xk
```

You want to create your own repository on gitlab instead of working directly on our git repository. To do so, create a new project on gitlab with `blank` template. Set visibility to private (we don't want students in other groups to see your code).

Let's say your project is called <proj_name>, in the xk directory.
```
$ git remote rename origin upstream
$ git remote add origin git@gitlab.cs.washington.edu:<uwid>/<proj_name>.git
$ git push -u origin --all
```
This generates a copy of our repository in your project. You need to add all the course staffs
as `Reporter` in your repository.

Before you start coding, add a git tag
```
$ git tag start_lab1
$ git push origin master --tags
```

Git allows you to keep track of the changes you make to the code. For example, if you are
finished with one of the exercises, and want to checkpoint your progress, you can commit
your changes by running:
```
$ git commit -am 'my solution for lab1'
Created commit 60d2135: my solution for lab1
 1 files changed, 1 insertions(+), 0 deletions(-)
```

You can keep track of your changes by using the `git diff` command. Running `git diff` will display
the changes to your code since your last commit, and `git diff upstream/master` will display the
changes relative to the initial code supplied. Here, `upstream/master` is the name of the git
branch with the initial code you downloaded from the course server for this assignment.

We have set up the appropriate compilers and simulators for you on attu. Run the following command:
```
export PATH=/cse/courses/cse451/17au/bin/x86_64-softmmu:$PATH
```
or add it to your shell startup file. If you are working on your own machine, you’ll need to install the toolchains. Follow the instructions from the [software setup page](tools.md)

After you finish lab1, add another git tag
```
$ git tag end_lab1
$ git push origin master --tags
```

This will allow us to pull a version of your lab1 solution even when you start working on the next lab.

## Organization of source code

```
xk
├── inc           // all the header files (i.e., .h); includes definition of all the data structures and the system call interfaces
├── kernel        // the kernel source code
│   └── Makefrag  // compilation scripts for kernel (xk.img), qemu scripts
├── user          // all the source code for user applications
│   └── Makefrag  // compilation scripts for user applications
├── lab           // specifications for labs
├── Makefile      // starting point of compilation
└── sign.pl       // make sure the boot block is below a block size on the disk
```

After compilation, a new folder `out` will appear, which contains the kernel image and all the intermediate binaries (.d, .o, .asm).

## Starting xk

The purpose of the first exercise is to introduce you to x86_64 assembly
language and the PC bootstrap process, and to get you started with
QEMU and QEMU/GDB debugging. You will not have to write any code
for this part of the lab, but you should go through it anyway for
your own understanding.

### Getting started with x86_64 assembly

The definitive reference for x86_64 assembly language programming is Intel’s
instruction set architecture reference is
[Intel 64 and IA-32 Architectures Software Developer’s Manuals](https://software.intel.com/en-us/articles/intel-sdm).
It covers all the features of the most recent processors that we won’t need in class but
you may be interested in learning about. An equivalent (and often friendlier) set of
manuals is [AMD64 Architecture Programmer’s Manual](http://developer.amd.com/resources/developer-guides-manuals/).
Save the Intel/AMD architecture manuals for later or use them for reference when you want
to look up the definitive explanation of a particular processor feature or instruction.

You don't have to read them now, but you'll almost certainly want
to refer to some of this material when reading and writing x86_64 assembly.

### Simulating the x86_64

Instead of developing the operating system on a real, physical
personal computer (PC), we use a program that faithfully emulates
a complete PC: the code you write for the emulator will boot on a
real PC too. Using an emulator simplifies debugging; you can, for
example, set break points inside of the emulated x86_64, which is
difficult to do with the silicon version of an x86_64.

In xk, we will use the [QEMU Emulator](http://www.qemu.org/), a
modern and fast emulator. While QEMU's built-in monitor
provides only limited debugging support, QEMU can act as a remote
debugging target for the GNU debugger (GDB), which we'll use in
this lab to step through the early boot process.

To get started, extract xk source code into your own directory, then type
`make` in the root
directory to build xk you will start with.

Now you're ready to run QEMU, supplying the file `out/xk.img` and `out/fs.img`,
created above, as the contents of the emulated PC's "virtual hard
disk." Those hard disk images contain our boot loader `out/bootblock`
, our kernel `out/xk.bin` and a list of user applications in `out/user`.

Type `make qemu` to run QEMU with the options required to
set the hard disk and direct serial port output to the terminal.
Some text should appear in the QEMU window:

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

free pages: 3741
cpu0: starting
sb: size 100000 nblocks 99973 bmap start 2 inodestart 27
hello world
```

Press Ctrl-a x to exit the QEMU virtual instance.

### Question #1:

GDB can be used as a remote debugger for xk. To attach GDB to xk, you need to open two separate terminals. Both of them should be in the xk root directory. In one terminal, type `make qemu-gdb`. This starts the qemu process and wait for GDB to attach. In another terminal, type `make gdb`. Now the GDB process is attached to qemu. If you are using `attu`, note that there are actually several attus. Make sure both of your terminals are connected to the same physical machine (e.g., explicitly using `attu2.cs.washington.edu`).

In xk, when bootloader loads the kernel from disk to memory, the CPU operates in 32-bit mode. The starting point of the 32-bit kernel is in `kernel/entry.S`. `kernel/entry.S` setups 64-bit virtual memory and enables 64-bit mode. You don't need to understand `kernel/entry.S`. `kernel/entry.S` jumps to `kernel/main.c` which is the starting point of 64-bit OS.

In the GDB script (`.gdbinit.tmpl`) that is provided, we already set a breakpoint at the entrance of xk (`main` in `kernel/main.c`). xk will go through the booting and stop at `main`. Type `n` to see which line of code in `main` (a) prints the cpu information and (b) prints the physical mmory table.

### Question #2:
We can examine memory using GDB’s `x` command. The GDB manual has full details, but for now, it is enough to know that the command `x/Nx ADDR` prints `N` words of memory at `ADDR`. (Note that both ‘x’s in the command are lowercase)

To examine instructions in memory (besides the immediate next one to be executed, which GDB prints automatically), use the `x/i` command. This command has the syntax `x/Ni ADDR`, where `N` is the number of consecutive instructions to disassemble and `ADDR` is the memory address at which to start disassembling.

What's the memory address of `main`? 0xffffffff80100612

Does GDB work with real physical address? No


## Part 2: Memory Management
Please take a look at the [memory management in xk](memory.md). After answer the following questions:

### Question #3
Why does xk map kernel and user-application into the same address space? (Why does the kernel not have a separate address space?)

### Question #4
Why is the xk user malloc (umalloc) different from the xk kernel malloc (kmalloc)? Why is the xk user printf different from the xk kernel printf?


## Part 3: Kernel

### Starting the first user process

<<<<<<< HEAD
xk then initialize the first user space program in `userinit`. `userinit`’s first action is to call `allocproc` in `kernel/proc.c`. The job
of `allocproc` is to allocate a slot (a struct proc) in the process table and to
initialize the parts of the process’s state required for its kernel thread to execute. `allocproc`
is called for each new process. The first user space program is `kernel/lab1test.c`, which outputs "hello world" and trapped in an infinite loop.
``` C
int main() {
  // if(open("console", O_RDWR) < 0){
  //  return -1;
  // }
  // dup(0);  // stdout
  // dup(0);  // stderr

  printf(1, "hello world\n");

  while(1) {}

  // testing code below
  ...
}
```
Later on, you will uncomment the first 4 lines of code and remove the `while(1) {}` statement so the
lab1 test cases can run.

## Part 3: System calls
xk has to support a list of system calls. Here is a list of system calls that are already implemented.
- `kill(pid)`
  + kill a process with a given process id
- `sleep(n)`
  + sleep for n clock ticks

### Trap
xk uses software interrupt to implement system calls. When user application needs to invoke a system call,
they issue an interrupt by `int 0x40`. The system call number is defined in `inc/syscall.h`. When user program
issues system call, user process is responsible to set register `%rax` to be the system call number.

The software interrupt is captured by the registered trap vector (`kernel/vectors.S`) and the handler in `kernel/vectors.S`
will run. The handler will reach the `trap` function in `kernel/trap.c` and the `trap` function to demux the interrupt to
`syscall` function implemented in `kernel/syscall.c`. `syscall` then demuxes the call to the respective handler in
`kernel/sysproc.c` or `kernel/sysfile.c`.

### Implement system calls
Your task is to implement the listed system calls (listed below). You will need a simple understanding of the file system used in xk.

#### File, file descriptor and inode
The kernel needs to keep track of the open files so it's can read, write, and eventually close the
files. A file descriptor is an integer that represents this open file. Somewhere in the kernel you
will need to keep track of these open files. Remember that file descriptors must be reusable between
processes. Having file descriptor 4 should be able to be different than file descriptor 4 in another
(although they could reference the same open file).

Traditionally the file descriptor is an index into an array of open files.

Remember console is simply a file from user application point of view. Even for reading from keyboard
and writing to screen, those functionality is accessed through the file interface.

#### Pipes

Pipes are a special method for inter-process communication.
An inode can be a regular file on the disk or the console.

### Question #5
What is the first line of code executing in kernel when there is an interrupt?

### Question #6
How large (in bytes) is the trap frame?

### Question #7
Set a breakpoint in the kernel implementation of a system call
(e.g., sys_write).
Do a backtrace. What functions are on the kernel stack when it reaches
sys_write?

### Exercise

#### Remember
- File descriptors are just integers.
- Look at already implemented sys calls to see how to parse the arguments.
- If a new file descriptor is allocated, it must be saved in the process's file descriptor tables. Similarly, if a file descriptor is released, this must be reflected in the file descriptor table.
- A full file descriptor table is an error.
- We have only a single user process in this assignment, so synchronization is not needed (e.g., the pipe is to/from the same process).

#### What To Implement
1) File Descriptor Opening
```c
/*
 * arg0: char * [path to the file]
 * arg1: int [mode for opening the file (see inc/fcntl.h)]
 *
 * Given a pathname for a file, sys_open() returns a file descriptor, a small,
 * nonnegative integer for use in subsequent system calls. The file descriptor
 * returned by a successful call will be the lowest-numbered file descriptor
 * not currently open for the process.
 *
 * returns -1 on error
 */
int
sys_open(void);
```

2) File Descriptor Reading
```c
/*
 * arg0: int [file descriptor]
 * arg1: char * [buffer to write read bytes to]
 * arg2: int [number of bytes to read]
 *
 * reads arg2 bytes from the file descriptor arg0 and places them in arg1.
 * returns number of bytes read, or -1 if there was an error.
 */
int
sys_read(void);
```

3) File Descriptor Writing
```c
/*
 * arg0: int [file descriptor]
 * arg1: char * [buffer of bytes to write to the given fd]
 * arg2: int [number of bytes to write]
 *
 * write arg2 bytes from arg1 to file descriptor arg0.
 * returns number of bytes written, or -1 if there was an error.
 */
int
sys_write(void);
```

4) Close a File
```c
/*
 * arg0: int [file descriptor]
 *
 * closes the passed in file descriptor
 * returns 0 on successful close, -1 otherwise
 */
int
sys_close(void);
```

5) Duplicate a File Descriptor
```c
/*
 * arg0: int [file descriptor]
 *
 * duplicate the file descriptor arg0, must use the smallest unused file descriptor
 * returns a new file descriptor of the duplicated file, -1 otherwise
 */
int
sys_dup(void);
```

6) Create a Pipe
```c
/*
 * arg0: int[2] [an array of sufficient size to hold the returned file descriptors]
 * NOTE: the arg0 pointer should not have valid file descriptors already in it.
 * the kernel will supply the pipe file descriptors
 *
 * sys_pipe() creates a pipe, a unidirectional data channel. The array arg0 is
 * used to return two file descriptors referring to the ends of the pipe.
 * arg0[0] refers to the read end of the pipe. arg0[1] refers to the write
 * end of the pipe. Data written to the write end of the pipe is buffered by
 * the kernel until it is read from the read end of the pipe.
 *
 * returns 0 on success, -1 otherwise
 */
int
sys_pipe(void);
```

## Testing and hand-in
After you implement the system calls described above. The kernel should print `lab1 tests passed!`.

### Question #8
For each member of the project team, how many hours did you
spend on this lab?

Create a file `lab1.txt` in the top-level xk directory with
your answers to the questions listed above.

Zip your source code and and upload via dropbox.
