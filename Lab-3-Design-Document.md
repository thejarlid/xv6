# Lab 3 Design Document

# Overview

The overall aim for this lab is to add address space management to xk and after doing so we can run xk’s shell. 

To begin the assignment we first need to implement the `sbrk` system call that will grow or shrink our heap memory by the increment passed to it. A user can pass `n` bytes as an argument to the function call and this can be positive or negative. Negative indicating to shrink the heap and positive indicating to grow the heap. In the larger scale, our `exec` call sets up a process to have an initial heap size of 0 so when a user calls malloc it will trap and call `sbrk`.  `sbrk()` returns the virtual memory address of the new n bytes. 

Once we complete the above step xk should be able to launch a functional shell upon boot. The second major implementation for this project is to introduce memory-mapped I/O. Up until now I/O in xk uses read/write system calls on file descriptors. By introducing memory-mapped I/O we can map a file’s contents into a user memory region. What this allows us to do is that users can read/write the file using regular machine instructions on mapped memory. If two process map the same file then the memory region acts as shared memory between the two process where each process can see the other process’ writes to the memory region. We will need to use the read-modify-write instructions to ensure mutual exclusion and data race freedom. To simplify our process we will map only one file per process. 

Following all this implementation of growing memory, we now want to shift gears onto how we can reduce the amount of memory xk consumes. The first is to grow the user stack on-demand. Currently our user stack is a fixed size of one page when we call `exec` to load a user program. However we can change that to allocate only the memory that is needed at runtime. Whenever a user issues an instruction that reads or writes the user stack (eg. creating a stack frame, accessing local variables), we grow the stack as needed. 

The second method for which we will optimize memory performance is by modifying our implementation of `fork` to use copy-on-write. Currently `fork` duplicates every page of user memory in the parent process which can take quite a bit of time if the parent process’ memory is very large. To reduce the cost of `fork` we allow multiple process’ to share the same physics memory while at a logical level still behaving as if memory was copied. The idea is that as long as neither process modifies the memory it can stay shared, if either process changes a page, a copy of that page will be made.


# In-depth Analysis and Implementation 
## Sbrk

Our implementation for `sbrk` involves modifying two files sysproc.c and proc.c. Like every method in sys_file and sys_proc the goal of the wrapper method is to extract the user arguments and then pass them onto an actual implementation for sbrk. 

Within sysproc.c we will implement the `sys_sbrk` method. This method should extract an integer parameter that represents the number of bytes by which to increment or decrement our heap space by. If we fail to obtain the int we will return -1. Upon successfully extracting our parameter we will then call sbrk in the proc.c file. 

As a a side note we need to add the method header for sbrk to the defs.h file to make it publically accessible. It makes sense that sbrk is part of the proc file because it’s a per process action that modified the process itself. The interface for sbrk takes an int representing amount by which to grow or shrink the heap and returns the virtual address of the end of the heap also known as the break. 

When we first `exec` a program it’s heap is initialized to a size of zero, `men_region[HEAP].size = 0` and so when the client first calls malloc we always have to call sbrk first. 


