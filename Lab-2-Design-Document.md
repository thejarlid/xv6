# Lab 2 Design Document


# Overview

Our overall goal for this project is to add multiprocessing to xk so that we can have multiple processes running concurrently on both the user level and the kernel side. This will allow for more a more complex and robust operating system. To achieve the goal of multiprocessing we will first have to begin by fixing the synchronization issues that will arise from multiple processes. From this step we will implement some UNIX methods including fork, wait, exit, and finally exec.

To begin multiprocessing we have to start by making the current code base safe to be shared between multiple processes. This is the intent behind adding synchronization. To make sure that shared data between threads does not cause race conditions we will have to introduce locks and condition variables at various points in our kernel to ensure safe concurrent code.

Following synchronization we implement the UNIX system calls such as fork, wait, and exit. These will allow us to develop the lifecycle of a process in a sense. A challenge we have to address is that a process cannot delete all of its own memory so we have to find a way to clear its memory from the outside or another process. With the wait system call we have to challenging cases that we must handle. The first is if the parent process waits before a child exits we have to stall the parent process using a condition variable. The converse situation is that if the parent process exits before waiting for its children, we must still clean up the children’s data structures. One way to handle this is to have grandparents adopt the child process and manage cleaning up resources. While this is a valid solution, I am considering marking a process as a ZOMBIE and then on any call to a synchronization method we can call a helper method to clean up any zombie processes that exist. 

The final aspect of adding synchronization is to implement the exec method. The data structures used in this method are similar to those used in the fork method except we also have to pull the arguments handed to exec. The difficulty is that we must do a deep copy of the arguments from one process’s stack to the next, because each user-level process has its own stack frame. 


# In-depth Analysis and Implementation


## Synchronization

The first step to enable synchronization is the addition of locks to shared objects that we implemented in lab 1. Among the shared objects include the global file descriptor table and our pipe structs. For pipes in particular, in addition to using a lock, we have to also use condition variables as a mechanism to facilitate the pipe blocking on read while there is nothing to read. 

Beginning with the file descriptor system we have to protect against multiple threads accessing the same underlying data and to do this we will introduce a `sleeplock` into the global open file descriptor. With the addition of this field we now have to edit the methods which access the global file table to `acquiresleep` the lock upon entering and `releasesleep` the lock on exiting the method Methods which will require this addition include:


[x] file::openFile()
[x] file::readFile()
[x] file::writeFile()
[x] file::closeFile()
[x] file::dupFile()

By locking the global file table we can make changes to the file table from multiple processes and avoid race conditions. For example in the case of two processes both opening a file, instead of both processes being interleaved such that they both see index 7 is empty and then both processes attempt to claim that position with an open file we have a problem in that only on of the processes data will exist in the end. The `sleeplock` allows us to run one of the opens to completion before proceeding to the next. It also speeds up the Operating System by putting the process waiting to sleep while other process can run and then once the process that had the lock relinquishes it the sleeping process can wakeup and try acquire it again. In addition this allows us to modify files from different processes at the same time. If multiple readers and writers exist for a shared file (specifically for inode), we use the sleeplock inside the inode to protect it. We need to `acquiresleep(&f->ip->lock)` before changing the offset in the file and `releasesleep(&f->ip->lock)` after our operation on the offset.

Our implementation of pipes requires a different approach to synchronization than with the global file table. We also want to add the capability of a blocking call to `readPipe()` such that it will wait while there is nothing to read. Meaning `readPipe()` will wait until the pipe is written to. The blocking feature will be created using condition variables. The main difference is that we use spinlocks with pipes because condition variables require the use of spinlocks instead of sleeplocks.

In the my xk implementation pipes are allocated dynamically unlike with the global file table which is statically allocated. As a result we will add a `sleeplock` to each `struct pipe`. With the lock we can ensure mutual exclusion of the pipe so that the shared data is accessed atomically since only one process at a time can hold the lock. Each of the pipe methods must be edited to work with pipes. We must `acquire` and `release` inside `readPipe(), writePipe(), closePipe(), and openPipe()`.

The other aspect for pipes is to include the waiting which involves using condition variables. To implement this only our `readPipe() and writePipe()` will need to be changed. Inside read we have to check if the current read offset is equal to the write offset, if so we want to wait and release the lock this will be accomplished using `sleep()`. Under write we want to continue writing like normal but when we complete we want to signal to any other readers or writers that are waiting that they can continue and re-aquire the lock when the scheduler runs. To do this we use the `wakeup()` method. An additional point made by the xv6 book is that kill sets the process kill flag and the pipe read and write need to check fo this condtion. If this is the case we should simple return. The wakeup method does not release the lock it instead just places all the waiting threads on the ready list. We have to release the lock and then as we continue to time slice threads any of the previously waiting threads can then acquire it.

With the introduction of condition variables there are some issue and edge cases we have to take care off. If a process closes a write and we try to read but there is nothing to read then we cause the process to wait, but that wait will never stop because a write will never occur to call `wakeup`. 

As for synchronization and making access to shared objects safe, the above should allow for safe multiprocessing on our lab 1 implementations. 



## fork(), wait(), exit()

**fork()**
Fork is the first UNIX method we will implement for multiprocessing. This method creates a copy of the existing process from which it is called within. Upon calling this method 0 is returned to the child process and the process id is returned to the parent. This is the general guidelines for the method but the implementation that we will delve into are much more fine grained. 

