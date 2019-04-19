# Lab 5: Filesystem

## Introduction
A restriction on the file system behavior in previous labs is that
files cannot be created, deleted, or extended.  In this lab, we are going to add
that functionality to the filesystem. This is non-trivial because these operations
(often) involve updating multiple disk blocks,
but they need to be performed in a way that is crash-safe.

Another restriction in the implementation is that there is only one file directory (root or '/').
The information in the directory -- the name of each file and where to find the information about each file
is stored in a file.  For simplicity, there are no sub-directories.  We will keep that restriction in lab 5.

## Caution!
When your code writes to the QEMU simulated disk, QEMU will store the contents of the disk in a file.
The next time you run QEMU (i.e., run `make qemu` without `make clean`), it will used the modified disk, rather than the original disk,
unless you explicitly regenerate the disk (i.e., run `make clean` to delete the disk, `make` will re-generate the disk content). Sometimes, this will be what you want -- e.g., to test
crash safety.  At other times, e.g., if you have a bug that leaves the disk in a corrupted state,
you will want to make sure to regenerate the disk to ensure you start with the disk in a known state.
You are not required to write a file system checker (fsck), but you may find it useful to add some
code to the xk initialization that checks and warns the user if the disk is original or modified.

## Disk layout
In xk's baseline implementation, all files are laid out on disk sequentially.
Your first task is to change the disk layout to enable files to be extended from their initial size.

In lab4, you have already changed `mkfs.c` to add the swap section on the disk. This lab requires you to
understand how files are laid out on disk. In your swap implementation, you will have a
disk layout as the following:

	+------------------+  <- number of blocks in the hard drive
	|                  |
	|      Unused      |
	|                  |
	+------------------+  <- block 2 + nswap + nbitmap + size of inodes + cumulative extents
	|                  |
	|                  |
	|      Extents     |
	|                  |
	|                  |
	+------------------+  <- block 2 + nswap + nbitmap + size of inodes
	|                  |
	|      Inodes      |
	|                  |
	+------------------+  <- block 2 + nswap + nbitmap
	|      Bitmap      |
	+------------------+  <- block 2 + nswap
	|                  |
	|       Swap       |
	|                  |
	+------------------+  <- block 2
	|   Super Block    |
	+------------------+  <- block 1
	|    Boot Block    |
	+------------------+  <- block 0

The boot block is used for the bootloader; the superblock describes how the disk is formatted.
The swap area is used for paging.  The bitmap is a bit array with 1/0 for whether a particular
disk block is used/free.  The inode table is a contiguous region of the disk to save metadata about
each file; each inode is (for now) 64 bytes and describes where to find the disk blocks for that file,
along with some additional information about the file (e.g., file permissions on systems that
support file permissions).  The inode table is stored as a file, with its own inode;
this would allow us to extend the number
of files in the system.

The zeroth inode is by default the metadata for the inode file; the next inode is the root directory.
Where to find the start of the inode file is stored in the superblock.

A directory holds file names and inode numbers -- the index into the inode table.
Note that we have no sub-directories, and we won't ask you to implement those.

The data for each file is stored in the extents region. On boot, each file is contiguously allocated.
The lab will ask you to add the ability to extend files; this will require using the disk region
beyond the end of the pre-allocated extents.

You are free to choose your own on-disk format.
We only require you to maintain compatibility with the file system call interface; you can change
the file layout however you choose.  (We recommend you try to keep the changes minimal.)

In particular, you will likely want to add fields to the inode
definition, e.g., to support multiple extents.  You can use the vacant part of
the `struct dinode` in `inc/fs.h` to include more extents (e.g, a region on disk),
or you can create a block whose content is a list of block numbers (i.e., an indirect blocks).
If you modify `struct dinode`, you need to make sure its size is a power of 2.
This is to keep individual inodes from spanning multiple disk blocks.

### Question #1
What's the purpose of the super block? What fields are in the super block?

### Question #2
What's the purpose of the disk bitmap?

## Write to file
Currently, write is forbidden at inode level. You can modify `writei` in `kernel/fs.c` so that
the filesystem can write to the disk. Similar to lab4, you are going to use
`bread`, `bwrite` and `brelease` to write to disk blocks.
You can take a look at `readi` implementation in `kernel/fs.c` to learn about how to handle
file offset and files whose length is more than a single block.

You will also need to change the implementation of the open system call. In the previous labs,
the only allowed flag to open regular files is `O_RDONLY`. Now you will
need to support `O_RDWR` for read/write access to file.
You can look into `user/lab5test_a.c` to see the expected uses cases.

### Question #3
Explain how `dirlookup` works.

## Append to a file
Once you can write to a file, the next step is to be able to extend the size of a file.
This is done by writing beyond the current end of the file. You will need to allocate
additional space for the file (e.g., with extra block pointers or an extra extent pointer)
and then fill that space with the data being written.

If a file is being mmap'ed at the same time it is being appended to, there is no change to the
virtual memory system -- that is, mmap maps a fixed region of memory.

## Create files
In addition to writing and appending to files, you will also need to support creation
of files from the root directory.  You create new files by passing `O_CREATE` to the open system call.

