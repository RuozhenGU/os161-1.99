/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include "opt-A3.h"
#include <mips/trapframe.h>


/*
 * Dumb MIPS-only "VM system" that is intended to only be just barely
 * enough to struggle off the ground.
 */

/* under dumbvm, always have 48k of user stack */
#define DUMBVM_STACKPAGES    12

/*
 * Wrap rma_stealmem in a spinlock.
 */
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;


#if OPT_A3
struct coremap {
	paddr_t baseAddr; // != base of physical mem
	int * inUse; //Array
	int * containNext; //Array
	int size; //number of available frames/Size of arrays
};

struct coremap *core_map;

bool iscmapCreated = false;

static struct spinlock spinlock_coremap;

paddr_t addr_lo, addr_hi;

#endif //OPT_A3

void
vm_bootstrap(void)
{
#if OPT_A3
	//add_lo and hi are physical addr


	//initialize spinlock for core_map
	spinlock_init(&spinlock_coremap);

	//Get the remaining available physical memory in sys in case ram_stealmen ran before
	ram_getsize(&addr_lo, &addr_hi);
	//Converts a physical address to a kernel virtual address.
	core_map = (struct coremap *)PADDR_TO_KVADDR(addr_lo);

	//Count frame numbers = size of array
	int frameCount = (addr_hi - addr_lo) / PAGE_SIZE;

	//Insert coremap in physical mem, find new base addr of available phsical addr
	addr_lo += sizeof(struct coremap);
	//init inuse array
	core_map->inUse = (int *)PADDR_TO_KVADDR(addr_lo);
	//insert space
	addr_lo += sizeof(int) * frameCount;
	//init containnext array
	core_map->containNext = (int *)PADDR_TO_KVADDR(addr_lo);
	//insert the space
	addr_lo += sizeof(int) * frameCount;

	//After insertion, if start physical addr does not align the start of one page/frame, update
	while (addr_lo % PAGE_SIZE != 0) addr_lo++;


	core_map->baseAddr = addr_lo;

	core_map->size = (addr_hi - addr_lo) / PAGE_SIZE; /* recalculate */

	kprintf("coremapSize and frameSize: %d %d\n", core_map->size, frameCount);

	for (int i = 0; i < frameCount; i++) {
		core_map->inUse[i] = 0;
		core_map->containNext[i] = 0;
	}

	/* coremap is successfully built */
	iscmapCreated = true;
	kprintf("Bootstrap successful, mem range: %d - %d\n", addr_lo, addr_hi);
#endif //OPT_A3
}

