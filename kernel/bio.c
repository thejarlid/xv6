// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.
//
// The implementation uses two state flags internally:
// * B_VALID: the buffer data has been read from the disk.
// * B_DIRTY: the buffer data has been modified
//     and needs to be written to disk.

#include <cdefs.h>
#include <defs.h>
#include <param.h>
#include <spinlock.h>
#include <sleeplock.h>
#include <fs.h>
#include <buf.h>

int crashn_enable = 0;
int crashn = 0;

int num_disk_reads = 0;

struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // head.next is most recently used.
  struct buf head;
} bcache;

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

//PAGEBREAK!
  // Create linked list of buffers
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  acquire(&bcache.lock);

  // Is the block already cached?
  for(b = bcache.head.next; b != &bcache.head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached; recycle some unused buffer and clean buffer
  // "clean" because B_DIRTY and not locked means log.c
  // hasn't yet committed the changes to the buffer.
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    if(b->refcnt == 0 && (b->flags & B_DIRTY) == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->flags = 0;
      b->refcnt = 1;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  num_disk_reads += 1;
  struct buf *b;

  b = bget(dev, blockno);
  if(!(b->flags & B_VALID)) {
    iderw(b);
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if (crashn_enable) {
    crashn --;
    if (crashn < 0)
      reboot();
  }
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  b->flags |= B_DIRTY;
  if (is_trx()) {
    // put data in buffer cache
    // put data in log logHeader
    // write log header to Disk
    // write log index

    addNewWriteLocationToLog(b->blockno);

    // need to redirect b to the log
    // get log block number and offset
    uint logstart = getLogStart();
    uint datablock = logstart + 1 + getBlocksInLog();
    struct buf *logHeaderBuf = bget(ROOTDEV, logstart);
    struct buf *logBlockBuf = bget(ROOTDEV, datablock);

    // write b to logBlockBuf
    // write metadata to logHeaderBuf
    struct logHeader *header = getLogHeader();
    acquiresleep(&logHeaderBuf->lock);
    memmove(logHeaderBuf->data, (char *)header, sizeof(struct logHeader));
    releasesleep(&logHeaderBuf->lock);

    acquiresleep(&logBlockBuf->lock);
    memmove(logBlockBuf->data, (char *)b->data, BSIZE);
    releasesleep(&logBlockBuf->lock);

    // flush the log to disk
    iderw(logHeaderBuf);
    iderw(logBlockBuf);
  } else {
    iderw(b);
  }
}

void
bwriteBlockAToBlockB(uint srcBlockNum, uint destBlockNum) {
  struct buf *srcBuf = bget(ROOTDEV, srcBlockNum);
  struct buf *destBuf = bget(ROOTDEV, destBlockNum);

  acquiresleep(&destBuf->lock);
  memmove(destBuf->data, (char *)srcBuf->data, BSIZE);
  releasesleep(&destBuf->lock);

  // flush to disk
  iderw(destBuf);
}

// Release a locked buffer.
// Move to the head of the MRU list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  acquire(&bcache.lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }

  release(&bcache.lock);
}
//PAGEBREAK!
// Blank page.