You need to create a empty inode on disk, change the root directory to add a link to the new file, and (depending on your disk layout) change bitmap on disk. File deletion is not required.

### Exercise
Enable writes to filesystem. Run `lab5test_a.c` from the shell, you should pass all the tests.

## Crash-safe file system

Now that xk can add, and modify files, the file system is still vulnerable to crashes.
A main challenge in file system design is to make the system crash-safe: the file system state
is always correct wherever the execution is when the machine crashes.

For example, suppose you use the system call write to append a block of data to a file.  
Appending to a file (may) require changing the bitmap to allocate a new data block, changing the inode
to hold the new file length, and writing the actual data.  The underlying disk system, however, writes
a single block (actually, an individual sector!) at a time.  
Without crash-safety, the file could end up with inconsistent data:
e.g., the bitmap having allocated the block but the file doesn't use it, or vice versa.  Or the
file length changed, but the data not written so that a read to the end of the file returns garbage.
A crash-safe filesystem ensures that the file system is either entirely the new data and entirely old data.

There are several ways to ensure crash-safety, and we don't specify which one you should choose.
We will talk about several in lecture.  The most common technique is to add journaling.
The main idea is that for each multi-disk operation,
to write each part of the operation to a separate area of the disk (the log) first, before any
of the changes are written to the main part of the disk (the inode table, the bitmap, etc.).  
Once all parts of the operation are in the log, you can safely write the changes back to the file system.
If a failure occurs, you read the log to see if there were any completed operations, and if so
you will need to copy those changes back to the disk before continuing.  In other words, the
contents of the log are idempotent -- able to be applied multiple times without changing the outcome.

The log can be allocated at the beginning of the disk, similar to what we did with the swap area.
Thus, we need to be able to ensure that each operation fits in the log.  Generally, this is done
by ensuring that file create and single block write/append are atomic and crash safe.
Multi-block write/append is implemented as a sequence of one block atomic and crash safe write/appends.
The data format in directories and inodes are designed so that updates apply to a single block.

If you choose to implement journaling, you can put the logging layer between inode layer and block cache layer. In this way, you can use `bread`, `bwrite` interfaces. You need to implement two helper functions `begin_trans()` and `end_trans()` to package a transaction. You also need to implement a wrapper function `log_write`. The difference between `log_write` and `bwrite` is that `log_write` does not write to the actual disk location. `log_write` only does bookkeeping. All the actually disk writes will happen in `end_trans()` (i.e., writing block content to log, write log header, move disk content from log to actual location, clean up the log). If the machine crashes before log header is modified, the system behaves as if the multi-block transaction has not happened. If the machine crashes after the log header is written, then after the machine reboots, xk has full knowledge of what the multi-block transaction is in the log so that xk can enforce the transaction to succeed. Previously when xk writes to a disk block `disk_addr`, it does something like the following:
```c
// disk operation 1
buf = bread(dev, disk_addr);
memmove(buf->data, P2V(ph_addr), BSIZE);
bwrite(buf);
brelse(buf);
// more disk operations
// ....
```
Now, to use such a logging layer, xk should do following:
```c
begin_trans()
// disk operation 1
buf = bread(dev, disk_addr);
memmove(buf->data, P2V(ph_addr), BSIZE);
log_write(buf);
brelse(buf);
// more disk operations
// ....
end_trans()
```
There is one caveat: if the reference count of a block is 0 in the block cache layer, it is going to be garbage claimed (see `kernel/bio.c`). In the above example, you can think of it as we want variable `buf` not to be garbage collected after `brelse` because `end_trans()` need the content of `buf`. The only way to prevent a block cache from eviction is to set it to be a dirty block. For example, if you want to prevent buffer b from eviction you can set the flags to be dirty by `b->flags |= B_DIRTY`.

Testing for crash-safety is a bit complex.
In your file system, there is a test file called `user/lab5test_b.c`.
The test code calls a helper system call `crashn` which causes the system to reboot the OS
after `n` disk operations. The test first sets up a future crash point via `crashn` and creates a zero length file, then appends three blocks to the file
using a single `write` system call.
If your file system is crash-safe, then for whatever `n` passed into `crashn`, after xk boots,
the file should either be in its initial state, not exist,
be size 0, size 1 block, size 2 blocks, or size 3 blocks
(after write).  Also the content of the file must be correct when the file size is above 0.

Our scripts `crash_safety_test.py` reboots xk three hundreds times with different `n` value for `crashn`.
Each time xk reboots, we check whether the file system is still in a good state. Another thing we check is that if a large `n` is provided, the end state should be achieved.

### Question #4
File delete is not a required feature in lab5. Describe how you can implement it in a crash-safe manner even if the file spans multiple blocks.

### Question #5
Support for multi-block write is not required in lab5. Describe how to extend your current implementation to support it.

### Exercise
Build a crash-safe file system. Run `crash_safety_test.py`. It should print out `file system is crash-safe`.

### Question #6
For each member of the project team, how many hours did you
spend on this lab?

Create a `lab5.txt` file in the top-level xk directory with
your answers to the questions listed above.

Zip your source code and and upload via dropbox.
