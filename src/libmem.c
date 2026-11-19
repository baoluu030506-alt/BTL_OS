/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 *
 * demacOprx release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 *
 * System Library
 * Memory Module Library libmem.c
 */

// #ifdef MM_PAGING

#include "string.h"
#include "mm64.h"
#include "syscall.h"
#include "libmem.h"
#include "dma.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

static pthread_mutex_t mmvm_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t kmm_lock = PTHREAD_MUTEX_INITIALIZER;

/*enlist_vm_freerg_list - add new rg to freerg_list
 *@mm: memory region
 *@rg_elmt: new region
 *
 */
int enlist_vm_freerg_list(struct mm_struct *mm, struct vm_rg_struct *rg_elmt)
{
  struct vm_rg_struct *rg_node = mm->mmap->vm_freerg_list;

  if (rg_elmt->rg_start >= rg_elmt->rg_end)
    return -1;

  if (rg_node != NULL)
    rg_elmt->rg_next = rg_node;

  /* Enlist the new region */
  mm->mmap->vm_freerg_list = rg_elmt;

  return 0;
}

/*get_symrg_byid - get mem region by region ID
 *@mm: memory region
 *@rgid: region ID act as symbol index of variable
 *
 */
struct vm_rg_struct *get_symrg_byid(struct mm_struct *mm, int rgid)
{
  if (rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ)
    return NULL;

  return &mm->symrgtbl[rgid];
}

/*__alloc - allocate a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *@alloc_addr: address of allocated memory region
 *
 */
