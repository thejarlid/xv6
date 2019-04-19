// File system implementation.  Five layers:
//   + Blocks: allocator for raw disk blocks.
//   + Files: inode allocator, reading, writing, metadata.
//   + Directories: inode with special contents (list of other inodes!)
//   + Names: paths like /usr/rtm/xk/fs.c for convenient naming.
//
// This file contains the low-level file system manipulation
// routines.  The (higher-level) system call implementations
// are in sysfile.c.

#include <cdefs.h>
#include <defs.h>
#include <param.h>
#include <stat.h>
#include <mmu.h>
#include <proc.h>
#include <spinlock.h>
#include <sleeplock.h>
#include <fs.h>
#include <buf.h>
#include <file.h>

// there should be one superblock per disk device, but we run with
// only one device
struct superblock sb;

struct log {
  struct sleeplock lock;
  struct logHeader header;
} log;

int trx_in_progress;

// Read the super block.
void
readsb(int dev, struct superblock *sb)
{
  struct buf *bp;

  bp = bread(dev, 1);
  memmove(sb, bp->data, sizeof(*sb));
  brelse(bp);
}


// Inodes.
//
// An inode describes a single unnamed file.
// The inode disk structure holds metadata: the file's type,
// its size, the number of links referring to it, and the
// range of blocks holding the file's content.
//
// The inodes themselves are contained in a file known as the
// inodefile. This allows the number of inodes to grow dynamically
// appending to the end of the inode file. The inodefile has an
// inum of 1 and starts at sb.startinode.
//
// The kernel keeps a cache of in-use inodes in memory
// to provide a place for synchronizing access
// to inodes used by multiple processes. The cached
// inodes include book-keeping information that is
// not stored on disk: ip->ref and ip->flags.
//
// Since there is no writing to the file system there is no need
// for the callers to worry about coherence between the disk
// and the in memory copy, although that will become important
// if writing to the disk is introduced.
//
// Clients use iload() to populate an inode with valid information
// from the disk. idup() can be used to add an in memory reference
// to and inode. iput() will decrement the in memory reference count
// and will free the inode if there are no more references to it,
// freeing up space in the cache for the inode to be used again.

void init_inodefile(int dev);
void setDinodeAtBlockIndex(int index, struct buf *buf, struct dinode *dinode);

struct {
  struct spinlock lock;
  struct inode inode[NINODE];
  struct inode inodefile;
} icache;

void
iinit(int dev)
{
  int i = 0;

  initlock(&icache.lock, "icache");
  for(i = 0; i < NINODE; i++) {
    initsleeplock(&icache.inode[i].lock, "inode");
  }
  initsleeplock(&icache.inodefile.lock, "inodefile");
  initsleeplock(&log.lock, "log");

  readsb(dev, &sb);
  cprintf("sb: size %d nblocks %d bmap start %d inodestart %d\n",
     sb.size, sb.nblocks, sb.bmapstart, sb.inodestart);

  init_inodefile(dev);
}

// Find the inode file on the disk and load it into memory
// should only be called once, but is idempotent.
void
init_inodefile(int dev) {
  struct buf *b;
  struct dinode di;

  acquiresleep(&icache.inodefile.lock);
  b = bread(dev, sb.inodestart);
  memmove(&di, b->data, sizeof(struct dinode));

  icache.inodefile.inum = INODEFILEINO;
  icache.inodefile.dev = dev;
  icache.inodefile.type = di.type;
  icache.inodefile.major = di.major;
  icache.inodefile.minor = di.minor;
  icache.inodefile.nlink = di.nlink;
  icache.inodefile.size = di.size;
  icache.inodefile.data = di.data;

  brelse(b);
  releasesleep(&icache.inodefile.lock);
}


