#include <param.h>
#include <cdefs.h>
#include <defs.h>
#include <x86_64.h>
#include <memlayout.h>
#include <mmu.h>
#include <proc.h>
#include <segment.h>
#include <elf.h>
#include <msr.h>
#include <fs.h>
#include <file.h>

extern char data[];  // defined by kernel.ld
pml4e_t *kpml4;  // for use in scheduler()

// Set up CPU's kernel segment descriptors.
// Run once on entry on each CPU.
void
seginit(void)
{
  struct cpu *c = &cpus[cpunum()];

  uint64_t *gdt;
  uint *tss;
  uint64_t addr;

  gdt = (uint64_t*)c->gdt;
  tss = (uint*) &(c->ts);
  tss[16] = 0x00680000; // IO Map Base = End of TSS

  addr = (uint64_t) tss;
  gdt[0] = 0; // first entry is 0
  c->gdt[SEG_KCODE] = SEG64(STA_X, 0, 0, 1, 0);
  c->gdt[SEG_KDATA] = SEG64(STA_W, 0, 0, 0, 0);
  c->gdt[SEG_KCPU]  = SEG64(STA_W, &c->cpu, 8, 0, 0);
  c->gdt[SEG_UCODE] = SEG64(STA_X, 0, 0, 1, DPL_USER);
  c->gdt[SEG_UDATA] = SEG64(STA_W, 0, 0, 0, DPL_USER);
  c->gdt[SEG_TSS] = SEG16(STS_T64A, addr, sizeof(struct tss), DPL_USER);
  gdt[SEG_TSS+1] = (addr >> 32);

  lgdt((void*) gdt, 8 * sizeof(uint64_t));
  ltr(SEG_TSS << 3);

  loadgs(SEG_KCPU << 3);
  wrmsr(MSR_IA32_GS_BASE, (uint64_t)&c->cpu);
  wrmsr(MSR_IA32_KERNEL_GS_BASE, (uint64_t)&c->cpu);

  // Initialize cpu-local storage.
  c->cpu = c;
  c->proc = 0;
};


// Return the address of the PTE in page table pgdir
// that corresponds to virtual address va.  If alloc!=0,
// create any required page table pages.
pte_t *
walkpml4(pml4e_t *pml4, const void *va, int alloc)
{
  pml4e_t *pml4e;
  pdpte_t *pdpt, *pdpte;
  pde_t *pgdir, *pde;
  pte_t *pgtab;

  pml4e = &pml4[PML4_INDEX(va)];

  if (*pml4e & PTE_P) {
    pdpt = (pdpte_t*)P2V(PDPT_ADDR(*pml4e));
  } else {
    if(!alloc || (pdpt = (pdpte_t*)kalloc()) == 0)
      return 0;
    memset(pdpt, 0, PGSIZE);
    *pml4e = V2P(pdpt) | PTE_P | PTE_W | PTE_U;
  }

  pdpte = &pdpt[PDPT_INDEX(va)];

  if (*pdpte & PTE_P) {
    pgdir = (pde_t*)P2V(PDE_ADDR(*pdpte));
  } else {
    if(!alloc || (pgdir = (pde_t*)kalloc()) == 0)
      return 0;
    memset(pgdir, 0, PGSIZE);
    *pdpte = V2P(pgdir) | PTE_P | PTE_W | PTE_U;
  }

  pde = &pgdir[PD_INDEX(va)];

  if (*pde & PTE_P) {
    pgtab = (pte_t*)P2V(PTE_ADDR(*pde));
  } else {
    if(!alloc || (pgtab = (pte_t*)kalloc()) == 0)
      return 0;
    memset(pgtab, 0, PGSIZE);
    *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U;
  }

  return &pgtab[PT_INDEX(va)];
}


// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned.
int
mappages(pml4e_t *pml4, uint64_t virt_pn, int num_page, uint64_t phy_pn, int perm, int pid)
{
  pte_t *pte;
  int i;

  for(i=0;i<num_page;i++){
    if((pte = walkpml4(pml4, (char*)(virt_pn << PT_SHIFT), 1)) == 0) {
      panic("not enough memory");
      return -1;
    }
    if(*pte & PTE_P)
      panic("remap");
    *pte = PTE(phy_pn << PT_SHIFT, perm | PTE_P);

    add_phy_mem_map(pid, virt_pn << PT_SHIFT, phy_pn << PT_SHIFT);

    virt_pn ++;
    phy_pn ++;
  }
  return 0;
}


