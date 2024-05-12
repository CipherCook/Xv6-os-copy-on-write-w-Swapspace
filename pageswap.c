#include "types.h"
#include "param.h"
#include "defs.h"
#include "fs.h"
#include "mmu.h"
#include "memlayout.h"
#include "x86.h"
#include "proc.h"

//Utk copy
//finds the page table entry for the given virtual address. If doesn't exist, allocates a new page table entry if alloc is set.
static pte_t *
walkpgdir(pde_t *pgdir, const void *va, int alloc)
{
  pde_t *pde;
  pte_t *pgtab;

  pde = &pgdir[PDX(va)];
  if(*pde & PTE_P){
    pgtab = (pte_t*)P2V(PTE_ADDR(*pde));
  } else {
    if(!alloc || (pgtab = (pte_t*)kalloc()) == 0)
      return 0;
    // Make sure all those PTE_P bits are zero.
    memset(pgtab, 0, PGSIZE);
    // The permissions here are overly generous, but they can
    // be further restricted by the permissions in the page table
    // entries, if necessary.
    *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U;
  }
  return &pgtab[PTX(va)];
}

struct swap_header{
    int isfree;
    int perm;
    int blockno;
    
};
//pagesize/ block size = 8 i.e. 8 blocks in a page
struct swap_header swap_table[SWAPSIZE/8];

void swapinit(void){
    for(int i=0;i<SWAPSIZE/8;i++){
        swap_table[i].isfree = 1;
        swap_table[i].perm = 0; 
        swap_table[i].blockno = 2 + i*8;
    }
     cprintf("Swap table initialized\n");
}

void swapout(){
    struct proc* victim_process = find_victim_process();
    pte_t* vic_page = findvictim_page(victim_process);
    //debug
    cprintf("Victim process: %x\n", victim_process->pid);
    cprintf("Victim page: %x\n", vic_page);
    if(vic_page == 0)
    {
        reset_access(victim_process);
        vic_page = findvictim_page(victim_process);
        if(vic_page == 0)
        {
            panic("No page found to swap out");
        }
        //clear the page table entry of the swapped-out page.
    }
    cprintf("victim process pid = %d\n", victim_process->pid);
    victim_process->rss -= PGSIZE;
    // The victim page is written into an available swap slot, and we update the page table entry of the swapped-out page.
    for(int i=0;i<SWAPSIZE/8;i++){
        if(swap_table[i].isfree == 1){
            cprintf("Free swap slot found at %d\n", i);
            swap_table[i].isfree = 0;
            //write to disk
            uint phys_addr_vic = PTE_ADDR(*vic_page);
            write_page_to_disk((char*)P2V(phys_addr_vic), swap_table[i].blockno);
            swap_table[i].perm = PTE_FLAGS(*vic_page); //
            kfree((char*)P2V(PTE_ADDR(*vic_page)));
            *vic_page = (swap_table[i].blockno << 12) | PTE_FLAGS(*vic_page);
            *vic_page &= ~PTE_P; //no longer in memory
            *vic_page |= 0x008;
            cprintf("Page %x swapped out to block %d\n", (vic_page), swap_table[i].blockno);
            break;
        }
        else if(i == SWAPSIZE/8 - 1){
            panic("No free swap slot available");
        }
    }


}

void page_fault()
{
    struct proc* curproc = myproc();
    uint addr = rcr2();
    curproc->rss += PGSIZE;
    pte_t* pte = walkpgdir(curproc->pgdir, (char*)addr, 0);
    int block_no = PTE_ADDR(*pte) >> 12;
    char* new_page = kalloc();
    write_page_from_disk(new_page, block_no);
    int swap_slot_no = (block_no - 2)/8;
    swap_table[swap_slot_no].isfree = 1;
    *pte = V2P(new_page) | swap_table[swap_slot_no].perm;
    *pte |= PTE_P;
    //should u flag be set?
    *pte |= PTE_U;
}

void swap_clear(int slot_no){
    swap_table[slot_no].isfree = 1;
    swap_table[slot_no].perm = 0;
}


void copy_on_write_pgflt()
{
    uint va = rcr2();    

  if(myproc() == 0)     // er1
  { 
    panic("NULLPROC");
  }
  pte_t *pte;
  pte = walkpgdir(myproc()->pgdir, (void*)va, 0); //get page table entry for given va

  if(pte == 0)
  {
    panic("PGflt error - PTE not found!");
  }

  if(!(*pte & PTE_P)){
    //lab4 - swap waala parrt
    cprintf("Swap in\n");
    struct proc* curproc = myproc();
    // uint addr = rcr2(); = va now
    curproc->rss += PGSIZE;
    pte_t* pte = walkpgdir(curproc->pgdir, (char*)va, 0);
    int block_no = PTE_ADDR(*pte) >> 12;
    char* new_page = kalloc();
    write_page_from_disk(new_page, block_no);
    int swap_slot_no = (block_no - 2)/8;
    swap_table[swap_slot_no].isfree = 1;
    *pte = V2P(new_page) | swap_table[swap_slot_no].perm;
    *pte |= PTE_P;
    //should u flag be set?
    *pte |= PTE_U;
    return;
  }
  if(!(*pte & PTE_U)){
    panic("PGflt error - Not user page!");
  }
  if(va >= KERNBASE){ 
    panic("PGflt error - va >= KERNBASE!");
  }

    uint pa = PTE_ADDR(*pte);                     
    uint refC = get_refc(pa);                

    if(refC < 1)
    {
        panic("Aise kaise");
    }
    else if(refC == 1)
    {
        *pte = PTE_W | *pte;  //set writeable bit
        lcr3(V2P(myproc()->pgdir));
        return;
    }
    else                       
    {
        char* mem = kalloc();
        if(mem != 0)  // page available
        {
               
          memmove(mem, (char*)P2V(pa), PGSIZE);  //copy content of parent page to child page
          *pte =  PTE_U | PTE_W | PTE_P | V2P(mem);
          decrement_rmap(pa, myproc()->pid); //decrement ref count of parent page
          lcr3(V2P(myproc()->pgdir)); //flush TLB
          return;
        }
        myproc()->killed = 1; //page not available
        return;

    }
    //Flush for safety. When it's brown, flush it down. When it's yellow, let it mellow.
    lcr3(V2P(myproc()->pgdir));
}