int __alloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
{
  /*Allocate at the toproof */
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct rgnode;
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  if (get_free_vmrg_area(caller, vmaid, size, &rgnode) == 0)
  {
    caller->mm->symrgtbl[rgid].rg_start = rgnode.rg_start;
    caller->mm->symrgtbl[rgid].rg_end = rgnode.rg_end;

    *alloc_addr = rgnode.rg_start;

    pthread_mutex_unlock(&mmvm_lock);
    return 0;
  }

  /* TODO get_free_vmrg_area FAILED handle the region management (Fig.6)*/

  /*Attempt to increate limit to get space */
  int old_sbrk;

  old_sbrk = cur_vma->sbrk;

  /* TODO INCREASE THE LIMIT
   * SYSCALL 1 sys_memmap
   */
  struct sc_regs regs;
  regs.a1 = SYSMEM_INC_OP;
  regs.a2 = vmaid;
  regs.a3 = size;

  _syscall(caller->krnl, caller->pid, 17, &regs); /* SYSCALL 17 sys_memmap */
  /*Successful increase limit */
  caller->mm->symrgtbl[rgid].rg_start = old_sbrk;
  caller->mm->symrgtbl[rgid].rg_end = old_sbrk + size;

  *alloc_addr = old_sbrk;

  /* Nếu sau khi SYSCALL 17 nâng sbrk (theo bội số trang) mà vẫn còn
    phần bộ nhớ dư [old_sbrk + size, cur_vma->sbrk], tạo node vùng trống mới
    và đưa vào danh sách vm_freerg_list để tái sử dụng cho các lần alloc sau. */
  if (old_sbrk + size < cur_vma->sbrk)
  {
    struct vm_rg_struct *rg_free = malloc(sizeof(struct vm_rg_struct));
    rg_free->rg_start = old_sbrk + size;
    rg_free->rg_end = cur_vma->sbrk;
    rg_free->vmaid = vmaid;
    rg_free->rg_next = NULL;
    enlist_vm_freerg_list(caller->mm, rg_free);
  }

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*__free - remove a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __free(struct pcb_t *caller, int vmaid, int rgid)
{
  pthread_mutex_lock(&mmvm_lock);

  if (rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  /* TODO: Manage the collect freed region to freerg_list */
  struct vm_rg_struct *rgnode = get_symrg_byid(caller->mm, rgid);

  if (rgnode->rg_start == 0 && rgnode->rg_end == 0)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  struct vm_rg_struct *freerg_node = malloc(sizeof(struct vm_rg_struct));
  freerg_node->rg_start = rgnode->rg_start;
  freerg_node->rg_end = rgnode->rg_end;
  freerg_node->rg_next = NULL;

  /* Gán vmaid cho freenode vừa tạo */
  freerg_node->vmaid = vmaid;

  rgnode->rg_start = rgnode->rg_end = 0;
  rgnode->rg_next = NULL;

  /*enlist the obsoleted memory region */
  enlist_vm_freerg_list(caller->mm, freerg_node);

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*liballoc - PAGING-based allocate a region memory
 *@proc:  Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */
int liballoc(struct pcb_t *proc, addr_t size, uint32_t reg_index)
{
  addr_t addr;
  int val = __alloc(proc, 0, reg_index, size, &addr);
  if (val == -1)
  {
    return -1;
  }
#ifdef IODUMP
  /* TODO dump IO content (if needed) */
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif

  /* By default using vmaid = 0 */
  return val;
}

/*libfree - PAGING-based free a region memory
 *@proc: Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */

int libfree(struct pcb_t *proc, uint32_t reg_index)
{
  int val = __free(proc, 0, reg_index);
  if (val == -1)
  {
    return -1;
  }
  printf("%s:%d\n", __func__, __LINE__);
#ifdef IODUMP
  /* TODO dump IO content (if needed) */
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif
  return 0; // val;
}

/*pg_getpage - get the page in ram
 *@mm: memory region
 *@pagenum: PGN
 *@framenum: return FPN
 *@caller: caller
 *
 */
int pg_getpage(struct mm_struct *mm, int pgn, int *fpn, struct pcb_t *caller)
{
  struct mm_struct *target_mm = (mm != NULL) ? mm : ((caller->mm != NULL) ? caller->mm : caller->krnl->mm);
  uint64_t pte = pte_get_entry(target_mm, pgn, caller);

  if (!PAGING_PAGE_PRESENT(pte))
  { /* Page is not online, make it actively living */
    addr_t vicpgn;
    addr_t vicpte;
    addr_t vicfpn, swpfpn;
    struct sc_regs regs;

    /* TODO Initialize the target frame storing our variable */
    addr_t tgtfpn = PAGING_SWP(pte);
    //  addr_t tgtfpn

    /* TODO: Play with your paging theory here */
    /* Find victim page */
    if (find_victim_page(target_mm, &vicpgn) == -1)
    {
      return -1;
    }

    /* Get free frame in MEMSWP */
    if (MEMPHY_get_freefp(caller->krnl->active_mswp, &swpfpn) == -1)
    {
      return -1;
    }

    /* TODO: Implement swap frame from MEMRAM to MEMSWP and vice versa*/
    vicpte = pte_get_entry(target_mm, vicpgn, caller);
    vicfpn = PAGING_FPN(vicpte);
    /* TODO copy victim frame to swap
     * SWP(vicfpn <--> swpfpn)
     * SYSCALL 1 sys_memmap
     */
    regs.a1 = SYSMEM_SWP_OP;
    regs.a2 = vicfpn;
    regs.a3 = swpfpn;
    _syscall(caller->krnl, caller->pid, 17, &regs);
    __swap_cp_page(caller->krnl->active_mswp, tgtfpn, caller->krnl->mram, vicfpn);
    MEMPHY_put_freefp(caller->krnl->active_mswp, tgtfpn);

    /* Update page table */
    pte_set_swap(target_mm, vicpgn, 0, swpfpn, caller);
    // pte_set_swap(...);

    /* Update its online status of the target page */
    pte_set_fpn(target_mm, pgn, vicfpn, caller);
    // pte_set_fpn(...);

    enlist_pgn_node(&target_mm->fifo_pgn, pgn);
  }

  *fpn = PAGING_FPN(pte_get_entry(target_mm, pgn, caller));

  return 0;
}

/*pg_getval - read value at given offset
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
int pg_getval(struct mm_struct *mm, int addr, BYTE *data, struct pcb_t *caller)
{
#ifdef MM64
  addr_t pgn = PAGING64_PGN(addr);
  addr_t off = PAGING64_OFFST(addr);
#else
  int pgn = PAGING_PGN(addr);
  // int off = ...
  int off = PAGING_OFFST(addr);
#endif
  int fpn;

  if (pg_getpage(mm, pgn, &fpn, caller) != 0)
    return -1; /* invalid page access */

  // int phyaddr = ...
#ifdef MM64
  addr_t phyaddr = (fpn * PAGING64_PAGESZ) + off;
#else
  int phyaddr = (fpn << (PAGING_ADDR_OFFST_HIBIT + 1)) + off;
#endif

  /* TODO
   *  MEMPHY_read(caller->krnl->mram, phyaddr, data);
   *  MEMPHY READ
   *  SYSCALL 17 sys_memmap with SYSMEM_IO_READ
   */
  struct sc_regs regs;
  regs.a1 = SYSMEM_IO_READ;
  regs.a2 = phyaddr;
  regs.a3 = 0;
  _syscall(caller->krnl, caller->pid, 17, &regs);

  *data = (BYTE)regs.a3;

  return 0;
}

/*pg_setval - write value to given offset
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
int pg_setval(struct mm_struct *mm, int addr, BYTE value, struct pcb_t *caller)
{
#ifdef MM64
  addr_t pgn = PAGING64_PGN(addr);
  addr_t off = PAGING64_OFFST(addr);
#else
  int pgn = PAGING_PGN(addr);
  // int off = ...
  int off = PAGING_OFFST(addr);
#endif
  int fpn;

  /* Get the page to MEMRAM, swap from MEMSWAP if needed */
  if (pg_getpage(mm, pgn, &fpn, caller) != 0)
    return -1; /* invalid page access */
#ifdef MM64
  addr_t phyaddr = (fpn * PAGING64_PAGESZ) + off;
#else
  int phyaddr = (fpn << (PAGING_ADDR_OFFST_HIBIT + 1)) + off;
#endif

  /* TODO
   *  MEMPHY_write(caller->krnl->mram, phyaddr, value);
   *  MEMPHY WRITE with SYSMEM_IO_WRITE
   * SYSCALL 17 sys_memmap
   */
  struct sc_regs regs;
  regs.a1 = SYSMEM_IO_WRITE;
  regs.a2 = phyaddr;
  regs.a3 = value;
  _syscall(caller->krnl, caller->pid, 17, &regs);

  return 0;
}

/*__read - read value in region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __read(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
  pthread_mutex_lock(&mmvm_lock);
  struct mm_struct *mm;
  if (caller->mm != NULL)
  {
    mm = caller->mm;
  }
  else
  {
    mm = caller->krnl->mm;
  }
  struct vm_rg_struct *currg = get_symrg_byid(mm, rgid);
  struct vm_area_struct *cur_vma = get_vma_by_num(mm, vmaid);

  // struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);
  if (currg == NULL || cur_vma == NULL ||
      currg->rg_start >= currg->rg_end ||
      currg->rg_start + offset >= currg->rg_end ||
      currg->mode_bit == 0)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  /* TODO Invalid memory identify */

  if (pg_getval(mm, currg->rg_start + offset, data, caller) != 0)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*libread - PAGING-based read a region memory */
int libread(
    struct pcb_t *proc, // Process executing the instruction
    uint32_t source,    // Index of source register
    addr_t offset,      // Source address = [source] + [offset]
    uint32_t *destination)
{
  BYTE data;
  // printf("%s:%d\n", __func__, __LINE__);
  int val = __read(proc, 0, source, offset, &data);
  if (val == -1)
  {
    return -1;
  }

  *destination = data;
#ifdef IODUMP
  /* TODO dump IO content (if needed) */
  printf("read region=%d offset=%lu value=%d\n", source, (unsigned long)offset, data);
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
  MEMPHY_dump(proc->krnl->mram);
#endif

  return val;
}

/*__write - write a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __write(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  pthread_mutex_lock(&mmvm_lock);
  struct mm_struct *mm;
  if (caller->mm != NULL)
  {
    mm = caller->mm;
  }
  else
  {
    mm = caller->krnl->mm;
  }
  struct vm_rg_struct *currg = get_symrg_byid(mm, rgid);
  struct vm_area_struct *cur_vma = get_vma_by_num(mm, vmaid);

  if (currg == NULL || cur_vma == NULL ||
      currg->rg_start >= currg->rg_end ||
      currg->rg_start + offset >= currg->rg_end ||
      currg->mode_bit == 0)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  if (pg_setval(mm, currg->rg_start + offset, value, caller) != 0)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*libwrite - PAGING-based write a region memory */
int libwrite(
    struct pcb_t *proc,   // Process executing the instruction
    BYTE data,            // Data to be wrttien into memory
    uint32_t destination, // Index of destination register
    addr_t offset)
{
  int val = __write(proc, 0, destination, offset, data);
  if (val == -1)
  {
    return -1;
  }
#ifdef IODUMP
  /* TODO dump IO content (if needed) */
  printf("write region=%d offset=%lu value=%d\n", destination, (unsigned long)offset, data);
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
  MEMPHY_dump(proc->krnl->mram);
#endif

  return val;
}

/*libkmem_malloc- alloc region memory in kmem
 *@caller: caller
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: memory size
 */

int libkmem_malloc(struct pcb_t *caller, uint32_t size, uint32_t reg_index)
{
  /*
   * TODO: provide OS level management
   *       and forward the request to helper
   */
  // addr_t  addr;
  // int val = ...

  /*
   * TODO: provide OS kmem allocation validation
   */

  addr_t addr;
  int val = (int)__kmalloc(caller, 0, reg_index, size, &addr);
  if (val < 0)
  {
    return -1;
  }

#ifdef IODUMP
#ifdef PAGETBL_DUMP
  print_pgtbl(caller, 0, -1);
#endif
#endif

  return val;

  return 0;
}

// Ham phu tim vung nho trong theo First-Fit
static int get_free_kmem_area(struct mm_struct *mm, int vmaid, addr_t size, struct vm_rg_struct *newrg)
{
  if (mm == NULL)
    return -1;

  struct vm_area_struct *cur_vma = get_vma_by_num(mm, vmaid);
  if (cur_vma == NULL)
    return -1;

  struct vm_rg_struct *rgit = cur_vma->vm_freerg_list;
  if (rgit == NULL)
    return -1;

  int found = 0;

  // Duyet tu dau danh sach, chot neu gap
  while (rgit != NULL)
  {
    if (rgit->rg_start + size <= rgit->rg_end)
    {
      newrg->rg_start = rgit->rg_start;
      newrg->rg_end = rgit->rg_start + size;

      // Neu con du bo nho sau khi lay thi tach thanh vung nho khac
      if (rgit->rg_start + size < rgit->rg_end)
      {
        rgit->rg_start = rgit->rg_start + size;
      }
      else
      {
        // Chen node hien tai bang node ke tiep
        struct vm_rg_struct *nextrg = rgit->rg_next;
        if (nextrg != NULL)
        {
          rgit->rg_start = nextrg->rg_start;
          rgit->rg_end = nextrg->rg_end;
          rgit->rg_next = nextrg->rg_next;
          free(nextrg);
        }
        else
        {
          rgit->rg_start = rgit->rg_end;
          rgit->rg_next = NULL;
        }
      }
      found = 1;
      break;
    }
    rgit = rgit->rg_next;
  }

  return found ? 0 : -1;
}

/*kmalloc - alloc region memory in kmem
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: memory size
 *@alloc_addr: allocated address
 */
addr_t __kmalloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
{
  /* TODO: provide OS kernel memory allocation
   *       update krnl_pgd for OS kernel level management */

  // struct krnl_t *krnl = caller->krnl;
  // krnl->symrgtbl...
  // krnl->krnl_pgd ...

  // Kiem tra hop le
  if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL ||
      size == 0 || rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ)
  {
    return -1;
  }

  // Khoa mutex bao ve
  pthread_mutex_lock(&kmm_lock);

  struct krnl_t *krnl = caller->krnl;
  struct mm_struct *mm = krnl->mm;
  struct vm_area_struct *cur_vma = get_vma_by_num(mm, vmaid);
  if (cur_vma == NULL)
  {
    pthread_mutex_unlock(&kmm_lock);
    return -1;
  }

  // Tim vung trong bang First-Fit
  struct vm_rg_struct rgnode;
  if (get_free_kmem_area(mm, vmaid, size, &rgnode) == 0)
  {
    mm->symrgtbl[rgid].rg_start = rgnode.rg_start;
    mm->symrgtbl[rgid].rg_end = rgnode.rg_end;
    mm->symrgtbl[rgid].vmaid = vmaid;

    if (alloc_addr != NULL)
      *alloc_addr = rgnode.rg_start;

    pthread_mutex_unlock(&kmm_lock);
    return 0;
  }

  // Neu khong co vung nho du lon, xin them ram that
#ifdef MM64
  addr_t pagesz = PAGING64_PAGESZ;
#else
  addr_t pagesz = PAGING_PAGESZ;
#endif
  int inc_pgnum = (size + pagesz - 1) / pagesz;
  addr_t inc_amt = inc_pgnum * pagesz;
  addr_t old_sbrk = cur_vma->sbrk;

  struct framephy_struct *frm_lst = NULL;
  if (alloc_pages_range(caller, inc_pgnum, &frm_lst) < 0)
  {
    pthread_mutex_unlock(&kmm_lock);
    return -1;
  }

  struct vm_rg_struct ret_rg;
  if (vmap_page_range(caller, old_sbrk, inc_pgnum, frm_lst, &ret_rg) < 0)
  {
    while (frm_lst != NULL)
    {
      struct framephy_struct *tmp = frm_lst;
      frm_lst = frm_lst->fp_next;
      free(tmp);
    }
    pthread_mutex_unlock(&kmm_lock);
    return -1;
  }

  // Giai phong danh sach node tam
  while (frm_lst != NULL)
  {
    struct framephy_struct *tmp = frm_lst;
    frm_lst = frm_lst->fp_next;
    free(tmp);
  }

  // cap nhap sbrk va dong bo krnl
  cur_vma->sbrk = old_sbrk + inc_amt;
  if (cur_vma->sbrk > cur_vma->vm_end)
  {
    cur_vma->vm_end = cur_vma->sbrk;
  }
  krnl->krnl_pgd = mm->pgd;

  // 5. Ghi thong tin
  mm->symrgtbl[rgid].rg_start = old_sbrk;
  mm->symrgtbl[rgid].rg_end = old_sbrk + size;
  mm->symrgtbl[rgid].vmaid = vmaid;

  if (alloc_addr != NULL)
    *alloc_addr = old_sbrk;

  // 6. Bo di phan du cho First-Fit sau tim thay
  if (old_sbrk + size < cur_vma->sbrk)
  {
    struct vm_rg_struct *rg_free = malloc(sizeof(struct vm_rg_struct));
    if (rg_free != NULL)
    {
      rg_free->rg_start = old_sbrk + size;
      rg_free->rg_end = cur_vma->sbrk;
      rg_free->vmaid = vmaid;
      rg_free->rg_next = NULL;
      enlist_vm_freerg_list(mm, rg_free);
    }
  }

  pthread_mutex_unlock(&kmm_lock);
  return 0;
}

