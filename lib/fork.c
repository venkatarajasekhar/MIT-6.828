// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;
	unsigned pn;
	pte_t pte;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
	pn = (unsigned)addr / PGSIZE;
	pte = uvpt[pn];
	if (!(err & FEC_WR) || !(pte & PTE_COW))
		panic("pgfault: not a copy-on-write situation");

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here.
	if ((r = sys_page_alloc(0, PFTEMP, PTE_P|PTE_U|PTE_W)) < 0)
		panic("sys_page_map: %e", r);
	
	memmove(PFTEMP, ROUNDDOWN(addr, PGSIZE), PGSIZE);
	
	if ((r = sys_page_map(0, PFTEMP, 0, ROUNDDOWN(addr, PGSIZE), PTE_P|PTE_U|PTE_W)) < 0)
		panic("sys_page_map: %e", r);

	if ((r = sys_page_unmap(0, PFTEMP)) < 0)
		panic("sys_page_unmap: %e", r);
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	int r;
	pte_t pte;
	uintptr_t addr;
	
	// LAB 4: Your code here.
	pte = uvpt[pn];
	addr = pn * PGSIZE;
	
	if (pte & PTE_W || pte & PTE_COW) {
		// mark copy-on-write for child's pte 
		if ((r = sys_page_map(0, (void *)addr, envid, (void *)addr, PTE_P|PTE_U|PTE_COW)) < 0)
			panic("sys_page_map: %e", r);
	
		// makr copy-on-write for parent's pte
		if ((r = sys_page_map(0, (void *)addr, 0, (void *)addr, PTE_P|PTE_U|PTE_COW)) < 0)
			panic("sys_page_map: %e", r);
	} else
		panic("duppage: pn is not a writable or copy-on-write page");
		
	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
	envid_t envid;
	int r, pn;
	pte_t pte;
	uintptr_t i, addr;

	set_pgfault_handler(pgfault);	
	envid = sys_exofork();

	if (envid < 0)
		panic("sys_exofork: %e", envid);

	// We're the child 
	if (envid == 0) {
	//	set_pgfault_handler(pgfault);	
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}

	// We're the parent
	for (addr = UTEXT; addr < UTOP; addr += PTSIZE) {
		if ((uvpd[PDX(addr)] & PTE_P) == 0)
			continue;			// pde is not present
		
		for (i = addr; (i < addr + PTSIZE) && (i < UTOP - PGSIZE); i += PGSIZE) {  
			pn = i / PGSIZE;	
			pte = uvpt[pn];
			
			if ((pte & PTE_P) == 0) 
				continue;			// pte is not present
			
			if (pte & PTE_W || pte & PTE_COW)	
				duppage(envid, pn);
			else 
				sys_page_map(0, (void *)i, envid, (void *)i, PTE_P|PTE_U);				
		}	
	}
	
	// Set the exception stack and env_pgfault_upcall for the child	
	if ((r = sys_page_alloc(envid, (void *)(UXSTACKTOP - PGSIZE), PTE_P|PTE_U|PTE_W) < 0))
		panic("sys_page_alloc: %e", r);

	if ((r = sys_env_set_pgfault_upcall(envid, thisenv->env_pgfault_upcall) < 0))
		panic("sys_env_set_status %e", r);

	// Starting the child environment running
	if ((r = sys_env_set_status(envid, ENV_RUNNABLE)) < 0)
		panic("sys_env_set_status: %e", r);

	return envid;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}