// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"

void freerange(void *vstart, void *vend);
extern char end[]; // first address after kernel loaded from ELF file
                   // defined by the kernel linker script in kernel.ld

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  int use_lock;
  struct run *freelist;
  uint num_free_pages;  //store number of free pages
  uint refc[(1<<10)+1];
} kmem;


void decrement_rmap(uint pa, int pid)
{
  int offset = pa>>PTXSHIFT;
  acquire(&kmem.lock);
  kmem.refc[offset] = kmem.refc[offset] - 1;
  // kmem.refc[offset].proc_list[p_id] = 0;
  release(&kmem.lock);
}

int get_refc(uint pa)
{
  int offset = pa>>PTXSHIFT;
  acquire(&kmem.lock);
  int count = kmem.refc[offset];
  release(&kmem.lock);
  return count; 
} 

void increment_rmap(uint pa, int pid)
{
  if(pa >= PHYSTOP || pa < (uint)V2P(end))
    panic("incrementReferenceCount"); 
  int offset = pa>>PTXSHIFT;
  acquire(&kmem.lock);
  kmem.refc[offset] = kmem.refc[offset] + 1;
  release(&kmem.lock);
}

// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.
void
kinit1(void *vstart, void *vend)
{
  initlock(&kmem.lock, "kmem");
  kmem.use_lock = 0;
  kmem.num_free_pages = 0;
  freerange(vstart, vend);
}

void
kinit2(void *vstart, void *vend)
{
  freerange(vstart, vend);
  kmem.use_lock = 1;
}

void
freerange(void *vstart, void *vend)
{
  char *p;
  p = (char*)PGROUNDUP((uint)vstart);
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE)
  {
    kmem.refc[V2P(p)>>PTXSHIFT] = 0;
    kfree(p);
    // kmem.num_free_pages+=1; Utk commented since it is already incremented in kfree
  }  
}
//PAGEBREAK: 21
// Free the page of *physical memory* pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(char *v)
{
  struct run *r;

  if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
    panic("kfree");


  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = (struct run*)v;

  //UTK
  if(kmem.refc[V2P(v) >> PTXSHIFT] > 0)         
  {
    kmem.refc[V2P(v) >> PTXSHIFT] = kmem.refc[V2P(v) >> PTXSHIFT] - 1;
  }

  if(kmem.refc[V2P(v) >> PTXSHIFT] == 0){       
    
    memset(v, 1, PGSIZE);     ///1 bitss bhardo har jagah cuz we are freeing the page and there shouldnt be any dangling references
    r->next = kmem.freelist;
    kmem.num_free_pages = kmem.num_free_pages + 1;
    kmem.freelist = r;
  }

  if(kmem.use_lock)
    release(&kmem.lock);
}
// Allocate one 4096-byte page of *physical memory*.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
char* kalloc(void)
{
  struct run *r;
  int found = 0;
  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
  {
    kmem.freelist = r->next;
    kmem.num_free_pages-=1;
    kmem.refc[V2P((char*)r)>>PTXSHIFT] = 1; //on allocation, refc is set to 1 UTK
    found = 1;
  }
  else{
    cprintf("No free pages available\n");
  }

  if(kmem.use_lock)
    release(&kmem.lock);

  if(!found){
    swapout();
    return kalloc();
    if(kmem.use_lock)
    {
      acquire(&kmem.lock);
    }
    r = kmem.freelist;
    if(r)
    {
      kmem.freelist = r->next;
      kmem.num_free_pages-=1;
      kmem.refc[V2P((char*)r)>>PTXSHIFT] = 1; //on allocation, refc is set to 1 UTK
    }
    else{
      cprintf("No free pages available\n");
    }
    if(kmem.use_lock)
      release(&kmem.lock);
  }
  return (char*)r;
}

uint num_of_FreePages(void)
{
  acquire(&kmem.lock);
  uint num_free_pages = kmem.num_free_pages;
  release(&kmem.lock);
  return num_free_pages;
}

