#include "param.h"
#include "types.h"
#include "defs.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "elf.h"
#include "fs.h"
#include "spinlock.h"

#define NONE 0
#define NFUA 1
#define LAPA 2
#define SCFIFO 3
#define AQ 4
#define FIFO 9




extern char data[];  // defined by kernel.ld
pde_t *kpgdir;  // for use in scheduler()

struct spinlock lock;

char pg_refcount[PHYSTOP >> PGSHIFT]; // array to store refcount, pgshift defined in memlayout.h
static char buffer[PGSIZE];

// Set up CPU's kernel segment descriptors.
// Run once on entry on each CPU.p = myproc();
void
seginit(void)
{
  struct cpu *c;

  // Map "logical" addresses to virtual addresses using identity map.
  // Cannot share a CODE descriptor for both kernel and user
  // because it would have to have DPL_USR, but the CPU forbids
  // an interrupt from CPL=0 to DPL=3.
  c = &cpus[cpuid()];
  c->gdt[SEG_KCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, 0);
  c->gdt[SEG_KDATA] = SEG(STA_W, 0, 0xffffffff, 0);
  c->gdt[SEG_UCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, DPL_USER);
  c->gdt[SEG_UDATA] = SEG(STA_W, 0, 0xffffffff, DPL_USER);
  lgdt(c->gdt, sizeof(c->gdt));
}

// Return the address of the PTE in page table pgdir
// that corresponds to virtual address va.  If alloc!=0,
// create any required page table pages.
 pte_t *                                                    /// we removed static for use in trap.c
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
    // Make sure all those PT`E_P bits are zero.
    memset(pgtab, 0, PGSIZE);
    // The permissions here are overly generous, but they can
    // be further restricted by the permissions in the page table
    // entries, if necessary.
    *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U;
  }
  return &pgtab[PTX(va)];
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned.
int
mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm)
{
  char *a, *last;
  pte_t *pte;

  a = (char*)PGROUNDDOWN((uint)va);
  last = (char*)PGROUNDDOWN(((uint)va) + size - 1);
  for(;;){
    if((pte = walkpgdir(pgdir, a, 1)) == 0)
      return -1;
    if(*pte & PTE_P)
      panic("remap");
    *pte = pa | perm | PTE_P;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// There is one page table per process, plus one that's used when
// a CPU is not running any process (kpgdir). The kernel uses the
// current process's page table during system calls and interrupts;
// page protection bits prevent user code from using the kernel's
// mappings.
//
// setupkvm() and exec() set up every page table like this:
//
//   0..KERNBASE: user memory (text+data+stack+heap), mapped to
//                phys memory allocated by the kernel
//   KERNBASE..KERNBASE+EXTMEM: mapped to 0..EXTMEM (for I/O space)
//   KERNBASE+EXTMEM..data: mapped to EXTMEM..V2P(data)
//                for the kernel's instructions and r/o data
//   data..KERNBASE+PHYSTOP: mapped to V2P(data)..PHYSTOP,
//                                  rw data + free physical memory
//   0xfe000000..0: mapped direct (devices such as ioapic)
//
// The kernel allocates physical memory for its heap and for user memory
// between V2P(end) and the end of physical memory (PHYSTOP)
// (directly addressable from end..P2V(PHYSTOP)).

// This table defines the kernel's mappings, which are present in
// every process's page table.
static struct kmap {
  void *virt;
  uint phys_start;
  uint phys_end;
  int perm;
} kmap[] = {
 { (void*)KERNBASE, 0,             EXTMEM,    PTE_W}, // I/O space
 { (void*)KERNLINK, V2P(KERNLINK), V2P(data), 0},     // kern text+rodata
 { (void*)data,     V2P(data),     PHYSTOP,   PTE_W}, // kern data+memory
 { (void*)DEVSPACE, DEVSPACE,      0,         PTE_W}, // more devices
};

// Set up kernel part of a page table.
pde_t*
setupkvm(void)
{
  pde_t *pgdir;
  struct kmap *k;

  if((pgdir = (pde_t*)kalloc()) == 0)
    return 0;
  memset(pgdir, 0, PGSIZE);
  if (P2V(PHYSTOP) > (void*)DEVSPACE)
    panic("PHYSTOP too high");
  for(k = kmap; k < &kmap[NELEM(kmap)]; k++)
    if(mappages(pgdir, k->virt, k->phys_end - k->phys_start,
                (uint)k->phys_start, k->perm) < 0) {
      freevm(pgdir);
      return 0;
    }
  return pgdir;
}

// Allocate one page table for the machine for the kernel address
// space for scheduler processes.
void
kvmalloc(void)
{
  kpgdir = setupkvm();
  switchkvm();
}

// Switch h/w page table register to the kernel-only page table,
// for when no process is running.
void
switchkvm(void)
{
  lcr3(V2P(kpgdir));   // switch to the kernel page table
}

// Switch TSS and h/w page table to correspond to process p.
void
switchuvm(struct proc *p)
{
  if(p == 0)
    panic("switchuvm: no process");
  if(p->kstack == 0)
    panic("switchuvm: no kstack");
  if(p->pgdir == 0)
    panic("switchuvm: no pgdir");

  pushcli();
  mycpu()->gdt[SEG_TSS] = SEG16(STS_T32A, &mycpu()->ts,
                                sizeof(mycpu()->ts)-1, 0);
  mycpu()->gdt[SEG_TSS].s = 0;
  mycpu()->ts.ss0 = SEG_KDATA << 3;
  mycpu()->ts.esp0 = (uint)p->kstack + KSTACKSIZE;
  // setting IOPL=0 in eflags *and* iomb beyond the tss segment limit
  // forbids I/O instructions (e.g., inb and outb) from user space
  mycpu()->ts.iomb = (ushort) 0xFFFF;
  ltr(SEG_TSS << 3);
  lcr3(V2P(p->pgdir));  // switch to process's address space
  popcli();
}

// Load the initcode into address 0 of pgdir.
// sz must be less than a page.
void
inituvm(pde_t *pgdir, char *init, uint sz)  // for now no pg_refcount
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pgdir, 0, PGSIZE, V2P(mem), PTE_W|PTE_U);
  memmove(mem, init, sz);
}

