#pragma once

#include <sleeplock.h>
#include <extent.h>


// in-memory copy of an inode
struct inode {
  uint dev;           // Device number
  uint inum;          // Inode number
  int ref;            // Reference count
  struct sleeplock lock;
  int flags;          // I_VALID

  short type;         // copy of disk inode
  short major;
  short minor;
  short nlink;
  uint size;
  struct extent data;

};
#define I_VALID 0x2

// table mapping major device number to
// device functions
struct devsw {
  int (*read)(struct inode*, char*, int);
  int (*write)(struct inode*, char*, int);
};

extern struct devsw devsw[];

#define CONSOLE 1

#define FTYPE_INODE  1   // inode
#define FTYPE_PIPE 2     // pipe

#define PIPE_SIZE 2048    // max number of bytes for pipe buffer

// file struct
struct file {
  struct sleeplock lock;  // sleeplock for synchronization
  short type;          // File type: inode or pipe
  int offset;          // the offset into the file
  struct inode *inode; // underlying inode
  struct pipe *pipe;   // underlying pipe
  int permissions;     // read or write permissions for file
  int refCount;        // number of references to this file
};

// pipe struct
struct pipe {
  struct spinlock lock;
  int readOffset;         // bytes read
  int writeOffset;        // bytes written
  char buffer[PIPE_SIZE]; // pipe buffer
  int readClosed;         // if 1 read side is closed
  int writeClosed;        // if 1 write side is closed
  int referenceCount;     // number of references to pipe
};

//PAGEBREAK!
// Blank page.