//PAGEBREAK!
// Find the inode with number inum on device dev
// and return the in-memory copy. Does not read
// the inode from from disk.
struct inode*
iget(uint dev, uint inum)
{
  struct inode *ip, *empty;

  acquire(&icache.lock);

  // Is the inode already cached?
  empty = 0;
  for(ip = &icache.inode[0]; ip < &icache.inode[NINODE]; ip++){
    if(ip->ref > 0 && ip->dev == dev && ip->inum == inum){
      ip->ref++;
      release(&icache.lock);
      return ip;
    }
    if(empty == 0 && ip->ref == 0)    // Remember empty slot.
      empty = ip;
  }

  // Recycle an inode cache entry.
  if(empty == 0)
    panic("iget: no inodes");

  ip = empty;
  ip->dev = dev;
  ip->inum = inum;
  ip->ref = 1;
  ip->flags = 0;
  release(&icache.lock);

  return ip;
}

static void
read_dinode(uint inum, struct dinode* dip)
{
  acquiresleep(&icache.inodefile.lock);
  readi(&icache.inodefile, (char *)dip, INODEOFF(inum), sizeof(*dip));
  releasesleep(&icache.inodefile.lock);
}

// Increment reference count for ip.
// Returns ip to enable ip = idup(ip1) idiom.
struct inode*
idup(struct inode *ip)
{
  acquire(&icache.lock);
  ip->ref++;
  release(&icache.lock);
  return ip;
}

// Reads the inode from disk if necessary.
void
iload(struct inode *ip)
{
  struct dinode dip;

  if(ip == 0 || ip->ref < 1)
    panic("iload");

  if(!(ip->flags & I_VALID)){
    read_dinode(ip->inum, &dip);
    ip->type = dip.type;
    ip->major = dip.major;
    ip->minor = dip.minor;
    ip->nlink = dip.nlink;
    ip->size = dip.size;
    ip->data = dip.data;
    ip->flags |= I_VALID;
    if(ip->type == 0)
      panic("iload: no type");
  }
}

// Drop a reference to an in-memory inode.
// If that was the last reference, the inode cache entry can
// be recycled.
// If that was the last reference and the inode has no links
// to it, free the inode (and its content) on disk.
void
iput(struct inode *ip)
{
  acquire(&icache.lock);
  if(ip->ref == 1 && (ip->flags & I_VALID) && ip->nlink == 0){
    // inode has no links and no other references: truncate and free.
    release(&icache.lock);
    ip->type = 0;
    acquire(&icache.lock);
    ip->flags = 0;
  }
  ip->ref--;
  release(&icache.lock);
}

// Copy stat information from inode.
void
stati(struct inode *ip, struct stat *st)
{
  st->dev = ip->dev;
  st->ino = ip->inum;
  st->type = ip->type;
  st->nlink = ip->nlink;
  st->size = ip->size;
}

//PAGEBREAK!
// Read data from inode.
int
readi(struct inode *ip, char *dst, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if(ip->type == T_DEV){
    if(ip->major < 0 || ip->major >= NDEV || !devsw[ip->major].read)
      return -1;
    return devsw[ip->major].read(ip, dst, n);
  }

  if(off > ip->size || off + n < off)
    return -1;
  if(off + n > ip->size)
    n = ip->size - off;

  for(tot=0; tot<n; tot+=m, off+=m, dst+=m){
    bp = bread(ip->dev, ip->data.startblkno + off/BSIZE);
    m = min(n - tot, BSIZE - off%BSIZE);
    /*
    cprintf("data off %d:\n", off);
    for (int j = 0; j < min(m, 10); j++) {
      cprintf("%x ", bp->data[off%BSIZE+j]);
    }
    cprintf("\n");
    */
    memmove(dst, bp->data + off%BSIZE, m);
    brelse(bp);
  }
  return n;
}

