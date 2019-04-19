# Lab 4: Virtual Memory

## Introduction
Physical machines have limited amount of physical memory. To allow applications
(individually, or in aggregate) to consume more memory than this physical memory
limitation, operating system uses a designated region on the disk to serve as an extended memory.
On reaching the physical memory limit, operating systems will start to flush memory pages to disk
and load them back when needed.

## LRU page swap
In this lab, you are going to implement least-recent used (LRU) page swap. Currently, in QEMU (the hardware simulator), we set the amount of memory to be 16MB (4096 pages). When xk boots, it will show you how many memory pages are left. This number should be between 3000 - 4000 depending on how memory efficient your implementation is in the previous labs.
```
cpu0: starting xk

free pages: 3601
```
You can always query the amount of free pages using `sysinfo`. When memory pages are fully utilized, `kalloc` will return 0.

We ask you to implement a swap region of 32MB (8192 pages). With your implementation,
the system should behave as if it has 32MB (8192 pages) of physical memory.
We will not test scenarios when the system runs out of the swap space (in a real system
one would need to correctly handle that case).


### Question #1
How is the core_map allocated? Is it through `kalloc`? Will the core_map ever be evicted to disk?

### Reserve swap space on disks
`mkfs.c` is a utility tool to generate the content on the hard disk. You need to modify it to add the swap section. Currently, xk's hard drive has a disk layout as the following:

	+------------------+  <- number of blocks in the hard drive
	|                  |
	|      Unused      |
	|                  |
	+------------------+  <- block 2 + nbitmap + size of inodes + size of extent
	|                  |
	|                  |
	|      Extent      |
	|                  |
	|                  |
	+------------------+  <- block 2 + nbitmap + size of inodes
	|                  |
	|      Inodes      |
	|                  |
	+------------------+  <- block 2  + nbitmap
	|      Bitmap      |
	+------------------+  <- block 2
	|   Super Block    |
	+------------------+  <- block 1
	|    Boot Block    |
	+------------------+  <- block 0

The best place to add the swap space is between the super block and the bitmap. You can allocate 8192 * 8 blocks (each block is 512 bytes, you need 8 blocks to store a page of memory) in the swap region.

### Question #2
`mkfs.c` has functions like `xint`, `xshort`. What is the purpose of them?

### Disk read/write
You will use `bread` and `bwrite` to read/write the disk. Take a look at `struct buf` in `inc/buf.h`. To read data from a disk block, you can need some code similar to the following:
```c
buf = bread(dev, disk_addr);
memmove(mem, buf->data, BSIZE);
brelse(buf);
```
Here `disk_addr` should be the block number on disk. `buf` is the buffer that holds the disk content. Each `bread` will read 512 bytes from disk. To write to a disk block, you will need to use `bwrite` and its semantics is a little bit more complex than read. You need the following code snippet:
```c
buf = bread(dev, disk_addr);
memmove(buf->data, P2V(ph_addr), BSIZE);
bwrite(buf);
brelse(buf);
```

The reason you need to do `bread`, `bwrite` and then `brelse` to write to a disk block is that
there is a buffer cache layer under the block level API (e.g., `bread`, `bwrite`).
The buffer cache is a list of disk blocks that are cached in memory to reduce the number
of disk writes. `bread` will read from block cache if the block is already in the cache or otherwise load the disk block from disk into the buffer cache. `bwrite` flush the data in a `buf` to disk. Because there can be multiple readers/writers to a buffer cache block, the buffer cache layer has a reference count on any in-memory disk block. `brelse` decrements the reference count and deallocate the in-memory disk block when reference count drops to 0.

The interaction of virtual memory and the file buffer cache is one of the most complex
parts of operating systems. We designed it the interfaces for simplicity -- a more realistic
implementation would avoid the initial read on page evictions.

### Question #3
What will happen if xk runs out of block cache entries?

## Keep track of a memory page's state

You need to think about a list of questions:
- When should we flush pages to swap region and when should we load them back?
- How should we keep track of a memory page that is in swap region?
- What should happen when a swapped memory page is shared via copy-on-write fork?
- Is there a set of memory pages you don't want to flush to swap?
- What will happen when forking a process some of whose memory is in the swap region?
- How will the page table entry change for memory pages that are swapped out?
- What will happen when exiting a process whose memory is in the swap region?

You might need to define extra data structures in xk to realize those functionalities.
You might also need to use extra bits in the page table entry.
You can safely use bit 10 and bit 9 in the page table entry, as those are not used
by hardware.

### Question #4
xk guarantees that a physical memory page has a single virtual address because shared memory
is created by either `mmap` or `fork` (both of them keep virtual address same in both
child and parent process). However, in commercial operating systems, `mmap` can map a
file to different user memory space. Would your design for swap need to change if shared
page can have different virtual addresses? How will you change it?

## Reducing number of disk operations
If your implementation is naive, you are going to run into an issue when
the system starts to page, where it will perform more evictions than necessary.
Our test cases will stress your system so that you will need to implement
some approximation to least-recent used (LRU). This means, when a page is used recently,
it should not be evicted to the swap region on the disk.
The memory page swapped to the disk should be a page that is not recently used.
This way, future reads and writes will be less likely to touch that on-disk memory page
to reduce the number of reads and writes to the disk.
We of course do not require you to implement strict LRU; any reasonable approximation is
likely to pass the tests.

There are many options here. We have two suggestions.
(1) Use either page fault or `accessed` bit (`PTE_A`) to detect if a physical memory page is
used recently (2) Implement an eviction algorithm to pick an unused page to swap out.

### Exercise
Implement LRU page swap.

### Question #5
For each member of the project team, how many hours did you
spend on this lab?

Create a `lab4.txt` file in the top-level xk directory with
your answers to the questions listed above.

Zip your source code and and upload via dropbox.
