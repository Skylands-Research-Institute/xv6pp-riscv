#include "memlayout.h"
#include "types.h"
#include "riscv.h"
#include "virtual_memory.h"
#include "xv6pp.h"

extern char etext[];  // kernel.ld sets this to end of kernel code.
extern char trampoline[]; // trampoline.S

virtual_memory::virtual_memory(const char *name) :
    kernel_module(name) {
}

void virtual_memory::init() {
  log(log_level::INFO, "init, size=%ld\n", sizeof(*this));
  kernel_pagetable = (pagetable_t) kernel.allocator.alloc();
  memset(kernel_pagetable, 0, PGSIZE);
  // uart registers
  map(kernel_pagetable, UART0, UART0, PGSIZE, PTE_R | PTE_W);
  // virtio mmio disk interface
  map(kernel_pagetable, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);
  // PLIC
  map(kernel_pagetable, PLIC, PLIC, 0x4000000, PTE_R | PTE_W);
  // map kernel text executable and read-only.
  map(kernel_pagetable, KERNBASE, KERNBASE, (uint64) etext - KERNBASE,
  PTE_R | PTE_X);
  // map kernel data and the physical RAM we'll make use of.
  map(kernel_pagetable, (uint64) etext, (uint64) etext,
  PHYSTOP - (uint64) etext, PTE_R | PTE_W);
  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  map(kernel_pagetable, TRAMPOLINE, (uint64) trampoline, PGSIZE, PTE_R | PTE_X);
  // allocate and map a kernel stack for each process.
  //proc_mapstacks(kernel_pagetable);
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.
void virtual_memory::inithart() {
  // wait for any previous writes to the page table memory to finish.
  sfence_vma();
  w_satp(MAKE_SATP(kernel_pagetable));
  // flush stale entries from the TLB.
  sfence_vma();
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void virtual_memory::map(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm) {
  if (mappages(kpgtbl, va, sz, pa, perm))
    panic("kvmmap");
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa.
// va and size MUST be page-aligned.
// Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int virtual_memory::mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm) {
  if (va % PGSIZE)
    panic("mappages: va not aligned");
  if (size % PGSIZE)
    panic("mappages: size not aligned");
  if (!size)
    panic("mappages: size");
  auto a = va;
  auto last = va + size - PGSIZE;
  for (;;) {
    auto pte = walk(pagetable, a, 1);
    if (!pte)
      return -1;
    if (*pte & PTE_V)
      panic("mappages: remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    if (a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t virtual_memory::create() {
  pagetable_t pagetable = (pagetable_t) kernel.allocator.alloc();
  if (!pagetable)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void virtual_memory::first(pagetable_t pagetable, uchar *src, uint sz) {
  if (sz >= PGSIZE)
    panic("uvmfirst: more than a page");
  auto mem = kernel.allocator.alloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64) mem, PTE_W | PTE_R | PTE_X | PTE_U);
  memmove(mem, src, sz);
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64 virtual_memory::alloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm) {
  if (newsz < oldsz)
    return oldsz;
  oldsz = PGROUNDUP(oldsz);
  for (auto a = oldsz; a < newsz; a += PGSIZE) {
    auto mem = kernel.allocator.alloc();
    if (mem == 0) {
      dealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if (mappages(pagetable, a, PGSIZE, (uint64) mem, PTE_R | PTE_U | xperm) != 0) {
      kernel.allocator.free(mem);
      dealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64 virtual_memory::dealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz) {
  if (newsz >= oldsz)
    return oldsz;
  if (PGROUNDUP(newsz) < PGROUNDUP(oldsz)) {
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    unmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }
  return newsz;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
void virtual_memory::unmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free) {
  if ((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");
  pte_t *pte;
  for (auto a = va; a < va + npages * PGSIZE; a += PGSIZE) {
    if ((pte = walk(pagetable, a, 0)) == 0)
      panic("uvmunmap: walk");
    if ((*pte & PTE_V) == 0)
      panic("uvmunmap: not mapped");
    if (PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    if (do_free) {
      uint64 pa = PTE2PA(*pte);
      kernel.allocator.free((void*) pa);
    }
    *pte = 0;
  }
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
pte_t* virtual_memory::walk(pagetable_t pagetable, uint64 va, int alloc) {
  if (va >= MAXVA)
    panic("walk");
  for (int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if (!pte)
      panic("null pte");
    if (*pte & PTE_V) {
      pagetable = (pagetable_t) PTE2PA(*pte);
    } else {
      if (!alloc || (pagetable = (pde_t*) kernel.allocator.alloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int virtual_memory::copy(pagetable_t old, pagetable_t newpt, uint64 sz) {
  pte_t *pte;
  for (uint64 i = 0; i < sz; i += PGSIZE) {
    if ((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if ((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    auto pa = PTE2PA(*pte);
    auto flags = PTE_FLAGS(*pte);
    auto mem = kernel.allocator.alloc();
    if (mem == nullptr) {
      unmap(newpt, 0, i / PGSIZE, 1);
      return -1;
    }
    memmove(mem, (char*) pa, PGSIZE);
    if (mappages(newpt, i, PGSIZE, (uint64) mem, flags) != 0) {
      kernel.allocator.free(mem);
      unmap(newpt, 0, i / PGSIZE, 1);
      return -1;
    }
  }
  return 0;
}

// Free user memory pages,
// then free page-table pages.
void virtual_memory::free(pagetable_t pagetable, uint64 sz) {
  if (sz > 0)
    unmap(pagetable, 0, PGROUNDUP(sz) / PGSIZE, 1);
  freewalk(pagetable);
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void virtual_memory::freewalk(pagetable_t pagetable) {
  // there are 2^9 = 512 PTEs in a page table.
  for (auto i = 0; i < 512; i++) {
    pte_t pte = pagetable[i];
    if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0) {
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t) child);
      pagetable[i] = 0;
    } else if (pte & PTE_V) {
      panic("freewalk: leaf");
    }
  }
  kernel.allocator.free((void*) pagetable);
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void virtual_memory::clear(pagetable_t pagetable, uint64 va) {
  auto pte = walk(pagetable, va, 0);
  if (!pte)
    panic("clear");
  *pte &= ~PTE_U;
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64 virtual_memory::walkaddr(pagetable_t pagetable, uint64 va) {
  pte_t *pte;
  uint64 pa;

  if (va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if (pte == 0)
    return 0;
  if ((*pte & PTE_V) == 0)
    return 0;
  if ((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int virtual_memory::copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len) {
  while (len > 0) {
    auto va0 = PGROUNDDOWN(dstva);
    if (va0 >= MAXVA)
      return -1;
    auto pte = walk(pagetable, va0, 0);
    if (pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0 || (*pte & PTE_W) == 0)
      return -1;
    auto pa0 = PTE2PA(*pte);
    auto n = PGSIZE - (dstva - va0);
    if (n > len)
      n = len;
    memmove((void*) (pa0 + (dstva - va0)), src, n);
    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int virtual_memory::copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len) {
  while (len > 0) {
    auto va0 = PGROUNDDOWN(srcva);
    auto pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0)
      return -1;
    auto n = PGSIZE - (srcva - va0);
    if (n > len)
      n = len;
    memmove(dst, (void*) (pa0 + (srcva - va0)), n);
    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int virtual_memory::copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max) {
  uint64 n, va0, pa0;
  int got_null = 0;

  while (got_null == 0 && max > 0) {
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if (n > max)
      n = max;

    char *p = (char*) (pa0 + (srcva - va0));
    while (n > 0) {
      if (*p == '\0') {
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if (got_null) {
    return 0;
  } else {
    return -1;
  }
}

int virtual_memory::either_copyout(int user_dst, uint64 dst, void *src, uint64 len) {
  if (user_dst) {
    return copyout(kernel.cpus.curproc()->get_pagetable(), dst, (char*) src, len);
  } else {
    memmove((char*) dst, src, len);
    return 0;
  }
}

int virtual_memory::either_copyin(void *dst, int user_src, uint64 src, uint64 len) {
  if (user_src) {
    return copyin(kernel.cpus.curproc()->get_pagetable(), (char*) dst, src, len);
  } else {
    memmove(dst, (char*) src, len);
    return 0;
  }
}