// Load a program segment into pgdir.  addr must be page-aligned
// and the pages from addr to addr+sz must already be mapped.
int
loaduvm(pde_t *pgdir, char *addr, struct inode *ip, uint offset, uint sz)
{
  uint i, pa, n;
  pte_t *pte;

  if((uint) addr % PGSIZE != 0)
    panic("loaduvm: addr must be page aligned");
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, addr+i, 0)) == 0)
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

char* SC_FIFO(struct proc* p){
  cprintf("enter scfifo\n");
  pte_t* pte;
  int oldOut;
  for(; p->out_index < MAX_PSYC_PAGES; p->out_index = (p->out_index + 1) % 16){
    pte = walkpgdir(p->pgdir,p->ram_queue[p->out_index].va,0);
      if(*pte & PTE_A){ 
        *pte &= ~PTE_A;
        continue;
      }
      else{
          oldOut = p->out_index;
          p->out_index = (p->out_index + 1) % 16;
          return p->ram_queue[oldOut].va;
      }
  }
  return 0; // we shouldnt get here
}  

char * fifo(struct proc * p){ //default is second chance fifo, for now we do fifo
  cprintf("fifo\n");
  int oldOut = p->out_index;
  p->out_index = (p->out_index + 1) % 16;
  return (char*)p->ram_queue[oldOut].va;
}


char * nfua(struct proc * p){
  cprintf("enter nfua\n");
  uint max = p->ram_queue[0].nfua_counter;
  for(int i = 1;i < MAX_PSYC_PAGES; i++){ // find max
    if(max <= p->ram_queue[i].nfua_counter)
      max = p->ram_queue[i].nfua_counter;
  }
  for(int i=0; i < MAX_PSYC_PAGES; i++){
    if (max == p->ram_queue[i].nfua_counter)
      return p->ram_queue[i].va;
  }
  panic("finding max makes no sense");
  return 0;
}

int find_numOnes(uint curr){
  int count = 0;
  for(int i = 0; i < 32; i++){
    if(curr & 1){
      count ++;
    }
    curr = curr >> 1;
  }
  return count;
}

char* lapa(struct proc* p){
  int count = 0;
  int arr [16];
  int value = 0xFFFFFFFF;
  int max_num_of_one = find_numOnes(p->ram_queue[0].nfua_counter);
  for(int i = 1;i < MAX_PSYC_PAGES; i++){ 
    int curr = find_numOnes(p->ram_queue[i].nfua_counter);
      if(max_num_of_one < curr)
         max_num_of_one = curr;
      else if( max_num_of_one == curr){
        count ++;
        arr[i] = 1;
      }
  }
  if(count == 0){
    for(int i=0; i < MAX_PSYC_PAGES; i++){
      if (max_num_of_one == p->ram_queue[i].nfua_counter)
        return p->ram_queue[i].va;
      }
  }
  else{
    for(int i=0; i < MAX_PSYC_PAGES; i++){
      if(arr[i] == 1){
          if(p->ram_queue[i].nfua_counter < value){
            value = p->ram_queue[i].nfua_counter;
          }
        }
      }
      //value is smallest
      for(int i=0; i < MAX_PSYC_PAGES; i++){
        if(arr[i] == 1){
          if (value == p->ram_queue[i].nfua_counter)
            return p->ram_queue[i].va;
        }
     }
  }
  return 0;
}