static
paddr_t
getppages(unsigned long npages)
{
#if OPT_A3

	int pageRequired = (int) npages;

	paddr_t addr;

	spinlock_acquire(&stealmem_lock);

	if (iscmapCreated == false){
		addr = ram_stealmem(npages);
		kprintf("physical memory stealed!");
		spinlock_release(&stealmem_lock);
		return addr;
	} else /* core map exists */ {
		KASSERT(core_map->size >= pageRequired);
		for(int i = 0; i < core_map->size; i++) {
			if (core_map->inUse[i] == 0) {
				int sofar = i;
				int count = 0;
				/* check if the free mem is enough to use */
				while(count < pageRequired && sofar < core_map->size) {
					if (core_map->inUse[sofar]) break; //ensure the mem loc is still available
					count++; sofar++;
				}
				if (count < pageRequired) {
					i += count;
					continue; /*not enough*/
				} else {
					/* update status of those found entries on physical mem */
					for (int j = 0; j < pageRequired; j++) {
						const int targetLoc = i + j;
						/* update core map*/
						core_map->inUse[targetLoc] = 1;
						if (j != pageRequired - 1) core_map->containNext[targetLoc] = 1;
						else core_map->containNext[targetLoc] = 0; //last element
					}
					addr = i * PAGE_SIZE + core_map->baseAddr; //beginning addr grabbed
					spinlock_release(&stealmem_lock);
					KASSERT(addr <= addr_hi && addr >= addr_lo);
					return addr;
				}
			}
			/* else case: continue until free mem is found */
		} //for loop end
		/* memory error */
		kprintf("Error! Available physical memory is not enough! Try to free some before acquiring.\n");
		spinlock_release(&stealmem_lock);
		return ENOMEM;
	} //if coremap exist end
#else
	/* no core map case */
	spinlock_acquire(&stealmem_lock);
	addr = ram_stealmem(npages);
	spinlock_release(&stealmem_lock);
	return addr;

#endif //OPT_A3
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t
alloc_kpages(int npages)
{
	paddr_t pa;
	pa = getppages(npages); //what section of memory is available to handle
	if (pa==0) {
		return 0;
	}
	return PADDR_TO_KVADDR(pa);
}

void
free_kpages(vaddr_t addr)
{
#if OPT_A3
	(void) addr;
	if (iscmapCreated == false) {
		kprintf("no coremap to free\n");
		return;
	}
	spinlock_acquire(&spinlock_coremap);
	int targetAddr =  addr - (core_map->baseAddr + MIPS_KSEG0);
	targetAddr /= PAGE_SIZE;
	int i;
	for(i = targetAddr; i < core_map->size && core_map->containNext[i] == 1; i++){
		core_map->inUse[i] = 0;
	}
	if (i < core_map->size) core_map->inUse[i] = 0;
	spinlock_release(&spinlock_coremap);
#else
	(void) addr;
	return;
#endif //OPT_A3
}

void
vm_tlbshootdown_all(void)
{
	panic("dumbvm tried to do tlb shootdown?!\n");
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("dumbvm tried to do tlb shootdown?!\n");
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;
	paddr_t paddr;
	int i;
	uint32_t ehi, elo;
	struct addrspace *as;
	int spl;
	faultaddress &= PAGE_FRAME;

	DEBUG(DB_VM, "dumbvm: fault: 0x%x\n", faultaddress);

	switch (faulttype) {

	    case VM_FAULT_READONLY:
#if OPT_A3
				return EX_MOD;
#else
				/* We always create pages read-write, so we can't get this */
				panic("dumbvm: got VM_FAULT_READONLY\n");
#endif //OPT_A3

	    case VM_FAULT_READ:
	    case VM_FAULT_WRITE:
		break;
	    default:
		return EINVAL;
	}

	if (curproc == NULL) {
		/*
		 * No process. This is probably a kernel fault early
		 * in boot. Return EFAULT so as to panic instead of
		 * getting into an infinite faulting loop.
		 */
		return EFAULT;
	}

	as = curproc_getas();
	if (as == NULL) {
		/*
		 * No address space set up. This is probably also a
		 * kernel fault early in boot.
		 */

		return EFAULT;
	}
	/* Assert that the address space has been set up properly. */

	KASSERT(as->as_vbase1 != 0);
	KASSERT(as->as_npages1 != 0);
	KASSERT(as->as_vbase2 != 0);
	KASSERT(as->as_npages2 != 0);
#if OPT_A3
	KASSERT(as->as_ptable1 != NULL);
	KASSERT(as->as_ptable2 != NULL);
	KASSERT(as->as_stackptable != NULL);
#else
	KASSERT(as->as_stackpbase != 0);
	KASSERT(as->as_pbase1 != 0);
	KASSERT(as->as_pbase2 != 0);
	KASSERT((as->as_pbase1 & PAGE_FRAME) == as->as_pbase1);
	KASSERT((as->as_pbase2 & PAGE_FRAME) == as->as_pbase2);
	KASSERT((as->as_stackpbase & PAGE_FRAME) == as->as_stackpbase);
#endif //OPT_A3
	KASSERT((as->as_vbase1 & PAGE_FRAME) == as->as_vbase1);
	KASSERT((as->as_vbase2 & PAGE_FRAME) == as->as_vbase2);

	vbase1 = as->as_vbase1;
	vtop1 = vbase1 + as->as_npages1 * PAGE_SIZE;
	vbase2 = as->as_vbase2;
	vtop2 = vbase2 + as->as_npages2 * PAGE_SIZE;
	stackbase = USERSTACK - DUMBVM_STACKPAGES * PAGE_SIZE;
	stacktop = USERSTACK;

	//physical addr = frameNumber * page size + offset

	if (faultaddress >= vbase1 && faultaddress < vtop1) {
#if OPT_A3
		int offset = faultaddress - vbase1;
		paddr = (as->as_ptable1[offset / PAGE_SIZE]).frameNumber + offset % PAGE_SIZE;
#else
		paddr = (faultaddress - vbase1) + as->as_pbase1;
#endif //OPT_A3
	}
	else if (faultaddress >= vbase2 && faultaddress < vtop2) {
#if OPT_A3
		int offset = faultaddress - vbase2;
		paddr = (as->as_ptable2[offset / PAGE_SIZE]).frameNumber + offset % PAGE_SIZE;
#else
		paddr = (faultaddress - vbase2) + as->as_pbase2;
#endif //OPT_A3
	}
	else if (faultaddress >= stackbase && faultaddress < stacktop) {
#if OPT_A3
		int offset = faultaddress - stackbase;
		paddr = (as->as_stackptable[offset / PAGE_SIZE]).frameNumber + offset % PAGE_SIZE;
#else
		paddr = (faultaddress - stackbase) + as->as_stackpbase;
#endif //OPT_A3
	}
	else {
		return EFAULT;
	}

	/* make sure it's page-aligned */
	KASSERT((paddr & PAGE_FRAME) == paddr);

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID) {
			continue;
		}

		ehi = faultaddress;
		elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
#if OPT_A3
		if(faultaddress < vtop1 && faultaddress >= vbase1 && as->loadCode_done == 1) elo &= ~TLBLO_DIRTY;
#endif //OPT_A3
		DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
		tlb_write(ehi, elo, i);
		splx(spl);
		return 0; //TLB is not full
	}

	//TLB is Full
	ehi = faultaddress;
	elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
