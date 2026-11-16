/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

#include "os-mm.h"
#include "syscall.h"
#include "libmem.h"
#include "queue.h"
#include <stdlib.h>
#include <stdio.h>

#ifdef MM64
#include "mm64.h"
#else
#include "mm.h"
#endif

//typedef char BYTE;

int __sys_memmap(struct krnl_t *krnl, uint32_t pid, struct sc_regs* regs)
{
   int memop = regs->a1;
   BYTE value;
   /* 
    * TODO THIS DUMMY CREATE EMPTY PROC TO AVOID COMPILER NOTIFY 
    *      need to be eliminated
	*/
    // bo malloc gia
   struct pcb_t *caller = NULL;
   /*
    * @bksysnet: Please note in the dual spacing design
    *            syscall implementations are in kernel space.
    */

   /* TODO: Traverse proclist to terminate the proc
    *       stcmp to check the process match proc_name
    */

    /* TODO Maching and marking the process */
    /* user process are not allowed to access directly pcb in kernel space of syscall */
    //....

    // tim trong running_list truoc vi process dang goi syscall thuong dang chay
   for (int i = 0; i < krnl->running_list->size; i++)
      if (krnl->running_list->proc[i]->pid == pid)
         caller = krnl->running_list->proc[i];

   // khong tim thay pid thi bao loi, khong dung caller la NULL
   if (caller == NULL) {
      printf("sys_memmap: khong tim thay pid %u\n", pid);
      return -1;
   }

   switch (memop) {
   case SYSMEM_MAP_OP:
            /* Reserved process case*/
			vmap_pgd_memset(caller, regs->a2, regs->a3); // a2 = dia chi ao bat dau, a3 = so trang
            break;
   case SYSMEM_INC_OP:
            inc_vma_limit(caller, regs->a2, regs->a3); // a2 = vmaid, a3 = so byte can them
            break;
   case SYSMEM_SWP_OP:
            __mm_swap_page(caller, regs->a2, regs->a3); // a2 = khung RAM bi day ra, a3 = khung SWAP
            break;
   case SYSMEM_IO_READ:
            MEMPHY_read(krnl->mram, regs->a2, &value); // a2 = dia chi vat ly, ket qua tra ve qua a3
            regs->a3 = (unsigned char) value; // byte co dau, ep kieu de tranh mo rong dau
            break;
   case SYSMEM_IO_WRITE:
            MEMPHY_write(krnl->mram, regs->a2, regs->a3); // a2 = dia chi vat ly, a3 = gia tri can ghi
            break;
   default:
            printf("Memop code: %d\n", memop);
            break;
   }
   
   return 0;
}