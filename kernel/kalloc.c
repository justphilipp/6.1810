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

#define PHY_PAGES ((uint64)(PHYSTOP - KERNBASE) / PGSIZE)
#define PA2RCI(pa) ((uint64)pa - (uint64)KERNBASE) / PGSIZE  // Physical address -> Reference count index
int ref_cnt[PHY_PAGES];

struct spinlock ref_lock;


struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

void
kinit()
{
	initlock(&ref_lock, "ref_lock");
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
	// clear reference count of physical pages
	memset(ref_cnt, 0, sizeof(int) * PHY_PAGES);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
		kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
	decref(pa);
	uint64 index;
	index = PA2RCI(pa);
	if(ref_cnt[index] > 0)
		return;

  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);
  r = (struct run*)pa;
  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
	int index;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r){
    kmem.freelist = r->next;
		// uint64 diff;
		// diff = (uint64)r - (uint64)KERNBASE;
		// printf("kalloc diff: %x, index : %d, ref count : %d\n", diff, index, ref_cnt[index]);
	}
  release(&kmem.lock);

  if(r){
		index = PA2RCI(r);
		ref_cnt[index] = 1;
    memset((char*)r, 5, PGSIZE); // fill with junk
	}
  return (void*)r;
}

void
incref(void *pa)
{
  acquire(&ref_lock);
	int index;
	index = PA2RCI(pa);
	//if(!ref_cnt[index])
	//	panic("kref");
	ref_cnt[index]++;
  release(&ref_lock);
}

void
decref(void *pa)
{
  acquire(&ref_lock);
	int index;
	index = PA2RCI(pa);
	ref_cnt[index]--;
  release(&ref_lock);
}