// Set up kernel part of a page table.
pml4e_t*
setupkvm(void)
{
  pml4e_t *pml4;
  struct kmap *k;

  if((pml4 = (pml4e_t*)kalloc()) == 0)
    return 0;
  memset(pml4, 0, PGSIZE);

  struct kmap {
    void *virt;
    uint64_t phys_start;
    uint64_t phys_end;
    int perm;
  } kmap[] = {
    { (void*)KERNBASE, 0,             EXTMEM,    PTE_W}, // I/O space
    { (void*)KERNLINK, V2P(KERNLINK), V2P(data), 0},     // kern text+rodata
    { (void*)data,     V2P(data),     (uint64_t) npages * PGSIZE,   PTE_W}, // kern data+memory
    { (void*)DEVSPACE, 0xFE000000,    0x100000000,         PTE_W}, // more devices
  };

  for(k = kmap; k < &kmap[NELEM(kmap)]; k++)
    if(mappages(pml4, (uint64_t)(k->virt) >> PT_SHIFT, (k->phys_end - k->phys_start) >> PT_SHIFT, k->phys_start >> PT_SHIFT, k->perm, -1) < 0)
      return 0;
  return pml4;
}

void
kvmalloc(void)
{
  kpml4 = setupkvm();
  switchkvm();
}

// Switch h/w page table register to the kernel-only page table,
// for when no process is running.
void
switchkvm(void)
{
  lcr3(V2P(kpml4));   // switch to the kernel page table
}

// Switch TSS and h/w page table to correspond to process p.
void
switchuvm(struct proc *p)
{
  if(p == 0)
    panic("switchuvm: no process");
  if(p->kstack == 0)
    panic("switchuvm: no kstack");
  if(p->pml4 == 0)
    panic("switchuvm: no pml4");

  pushcli();
  mycpu()->ts.rsp0 = (uint64_t)p->kstack + KSTACKSIZE;
  lcr3(V2P(p->pml4));  // switch to process's address space
  popcli();
}


// Load the initcode into address 0 of pgdir.
// sz must be less than a page.
void
inituvm(pml4e_t *pml4, char *init, int sz, int pid)
{
  char *mem;
  uint64_t i;
  pte_t * pte;

  if(sz >= 10 * PGSIZE)
    panic("inituvm: more than ten pages");

  i = 0;
  while (sz > 0) {
    mem = kalloc();
    if (mem == 0)
      panic("inituvm: kalloc failure 1");
    memset(mem, 0, PGSIZE);

    if (mappages(pml4, i, 1, V2P(mem) >> PT_SHIFT, PTE_W|PTE_U, pid) < 0)
      panic("inituvm :mappages failure 1");
    memmove(mem, init + i * PGSIZE, min(sz, PGSIZE));

    i++;
    sz -= PGSIZE;
  }

  // allocate the guard page
  mem = kalloc();
  if (mem == 0)
    panic("inituvm: kalloc failure 2");
  memset(mem, 0, PGSIZE);
  if (mappages(pml4, i, 1, V2P(mem) >> PT_SHIFT, PTE_W|PTE_U, pid) < 0)
    panic("inituvm :mappages failure 2");
  pte = walkpml4(pml4, (void*) (uint64_t)(i * PGSIZE), 0);
  *pte &= ~PTE_P;
  i ++;

  // allocate ustack
  mem = kalloc();
  if (mem == 0)
    panic("inituvm: kalloc failure 3");
  memset(mem, 0, PGSIZE);
  if (mappages(pml4, i, 1, V2P(mem) >> PT_SHIFT, PTE_W|PTE_U, pid) < 0)
    panic("inituvm :mappages failure 3");
}