char* aq(struct proc * p){

  return p->ram_queue[15].va;
}


char * choosePage(struct proc * p){ //default is second chance fifo

 #if SELECTION == SCFIFO
     return SC_FIFO(p);
  #endif

 #if SELECTION == FIFO
     return fifo(p);
  #endif

 #if SELECTION == NFUA
     return nfua(p);
  #endif

 #if SELECTION == LAPA
     return lapa(p);
  #endif

  #if SELECTION == AQ
     return aq(p);
  #endif
  

  return 0;
}

void pageOut(struct proc* p){
  cprintf("pageOut\n");
    p->numOfPageOut ++;
    char* pg = choosePage(p);
    if(pg == 0)
      panic("pg = 0"); 
    pte_t* pte;
    uint pa;
    pte = walkpgdir(p->pgdir,pg,0);            // get the PTE of the chosen page
    pa = PTE_ADDR(*pte);
    memmove(buffer,pg,PGSIZE);                 // pg or v2p(pg)? the v2p caused a deadlock for some reason
    if(writeToSwapFile(p, buffer, (p->swapMetaCounter * PGSIZE), PGSIZE) < 0)   // write the data of the chosen page to the swap file in the right loctaion
      panic("writeToSwapFile failed\n");
    // memmove(buffer,pg + PGSIZE/2,PGSIZE/2);      // pg or v2p(pg)? the v2p caused a deadlock for some reason
    // if(writeToSwapFile(p, buffer, (p->swapMetaCounter * PGSIZE) + PGSIZE/2, PGSIZE/2) < 0)   // write the data of the chosen page to the swap file in the aviable loctaion
    //   panic("writeToSwapFile failed\n");
    p->meta[p->swapMetaCounter].va = pg;       // put the va in meta for task 1.2
    pg_refcount[pa >> PGSHIFT] -= 1;           // refCount--
    if(pg_refcount[pa >> PGSHIFT] == 1)
      kfree(pg);                               // free the page
    p->meta[p->swapMetaCounter].location = p->swapMetaCounter * PGSIZE;
    p->swapMetaCounter++;
    *pte &= ~PTE_P;   
    *pte |= PTE_PG;                            // set\turn on the PTE_PG bit in page table
    cprintf("fun with flags\n");
    // *p->pgdir &= ~PTE_P;                    // clear\turn off the PTE_P bit in directory table(same as clear pte_u, line 341)
    // *p->pgdir |= PTE_PG;                    // set\turn on the PTE_PG bit in directory table
    lcr3(V2P(p->pgdir));                       // refresh the TLB
    p->physical_num_of_pages --;
    p->phy_index--;                          
}