/*libmem_dma_memcpy - DMA memcpy
 *@caller: caller
 *@src: source region
 *@dst: destination region
 *@offset: memory offset
 *@size: copied data size
 */
int libmem_dma_memcpy(struct pcb_t *caller, uint32_t src, uint32_t dst, uint32_t offset, uint32_t size)
{
  /* TODO: provide DMA mempy */

  // struct krnl_t *krnl = caller->krnl;

  /*
   *  TODO: provide asynchronous dma memcpy
   *        handling the dma_done_callback
   */
  // do_dma(caller, (uint64_t) src, (uint64_t) dst, (uint64_t) size);
  // krnl->...
  // krnl->...
  do_dma(caller, (uint64_t)src + offset, (uint64_t)dst + offset, (uint64_t)size);
  return 0;
}

int libkmem_copy_from_user(struct pcb_t *caller, uint32_t source, uint32_t destination, uint32_t offset, uint32_t size)
{
  /* TODO: provide OS level management kmem
   */
  /*
   * TODO: Map kernel address range
   */
  //__read_user_mem(...)
  //__write_kernel_mem(...);

  BYTE data;
  for (uint32_t i = 0; i < size; i++) // một mảng size byte và chép lần lượt từng byte
  {
    // 1. Đọc từ User Space
    if (__read_user_mem(caller, 0, source, offset + i, &data) != 0)
    {
      return -1;
    }
    // 2. Ghi vào Kernel Space
    if (__write_kernel_mem(caller, 0, destination, offset + i, data) != 0)
    {
      return -1;
    }
  }
  return 0;
}

