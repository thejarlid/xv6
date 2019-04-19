//
// File descriptors
//

#include <cdefs.h>
#include <defs.h>
#include <param.h>
#include <stat.h>
#include <mmu.h>
#include <param.h>
#include <fcntl.h>
#include <fs.h>
#include <spinlock.h>
#include <sleeplock.h>
#include <file.h>
#include <proc.h>

struct devsw devsw[NDEV];

struct {
  struct spinlock lock;
  struct file globalFileTable[NFILE];
} gftTable;

void
finit(void)
{
  initlock(&gftTable.lock, "gtfTable");
}

/*
  Allocates a file in the global file table if there
  is space, returns a pointer to the file in the table,
  returns NULL on error
*/
struct file*
openFile() {
  // loop through the global file table to find an open position
  acquire(&gftTable.lock);  // acquire lock
  for (int i = 0; i < NFILE; i++) {
    struct file *file = &gftTable.globalFileTable[i];
    if (file->refCount == 0) {
      // found an open position
      initsleeplock(&file->lock, "file");
      file->refCount = 1;
      release(&gftTable.lock);  // release lock on return
      return file;
    }
  }
  release(&gftTable.lock);  // release lock on return
  return NULL;
}

/*
  Takes a pointer to a struct file, a buffer and an int numBytes
  and tries to read as much upto numBytes into buffer. Returns
  the number of bytes read, -1 if fails to read
*/
int
readFile(struct file *file, char *buffer, int numBytes) {
  acquiresleep(&file->lock);
  // check if file has read permissions
  if (file->permissions == O_WRONLY || file->permissions == O_CREATE) {
    releasesleep(&file->lock);
    return -1;
  }

  // read based on file type
  int numRead = 0;
  if (file->type == FTYPE_INODE) {
    // read with inode
    acquiresleep(&file->inode->lock);
    numRead = readi(file->inode, buffer, file->offset, numBytes);
    releasesleep(&file->inode->lock);
    file->offset += numRead;
  } else {
    // read with pipe
    releasesleep(&file->lock);
    return readPipe(file->pipe, buffer, numBytes);
  }
  releasesleep(&file->lock);
  return numRead;
}

/*
  Writes numBytes worth of data from buffer to the struct file
*/
int
writeFile(struct file *file, char *buffer, int numBytes) {
  acquiresleep(&file->lock);
  if (file->permissions == O_RDONLY || file->permissions == O_CREATE) {
    releasesleep(&file->lock);
    return -1;
  }

  int numWritten = 0;
  // write based on file type
  if (file->type == FTYPE_INODE) {
    // write with inode
    acquiresleep(&file->inode->lock);
    numWritten = writei(file->inode, buffer, file->offset, numBytes);
    releasesleep(&file->inode->lock);
    file->offset += numWritten;
  } else {
    // write with pipe
    releasesleep(&file->lock);
    return writePipe(file->pipe, buffer, numBytes);
  }
  releasesleep(&file->lock);
  return numWritten;
}

/*
  given a file closes and removes a reference from it
  return 0 on success -1 otherwise
*/
int
closeFile(struct file *file) {
  acquiresleep(&file->lock);
  if (file->refCount == 0) {
    releasesleep(&file->lock);
    return -1;
  } else {
    file->refCount--;
    if (file->refCount == 0) {
      // release underlying inode or pipe
      if (file->type == FTYPE_INODE) {
        // release inode
        iput(file->inode);
      } else {
        // release pipe
        if (file->permissions == O_WRONLY) {
          file->pipe->writeClosed = 1;
        } else {
          file->pipe->readClosed = 1;
        }
        closePipe(file->pipe);
      }

      // clear file struct
      file->type = 0;
      file->permissions = 0;
      file->offset = 0;
      file->inode = 0;
      file->pipe = 0;
    }
    releasesleep(&file->lock);
    return 0;
  }
}

/*
  given a file adds a new reference to it
  returns -1 on error 0 on success
*/
int
dupFile(struct file *file) {
  acquiresleep(&file->lock);
  if (file->refCount == 0) {
    releasesleep(&file->lock);
    return -1;
  }
  file->refCount++;
  releasesleep(&file->lock);
  return 0;
}

/*
  returns the stat for a given file
*/
int
fstat(struct file *file, struct stat *st) {
  if(file->type == FTYPE_INODE){
    iload(file->inode);
    stati(file->inode, st);
    return 0;
  }
  return -1;
}

/*
  Given two pointers to file structs, makes a pipe
  that references those files and returns that pipe
*/
struct pipe *
openPipe(struct file *f1, struct file *f2) {
  // try creating 2 new files
  struct pipe *pipeSpace = (struct pipe*) kalloc();
  if (pipeSpace == NULL) {
    return NULL;
  }

  // set appropriate type and references to the pipe
  f1->type = FTYPE_PIPE;
  f1->pipe = pipeSpace;
  f2->type = FTYPE_PIPE;
  f2->pipe = pipeSpace;

  // set pipe struct values
  initlock(&pipeSpace->lock, "pipe");
  pipeSpace->readOffset = 0;
  pipeSpace->writeOffset = 0;
  pipeSpace->readClosed = 0;
  pipeSpace->writeClosed = 0;
  pipeSpace->referenceCount = 2;
  return pipeSpace;
}

