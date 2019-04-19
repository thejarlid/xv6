#pragma once

#include "extent.h"

// On-disk file system format.
// Both the kernel and user programs use this header file.

#define INODEFILEINO   0  // inode file inum
#define ROOTINO        1  // root i-number
#define BSIZE        512  // block size


// Disk layout:
// [ boot block | super block | swap space | free bit map |
//                                          inode file | data blocks]
//
// mkfs computes the super block and builds an initial file system. The
// super block describes the disk layout:
struct superblock {
  uint size;         // Size of file system image (blocks)
  uint nblocks;      // Number of data blocks
  uint bmapstart;    // Block number of first free map block
  uint inodestart;   // Block number of the start of inode file
  uint swapstart;    // Block number of the start of the swap space
  uint swapsize;     // Size of swap space in block
  uint logstart;     // start of the log space
  uint logsize;      // size of the log region
};

// On-disk inode structure
struct dinode {
  short type;           // File type
  short major;          // Major device number (T_DEV only)
  short minor;          // Minor device number (T_DEV only)
  short nlink;          // Number of links to inode in file system
  uint size;            // Size of file (bytes)
  struct extent data;   // Data blocks of file on disk
  char pad[44];         // So disk inodes fit contiguosly in a block
};

struct logHeader {
  int valid;
  uint nblocks;
  uint writeLocation[30];
};

// offset of inode in inodefile
#define INODEOFF(inum) ((inum) * sizeof(struct dinode))

// Bitmap bits per block
#define BPB            (BSIZE*8)

// Block of free map containing bit for block b
#define BBLOCK(b, sb)  ((b)/BPB + (sb).bmapstart)

// Directory is a file containing a sequence of dirent structures.
#define DIRSIZ 14

struct dirent {
  ushort inum;
  char name[DIRSIZ];
};
