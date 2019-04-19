// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include <cdefs.h>
#include <defs.h>
#include <param.h>
#include <memlayout.h>
#include <mmu.h>
#include <e820.h>
#include <spinlock.h>
#include <sleeplock.h>
#include <fs.h>
#include <proc.h>
#include <buf.h>

#define NDISKPAGES 8192

int npages = 0;
int pages_in_use;
int pages_in_swap;
int free_pages;

struct core_map_entry *core_map = NULL;
struct swap_map_entry swap_core_map[NDISKPAGES];

struct core_map_entry*
pa2page(uint64_t pa)
{
  if (PGNUM(pa) >= npages) {
    cprintf("%x\n", pa);
    panic("pa2page called with invalid pa");
  }
  return &core_map[PGNUM(pa)];
}

uint64_t
page2pa(struct core_map_entry *pp)
{
  return (pp - core_map) << PT_SHIFT;
}

// --------------------------------------------------------------
// Detect machine's physical memory setup.
// --------------------------------------------------------------

void
detect_memory(void)
{
  uint32_t i;
  struct e820_entry *e;
  size_t mem = 0, mem_max = -KERNBASE;

  e = e820_map.entries;
  for (i = 0; i != e820_map.nr; ++i, ++e) {
    if (e->addr >= mem_max)
      continue;
    mem = max(mem, (size_t)(e->addr + e->len));
  }

  // Limit memory to 256MB.
  mem = min(mem, mem_max);
  npages = mem / PGSIZE;
  cprintf("E820: physical memory %dMB\n", mem / 1024 / 1024);
}

void freerange(void *vstart, void *vend);
extern char end[]; // first address after kernel loaded from ELF file

struct {
  struct spinlock lock;
  int use_lock;
  struct core_map_entry *freelist;
} kmem;

// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.
void
mem_init(void *vstart)
{
  void *vend;

  core_map = vstart;
  memset(vstart, 0, PGROUNDUP(npages * sizeof (struct core_map_entry)));
  vstart += PGROUNDUP(npages * sizeof (struct core_map_entry));

  initlock(&kmem.lock, "kmem");
  kmem.use_lock = 0;
  kmem.freelist = 0;

  vend = (void *) P2V((uint64_t)(npages * PGSIZE));
  freerange(vstart, vend);
  free_pages = (vend - vstart) >> PT_SHIFT;
  pages_in_use = 0;
  pages_in_swap = 0;
  kmem.use_lock = 1;
}

void
freerange(void *vstart, void *vend)
{
  char *p;
  p = (char*)PGROUNDUP((uint64_t)vstart);
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE)
    kfree(p);
}

//PAGEBREAK: 21
// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(char *v)
{
  struct core_map_entry *r;

  if((uint64_t)v % PGSIZE || v < _end || V2P(v) >= (uint64_t)(npages * PGSIZE))
    panic("kfree");

  if(kmem.use_lock)
    acquire(&kmem.lock);

  r = (struct core_map_entry*)pa2page(V2P(v));
  if (r->refCount > 0) {
    r->refCount--;
  }

  if (r->refCount > 0) {
    if(kmem.use_lock)
      release(&kmem.lock);
    return;
  }

  pages_in_use --;
  free_pages ++;

  // Fill with junk to catch dangling refs.
  memset(v, 2, PGSIZE);

  r->pid = -1;
  r->va = 0;
  r->next = kmem.freelist;
  r->refCount = 0;
  kmem.freelist = r;
  if(kmem.use_lock)
    release(&kmem.lock);
}

void
add_phy_mem_map(int pid, uint64_t va, uint64_t pa) {
  // check if it is a kernal mem map
  if (pid == -1)
    return;

  // for user mem, add an mapping to proc_info
  struct core_map_entry *r = pa2page(pa);

  r->pid = pid;
  r->va = va;
}