// PAGEBREAK!
// Write data to inode.
int
writei(struct inode *ip, char *src, uint off, uint n)
{
  // if we are writing a device we want to use device specific write
  // behaviour
  if(ip->type == T_DEV){
    if(ip->major < 0 || ip->major >= NDEV || !devsw[ip->major].write)
      return -1;
    return devsw[ip->major].write(ip, src, n);
  }

  // otherwise we are writing to a file
  // so firstly check all arguments for validity
  // then if the size of our file is larger than
  // the offset we are at and what we are reading
  // we need to increase the file size

  // in our version increasing the file size does
  // not involve extents since we do ours to have a
  // 20 block region
  uint tot, m;
  struct buf *bp;
  if(off > ip->size || off + n < off)
    return -1;
  if(off + n > ip->size)
    ip->size = off + n;

  // loop and write data to disk block chuncks or n bytes
  // at a time depending on which is smaller
  for(tot=0; tot<n; tot+=m, off+=m, src+=m){
    bp = bread(ip->dev, ip->data.startblkno + off/BSIZE);
    m = min(n - tot, BSIZE - off%BSIZE);
    memmove(bp->data + off%BSIZE, src, m);
    bwrite(bp);
    brelse(bp);
  }

  // after we have written the data to disk we now also
  // need to write the data for the changed inodeFile
  // that holds meta data for the file we are writing

  // create dinode and copy the metadata for the file
  // we wrote to into it
  struct dinode dinode;
  dinode.type = ip->type;
  dinode.major = ip->major;
  dinode.minor = ip->minor;
  dinode.nlink = ip->nlink;
  dinode.size = ip->size;
  dinode.data.startblkno = ip->data.startblkno;
  dinode.data.nblocks = ip->data.nblocks;

  // get the inodeFile and write the dinode data into the buffer
  // flush that buffer to disk
  struct inode *inodeFile = iget(ROOTDEV, INODEFILEINO);
  iload(inodeFile);
  bp = bread(inodeFile->dev, inodeFile->data.startblkno + INODEOFF(ip->inum)/BSIZE);
  memmove(bp->data + (uint)INODEOFF(ip->inum)%BSIZE, (char *)&dinode, sizeof(dinode));
  bwrite(bp);
  brelse(bp);
  return n;
}

//PAGEBREAK!
// Directories

int
namecmp(const char *s, const char *t)
{
  return strncmp(s, t, DIRSIZ);
}

// Look for a directory entry in a directory.
// If found, set *poff to byte offset of entry.
struct inode*
dirlookup(struct inode *dp, char *name, uint *poff)
{
  uint off, inum;
  struct dirent de;

  if(dp->type != T_DIR)
    panic("dirlookup not DIR");

  for(off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlink read");
    if(de.inum == 0)
      continue;
    // cprintf("%s == %s?\n", name, de.name);
    if(namecmp(name, de.name) == 0){
      // entry matches path element
      if(poff)
        *poff = off;
      inum = de.inum;
      return iget(dp->dev, inum);
    }
  }

  return 0;
}


//PAGEBREAK!
// Paths

// Copy the next path element from path into name.
// Return a pointer to the element following the copied one.
// The returned path has no leading slashes,
// so the caller can check *path=='\0' to see if the name is the last one.
// If no name to remove, return 0.
//
// Examples:
//   skipelem("a/bb/c", name) = "bb/c", setting name = "a"
//   skipelem("///a//bb", name) = "bb", setting name = "a"
//   skipelem("a", name) = "", setting name = "a"
//   skipelem("", name) = skipelem("////", name) = 0
//
static char*
skipelem(char *path, char *name)
{
  char *s;
  int len;

  while(*path == '/')
    path++;
  if(*path == 0)
    return 0;
  s = path;
  while(*path != '/' && *path != 0)
    path++;
  len = path - s;
  if(len >= DIRSIZ)
    memmove(name, s, DIRSIZ);
  else {
    memmove(name, s, len);
    name[len] = 0;
  }
  while(*path == '/')
    path++;
  return path;
}

