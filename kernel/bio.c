// Buffer cache.
//
// The buffer cache uses hash buckets of buf structures holding
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


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKET 31

struct {
  struct spinlock lock;
  struct buf *head;
} bucket[NBUCKET];

struct {
  struct spinlock lock;
  struct buf buf[NBUF];
  uint victim;
} bcache;

static uint
bhash(uint dev, uint blockno)
{
  return (blockno + dev) % NBUCKET;
}

static void
binsert(struct buf *b, uint h)
{
  b->next = bucket[h].head;
  b->prev = 0;
  if(b->next)
    b->next->prev = b;
  bucket[h].head = b;
}

void
binit(void)
{
  initlock(&bcache.lock, "bcache.evict");
  for(int h = 0; h < NBUCKET; h++) {
    initlock(&bucket[h].lock, "bcache.bucket");
  }
  for(struct buf *b = bcache.buf; b < bcache.buf + NBUF; b++) {
    initsleeplock(&b->lock, "buffer");
    binsert(b, 0);
  }
}

static struct buf *
blookup(uint dev, uint blockno, uint h)
{
  for(struct buf *b = bucket[h].head; b; b = b->next) {
    if(b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      return b;
    }
  }
  return 0;
}

static struct buf*
bget(uint dev, uint blockno)
{
  uint h = bhash(dev, blockno);
  struct buf *b;

  acquire(&bucket[h].lock);
  b = blookup(dev, blockno, h);
  release(&bucket[h].lock);
  if(b) {
    acquiresleep(&b->lock);
    return b;
  }

  acquire(&bcache.lock);
  acquire(&bucket[h].lock);
  b = blookup(dev, blockno, h);
  release(&bucket[h].lock);
  if(b) {
    release(&bcache.lock);
    acquiresleep(&b->lock);
    return b;
  }

  for(int n = 0; n < NBUF; n++) {
    b = &bcache.buf[bcache.victim];
    bcache.victim = (bcache.victim + 1) % NBUF;
    uint old = bhash(b->dev, b->blockno);
    acquire(&bucket[old].lock);
    if(b->refcnt == 0) {
      if(b->next)
        b->next->prev = b->prev;
      if(b->prev)
        b->prev->next = b->next;
      else
        bucket[old].head = b->next;
      b->refcnt = 1;
      release(&bucket[old].lock);

      acquire(&bucket[h].lock);
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      binsert(b, h);
      release(&bucket[h].lock);
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
    release(&bucket[old].lock);
  }
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// A referenced buffer cannot move between buckets.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");
  releasesleep(&b->lock);
  uint h = bhash(b->dev, b->blockno);
  acquire(&bucket[h].lock);
  b->refcnt--;
  release(&bucket[h].lock);
}

void
bpin(struct buf *b)
{
  uint h = bhash(b->dev, b->blockno);
  acquire(&bucket[h].lock);
  b->refcnt++;
  release(&bucket[h].lock);
}

void
bunpin(struct buf *b)
{
  uint h = bhash(b->dev, b->blockno);
  acquire(&bucket[h].lock);
  b->refcnt--;
  release(&bucket[h].lock);
}
