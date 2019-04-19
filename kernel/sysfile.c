//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include <cdefs.h>
#include <defs.h>
#include <param.h>
#include <stat.h>
#include <mmu.h>
#include <proc.h>
#include <fs.h>
#include <spinlock.h>
#include <sleeplock.h>
#include <file.h>
#include <fcntl.h>

int     getOpenFileDescriptor();


/*
 * arg0: int [file descriptor]
 *
 * duplicate the file descriptor arg0, must use the smallest unused file descriptor
 * returns a new file descriptor of the duplicated file, -1 otherwise
 */
int
sys_dup(void)
{
  int fd;
  struct proc *currentProcess = myproc();
  if (argint(0, &fd) < 0 || fd < 0 || fd >= NOFILE || currentProcess->oft[fd] == 0) {
    // failed to obtain argument or invalid argument
    return -1;
  }

  // search for new open file table position and return -1 if we cannot find an open spot
  int newFd = getOpenFileDescriptor();
  if (newFd == -1) {
    // failed to find an open file table position
    return -1;
  }

  // set the reference at that position to the same file reference and
  // then call the file layer to duplicate the file
  currentProcess->oft[newFd] = currentProcess->oft[fd];
  dupFile(currentProcess->oft[fd]);
  return newFd;
}

/*
 * arg0: int [file descriptor]
 * arg1: char * [buffer to write read bytes to]
 * arg2: int [number of bytes to read]
 *
 * reads arg3 bytes from the file descriptor arg1 and places them in arg2.
 * returns number of bytes read, or -1 if there was an error.
 */
int
sys_read(void)
{
  // your code here
  int fd;
  char *buf;
  int numBytes;

  // get and check arguments
  if (argint(0, &fd) < 0 || argint(2, &numBytes) < 0 || argptr(1, &buf, numBytes) < 0) {
    // failed to get argument
    return -1;
  }

  struct proc *currentProcess = myproc();
  // file descriptor is not valid or numBytes is invalid
  if (fd < 0 || fd >= NOFILE || numBytes < 0 || currentProcess->oft[fd] == 0) {
    return -1;
  }
  return readFile(currentProcess->oft[fd], buf, numBytes);
}

/*
 * arg0: int [file descriptor]
 * arg1: char * [buffer to write read bytes to]
 * arg2: int [number of bytes to read]
 *
 * reads arg2 bytes from the file descriptor arg0 and places them in arg1.
 * returns number of bytes read, or -1 if there was an error.
 */
int
sys_write(void)
{
  // you have to change the code in this function.
  // Currently it support printing one character to screen

  int fd;
  int n;
  char *p;

  // get arguments
  if(argint(2, &n) < 0 || argptr(1, &p, n) < 0 || argint(0, &fd) < 0) {
    // failed to retrieve arguments
    return -1;
  }

  struct proc *currentProcess = myproc();

  // check file descriptor and n's validity
  if (fd < 0 || fd >= NOFILE || n < 0 || currentProcess->oft[fd] == 0) {
    // invalid args
    return -1;
  }

  // get current process and call the file descriptor layer's implementation
  return writeFile(currentProcess->oft[fd], p, n);
}

/*
 * arg0: int [file descriptor]
 *
 * closes the passed in file descriptor
 * returns 0 on successful close, -1 otherwise
 */
int
sys_close(void)
{
  // your code here
  int fd;

  struct proc *currentProcess = myproc();
  if(argint(0, &fd) < 0 || fd < 0 || fd >= NOFILE || currentProcess->oft[fd] == 0) {
    // failed to retrieve arguments or invalid arguments
    return -1;
  }

  // get current file and make the open file table spot vacent
  struct file *currentFile = currentProcess->oft[fd];
  currentProcess->oft[fd] = 0;
  return closeFile(currentFile);
}