void shiftPages(struct proc * p, char * mem){
  for (int i = p->phy_index; i >= 1; i--){
    p->ram_queue[i].va = p->ram_queue[i-1].va;
    p->ram_queue[i].nfua_counter = p->ram_queue[i-1].nfua_counter;
  }
  p->ram_queue[0].va = mem;
}
/*
int classic_allocuvm(pde_t *pgdir, uint oldsz, uint newsz){
  char *mem;
  uint a;

  if(newsz >= KERNBASE)
    return 0;
  if(newsz < oldsz)
    return oldsz;

  a = PGROUNDUP(oldsz);
  for(; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      cprintf("allocuvm out of memory\n");
      deallocuvm(pgdir, newsz, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pgdir, (char*)a, PGSIZE, V2P(mem), PTE_W|PTE_U) < 0){
      cprintf("allocuvm out of memory (2)\n");
      deallocuvm(pgdir, newsz, oldsz);
      kfree(mem);
      return 0;
    }
  }
  return newsz;
}
*/
// Allocate page tables and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
int
allocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{

  char *mem;
  uint a;  

  if(newsz >= KERNBASE)
    return 0;
  if(newsz < oldsz)
    return oldsz;

  a = PGROUNDUP(oldsz);
  struct proc * p = myproc();
  if(p->current_num_of_pages < TOTAL_PSYC_PAGES){ //32
  for(; a < newsz; a += PGSIZE){
    // check if we alloc more pages or swap pages
    if (p->pid > 2){
      if (p->physical_num_of_pages >= MAX_PSYC_PAGES){ //16
          pageOut(p);
      }
    } 
    mem = kalloc();
    if(mem == 0){
      cprintf("allocuvm out of memory\n");
      deallocuvm(pgdir, newsz, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pgdir, (char*)a, PGSIZE, V2P(mem), PTE_W|PTE_U) < 0){ // a is a contagious addr, v2p(mem) is somewhere according to the freelist
      cprintf("allocuvm out of memory (2)\n");
      deallocuvm(pgdir, newsz, oldsz);
      kfree(mem);
      return 0;
    }
    // kalloc
    if(p->pid > 2){
    acquire(&lock);
    pg_refcount[V2P(mem) >> PGSHIFT] = pg_refcount[V2P(mem) >> PGSHIFT] + 1 ;
    release(&lock);
    }
    
    if(p->pid > 2){
      p->ram_queue[p->phy_index].va = mem;  // virtual address the kernel gave to the page
     
      #if SELECTION == AQ
        shiftPages(p,mem);
      #endif
     
      #if SELECTION == LAPA
        p->ram_queue[p->phy_index].nfua_counter = 0xFFFFFFFF;
      #endif
     
      #if SELECTION == NFUA
        p->ram_queue[p->phy_index].nfua_counter = 0;
      #endif
      
      p->phy_index++;
      p->physical_num_of_pages ++;           
      p->current_num_of_pages ++;
    }
  }
  return newsz;
}
cprintf("TOTAL_PSYC_PAGES\n");
return 0;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
int
deallocuvm(pde_t *pgdir, uint oldsz, uint newsz){
  pte_t *pte;
  uint a, pa;

  if(newsz >= oldsz)
    return oldsz;

  a = PGROUNDUP(newsz);
  for(; a  < oldsz; a += PGSIZE){
    pte = walkpgdir(pgdir, (char*)a, 0);
    if(!pte)
      a = PGADDR(PDX(a) + 1, 0, 0) - PGSIZE;
    else if((*pte & PTE_P) != 0){
      pa = PTE_ADDR(*pte);
      if(pa == 0)
        panic("kfree from deallocuvm");
      char *v = P2V(pa);
      *pte = 0;   // if refCount > 1 we dont want the pte but we dont do kfree
      if(myproc()->pid > 2){   
        acquire(&lock);
        pg_refcount[pa >> PGSHIFT] -= 1;
        if(pg_refcount[pa >> PGSHIFT] == 0){          // if no other page table is pointing to this page remove it 
          kfree(v);
        }
        release(&lock);
        if(myproc()->physical_num_of_pages > 0 && myproc()->current_num_of_pages >0){
        myproc()->physical_num_of_pages --;
        myproc()->current_num_of_pages --;
        myproc()->phy_index --;   // next time we put a different page instead of the page that is there now
        }
      }
      else{
        kfree(v);
      }
    }
  }
    return newsz;
 }

// Free a page table and all the physical memory pages
// in the user part.
void
freevm(pde_t *pgdir)
{
  uint i;

  if(pgdir == 0)
    panic("freevm: no pgdir");
  deallocuvm(pgdir, KERNBASE, 0);
  for(i = 0; i < NPDENTRIES; i++){
    if(pgdir[i] & PTE_P){
      char * v = P2V(PTE_ADDR(pgdir[i]));
      kfree(v);       
    }
  }
  kfree((char*)pgdir);
}

// Clear PTE_U on a page. Used to create an inaccessible
// page beneath the user stack.
void
clearpteu(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if(pte == 0)
    panic("clearpteu");
  *pte &= ~PTE_U;
}

pde_t*
copyuvm(pde_t *pgdir, uint sz)
{
  pde_t *d;
  pte_t *pte;
  uint pa, i, flags;
  char *mem;

  if((d = setupkvm()) == 0)
    return 0;
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, (void *) i, 0)) == 0)
      panic("copyuvm: pte should exist");
    if(!(*pte & PTE_P))
      panic("copyuvm: page not present");
    pa = PTE_ADDR(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto bad;
    memmove(mem, (char*)P2V(pa), PGSIZE);
    if(mappages(d, (void*)i, PGSIZE, V2P(mem), flags) < 0) {
      kfree(mem);
      goto bad;
    }
  }
  return d;

bad:
  freevm(d);
  return 0;
}