void
remove_phy_mem_map(int pid, uint64_t va, uint64_t pa) {
  // check if it is a kernal mem map
  if (pid == -1)
    return;

  // for user mem, add an mapping to proc_info
  struct core_map_entry *r = pa2page(pa);

  r->pid = -1;
  r->va = 0;
}



char*
kalloc(void)
{
  pages_in_use ++;
  free_pages --;

  struct core_map_entry *r;

  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = kmem.freelist;
  if(r) {
    kmem.freelist = r->next;
    r->refCount = 1;
    if(kmem.use_lock) {
      release(&kmem.lock);
    }

    return P2V(page2pa(r));
  } else {
    // we need to evict a page, free that page
    // then return that newly freed page
    if(kmem.use_lock) {
      release(&kmem.lock);
    }
    if (evictPage() == -1) {
      return 0;
    }
    if(kmem.use_lock) {
      acquire(&kmem.lock);
    }

    r = kmem.freelist;
    kmem.freelist = r->next;
    r->refCount = 1;
    if(kmem.use_lock) {
      release(&kmem.lock);
    }

    return P2V(page2pa(r));
  }

  if(kmem.use_lock) {
    release(&kmem.lock);
  }

  return 0;
}

int
evictPage() {
  // find a page to evict
  struct core_map_entry *pageToEvict = findPageToEvict();
  if (pageToEvict == 0) {
    return -1;
  }

  // move data to disk using the index and offset for the
  // swap_core_map
  int swapIndex = getFreeDiskPageIndex();
  if (swapIndex == -1) {
    return -1;
  }

  // loop through every block for this page and write
  // block by block
  // 8 blocks = 1 page
  uint64_t pa = page2pa(pageToEvict);
  for (int i = 0; i < 8; i++) {
    uint disk_addr = 2 + (swapIndex * 8) + i;
    struct buf *b = bread(ROOTDEV, disk_addr);
    memmove(b->data, P2V(pa) + (BSIZE * i), BSIZE);
    bwrite(b);
    brelse(b);
  }

  // put the core_map_entry in the swap_core_map
  //swap_core_map[swapIndex] = pageToEvict;
  swap_core_map[swapIndex].va = pageToEvict->va;
  swap_core_map[swapIndex].pid = pageToEvict->pid;
  swap_core_map[swapIndex].refCount = pageToEvict->refCount;
  swap_core_map[swapIndex].in_use = 1;

  // loop over all processes in the ptable, use walkpml4 to get a PTE for the VA of the
  // core_map_entry
  // see if the physical memeory address matches the physical address we are swapping out.
  // for each valid PTE we need to indicate that the page is on disk and turn of the present
  // bit
  uint64_t va = pageToEvict->va;
  for (int i = 0; i < NPROC; i++) {
    struct proc *process = getProcessAtIndex(i);
    // for each process go through every page attributed to it
    // and search for that va, if it exists then you can
    // do a page walk
    for (int region = 0; region < 4; region++) {
      uint64_t region_start = (uint64_t)process->mem_regions[region].start;
      uint64_t region_size = process->mem_regions[region].size;

      if (va >= region_start && va < (region_start + region_size)) {
        pte_t *pte = walkpml4(process->pml4, (char*)va, 0);
        if (PTE_ADDR(*pte) == pa) {
          *pte = PTE_FLAGS(*pte);
          *pte &= ~(PTE_P);
          *pte |= PTE_DSK;
          *pte |= swapIndex << PT_SHIFT;
        }
      }
    }
  }

  // free the page
  kfree(P2V(pa));
  return 0;
}

// returns a free index in the swap_core_map
// for which we can evict the physical
// memory page to
int
getFreeDiskPageIndex() {
  for (int i = 0; i < NDISKPAGES; i++) {
    if (swap_core_map[i].in_use == 0) {
      return i;
    }
  }
  return -1;
}