int libkmem_copy_to_user(struct pcb_t *caller, uint32_t source, uint32_t destination, uint32_t offset, uint32_t size)
{
  /* TODO: provide OS level management kmem
   */
  /*
   * TODO: Map kernel address range
   */
  //__read_kernel_mem(...)
  //__write_user_mem(...);

  BYTE data;
  for (uint32_t i = 0; i < size; i++)
  {
    if (__read_kernel_mem(caller, 0, source, offset + i, &data) != 0)
    {
      return -1;
    }
    if (__write_user_mem(caller, 0, destination, offset + i, data) != 0)
    {
      return -1;
    }
  }
  return 0;
}

/*__read_kernel_mem - read value in kernel region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __read_kernel_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
  /* TODO: provide OS memory operator for kernel memory region */
  // krnl->krnl_pgd ... or krnl->pgd ... based on kmem implementation strategy

  pthread_mutex_lock(&kmm_lock);
  struct mm_struct *mm = caller->krnl->mm;
  struct vm_rg_struct *currg = get_symrg_byid(mm, rgid);
  struct vm_area_struct *cur_vma = get_vma_by_num(mm, vmaid);

  if (currg == NULL || cur_vma == NULL ||
      currg->rg_start >= currg->rg_end ||
      currg->rg_start + offset >= currg->rg_end ||
      currg->mode_bit == 1) // 0 mới là thao táo trên kernel
  {
    pthread_mutex_unlock(&kmm_lock);
    return -1;
  }

  if (pg_getval(mm, currg->rg_start + offset, data, caller) != 0)
  {
    pthread_mutex_unlock(&kmm_lock);
    return -1;
  }

  pthread_mutex_unlock(&kmm_lock);
  return 0;
}

