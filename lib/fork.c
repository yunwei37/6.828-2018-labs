// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

extern void (*_pgfault_handler)(struct UTrapframe *utf);\
extern void _pgfault_upcall(void);

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
	//cprintf("start map in our own private writable copy %x\n", addr);
	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
	if (!(err & 2)) {
		panic("the faulting access was not write at %x %08x %08x", addr, utf->utf_eip, err);
	}
	if (!(uvpt[PGNUM(addr)] & PTE_COW)) {
		panic("the faulting access was not to a copy-on-write page at %08x %08x", addr, utf->utf_eip);
	}
	// cprintf("pg fault %x envid %d\n", addr, sys_getenvid());

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here.
	if ((r = sys_page_alloc(0, PFTEMP,
				PTE_P|PTE_U|PTE_W)) < 0)
		panic("allocating at %x in page fault handler: %e", addr, r);
	memmove(PFTEMP, ROUNDDOWN(addr, PGSIZE), PGSIZE);
	if ((r = sys_page_map(0, PFTEMP, 0, ROUNDDOWN(addr, PGSIZE), PTE_P|PTE_U|PTE_W)) < 0)
		panic("sys_page_map: %e", r);
	if ((r = sys_page_unmap(0, UTEMP)) < 0)
		panic("sys_page_unmap: %e", r);
	//cprintf("map in our own private writable copy %x\n", addr);
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
duppage(envid_t envid, void * pn)
{
	int r;

	// LAB 4: Your code here.
	// cprintf("duppage found page %08x\n", pn);
	if (uvpt[PGNUM(pn)] & PTE_COW || uvpt[PGNUM(pn)] & PTE_W) {
		r = sys_page_map(0, pn, envid, pn, PTE_P|PTE_U|PTE_COW);
		if (r < 0)
			panic("sys_page_map: %e", r);
		r = sys_page_map(0, pn, 0, pn, PTE_P|PTE_U|PTE_COW);
		if (r < 0)
			panic("sys_page_map: %e", r);
	} else {
		r = sys_page_map(0, pn, envid, pn, PTE_P|PTE_U);
		if (r < 0)
			panic("sys_page_map: %e", r);
	}
	return 0;
}

void
dump_duppage(envid_t dstenv, void *addr)
{
	int r;

	// This is NOT what you should do in your fork.
	if ((r = sys_page_alloc(dstenv, addr, PTE_P|PTE_U|PTE_W)) < 0)
		panic("sys_page_alloc: %e", r);
	if ((r = sys_page_map(dstenv, addr, 0, UTEMP, PTE_P|PTE_U|PTE_W)) < 0)
		panic("sys_page_map: %e", r);
	memmove(UTEMP, addr, PGSIZE);
	if ((r = sys_page_unmap(0, UTEMP)) < 0)
		panic("sys_page_unmap: %e", r);
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
	uint8_t *addr;
	extern unsigned char end[];
	int r;

	set_pgfault_handler(pgfault);
	envid = sys_exofork();
	if (envid < 0)
		return envid;
	if (envid == 0) {
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}

	// We're the parent.
	// Eagerly copy our entire address space into the child.
	for (addr = (uint8_t*) UTEXT; addr < end; addr += PGSIZE)
		duppage(envid, addr);
	//cprintf("fork duppage addr page %08x\n", addr);
	// Also copy the stack we are currently running on.
	dump_duppage(envid, ROUNDDOWN(&addr, PGSIZE));
	//cprintf("fork duppage dump_duppage page %08x\n", ROUNDDOWN(&addr, PGSIZE));
	for (addr; addr < (uint8_t*)USTACKTOP; addr += PGSIZE) {
		if ((uvpd[PDX(addr)] & PTE_P) && (uvpt[PGNUM(addr)] & PTE_P) && (uvpt[PGNUM(addr)] & PTE_SHARE)) {
			//cprintf("fork duppage PTE_SHARE page %08x\n", addr);
			r = sys_page_map(0, addr, envid, addr, PTE_SYSCALL);
			if (r < 0)
				panic("sys_page_map: %e", r);
		}
	}
	
	// Copy our page fault handler setup to the child.
	if ((r = sys_page_alloc(envid, (void*)(UXSTACKTOP - PGSIZE), PTE_P|PTE_U|PTE_W)) < 0)
		cprintf("set_pgfault_handler: sys_page_alloc: %e\n", r);
	if (sys_env_set_pgfault_upcall(envid, _pgfault_upcall) < 0){
		cprintf("set_pgfault_handler: sys_env_set_pgfault_upcall failed\n");
	}
	//cprintf("ROUNDDOWN(&_pgfault_handler, PGSIZE) %08x %08x\n", ROUNDDOWN(&_pgfault_handler, PGSIZE), &_pgfault_handler);
	dump_duppage(envid, ROUNDDOWN(&_pgfault_handler, PGSIZE));

	// Start the child environment running
	if ((r = sys_env_set_status(envid, ENV_RUNNABLE)) < 0)
		panic("sys_env_set_status: %e", r);
	// cprintf("fork success %08x\n", envid);
	return envid;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
