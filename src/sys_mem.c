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

#include "dma.h"
#include "sysbus.h"
#include <stddef.h>
#include <pthread.h>

#ifdef MM64
#include "mm64.h"
#else
#include "mm.h"
#endif

// typedef char BYTE;

int __sys_memmap(struct krnl_t *krnl, uint32_t pid, struct sc_regs *regs)
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
   if (caller == NULL)
   {
      printf("sys_memmap: khong tim thay pid %u\n", pid);
      return -1;
   }

   switch (memop)
   {
   case SYSMEM_MAP_OP:
      /* Reserved process case*/
      vmap_pgd_memset(caller, regs->a2, regs->a3); // a2 = dia chi ao bat dau, a3 = so trang
      break;
   case SYSMEM_INC_OP:
      return inc_vma_limit(caller, regs->a2, regs->a3);
   case SYSMEM_SWP_OP:
      __mm_swap_page(caller, regs->a2, regs->a3); // a2 = khung RAM bi day ra, a3 = khung SWAP
      break;
   case SYSMEM_IO_READ:
      MEMPHY_read(krnl->mram, regs->a2, &value); // a2 = dia chi vat ly, ket qua tra ve qua a3
      regs->a3 = (unsigned char)value;           // byte co dau, ep kieu de tranh mo rong dau
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

// Khai báo biến khóa mutex (ở trạng thái mở sẵn) để bảo vệ phần cứng DMA dùng chung
static pthread_mutex_t dma_lock = PTHREAD_MUTEX_INITIALIZER;

#ifndef pg_getpage
int pg_getpage(struct mm_struct *mm, int pgn, int *fpn, struct pcb_t *caller);
#endif

// Hàm này dùng để tra mã vùng nhớ (rgid) để lấy địa chỉ ảo rồi lấy địa chỉ vật lý trên mram
// Ưu tiên kiểm tra vùng kernel (kmalloc trong krnl->mm), nếu trống mới xét vùng user (caller->mm)
static int dma_resolve_phyaddr(struct krnl_t *krnl, struct pcb_t *caller,
                               uint32_t rgid, uint32_t offset, uint32_t size,
                               uint64_t *phyaddr)
{
   struct mm_struct *target_mm = NULL;
   struct vm_rg_struct *rg = NULL;

   // Tìm trong vùng nhớ của Kernel
   if (krnl->mm != NULL)
   {
      struct vm_rg_struct *krg = get_symrg_byid(krnl->mm, (int)rgid);
      if (krg != NULL && krg->rg_start < krg->rg_end)
      {
         target_mm = krnl->mm;
         rg = krg;
      }
   }

   // Nếu không có trong krnl thì xét caller->mm
   if (rg == NULL && caller->mm != NULL)
   {
      struct vm_rg_struct *urg = get_symrg_byid(caller->mm, (int)rgid);
      if (urg != NULL && urg->rg_start < urg->rg_end)
      {
         target_mm = caller->mm;
         rg = urg;
      }
   }

   if (rg == NULL || target_mm == NULL)
   {
      return -1;
   }

   // Kiểm tra lỗi vượt mức vùng nhớ
   if (rg->rg_start + offset + size > rg->rg_end)
   {
      return -1;
   }

   // Dịch địa chỉ ảo (rg_start + offset) sang địa chỉ vật lý trên mram
   addr_t vaddr = rg->rg_start + offset;
#ifdef MM64
   addr_t pgn = vaddr >> 12;
   addr_t pgoff = PAGING64_ADDR_OFFST(vaddr);
#else
   int pgn = PAGING_PGN(vaddr);
   int pgoff = PAGING_OFFST(vaddr);
#endif

   int fpn = 0;
   if (pg_getpage(target_mm, (int)pgn, &fpn, caller) != 0)
   {
      return -1;
   }

#ifdef MM64
   *phyaddr = ((uint64_t)fpn * PAGING64_PAGESZ) + pgoff;
#else
   *phyaddr = ((uint64_t)fpn << (PAGING_ADDR_OFFST_HIBIT + 1)) + pgoff;
#endif

   return 0;
}

int __sys_dmadriver(struct krnl_t *krnl, uint32_t pid, struct sc_regs *regs)
{
   if (krnl == NULL || regs == NULL || krnl->running_list == NULL)
   {
      return -1;
   }

   if (krnl->mram == NULL || krnl->mram->storage == NULL)
   {
      return -1;
   }

   // Tìm PCB của tiến trình đang gọi syscall qua pid
   struct pcb_t *caller = NULL;
   for (int i = 0; i < krnl->running_list->size; i++)
   {
      if (krnl->running_list->proc[i]->pid == pid)
      {
         caller = krnl->running_list->proc[i];
         break;
      }
   }

   if (caller == NULL)
   {
      return -1;
   }

   // Nhận 4 tham số của dma_memcpy từ thanh ghi syscall
   uint32_t src_rgid = (uint32_t)regs->a1;
   uint32_t dst_rgid = (uint32_t)regs->a2;
   uint32_t offset = (uint32_t)regs->a3;
   uint32_t size = (uint32_t)regs->a4;

   if (size == 0)
   {
      return 0;
   }

   // Quy đổi vùng nhớ nguồn và đích sang địa chỉ vật lý trên mram
   uint64_t src_phy = 0;
   uint64_t dst_phy = 0;

   if (dma_resolve_phyaddr(krnl, caller, src_rgid, offset, size, &src_phy) != 0)
   {
      return -1;
   }

   if (dma_resolve_phyaddr(krnl, caller, dst_rgid, offset, size, &dst_phy) != 0)
   {
      return -1;
   }

   // Khóa phần cứng DMA, gắn RAM hệ điều hành vào System Bus và chạy
   pthread_mutex_lock(&dma_lock);

   sysbus_attach_os_mem(krnl->mram->storage, (size_t)krnl->mram->maxsz);
   int ret = do_dma(caller, src_phy, dst_phy, (uint64_t)size);

   pthread_mutex_unlock(&dma_lock);

   return ret;
}
