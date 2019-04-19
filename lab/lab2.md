# Lab 2: Multiprocessing

## Introduction
This lab adds multiprocessing to xk. As a first step,
before you start running multiple processes, you will need
to revisit your solution to lab 1 to make sure it is correct
even when system calls are called concurrently.  (Even without
multiple processors, the timer may expire during a system call,
causing a context switch to a new process.) Next, you will implement
UNIX fork, wait, and exit. Exit needs to cleanly return kernel allocated
pages back to the kernel heap. Wait allows a process to pause until
one of its child processes finishes executing. UNIX fork creates
a copy of the current process, returning from the system call in each
context (but with different return values).
Finally, you will need to implement UNIX exec, loading a new program
onto an existing process.

From lab2 on, we asked you to write a small design document. You need to bring 3 printed copies of your design document to section on Thursday, Oct 19 for peer review. Follow the guidelines in [how to write a design document](designdoc.md).

## Configuration
To test lab2 code, in `kernel/Makefrag`, replace `lab1test` with `lab2test` in
```
$(O)/initcode : kernel/initcode.S user/lab1test.c $(ULIB)
	$(CC) -g -nostdinc -I inc -c kernel/initcode.S -o $(O)/initcode.o
	$(CC) -g -ffreestanding -MD -MP -mno-sse -I inc -c user/lab1test.c -o $(O)/lab1test.o
	$(LD) $(LDFLAGS) -N -e start -Ttext 0 -o $(O)/initcode.out $(O)/initcode.o $(O)/lab1test.o $(ULIB)
	$(OBJCOPY) -S -O binary $(O)/initcode.out $(O)/initcode
	$(OBJDUMP) -S $(O)/initcode.out > $(O)/initcode.asm
```
This will create a new initcode binary which has lab2test in it.

## Part #1: Add synchronization

Once multiple processes are concurrently running on xk,
xk must ensure any shared data structures are properly protected.
Take a look at the definition of ptable in `kernel/proc.c`.

``` C
struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;
```

ptable is the table of user processes.
Notice there is a `struct spinlock lock` in the data structure.
This spinlock ensures mutual exclusion -- that at most one
process accesses the ptable
at the same time. Whenever the kernel needs to read or write the
process table, it must acquire the lock first and release it when done.
This type of lock can be found protecting most shared xk data structures.

xk also has condition variables for spinlocks: wakeup1 is the equivalent
of cv::broadcast (there is no signal -- all waiters are woken up).
sleep is the equivalent of cv::wait, puts the thread to sleep and releases
the spinlock, and re-acquires on wakeup.  

In addition to spinlocks, xk also has partial support for sleeping
locks (locks where the process gives up the processor if it needs to
wait for the lock). However, these don't support condition variables,
and so it is unlikely you will need to use them.

Please fix your lab 1 code to be safe for concurrency.  In particular,
you will need to use a spinlock to protect access to the shared file descriptor
table, if you have one.  Similarly, your pipe implementation will
need to use spinlocks and condition variables, similar to bounded buffer
described in class, to support multiple readers and writers.

## Part #2: Implement the fork, wait, and exit system calls
These system calls work together as a unit to support multiprocessing.
You can start by implementing fork, but one of the first tests will
have the new process call process exit.

xk fork duplicates the state of the user-level application.
This might seem a bit silly (why do we want to have
two copies of the same process?), but it actually makes writing a shell
program much simpler. The system call fork returns twice, once in the parent,
with the return value of the process ID (pid) of the child, and
once in the child, with the return value of 0.

Relative to UNIX fork, our tests simplify file descriptors somewhat.
In UNIX, the parent and child share open file descriptors. (E.g., a read
in one will change the file position in the other.)  Instead, it is ok
to create a copy of the open file descriptors in the child process.

### Exercise
Implement `fork` in `kernel/proc.c`. There are several things need to happen in `fork`.
- A new entry in the process table must be created via `allocproc`
- User memory must be duplicated via `copyuvm`
- All the memory region descriptor must be duplicated in the new process entry in `ptable`
- Trapframe must be duplicated in the new process
- All the opened files must be duplicated in the new process
- Set the state of the new process to be `RUNNABLE`
- You have to think of a way so that the return value of `fork` is different in the child and the parent process