// returns the page to evict
// 0 if there is no page to evict
struct core_map_entry*
findPageToEvict() {
  // cycle through core map to find the
  // first non kernel page indicated by a pid
  // of -1 and return it
  if(kmem.use_lock) {
    acquire(&kmem.lock);
  }
  for (int i = 0; i < npages; i++) {
    struct core_map_entry *current = &core_map[i];
    if (current->pid > 2 && current->refCount > 0) { // changing to > 2 works 2 times, should focus on fixing memory clear where values become -1
      if(kmem.use_lock)
        release(&kmem.lock);
      return current;
    }
  }
  if(kmem.use_lock) {
    release(&kmem.lock);
  }
  return 0;
}

int
swapPageIn(uint64_t vAddr, pte_t *pte) {
  // 1. find the entry into the swap_core_map for the page
  uint64_t swapIndex = *pte >> PT_SHIFT; // will be the value stored in the pte

  // 2. kalloc a page
  char *mem = kalloc();
  if (mem == 0) {
    return -1;
  }

  // copy data from disk into physical memory
  for (int i = 0; i < 8; i++) {
    uint disk_addr = 2 + (swapIndex * 8) + i;
    struct buf *b = bread(ROOTDEV, disk_addr);
    memmove(mem + (BSIZE * i), b->data, BSIZE);
    brelse(b);
  }

  add_phy_mem_map(myproc()->pid, PGROUNDDOWN(vAddr), V2P(mem));
  struct core_map_entry *cme = pa2page(V2P(mem));
  cme->refCount = swap_core_map[swapIndex].refCount;
  cme->va = swap_core_map[swapIndex].va;
  cme->pid = swap_core_map[swapIndex].pid;

  // 3. set PTE for all processes properly to reflect that the pageToEvict is in physical memory and not on disk
  // increment page count for the page
  for (int i = 0; i < NPROC; i++) {
    struct proc *process = getProcessAtIndex(i);
    // for each process go through every page attributed to it
    // and search for that va, if it exists then you can
    // do a page walk
    for (int region = 0; region < 4; region++) {
      uint64_t region_start = (uint64_t)process->mem_regions[region].start;
      uint64_t region_size = process->mem_regions[region].size;

      if (vAddr >= region_start && vAddr < (region_start + region_size)) {
        pte_t *pte = walkpml4(process->pml4, (char*)vAddr, 0);
        if (((*pte) >> PT_SHIFT) == swapIndex) {
          *pte |= PTE_P;
          *pte &= ~(PTE_DSK);
          *pte = PTE(V2P(mem), PTE_FLAGS(*pte) | PTE_P | PTE_W | PTE_U);
          //inrementPageRefCount(V2P(mem));
        }
      }
    }
  }

  // 4. zero out index in swap_core_map
  if(kmem.use_lock) {
    acquire(&kmem.lock);
  }
  swap_core_map[swapIndex].va = 0;
  swap_core_map[swapIndex].pid = 0;
  swap_core_map[swapIndex].refCount = 0;
  swap_core_map[swapIndex].in_use = 0;
  if(kmem.use_lock) {
    release(&kmem.lock);
  }
  return 0;
}

void
inrementPageRefCount(uint64_t pa) {
  if(kmem.use_lock) {
    acquire(&kmem.lock);
  }
  struct core_map_entry *r = pa2page(pa);
  r->refCount++;
  if(kmem.use_lock) {
    release(&kmem.lock);
  }
}

void
decrementPageRefCount(uint64_t pa) {
  if(kmem.use_lock) {
    acquire(&kmem.lock);
  }
  struct core_map_entry *r = pa2page(pa);
  r->refCount--;
  if(kmem.use_lock) {
    release(&kmem.lock);
  }
}

void
decrementSwapCoreMapEntryRefCount(uint64_t index) {
  if(kmem.use_lock) {
    acquire(&kmem.lock);
  }
  swap_core_map[index].refCount--;
  if (swap_core_map[index].refCount <= 0) {
    swap_core_map[index].va = 0;
    swap_core_map[index].pid = 0;
    swap_core_map[index].refCount = 0;
    swap_core_map[index].in_use = 0;
  }
  if(kmem.use_lock) {
    release(&kmem.lock);
  }
}