![](https://i.stack.imgur.com/1aV6B.png)


Taking a look at the above image the “break” is the dotted line above the heap segment, it indicates the top of the heap. An increase in the heap moves that bar further up. Our implementation of `sbrk` will first need to take the number of bytes that we want to displace by and round it to the nearest page size. If its positive we round up to the nearest page size, otherwise if its negative we round down to the nearest page size. This is because to grow the process we have to work in page increments. After doing so we get a new page aligned size lets call it `page_aligned_size`. We now need to make sure that we don’t cross any bounds such as growing into the stack or decreasing below into the data region. So we need to check `page_aligned_size +  men_region[HEAP].size < 0 || page_aligned_size + men_region[HEAP].size > men_regions[GUARD]`. If either is true we will return an error. 

If the checks pass, we can proceed to allocate space. We begin by allocating a page using `kalloc`. We may have to allocate multiple pages based on whether the size passed in does or doesn’t fit in a single page. Once we have the pages we then call `memset(addr, 0, PGSIZE)` on the page to zero it out and then we call `mapages` that will create a page table entry in the given page table. This is all very very similar to how `initustack`  works. We pass in the virtual address shifted by PT_SHIFT and then the physical address the process id and the process’ page table. 

Following all of this we can set the region start to the new address of the page and then the size to `page_aligned_size +  men_region[HEAP].size`. Finally we return the start of the heap address. 


## !!!!!**USE ALLOCUVM AND DEALLOCUVM IT DOES EXACTLY THE ABOVE STUFF!!!!!!**


## Memory Mapped I/O

Every process will have the ability to open a file in a memory mapped region which it can read and write from. The area for this data exists above the top of the stack and so it is safe from being over written. One concern however is that there is kernel code above that area so we have a max file size to limit the files that can be placed in that region and stop the file from overwriting the kernel code or stack code.

Because our system is read only the writes that occur will be discarded on close. The general idea of this method is that if two processes simultaneously `mmap` a file, both should point to the same physics memory. In our implementation we can only `mmap` one file so if a user tries to map another we should throw an error. 

To make two process point to the same file we need to make sure the page table entry for a file is the same in every process that has opened that file. We must also use locks to protect against any data races that could occur.  We will need to use read-modify-write instructions to ensure mutual exclusion and data race freedom.

need to modify:

- file 
- kallloc
- kfree
- core map page table entry to keep reference for the page
- fork

There are two methods which we will implement for this 

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
    /*
     * arg0: int [file descriptor of the mapped region]
     *
     * Remove the memory mapped region
     *
     * returns -1 on error
     */
    int
    munmap(void)


## Grow User Stack On Demand

The user stack initially has a default size of one page and contains the user arguments passed to the program on launch. As the application runs the stack will have to grow to allow for more data. Unlike the heap, the stack doesn’t grow by the user calling a method explicitly. The way the stack should grow is that as a program runs and eventually comes across a point where it needs to read or write data to a position beyond the stack then we must trap into the kernel, determine if the page fault was something related to a legitimate cause and if so handle it by extending the stack otherwise we throw an error. 

To implement grow-stack-on-demand, we will need to understand how to handle page faults. A page fault is a hardware exception that occurs when a program accesses a virtual memory page without a valid page table entry, or with a valid entry, but where the program does not have permission to perform the operation. On the hardware exception, control will trap into the kernel; the exception handler will add memory to the stack region and resume execution. 

In this lab, we will assume user stack is never more than 10 pages. To do this stack extension we run code very similar to the heap page allocation however this time we map the stack region and extend the size. 


## Copy-On Write Fork

Currently, fork duplicates every page of user memory in the parent process. However we can optimize this using Copy-On Write. What we do is allow multiple processes to share the same physical memory, while at the logical level it acts as if the memory was copied. As long as the neither process modifies the memory, it can stay shared; if either process changes a page, a copy of that page will be made at that point. 

In `fork` we return the child process with the page table that points to the same memory pages as the parent. No additional memory is allocated for the new process other than to hold the page table. The page table of both processes will need to be changed to be read-only. That way if either process tries to alter the contents of their memory, a trap to the kernel will occur and the kernel can make a copy of the memory page at that point before resuming user code. 

With this addition we have to add synchronization to the child and parent so that they can concurrently attempt to write to the same page. To make a memory page read-only we change the bit to `PTE_RO` in the page table entry. Setting/unsetting this bit doesn’t change the behavior of xk but it can be used to keep track of whether the page is a copy-on-write page. When a page is set to be read-only, `PTE_W` must be unset and replaced with `PTE_RO`. Once the user tries to write and a fault is generated we need to make a deep copy of the page and set the `PTE_W` bit. 

We have to work with altering the page fault code. so that we can handle the writing on a read only page and make the deep copy. The other tricky part is that the child process with a set of copy on write pages can fork another child. thus any physical memory can actually be shared by many processes. This should exclude Memory-Mapped I/O we want the writes to still be shared. We can do this by just setting the bit to read write so we don’t trap to change it . 


# Risk Analysis

I have not looked into growing the stack on-demand very much and I have also not looked into copy-on write either, I will get to those as soon as I can but first I’ve gotta study for my AI midterm on Friday :D. I also don’t understand the full idea behind mmap and what is actually going on. Do I copy a file’s data into that area? What is actually being put in that area that I allocate and do I have to allocate and deallocate pages as such? 

I think this project will take a good 15 hours or so minimum. It will take a lot of research to learn how to do the copy-on write and the stack growing. Furthermore I intend for there to be some difficult errors which I will need time to debug. 

To get close to the 15 hours I will get the first 3 sections done as soon as possible so that I can dedicate most of my time towards the last 2 sections which seem to be the most difficult parts. 