// Given a parent process's page table, the 
// child points to his PTEs
pde_t*
cowuvm(pde_t *pgdir, uint sz){
  pde_t *d;
  pte_t *pte;
  uint pa, i, flags;

  if((d = setupkvm()) == 0)
    return 0;
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, (void *) i, 0)) == 0)
      panic("cowuvm: pte should exist");
    if(!(*pte & PTE_P))
      panic("cowuvm: page not present");
    if(*pte & PTE_W){         // check the page is writable
      *pte = *pte | PTE_COW;  // cow flag
      *pte &= ~PTE_W;         // make the page read only
    }
    pa = PTE_ADDR(*pte);
    flags = PTE_FLAGS(*pte);
    if(mappages(d, (void*)i, PGSIZE, pa, flags) < 0) {    // make the new pgdir point to pa
      goto bad;
    }
    acquire(&lock);
    pg_refcount[pa >> PGSHIFT] = pg_refcount[pa >> PGSHIFT] + 1;
    release(&lock);   
  }
  lcr3(V2P(pgdir));                 // flush TLB after the page table of the parent changed (we turn off the writable flag)
  return d;

bad:
  lcr3(V2P(pgdir));
  freevm(d);
  return 0;
}


//PAGEBREAK!
// Map user virtual address to kernel address.
char*
uva2ka(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
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
copyout(pde_t *pgdir, uint va, void *p, uint len)
{
  char *buf, *pa0;
  uint n, va0;

  buf = (char*)p;
  while(len > 0){
    va0 = (uint)PGROUNDDOWN(va);
    pa0 = uva2ka(pgdir, (char*)va0);
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

int findInSwapFile(struct proc* p, char* va){
  for(int i = 0; i < MAX_PSYC_PAGES ; i++){
    if(p->meta[i].va == va){
      return i;
    }
  }
  return -1;
}


void pageFault(){
  char* a;
  char* v;
  char * mem;
  uint pa;
  char * new_pg ;
  pte_t * pte;
  struct proc * p;
  int index;
  p = myproc();
  p->numOfPageFaults++;
  if (p->pid > 2){
    uint va = rcr2();                                     // catch virtual address of fault
    a = (char*)PGROUNDDOWN(va);                           // va of the page
    pte = walkpgdir(p->pgdir,a,0);
    pa = PTE_ADDR(*pte);
    if(*pte & PTE_PG){                                    // check if the page we want is in swapFile
      // if(pg_refcount[pa >> PGSHIFT] == 0) 
      if(p->physical_num_of_pages == MAX_PSYC_PAGES)
        pageOut(p);                                       // send a page to the swap file
      new_pg = kalloc(); 
      index = findInSwapFile(p,a);                        // find the va in meta  
      if(readFromSwapFile(p,buffer, p->meta[index].location,PGSIZE) < 0)   // need to be changed in task 3
        cprintf("unable to read from swapfile1\n");
      p->swapMetaCounter--;                               // get out the page
      memmove(new_pg,buffer,PGSIZE);                      // put the va that kalloc returned to the page(virtual addr) that we want to replace
      pg_refcount[pa >> PGSHIFT]++;                       
      *pte &= ~PTE_PG;
      *pte |= (uint)V2P(new_pg);                          //enter new pa to the pte that caused page fault
      *pte |= PTE_P;
      // mappages(p->pgdir,new_pg,PGSIZE,(uint)V2P(new_pg),PTE_FLAGS(*pte));

      return;
    }

    if(*pte & PTE_COW){                                   // if cow
      pa = PTE_ADDR(*pte);
      v = P2V(pa);
      acquire(&lock);
      if(pg_refcount[pa >> PGSHIFT] > 1){                 // side note: if we dont need cow anymore refcount will be equal to 1
          mem = kalloc();
          memmove(mem,v,PGSIZE);                          // not sure if v = a, but v is good here for sure
          *pte = V2P(mem) | PTE_FLAGS(*pte);              // put the new page in the address of pte(mem is an address of a page so its only 20 bits + offset)
          pg_refcount[pa >> PGSHIFT] -= 1;
          pg_refcount[V2P(mem) >> PGSHIFT] = 1;           // no +, only = (?)
      }                       
      *pte = *pte | PTE_W | PTE_P;
     
      if(pg_refcount[pa >> PGSHIFT] == 1){
        *pte &= ~PTE_COW;
      }
      release(&lock);  
       lcr3(V2P(p->pgdir));                                // flush TLB                
      return;
    }
  }
    return;
}