/*__write_kernel_mem - write a kernel region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __write_kernel_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  /* TODO: provide OS memory operator for kernel memory region */
  // krnl->krnl_pgd ... or krnl->pgd ... based on kmem implementation strategy

  pthread_mutex_lock(&kmm_lock);
  struct mm_struct *mm = caller->krnl->mm;

  struct vm_rg_struct *currg = get_symrg_byid(mm, rgid);
  struct vm_area_struct *cur_vma = get_vma_by_num(mm, vmaid);

  if (currg == NULL || cur_vma == NULL ||
      currg->rg_start >= currg->rg_end ||
      currg->rg_start + offset >= currg->rg_end ||
      currg->mode_bit == 1) // 0 mới là thao tác trên kernel
  {
    pthread_mutex_unlock(&kmm_lock);
    return -1;
  }

  if (pg_setval(mm, currg->rg_start + offset, value, caller) != 0)
  {
    pthread_mutex_unlock(&kmm_lock);
    return -1;
  }

  pthread_mutex_unlock(&kmm_lock);
  return 0;
}

/*__read_user_mem - read value in user region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __read_user_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
  /* TODO: provide OS level management user memory access */
  // krnl->pgd ... or krnl->krnl_pgd  ... based on kmem implementation strategy

  pthread_mutex_lock(&mmvm_lock);
  struct mm_struct *mm = caller->mm; // User truy cập mm của chính nó
  if (mm == NULL)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  struct vm_rg_struct *currg = get_symrg_byid(mm, rgid);
  struct vm_area_struct *cur_vma = get_vma_by_num(mm, vmaid);

  // Kiểm tra an toàn: Biến tồn tại? Tràn biên? Có phải vùng Kernel (mode_bit == 0) không?
  if (currg == NULL || cur_vma == NULL ||
      currg->rg_start >= currg->rg_end ||
      currg->rg_start + offset >= currg->rg_end ||
      currg->mode_bit == 0) // CHẶN KERNEL SPACE
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  if (pg_getval(mm, currg->rg_start + offset, data, caller) != 0)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*__write_user_mem - write a user region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __write_user_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  /* TODO: provide OS level management user memory access */
  // krnl->pgd ... or krnl->krnl_pgd  ... based on kmem implementation strategy

  pthread_mutex_lock(&mmvm_lock);
  struct mm_struct *mm = caller->mm;
  if (mm == NULL)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  struct vm_rg_struct *currg = get_symrg_byid(mm, rgid);
  struct vm_area_struct *cur_vma = get_vma_by_num(mm, vmaid);

  if (currg == NULL || cur_vma == NULL ||
      currg->rg_start >= currg->rg_end ||
      currg->rg_start + offset >= currg->rg_end ||
      currg->mode_bit == 0) // CHẶN KERNEL SPACE
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  if (pg_setval(mm, currg->rg_start + offset, value, caller) != 0)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*free_pcb_memphy - collect all memphy of pcb
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@incpgnum: number of page
 */