int
sys_fstat(void)
{
  int fd;
  struct file *f;
  struct stat *st;
  if(argint(0, &fd) < 0 || argptr(1, (void*)&st, sizeof(*st)) < 0) {
    // failed to retrieve arguments or invalid arguments
    return -1;
  }

  // get the current process's open file tabe, if the open file table entry for the
  // requested file descriptor is empty then return an error. Otherwise call fstat to
  // get the stat struct information
  struct proc *currentProcess = myproc();
  f = currentProcess->oft[fd];
  if(currentProcess->oft[fd] == 0) {
    // failed to retrieve arguments or invalid arguments
    return -1;
  }
  return fstat(f, st);
}

// Create the path new as a link to the same inode as old.
int
sys_link(void)
{
  return -1;
}


//PAGEBREAK!
int
sys_unlink(void)
{
  return -1;
}

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
sys_open(void)
{
  // your code here
  // 1. get arguments
  //    - path to file (char *)
  //    - mode for opening the file (int)
  // 2. handle file based on mode
  //    - 0 : read only open with r
  //    - 1 : write only - error for now
  //    - 2 : read + write - open with r + w only if the inode is a device
  //    - 0x200 : create - error for now
  // 3. Get inode corresponding to filepath
  // 4. Test for device if mode is O_RDWR
  // 5. Allocate a file in the global file table
  // 6. Fill in current process's oft
  // 7. Configure struct file
  // 8. return fd for process's oft

  int mode;
  char *filename;
  if (argint(1, &mode) < 0 || argstr(0, &filename) < 0) {
    // arguments not valid
    return -1;
  }

  // check if we need to create a file
  if ((mode & O_CREATE) && namei(filename) == 0) {
    // get the inodeFile from disk
    // log_start_tx();
    struct inode *inodeFile = iget(ROOTDEV, INODEFILEINO);
    iload(inodeFile);

    // make a new dinode for the new file
    struct dinode dinode;
    dinode.type = T_FILE;
    dinode.major = 0;
    dinode.minor = 0;
    dinode.nlink = 1;
    dinode.size = 0;
    dinode.data.startblkno = 0;
    dinode.data.nblocks = 0;

    // get an area of 20 blocks for the dinode
    // on disk, this method sets the startblockno
    // and nblocks of the dinode to a found region
    setBitmapWithDinode(&dinode);

    struct inode *rootDirectory = iget(ROOTDEV, ROOTINO);
    iload(rootDirectory);

    struct dirent entry;
    strncpy(entry.name, filename, DIRSIZ);


    acquiresleep(&inodeFile->lock);
    entry.inum = inodeFile->size / sizeof(dinode);
    writei(inodeFile, (char *)&dinode, inodeFile->size, sizeof(dinode));
    // char *b = kalloc();
    // readi(inodeFile, b, inodeFile->size, sizeof(dinode));
    // cprintf("buf = %s\n", b);
    releasesleep(&inodeFile->lock);

    acquiresleep(&rootDirectory->lock);
    writei(rootDirectory, (char*)&entry, rootDirectory->size, sizeof(entry));
    releasesleep(&rootDirectory->lock);

    init_inodefile(ROOTDEV);
    // log_end_tx();
  }

  struct inode *inode;
  if ((inode = namei(filename)) == 0) {
    // could not find an inode with given path and name
    return -1;
  }

  // load inode THIS IS WHERE THE BUG WAS WE COULD NOT
  // PRINT OR READ BECAUSE DATA WASN'T LOADED
  iload(inode);

  // test inode to make sure it is a device if using rdwr
  if (inode->type == T_DEV && mode != O_RDWR) {
    iput(inode);
    return -1;
  }
  // above snippet didnt work
  // if (mode == O_RDWR && inode->type != T_DEV) {
  //   return -1;
  // }

  // allocate file and then put that file into the open file table for the process
  struct proc *currentProcess = myproc();
  struct file *newFile = openFile();
  if (newFile == NULL) {
    // we have to release the inode reference we have if we faile to find an
    // open spot
    iput(inode);
    return -1;
  }

  int fd = getOpenFileDescriptor();
  if (fd == -1) {
    // we could not find an open position in the open file table so we
    // have to close the file space allocated and return an error
    closeFile(newFile);
    iput(inode);
    return -1;
  }
  currentProcess->oft[fd] = newFile;

  // set values on the file struct and return file descriptor
  newFile->type = FTYPE_INODE;
  newFile->inode = inode;
  newFile->offset = 0;
  newFile->permissions = mode;
  return fd;
}