/*
  Given a pipe and a buffer, reads from the pipe and into
  the buffer numBytes worth of information
*/
int readPipe(struct pipe *pipe, char *buffer, int numBytes) {
  int i = 0;
  // cannot read more than you write
  acquire(&pipe->lock);
  // here is where we need to test the condition variable
  // we have to wait on read until there is something to read
  struct proc *currentProcess = myproc();
  if (currentProcess->killed == 1 || pipe->readClosed == 1) {
    // pipe is invalid
    release(&pipe->lock);
    return -1;
  }

  while (pipe->readOffset == pipe->writeOffset && pipe->writeClosed == 0) {
    sleep(&pipe->readOffset, &pipe->lock);
  }

  while ((pipe->readOffset != pipe->writeOffset) && i < numBytes) {
  	buffer[i] = (pipe->buffer)[pipe->readOffset % PIPE_SIZE];
  	pipe->readOffset++;
  	i++;
  }
  release(&pipe->lock);
  return i;
}

/*
  Given a pipe and a buffer writes numBytes worth of data from
  the buffer into the pipe
*/
int writePipe(struct pipe *pipe, char *buffer, int numBytes) {
  int i = 0;
  acquire(&pipe->lock);
  struct proc *currentProcess = myproc();
  if (currentProcess->killed == 1 || pipe->writeClosed == 1) {
    // pipe is invalid
    wakeup(&pipe->readOffset);
    release(&pipe->lock);
    return -1;
  }

  while (i < numBytes) {
  	(pipe->buffer)[(pipe->writeOffset) % PIPE_SIZE] = buffer[i];
  	pipe->writeOffset++;
  	i++;
  }
  wakeup(&pipe->readOffset);
  release(&pipe->lock);
  return i;
}

/*
  Give a pipe closes it releasing any memory associated with it
*/
int closePipe(struct pipe *pipe) {
  acquire(&pipe->lock);
  if(pipe->referenceCount == 0) {
    wakeup(&pipe->readOffset);
    release(&pipe->lock);
    return -1;
  }

  // decrement pipe ref count
  pipe->referenceCount--;
  // only delete when both the read and write files have been closed
  if (pipe->referenceCount == 0) {
      wakeup(&pipe->readOffset);
      release(&pipe->lock);
      kfree((char *) pipe);
      return 0;
  }
  wakeup(&pipe->readOffset);
  release(&pipe->lock);
  return 0;
}

/*
  Given a file descriptor we copy the file's contents
  into user space so that it can be edited manually
  */
int mmap(int fd) {
  struct proc *currentProcess = myproc();
  struct file *file = currentProcess->oft[fd];

  //if the process currently has a mapped file return an error
  if (currentProcess->mem_regions[MMAP].start != 0) {
    return -1;
  }

  acquiresleep(&file->inode->lock);
  // allocate the appropriate pages starting at 2G that will hold the
  // file's content
  if (allocuvm(currentProcess->pml4, (char*)SZ_2G, 0, file->inode->size, currentProcess->pid) < 0) {
    releasesleep(&file->inode->lock);
    return -1;
  }

  // now that we have the pages allocated we need to place our file's contents in
  // that area
  if (loaduvm(currentProcess->pml4, (char*)SZ_2G, file->inode, 0, file->inode->size) < 0) {
    releasesleep(&file->inode->lock);
    return -1;
  }

  // set the current process's MMAP region properties appropriately
  currentProcess->mem_regions[MMAP].start = (char*)SZ_2G;
  currentProcess->mem_regions[MMAP].size = file->inode->size;
  currentProcess->mapped_file = file->inode;
  releasesleep(&file->inode->lock);
  return currentProcess->mem_regions[MMAP].size;
}

/*
  Given a file descriptor removes the memmory mapped file from user memory.
  Returns -1 on error 0 otherwise
*/
int munmap(int fd) {
  struct proc *currentProcess = myproc();
  struct file *file = currentProcess->oft[fd];

  // if this is not the mapped file return an error
  if (currentProcess->mapped_file != file->inode || currentProcess->mem_regions[MMAP].start == 0) {
    return -1;
  }

  acquiresleep(&file->inode->lock);
  // deallocate the appropriate pages starting at 2G that will hold the
  // file's content
  deallocuvm(currentProcess->pml4, currentProcess->mem_regions[MMAP].start,
    currentProcess->mem_regions[MMAP].size, 0, currentProcess->pid);

  // zero out the current process's MMAP region
  currentProcess->mem_regions[MMAP].start = 0;
  currentProcess->mem_regions[MMAP].size = 0;
  currentProcess->mapped_file = NULL;
  releasesleep(&file->inode->lock);
  return 0;
}