#if OPT_A3
	if(faultaddress < vtop1 && faultaddress >= vbase1 && as->loadCode_done == 1) elo &= ~TLBLO_DIRTY; //Dity bit off
#endif //OPT_A3
	tlb_random(faultaddress, elo); //Pick a random entry to pop off
	splx(spl);
	return 0;
}

struct addrspace *
as_create(void)
{
	struct addrspace *as = kmalloc(sizeof(struct addrspace));
	if (as==NULL) {
		return NULL;
	}
#if OPT_A3
	as->loadCode_done = 0;
	as->as_ptable1 = NULL;
	as->as_ptable2 = NULL;
	as->as_stackptable = NULL;
#else
	as->as_pbase1 = 0;
	as->as_pbase2 = 0;
	as->as_stackpbase = 0;
#endif //OPT_A3
	as->as_vbase1 = 0;
	as->as_npages1 = 0;
	as->as_vbase2 = 0;
	as->as_npages2 = 0;

	return as;
}

void
as_destroy(struct addrspace *as)
{
#if OPT_A3
/*
	kprintf("call desotry\n");
	free_kpages(PADDR_TO_KVADDR(as->as_pbase1));
	free_kpages(PADDR_TO_KVADDR(as->as_pbase2));
	free_kpages(PADDR_TO_KVADDR(as->as_stackpbase));
*/
	for(int i = 0; i < (int)as->as_npages1; i++) {
		free_kpages(PADDR_TO_KVADDR(as->as_ptable1[i].frameNumber));
	}
	for(int i = 0; i < (int)as->as_npages2; i++) {
		free_kpages(PADDR_TO_KVADDR(as->as_ptable2[i].frameNumber));
	}
	for(int i = 0; i < DUMBVM_STACKPAGES; i++) {
		free_kpages(PADDR_TO_KVADDR(as->as_stackptable[i].frameNumber));
	}
	kfree(as->as_stackptable);
	kfree(as->as_ptable2);
	kfree(as->as_ptable1);
#endif //OPT_A3
	kfree(as);
}

void
as_activate(void)
{
	int i, spl;
	struct addrspace *as;

	as = curproc_getas();
#ifdef UW
        /* Kernel threads don't have an address spaces to activate */
#endif
	if (as == NULL) {
		return;
	}

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

void
as_deactivate(void)
{
	/* nothing */
}

int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable)
{
	size_t npages;

	/* Align the region. First, the base... */
	sz += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;

	npages = sz / PAGE_SIZE;

	/* We don't use these - all pages are read-write */
	(void)readable;
	(void)writeable;
	(void)executable;

	if (as->as_vbase1 == 0) {
		as->as_vbase1 = vaddr;
		as->as_npages1 = npages;
#if OPT_A3
		as->as_ptable1 = kmalloc(sizeof(struct page_table) * npages); //npage is PTE

#endif //OPT_A3
		return 0;
	}

	if (as->as_vbase2 == 0) {
		as->as_vbase2 = vaddr;
		as->as_npages2 = npages;
#if OPT_A3
		as->as_ptable2 = kmalloc(sizeof(struct page_table) * npages); //npage is PTE
#endif //OPT_A3
		return 0;
	}

	/*
	 * Support for more than two regions is not available.
	 */
	kprintf("dumbvm: Warning: too many regions\n");
	return EUNIMP;
}

static
void
as_zero_region(paddr_t paddr, unsigned npages)
{
	bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}