// Load a program segment into pgdir.  addr must be page-aligned
// and the pages from addr to addr+sz must already be mapped.
int
loaduvm(pml4e_t *pml4, char *addr, struct inode *ip, uint offset, uint sz)
{
  uint64_t i, pa, n;
  pte_t *pte;

  if((uint64_t) addr % PGSIZE != 0)
    panic("loaduvm: addr must be page aligned");
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpml4(pml4, addr+i, 0)) == 0)
      panic("loaduvm: address should exist");
    pa = PTE_ADDR(*pte);
    if(sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    if(readi(ip, P2V(pa), offset+i, n) != n)
      return -1;
  }
  return 0;
}


void
initustack(pml4e_t *pml4, struct mem_region *ustack_region, int pid)
{
  char* mem_ustack;
  uint64_t ustack = SZ_2G - PGSIZE;
  mem_ustack = kalloc();
  if(mem_ustack == 0){
    panic("initustack out of memory (3)\n");
  }
    memset(mem_ustack, 0, PGSIZE);
  if(mappages(pml4, ustack >> PT_SHIFT, 1, V2P(mem_ustack) >> PT_SHIFT, PTE_W | PTE_U, pid) < 0){
    panic("initustack out of memory (4)\n");
  }

  ustack_region->start = (char*)ustack;
  ustack_region->size = PGSIZE;
}



// Allocate page tables and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or -1 on error.
int
allocuvm(pml4e_t *pml4, char* start, uint64_t oldsz, uint64_t newsz, int pid)
{
  char *mem;
  uint64_t a;

  if(newsz >= KERNBASE)
    return -1;
  if(newsz < oldsz)
    return oldsz;

  a = PGROUNDUP((uint64_t)start + oldsz);
  for(; a < (uint64_t)start + newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      cprintf("allocuvm out of memory\n");
      deallocuvm(pml4, start, newsz, oldsz, pid);
      return -1;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pml4, a >> PT_SHIFT, 1, V2P(mem) >> PT_SHIFT, PTE_W|PTE_U, pid) < 0){
      cprintf("allocuvm out of memory (2)\n");
      deallocuvm(pml4, start, newsz, oldsz, pid);
      kfree(mem);
      return -1;
    }
  }
  return newsz;
}

