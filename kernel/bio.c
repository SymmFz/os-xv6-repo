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

#define NBUCKETS 13

struct {
  struct spinlock main_lock;
  struct spinlock lock[NBUCKETS];
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf hashbucket[NBUCKETS];
} bcache;

static inline int
hash(uint blockno)
{
  return blockno % NBUCKETS;
}

void
binit(void)
{
  struct buf *b;
  initlock(&bcache.main_lock, "bcache.main");

  int i;
  for (i=0; i<NBUCKETS; i++) {
    initlock(&bcache.lock[i], "bcache");
    bcache.hashbucket[i].prev = &bcache.hashbucket[i];
    bcache.hashbucket[i].next = &bcache.hashbucket[i];
  }

  // Create linked list of buffers
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    int bucket_no = (b - bcache.buf) % NBUCKETS;
    b->next = bcache.hashbucket[bucket_no].next;
    b->prev = &bcache.hashbucket[bucket_no];
    initsleeplock(&b->lock, "buffer");
    bcache.hashbucket[bucket_no].next->prev = b;
    bcache.hashbucket[bucket_no].next = b;
  }
}

// Finds a cached buffer in a bucket.
// Assumes the bucket lock is held.
static inline struct buf*
find_cached_buf(int bucket_no, uint dev, uint blockno)
{
  struct buf *b;
  for(b = bcache.hashbucket[bucket_no].next; b != &bcache.hashbucket[bucket_no]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      return b;
    }
  }
  return 0;
}

// Finds an unused buffer in a bucket and set it up.
// Assumes the bucket lock is held.
static inline struct buf*
find_and_setup_unused_buf(int bucket_no, uint dev, uint blockno)
{
  struct buf *b;
  for(b = bcache.hashbucket[bucket_no].prev; b != &bcache.hashbucket[bucket_no]; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      return b;
    }
  }
  return 0;
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int dst_bucket_no = hash(blockno);

  acquire(&bcache.lock[dst_bucket_no]);

  // Is the block already cached?
  b = find_cached_buf(dst_bucket_no, dev, blockno);
  if (b) {
    release(&bcache.lock[dst_bucket_no]);
    acquiresleep(&b->lock);
    return b;
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  b = find_and_setup_unused_buf(dst_bucket_no, dev, blockno);
  if (b) {
    release(&bcache.lock[dst_bucket_no]);
    acquiresleep(&b->lock);
    return b;
  }

  release(&bcache.lock[dst_bucket_no]);
  acquire(&bcache.main_lock);
  acquire(&bcache.lock[dst_bucket_no]);

  b = find_cached_buf(dst_bucket_no, dev, blockno);
  if (b) {
    release(&bcache.lock[dst_bucket_no]);
    release(&bcache.main_lock);
    acquiresleep(&b->lock);
    return b;
  }

  b = find_and_setup_unused_buf(dst_bucket_no, dev, blockno);
  if (b) {
    release(&bcache.lock[dst_bucket_no]);
    release(&bcache.main_lock);
    acquiresleep(&b->lock);
    return b;
  }

  for (int src_bucket_no=0; src_bucket_no<NBUCKETS; src_bucket_no++) {
    if (src_bucket_no == dst_bucket_no) {
      continue;
    }
    
    acquire(&bcache.lock[src_bucket_no]);
    for(b = bcache.hashbucket[src_bucket_no].prev; b != &bcache.hashbucket[src_bucket_no]; b = b->prev){
      if(b->refcnt == 0) {
        b->prev->next = b->next;
        b->next->prev = b->prev;

        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;
        
        b->next = bcache.hashbucket[dst_bucket_no].next;
        b->prev = &bcache.hashbucket[dst_bucket_no];
        bcache.hashbucket[dst_bucket_no].next->prev = b;
        bcache.hashbucket[dst_bucket_no].next = b;
        
        release(&bcache.lock[src_bucket_no]);
        release(&bcache.lock[dst_bucket_no]);
        release(&bcache.main_lock);
        acquiresleep(&b->lock);
        return b;
      }
    }
    release(&bcache.lock[src_bucket_no]);
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

  int bucket_no = hash(b->blockno);
  acquire(&bcache.lock[bucket_no]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.hashbucket[bucket_no].next;
    b->prev = &bcache.hashbucket[bucket_no];
    bcache.hashbucket[bucket_no].next->prev = b;
    bcache.hashbucket[bucket_no].next = b;
  }
  
  release(&bcache.lock[bucket_no]);
}

void
bpin(struct buf *b) {
  int bucket_no = hash(b->blockno);
  acquire(&bcache.lock[bucket_no]);
  b->refcnt++;
  release(&bcache.lock[bucket_no]);
}

void
bunpin(struct buf *b) {
  int bucket_no = hash(b->blockno);
  acquire(&bcache.lock[bucket_no]);
  b->refcnt--;
  release(&bcache.lock[bucket_no]);
}