int free_pcb_memph(struct pcb_t *caller)
{
  pthread_mutex_lock(&mmvm_lock);
  int pagenum, fpn;
  uint32_t pte;

  for (pagenum = 0; pagenum < PAGING_MAX_PGN; pagenum++)
  {
    pte = caller->mm->pgd[pagenum];

    if (PAGING_PAGE_PRESENT(pte))
    {
      fpn = PAGING_FPN(pte);
      MEMPHY_put_freefp(caller->krnl->mram, fpn);
    }
    else
    {
      fpn = PAGING_SWP(pte);
      MEMPHY_put_freefp(caller->krnl->active_mswp, fpn);
    }
  }

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*find_victim_page - find victim page
 *@caller: caller
 *@pgn: return page number
 *
 */
int find_victim_page(struct mm_struct *mm, addr_t *retpgn)
{
  struct pgn_t *pg = mm->fifo_pgn;

  /* TODO: Implement the theorical mechanism to find the victim page */
  if (!pg)
  {
    return -1;
  }
  struct pgn_t *prev = NULL;
  while (pg->pg_next)
  {
    prev = pg;
    pg = pg->pg_next;
  }
  *retpgn = pg->pgn;
  if (prev == NULL)
  {
    mm->fifo_pgn = NULL;
  }
  else
  {
    prev->pg_next = NULL;
  }
  // prev->pg_next = NULL;
  free(pg);

  return 0;
}

/*get_free_vmrg_area - get a free vm region
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@size: allocated size
 *
 */
int get_free_vmrg_area(struct pcb_t *caller, int vmaid, int size, struct vm_rg_struct *newrg)
{
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  struct vm_rg_struct *rgit = cur_vma->vm_freerg_list;

  if (rgit == NULL)
    return -1;

  /* Probe unintialized newrg */
  newrg->rg_start = newrg->rg_end = -1;

  /* Traverse on list of free vm region to find a fit space */
  while (rgit != NULL)
  {
    if (rgit->rg_start + size <= rgit->rg_end)
    { /* Current region has enough space */
      newrg->rg_start = rgit->rg_start;
      newrg->rg_end = rgit->rg_start + size;

      /* Update left space in chosen region */
      if (rgit->rg_start + size < rgit->rg_end)
      {
        rgit->rg_start = rgit->rg_start + size;
      }
      else
      { /*Use up all space, remove current node */
        /*Clone next rg node */
        struct vm_rg_struct *nextrg = rgit->rg_next;

        /*Cloning */
        if (nextrg != NULL)
        {
          rgit->rg_start = nextrg->rg_start;
          rgit->rg_end = nextrg->rg_end;

          rgit->rg_next = nextrg->rg_next;

          free(nextrg);
        }
        else
        {                                /*End of free list */
          rgit->rg_start = rgit->rg_end; // dummy, size 0 region
          rgit->rg_next = NULL;
        }
      }
      break;
    }
    else
    {
      rgit = rgit->rg_next; // Traverse next rg
    }
  }

  if (newrg->rg_start == -1) // new region not found
    return -1;

  return 0;
}

// #endif