int
sys_mkdir(void)
{
  return -1;
}

int
sys_mknod(void)
{
  return -1;
}

int
sys_chdir(void)
{
  return -1;
}

int
sys_exec(void)
{
  // your code here
  // need to get arguments
  // need to then call exec with the two arguments
  // when pulling argptr for size use the fact that
  // the most number of arguments we will have is like
  // 30 or something there's a constant in param.h we can
  // use. Then inside the actual exec function we can
  // just loop till we see the null terminator to find
  // argc
  //
  // grab arguments, program name and then the pointer
  // to the char array
  //

  char *path;
  char **argv;
  if (argstr(0, &path) < 0 || argptr(1, (void *)&argv, 8)) {
    return -1;
  }
  return exec(path, argv);
}

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
sys_pipe(void)
{
  // retrieve argument int[2]
  int *arg;
  int totalSize = sizeof(arg[0]) + sizeof(arg[1]);
  int ret = argptr(0, (char **)&arg, totalSize);
  if (ret < 0) {
    return -1;
  }

  struct proc *currentProcess = myproc();

  // get fd1
  struct file *f1 = openFile();
  if (f1 == NULL) {
    // allocating space for file1 failed
    return -1;
  }
  f1->type = FTYPE_PIPE;

  // get fd2
  struct file *f2 = openFile();
  if (f2 == NULL) {
    // allocated a second file failed so we must
    // close the space for the first file
    closeFile(f1);
    return -1;
  }
  f2->type = FTYPE_PIPE;

  // get both the open file descriptors and if either fails, close
  // the other file structs we allocated
  int fd1 = getOpenFileDescriptor();
  if (fd1 == -1 ) {
    // we could not find an open position in the open file table so we
    // have to close the file space allocated and return an error
    closeFile(f1);
    closeFile(f2);
    return -1;
  }
  currentProcess->oft[fd1] = f1;

  int fd2 = getOpenFileDescriptor();
  if (fd2 == -1) {
    // we could not find an open position in the open file table so we
    // have to close the file space allocated and return an error
    closeFile(f1);
    closeFile(f2);
    currentProcess->oft[fd1] = f1;
    return -1;
  }
  currentProcess->oft[fd2] = f2;

  // make a pipe wirh given files
  struct pipe *pipe = openPipe(f1, f2);
  if (pipe == NULL) {
    // making the pipe failed, close all data and space
    // we allocated resetting the state to how it was
    // before the method call
    closeFile(f1);
    closeFile(f2);
    currentProcess->oft[fd1] = f1;
    currentProcess->oft[fd2] = f2;
    return -1;
  }

  // set permissions on the files
  f1->permissions = O_RDONLY;
  f2->permissions = O_WRONLY;

  // set the file descriptors in the return argument
  arg[0] = fd1;
  arg[1] = fd2;
  return 0;
}

/*
  Helper method to get an open file descriptor in the table,
  return the index into the process's open file table if it
  succeeds, -1 if it fails
*/
int
getOpenFileDescriptor() {
  struct proc *currentProcess = myproc();
  // loop through ever process to find an open file and whether we can use it
  for (int i = 0; i < NOFILE; i++) {
    if (currentProcess->oft[i] == 0) {
      return i;
    }
  }
  return -1;
}

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
sys_mmap(void)
{
  int fd;
  if (argint(0, &fd) < 0 || fd >= NOFILE || fd < 0) {
    // failed to retrieve arguments or invalid arguements
    return -1;
  }
  return mmap(fd);
}

/*
 * arg0: int [file descriptor of the mapped region]
 *
 * Remove the memory mapped region
 *
 * returns -1 on error
 */
int
sys_munmap(void)
{
  int fd;
  if (argint(0, &fd) < 0 || fd >= NOFILE || fd < 0) {
    // failed to retrieve arguments or invalid arguements
    return -1;
  }
  return munmap(fd);
}