Above are essentially all the information about the process that the kernel is keeping track of.

Once the new process is created, xk will run them concurrently via the
process scheduler. A hardware device generates a timer interrupt on
fixed intervals. If another process is RUNNABLE, the scheduler will switch
to it, essentially causing the current process to yield the CPU.

### Question #1
Describe the relationship between `scheduler`, `sched`, `swtch` in `kernel/proc.c`.

### Question #2
Describe why the child process is able to return to user-level application where `fork` is called.

## Synchronization between parent and child process
One of the first things our test cases will do is to have the forked child
call process exit. You need to implement exit to cleanly return the
allocated memory back to the kernel heap.  A complication is that the
process calling exit cannot free all of its memory, as it is running on
a stack in a process context, and e.g., the timer interrupt code will
assume that every interruptable process can be time sliced.

The textbook describes how to address this: when a process exits,
mark the process to be a `ZOMBIE`. This means that someone else (e.g.,
the process parent or the next process to run) is responsible for cleaning
up its memory.

The wait system call interacts with fork and exit: it is called by
the parent process to wait for a child. Note that a parent
can create multiple child processes. Wait does not have any argument.
Kernel finds one exited child and return the child's pid.
Further note that you need to be
careful with synchronization here: the parent may call wait before
any child calls exit.  In that case, the wait should stall (e.g., use
a condition variable) until one child exits.

Note that the parent need not call wait; it can exit without waiting
for the child.  The child's data structures must still be reclaimed
in this case.

There are various ways that you can implement all of this, so you should
think through the various cases before you start to code.
If you choose to let the parent clean up its exited children,
you will need some way to reclaim the children's
data when the parent exits first. (Note that in xk as in UNIX,
the initial process never exits.)

### Exercise
Implement `exit` and `wait`.

### Question #3
How does process `kill` work in xk? What's its relationship with `exit`?

## Part #3: Execute user program

User programs are in the file system using “Executable and Linkable Format” (i.e., ELF). Full information about this format is available in the [ELF specification](https://courses.cs.washington.edu/courses/cse451/16au/readings/elf.pdf) but you will not need to delve very deeply into the details of this format in this class. We have provided functions (i.e., `load_program_from_disk` in `kernel/exec.c`) to read data from disks.

You need to setup all the required kernel data structure for the program. Required data structures are the same as what you have done for `fork`.

One thing that is special about `exec` comparing with `fork` is that you need to pass in the arguments of the user program. In order to realize this functionality, you need to do first pull the arguments when a user process call `exec` and also carefully construct the user process stack and register state after loading the program to give the `main` function in the loaded program its arguments. Arguments xk pulled from and arguments xk giving to the new user-level program are both in user memory. However, simply copying pointer is not going to work because the page tables of the two user-level programs are different. You must create a deep copy of the argument from one user stack to another user stack.

`exec` has two arguments, a path to the executable ELF file, a pointer `argv` to an array of strings (`char *`). For example, let say we do `cat a.txt b.txt`. In `exec`, the first argument is the path to `cat` program. Second argument is an array of `char *` where the first one points to string `cat`, second points to string `a.txt`, the third points to string `b.txt` and the fourth element is `\0` indicating the end of an array.

When `exec` setups the user stack, we need to be careful. Note that every user program in xk has the same definition of main (except the testing scripts, because we didn't load testing scripts via `exec` yet).

``` C
int
main(int argc, char *argv[])
```

This means the first argument, `argc` is length of `argv`, where `argv` is a pointer to list of strings. In the previous example, this means you have to copy `a.txt` and `b.txt` to the user stack. Create an array on the user stack that first element is the pointer to `cat`, second element points to `a.txt`, third element points to `b.txt` and fourth element to be `\0`. You need to set `%rdi` register (first argument in x86_64 calling convention) to be 3 (length of argv) and `%rsi` register (second argument in x86_64 calling convention) to the argv array you created on the stack.

### Exercise
Implement `exec`.

## Testing and hand-in
After you implement the system calls described above. The kernel should be able to print `lab2 tests passed!`.

### Question #4
For each member of the project team, how many hours did you
spend on this lab?

Create a `lab2.txt` file in the top-level xk directory with
your answers to the questions listed above.

Zip your source code and and upload via dropbox.