uint64_t
find_next_possible_page(pml4e_t *pml4, uint64_t va)
{
  pml4e_t *pml4e;
  pdpte_t *pdpt, *pdpte;
  pde_t *pgdir, *pde;
  pml4e = &pml4[PML4_INDEX(va)];

  if (*pml4e & PTE_P) {
    pdpt = (pdpte_t*)P2V(PDPT_ADDR(*pml4e));
  } else {
    return PGADDR(PML4_INDEX(va) + 1, 0, 0, 0, 0) - PGSIZE;
  }

  pdpte = &pdpt[PDPT_INDEX(va)];

  if (*pdpte & PTE_P) {
    pgdir = (pde_t*)P2V(PDE_ADDR(*pdpte));
  } else {
    return PGADDR(PML4_INDEX(va), PDPT_INDEX(va) + 1, 0, 0, 0) - PGSIZE;
  }

  pde = &pgdir[PD_INDEX(va)];
  if (*pde & PTE_P) {
  } else {
    return PGADDR(PML4_INDEX(va), PDPT_INDEX(va), PD_INDEX(va) + 1, 0, 0) - PGSIZE;
  }

  panic("level is not missing");
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
int
deallocuvm(pml4e_t *pml4, char* start, uint64_t oldsz, uint64_t newsz, int pid)
{
  pte_t *pte;
  uint64_t a, pa;

  if(newsz >= oldsz)
    return oldsz;

  a = PGROUNDUP((uint64_t)start + newsz);
  for(; a  < (uint64_t)start + oldsz; a += PGSIZE){
    pte = walkpml4(pml4, (char*)a, 0);
    if(!pte) {
      a = find_next_possible_page(pml4, a);
    }
    else if((*pte & PTE_P) != 0){
      pa = PTE_ADDR(*pte);
      if(pa == 0)
        panic("kfree");
      char *v = P2V(pa);
      kfree(v);
      *pte = 0;
      remove_phy_mem_map(pid, a, pa);
    } else if ((*pte & PTE_P) == 0 && (*pte & PTE_DSK)) {
      // handle disk page
      // remove ref count
      uint64_t swapIndex = *pte >> PT_SHIFT;
      //cprintf("removing reference to disk page %d pid:%d\n", swapIndex, pid);
      decrementSwapCoreMapEntryRefCount(swapIndex);
    }
  }
  return newsz;
}

void
freevm_pgdir(pde_t *pgdir)
{
  uint i;
  for (i = 0; i < PTRS_PER_PD; i++) {
    if (pgdir[i] & PTE_P) {
      char *v = P2V(PTE_ADDR(pgdir[i]));
      kfree(v);
    }
  }
  kfree((char*) pgdir);
}

void
freevm_pdpt(pdpte_t *pdpt)
{
  uint i;
  for (i = 0; i < PTRS_PER_PDPT; i++) {
    if (pdpt[i] & PTE_P) {
      pde_t *pgdir = P2V(PDE_ADDR(pdpt[i]));
      freevm_pgdir(pgdir);
    }
  }
  kfree((char*) pdpt);
}


// Free a page table and all the physical memory pages
// in the user part.
void
freevm(pml4e_t *pml4, int pid)
{
  uint i;

  if(pml4 == 0)
    panic("freevm: no pml4");

  deallocuvm(pml4, 0, SZ_4G, 0, pid);
  for(i = 0; i < PTRS_PER_PML4; i++){
    if(pml4[i] & PTE_P){
      pdpte_t *pdpt = P2V(PDPT_ADDR(pml4[i]));
      freevm_pdpt(pdpt);
    }
  }
  kfree((char*)pml4);
}

int
copy_mem_region(pml4e_t *oldpml4, pml4e_t *newpml4, struct mem_region *region, int newpid)
{
  pte_t *pte;
  uint64_t pa, i, flags;
  char *mem;

  uint64_t start = (uint64_t)region->start;
  uint64_t end = (uint64_t)region->start + region->size;
  for(i = start; i < end; i += PGSIZE){
    if((pte = walkpml4(oldpml4, (void *) i, 0)) == 0)
      panic("copy_mem_region: pte should exist");
    if(!(*pte & PTE_P))
      panic("copy_mem_region: page not present");
    pa = PTE_ADDR(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      return -1;
    memmove(mem, (char*)P2V(pa), PGSIZE);
    if(mappages(newpml4, i >> PT_SHIFT, 1, V2P(mem) >> PT_SHIFT, flags, newpid) < 0)
      return -1;
  }
  return 0;
}



// Given a parent process's page table, create a copy
// of it for a child.
pml4e_t*
copyuvm(pml4e_t *pml4, struct mem_region *mem_regions, int newpid)
{
  pml4e_t *d;

  if((d = setupkvm()) == 0)
    return 0;

  if (copy_mem_region(pml4, d, &mem_regions[CODE], newpid) != 0) {
    goto bad;
  }
  if (copy_mem_region(pml4, d, &mem_regions[HEAP], newpid) != 0) {
    goto bad;
  }
  if (copy_mem_region(pml4, d, &mem_regions[USTACK], newpid) != 0) {
    goto bad;
  }

  return d;

bad:
  freevm(d, newpid);
  return 0;
}

//PAGEBREAK!
// Map user virtual address to kernel address.
char*
uva2ka(pml4e_t *pml4, char *uva)
{
  pte_t *pte;

  pte = walkpml4(pml4, uva, 0);
  if((*pte & PTE_P) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  return (char*)P2V(PTE_ADDR(*pte));
}

// Copy len bytes from p to user address va in page table pgdir.
// Most useful when pgdir is not the current page table.
// uva2ka ensures this only works for PTE_U pages.
int
copyout(pml4e_t *pml4, uint64_t va, void *p, uint len)
{
  char *buf, *pa0;
  uint64_t n, va0;

  buf = (char*)p;
  while(len > 0){
    va0 = (uint64_t)PGROUNDDOWN(va);
    pa0 = uva2ka(pml4, (char*)va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (va - va0);
    if(n > len)
      n = len;
    memmove(pa0 + (va - va0), buf, n);
    len -= n;
    buf += n;
    va = va0 + PGSIZE;
  }
  return 0;
}



//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.