Since we have to make a copy of the current process’s state we have to make a **deep copy** of everything in the current process. This deep copy means all kernel data structures that help govern the process such as the `struct proc, user memory, memory region, trapframe, and open files`. The memory region includes the code, heap, and stack for a process. The steps to implement this method are:


1. Make a new entry in the process table that must be created via `allocproc`
2. Copy user memory using `copyuvm`
3. The memory region descriptor must be duplicated in the new process entry in `ptable`
4. Duplicate the trap frame in the new process
5. Duplicate all the open files in the new process
  1. upon doing this we cause the child’s read and writes to also change the offset in the parent and vice-versa
6. Set the state of the new process to runnable
7. set the return register of fork to be different in both the child and parent process, it won’t matter which one gets which value because the processes are identical but the child should get 0 while the parent gets the parent id. 

**wait() and exit()**
These two methods work hand in hand similar to the pipe read and write methods. `wait` is a system call that a parent process uses to wait for a child to call `exit`. Our implementation will work closely to that described in xv6. To start we have to discuss `exit`, the job of this method is to remove a process from running and set it up to clear any memory. This involves first setting the process’s state to ZOMBIE so that it will not be time sliced in and then removing all state information that it holds. The difficulty is that the process itself cannot clear its own memory because if during exit we set our state to ZOMBIE and then before we can clear our state there is a context switch, the process we want to exit will never be given the processor again to complete deleting its state since it is a ZOMBIE. Instead we will make it so that the parent of a process is responsible for freeing the memory associated with the process. 

Under this high level design of exit, the more detailed approach is that upon exit we must first send a broadcast using `wakeup()`  for the parent of the exiting process to wakeup if it is being waited on. After this we have to take care of the children of the exiting process if there are any. This requires first to acquire the lock for the ptable and then search through the table to find any process whose parent is equal to the current process. For each such process we will set the parent of it to initproc as that is a process that will never be exited and has a loop to wait on all children. Following taking care of any adoption we can set the state to ZOMBIE, release the ptable lock and then calls `sched()` which will relinquish the CPU. Our parent process may be time sliced in at any point following its wakeup call but only once the lock is relinquished will it be able to perform any clean up. 

The above design ended with calling the parent process having now the responsibility to cleanup any exited children’s process data. Eventually a process whether it be the correct parent or `initproc` there will be a wait associated with the exit. Regardless of if it is called before or after the child exits. `wait()` will block until it finds a child that has exited and it can clean up its code. For this we will use condition variables as well as `sleep and wakeup`. Wait begins by acquiring the ptable lock, then it scans the process table looking for its children. If none of the children have exited, wait calls `sleep` to wait for one of them to exit and then scans again. If the parent process was sleeping in wait the processor will eventually run it if there is a child that calls exit since we make the wakeup call in the exit method. Upon the wakeup wait will return acquisition of the ptable lock and then scan the process table to find any exited children with state == Zombie. It records the child’s pid and then cleans up the struct proc freeing any memory. The parent must free `p→kstack and p→pgdir` when the child runs exit its stack sits in the memory allocated as `p→stack` and it uses its own page table. The other data that must be cleared include zeroing out all fields, making the state reflect that it can be used again and then releasing all the file references. Since we called dup to duplicate all the files on fork we have to decrement all their references upon exiting. 

One special caveat is that if a process calls wait and there are no children then it will wait forever and ever but never have a child that can possibly exit. In this case we can have wait return -1 and not block on wait. 


## exec()

`exec()` is the system call that creates the user part of an address space. It initialises the user part of an address space from a file stored in the file system. It also handles setting up all the required kernel data structures for the program. These structures are the same as those in `fork()`. Unlike `fork()` however, is that when `exec()` is called there are arguments passed to it that must be captured. We must first pull the arguments and then also construct the user stack and register state after loading the program to give the `main()`  method its arguments. We cannot simply do pointer copying because two user-programs have their own stack and page tables therefore we must do a deep copy.

We will use the `load_program_from_disk()` function to load a program into memory. When we get the file we have to read the ELF header for validity and information. We have to compare the ELF binary with the four-byte “magic” number 0x7F. `exec()` then allocates a new page table with no user mappings with `setupkvm`, allocates memory for each ELF segment with `alllocuvm`, and then loads each segment into memory with `loaduvm`. 

`exec` can now set up the user stack. To do so it allocates one stack page which `exec` then copies the argument strings to the top of the stack and records the pointers to them in `ustack`. It places a null pointer at the end of what will be the `argv` list passed to main. The first three entires in `ustack` are the fake return PC, argc, and argv pointer. Exec also places an inaccessible page just below the stack page so that the programs that try to use more than one page will fault. In this case the `copyout` function will return -1. If exec detects an error it jumps to the label bad, frees the new image and returns -1. Exec must wait to free the old image until it is sure that the system cal will succeed; if the old image is gone the system call cannot return -1. On success exec returns 0.

# Risk Analysis

As for Exec there are so many different caveats and issues that I have not fully fledged out the plan or the intent for the program. I intend to get much of the program done but adding to the stack is a difficult thing which would require a lot of work to get working correctly.

I belive the project would take me about 20 hours in the best case. If it is like the previous assignment then I am looking at around 30 hours or more, but that was because my partner left me a lot of work at the end that didnt work correctly. this time I will understand the system. The part that will take the most is the exec function. I will do everything till exec and spend the weekend on just that method.
