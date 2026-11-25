/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

//#ifdef MM_PAGING
/*
 * PAGING based Memory Management
 * Virtual memory module mm/mm-vm.c
 */

#include "string.h"
#include "mm64.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

/*get_vma_by_num - get vm area by numID
 *@mm: memory region
 *@vmaid: ID vm area to alloc memory region
 *
 */
struct vm_area_struct *get_vma_by_num(struct mm_struct *mm, int vmaid)
{
  struct vm_area_struct *pvma = mm->mmap;

  if (mm->mmap == NULL)
    return NULL;

  int vmait = pvma->vm_id;

  while (vmait < vmaid)
  {
    if (pvma == NULL)
      return NULL;

    pvma = pvma->vm_next;
    vmait = pvma->vm_id;
  }

  return pvma;
}

int __mm_swap_page(struct pcb_t *caller, addr_t vicfpn , addr_t swpfpn)
{
    __swap_cp_page(caller->krnl->mram, vicfpn, caller->krnl->active_mswp, swpfpn);
    return 0;
}

/*get_vm_area_node - get vm area for a number of pages
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@incpgnum: number of page
 *@vmastart: vma end
 *@vmaend: vma end
 *
 */
struct vm_rg_struct *get_vm_area_node_at_brk(struct pcb_t *caller, int vmaid, addr_t alignedsz)
{
  struct vm_rg_struct * newrg;
  /* TODO retrive current vma to obtain newrg, current comment out due to compiler redundant warning*/
  //struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);

  //newrg = malloc(sizeof(struct vm_rg_struct));

  /* TODO: update the newrg boundary
  // newrg->rg_start = ...
  // newrg->rg_end = ...
  */
  // struct vm_area_struct *cur_vma=....

  /* END TODO */

  return newrg;
}

/*validate_overlap_vm_area
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@vmastart: vma end
 *@vmaend: vma end
 *
 */
int validate_overlap_vm_area(struct pcb_t *caller, int vmaid, addr_t vmastart, addr_t vmaend)
{
  struct vm_area_struct *vma = caller->mm->mmap;

  /* TODO validate the planned memory area is not overlapped */
  if (vmastart >= vmaend)
  {
    return -1;
  }

  if (vma == NULL)
  {
    return -1;
  }

  /* TODO validate the planned memory area is not overlapped */

  struct vm_area_struct *cur_area = get_vma_by_num(caller->mm, vmaid);
  if (cur_area == NULL)
  {
    return -1;
  }

  while (vma != NULL)
  {
    if (vma != cur_area && OVERLAP(cur_area->vm_start, cur_area->vm_end, vma->vm_start, vma->vm_end))
    {
      return -1;
    }
    vma = vma->vm_next;
  }
  /* End TODO*/

  return 0;
}

/*inc_vma_limit - increase vm area limits to reserve space for new variable
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@inc_sz: increment size
 *
 */

int inc_vma_limit(struct pcb_t *caller, int vmaid, addr_t inc_sz)
{
    if (caller == NULL || caller->mm == NULL ||
        caller->krnl == NULL || caller->krnl->mram == NULL ||
        inc_sz == 0)
        return -1;

    struct vm_area_struct *vma =
        get_vma_by_num(caller->mm, vmaid);

    if (vma == NULL)
        return -1;

#ifdef MM64
    const addr_t pagesz = PAGING64_PAGESZ;
#else
    const addr_t pagesz = PAGING_PAGESZ;
#endif

    addr_t old_sbrk = vma->sbrk;

    if (old_sbrk % pagesz != 0 ||
        inc_sz > (addr_t)-1 - (pagesz - 1))
        return -1;

    addr_t alignedsz =
        ((inc_sz + pagesz - 1) / pagesz) * pagesz;

    if (old_sbrk > (addr_t)-1 - alignedsz)
        return -1;

    addr_t pgnum = alignedsz / pagesz;
    addr_t mapped = 0;

    for (addr_t i = 0; i < pgnum; i++)
    {
        addr_t fpn;
        addr_t pgn = old_sbrk / pagesz + i;

        if (MEMPHY_get_freefp(caller->krnl->mram, &fpn) != 0)
            goto rollback;

        if (pte_set_fpn(caller->mm, pgn, fpn, caller) != 0)
        {
            MEMPHY_put_freefp(caller->krnl->mram, fpn);
            goto rollback;
        }

        mapped++;
    }

    vma->sbrk = old_sbrk + alignedsz;

    if (vma->vm_end < vma->sbrk)
        vma->vm_end = vma->sbrk;

    return 0;

rollback:
    for (addr_t i = 0; i < mapped; i++)
    {
        addr_t pgn = old_sbrk / pagesz + i;
        addr_t pte = pte_get_entry(caller->mm, pgn, caller);

        MEMPHY_put_freefp(caller->krnl->mram, PAGING_FPN(pte));
        pte_set_entry(caller->mm, pgn, 0, caller);
    }

    return -1;
}

// #endif
