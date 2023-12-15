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


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKET 13



struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.

  // struct buf head;
} bcache;

struct bucket{
  struct spinlock bucket_lock;
};

struct bucket buckets[NBUCKET];

  void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");
  for (int i = 0; i < NBUCKET; i++){
    initlock(&buckets[i].bucket_lock, "bcache.bucket");
  }

  for(b = bcache.buf; b < bcache.buf + NBUF; b++){
    initsleeplock(&b->lock, "buffer");
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
  static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  struct bucket *bkt = &buckets[blockno % NBUCKET];
  acquire(&bkt->bucket_lock);
  // Is the block already cached?
  for(int i = 0; i < NBUF; i++){
    b = &bcache.buf[i];
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bkt->bucket_lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  release(&bkt->bucket_lock);


  // Not cached.
  // eviction
  acquire(&bcache.lock);
  acquire(&bkt->bucket_lock);

  // Still not cached!
  for(int i = 0; i < NBUF; i++){
    b = &bcache.buf[i];
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock);
      release(&bkt->bucket_lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  for(int i = 0; i < NBUF; i++){
    b = &bcache.buf[i];
    int obtained = 1;
    int evict_idx = b->blockno % NBUCKET;
    if(evict_idx != blockno % NBUCKET){
      acquire(&buckets[evict_idx].bucket_lock);
      obtained = 0;
    }
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.lock);
      if(!obtained){  // evicted block and target block is the same!
        release(&buckets[evict_idx].bucket_lock);
      }
      release(&bkt->bucket_lock);
      acquiresleep(&b->lock);
      return b;
    }
    if(!obtained){
      release(&buckets[evict_idx].bucket_lock);
    }
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

// Release a locked buffer.
// Move to the head of the most-recently-used list.
  void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int bkt_index = b->blockno % NBUCKET;
  acquire(&buckets[bkt_index].bucket_lock);
  b->refcnt--;
  release(&buckets[bkt_index].bucket_lock);
}

void
bpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt++;
  release(&bcache.lock);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt--;
  release(&bcache.lock);
}


