// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct kmem {
  struct spinlock lock;
  struct run *freelist;
};
struct kmem kmems[NCPU];
static char kmem_lock_names[NCPU][16];


void
kinit()
{
  uint64 total_free_mem = (char *)PHYSTOP - (char *)end;
  uint64 mem_per_cpu = total_free_mem / NCPU;

  int cpu_no;
 
  for (cpu_no=0; cpu_no<NCPU; cpu_no++) {
    snprintf(kmem_lock_names[cpu_no], sizeof(kmem_lock_names[cpu_no]), "kmem%d", cpu_no);
    initlock(&kmems[cpu_no].lock, kmem_lock_names[cpu_no]);

    void *pa_start = (void *)((char *)end + cpu_no * mem_per_cpu);
    void *pa_end = (void *)((char *)pa_start + mem_per_cpu);
    freerange(pa_start, pa_end);
  }
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  int cpu_id = cpuid();
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmems[cpu_id].lock);
  r->next = kmems[cpu_id].freelist;
  kmems[cpu_id].freelist = r;
  release(&kmems[cpu_id].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  int cpu_id = cpuid();
  struct run *r;

  acquire(&kmems[cpu_id].lock);
  r = kmems[cpu_id].freelist;
  if(r) {
    kmems[cpu_id].freelist = r->next;
    release(&kmems[cpu_id].lock);
  } else {
    release(&kmems[cpu_id].lock);

    int cpu_no;
    for (cpu_no=0; !r && cpu_no<NCPU; cpu_no++) {
      if (cpu_no == cpu_id) {
        continue;
      }

      acquire(&kmems[cpu_no].lock);
      r = kmems[cpu_no].freelist;
      if (r) {
        kmems[cpu_no].freelist = r->next;
      }
      release(&kmems[cpu_no].lock);
    }
  }

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