// Look up and return the inode for a path name.
// If parent != 0, return the inode for the parent and copy the final
// path element into name, which must have room for DIRSIZ bytes.
// Must be called inside a transaction since it calls iput().
static struct inode*
namex(char *path, int nameiparent, char *name)
{
  struct inode *ip, *next;

  if(*path == '/')
    ip = iget(ROOTDEV, ROOTINO);
  else
    ip = idup(namei("/"));

  while((path = skipelem(path, name)) != 0){
    iload(ip);
    if(ip->type != T_DIR){
      iput(ip);
      return 0;
    }
    if(nameiparent && *path == '\0'){
      // Stop one level early.
      return ip;
    }
    if((next = dirlookup(ip, name, 0)) == 0){
      iput(ip);
      return 0;
    }
    iput(ip);
    ip = next;
  }
  if(nameiparent){
    iput(ip);
    return 0;
  }
  return ip;
}

struct inode*
namei(char *path)
{
  char name[DIRSIZ];
  return namex(path, 0, name);
}

struct inode*
nameiparent(char *path, char *name)
{
  return namex(path, 1, name);
}

void
setBitmapWithDinode(struct dinode *dinode) {
  // loop through the inode region starting at the start of the
  // inode region on disk
  for (int i = sb.inodestart + 20000; i < FSSIZE; i++) {
    uint block =  BBLOCK(i, sb);
    struct buf *buf = bread(ROOTDEV, block);
    int found = 0;
    if (!(buf->data[(i%BPB)/8] & (1 << (i % 8)))) {
      for (int j = i; j < i + 20; j++) {
        if ((buf->data[(j%BPB)/8] & (1 << (j % 8))) != 0) {
          found = 1;
          break;
        }
      }
      if (found == 0) {
        setDinodeAtBlockIndex(i, buf, dinode);
        return;
      }
    }

    brelse(buf);
  }
}

void
setDinodeAtBlockIndex(int index, struct buf *buf, struct dinode *dinode) {
  dinode->data.startblkno = index;
  dinode->data.nblocks = 20;
  for (int j = index; j < index + 20; j++) {
    buf->data[(j%BPB)/8] |= (1 << (j % 8));
  }
  bwrite(buf);
  brelse(buf);
}

void
log_start_tx() {
  acquiresleep(&log.lock);
  log.header.nblocks = 0;
  log.header.valid = 0;
  trx_in_progress = 1;
}

void
log_end_tx() {
  // end transaction
  // set commit message
  trx_in_progress = 0;
  log.header.valid = 1;

  // write commit message to disk
  struct inode *logHeader = iget(ROOTDEV, getLogStart());
  iload(logHeader);
  acquiresleep(&logHeader->lock);
  struct buf *bp = bread(logHeader->dev, logHeader->data.startblkno);
  memmove(bp->data, &log.header, sizeof(struct logHeader));
  bwrite(bp);
  brelse(bp);
  releasesleep(&logHeader->lock);

  // loop through the log and write every thing to disk
  // same action as the recover step
  // clear commit message on successful completeion
  // set nblocks to 0
  write_log_to_disk(&log.header);
  releasesleep(&log.lock);
}

void
log_recover() {
  // get header from disk and then
  // pass that to write log to disk method
}

void
write_log_to_disk(struct logHeader *header) {
  // need to get header from disk
  uint logstart = getLogStart();
  for (int i = 0; i < header->nblocks; i++) {
    int blockToWrite = header->writeLocation[i];
    bwriteBlockAToBlockB(logstart + 1 + i, blockToWrite);
  }
  header->valid = 0;
  header->nblocks = 0;
}

int
is_trx() {
  return trx_in_progress;
}

int
getBlocksInLog() {
  return log.header.nblocks;
}

void addNewWriteLocationToLog(uint blockno) {
  log.header.writeLocation[log.header.nblocks] = blockno;
  log.header.nblocks++;
}

struct logHeader*
getLogHeader() {
  return &log.header;
}

int getLogStart() {
  return sb.logstart;
}