int
as_prepare_load(struct addrspace *as)
{
#if OPT_A3
	//page table for stack is created
	as->as_stackptable = kmalloc(sizeof(struct page_table) * DUMBVM_STACKPAGES);
	//sanity check
	if(as->as_ptable1 == NULL || as->as_ptable2 == NULL || as->as_stackptable == NULL) {
		return ENOMEM;
	}
	//pre-allocate frames for each page
	for(unsigned int i = 0; i < as->as_npages1; i++) {
		/* allocate each frame one at a time */
		as->as_ptable1[i].frameNumber = getppages(1);
		if (as->as_ptable1[i].frameNumber == 0) return ENOMEM;
		as_zero_region(as->as_ptable1[i].frameNumber, 1);
	}

	for(unsigned int i = 0; i < as->as_npages2; i++) {
		/* allocate each frame one at a time */
		as->as_ptable2[i].frameNumber = getppages(1);
		if (as->as_ptable2[i].frameNumber == 0) return ENOMEM;
		as_zero_region(as->as_ptable2[i].frameNumber, 1);
	}

	for(unsigned int i = 0; i < DUMBVM_STACKPAGES; i++) {
		/* allocate each frame one at a time */
		as->as_stackptable[i].frameNumber = getppages(1);
		if (as->as_stackptable[i].frameNumber == 0) return ENOMEM;
		as_zero_region(as->as_stackptable[i].frameNumber, 1);
	}

#else
	KASSERT(as->as_pbase1 == 0);
	KASSERT(as->as_pbase2 == 0);
	KASSERT(as->as_stackpbase == 0);

	as->as_pbase1 = getppages(as->as_npages1);
	if (as->as_pbase1 == 0) {
		return ENOMEM;
	}

	as->as_pbase2 = getppages(as->as_npages2);
	if (as->as_pbase2 == 0) {
		return ENOMEM;
	}

	as->as_stackpbase = getppages(DUMBVM_STACKPAGES);
	if (as->as_stackpbase == 0) {
		return ENOMEM;
	}

	as_zero_region(as->as_pbase1, as->as_npages1);
	as_zero_region(as->as_pbase2, as->as_npages2);
	as_zero_region(as->as_stackpbase, DUMBVM_STACKPAGES);
#endif //OPT_A3
	return 0;
}

//tlb entry = invalid
int
as_complete_load(struct addrspace *as)
{
	(void)as;
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
#if OPT_A3
	(void)as;
#else
	KASSERT(as->as_stackpbase != 0);
#endif //OPT_A3

	*stackptr = USERSTACK;
	return 0;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *new;

	new = as_create();
	if (new==NULL) {
		return ENOMEM;
	}

	new->as_vbase1 = old->as_vbase1;
	new->as_npages1 = old->as_npages1;
	new->as_vbase2 = old->as_vbase2;
	new->as_npages2 = old->as_npages2;
#if OPT_A3
	//create segments based on old addr spaces
	new->as_ptable1 = kmalloc(sizeof(struct page_table) * new->as_npages1);
	new->as_ptable2 = kmalloc(sizeof(struct page_table) * new->as_npages2);
#endif //OPT_A3
	/* (Mis)use as_prepare_load to allocate some physical memory. */
	if (as_prepare_load(new)) {
		as_destroy(new);
		return ENOMEM;
	}
#if OPT_A3
	KASSERT(new->as_ptable1 && new->as_ptable2 && new->as_stackptable);
	for(unsigned int i = 0; i < old->as_npages1; i++) { 
		memmove((void*)PADDR_TO_KVADDR(new->as_ptable1[i].frameNumber),
						(const void*)PADDR_TO_KVADDR(old->as_ptable1[i].frameNumber), PAGE_SIZE);
	}

	for(unsigned int i = 0; i < old->as_npages2; i++) {
		memmove((void*)PADDR_TO_KVADDR(new->as_ptable2[i].frameNumber),
						(const void*)PADDR_TO_KVADDR(old->as_ptable2[i].frameNumber), PAGE_SIZE);
	}

	for(int i = 0; i < (int)DUMBVM_STACKPAGES; i++) {
		memmove((void*)PADDR_TO_KVADDR(new->as_stackptable[i].frameNumber),
						(const void*)PADDR_TO_KVADDR(old->as_stackptable[i].frameNumber), PAGE_SIZE);
	}
#else
	KASSERT(new->as_pbase1 != 0);
	KASSERT(new->as_pbase2 != 0);
	KASSERT(new->as_stackpbase != 0);

	memmove((void *)PADDR_TO_KVADDR(new->as_pbase1),
		(const void *)PADDR_TO_KVADDR(old->as_pbase1),
		old->as_npages1*PAGE_SIZE);

	memmove((void *)PADDR_TO_KVADDR(new->as_pbase2),
		(const void *)PADDR_TO_KVADDR(old->as_pbase2),
		old->as_npages2*PAGE_SIZE);

	memmove((void *)PADDR_TO_KVADDR(new->as_stackpbase),
		(const void *)PADDR_TO_KVADDR(old->as_stackpbase),
		DUMBVM_STACKPAGES*PAGE_SIZE);
#endif //OPT_A3
	*ret = new;
	return 0;
}
