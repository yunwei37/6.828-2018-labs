/* See COPYRIGHT for copyright information. */

#include <inc/x86.h>
#include <inc/error.h>
#include <inc/string.h>
#include <inc/assert.h>

#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/trap.h>
#include <kern/syscall.h>
#include <kern/console.h>
#include <kern/sched.h>
#include <kern/time.h>
#include <kern/e1000.h>

// Print a string to the system console.
// The string is exactly 'len' characters long.
// Destroys the environment on memory errors.
static void
sys_cputs(const char *s, size_t len)
{
	// Check that the user has permission to read memory [s, s+len).
	// Destroy the environment if not.

	// LAB 3: Your code here.
	user_mem_assert(curenv, s, len, 0);

	// Print the string supplied by the user.
	cprintf("%.*s", len, s);
}

// Read a character from the system console without blocking.
// Returns the character, or 0 if there is no input waiting.
static int
sys_cgetc(void)
{
	return cons_getc();
}

// Returns the current environment's envid.
static envid_t
sys_getenvid(void)
{
	return curenv->env_id;
}

// Destroy a given environment (possibly the currently running environment).
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_destroy(envid_t envid)
{
	int r;
	struct Env *e;
	struct Env* cur_env = curenv;

	if ((r = envid2env(envid, &e, 1)) < 0)
		return r;
	if (e == curenv)
		cprintf("[%08x] exiting gracefully\n", cur_env->env_id);
	else
		cprintf("[%08x] destroying %08x\n", cur_env->env_id, e->env_id);
	env_destroy(e);
	return 0;
}

// Deschedule current environment and pick a different one to run.
static void
sys_yield(void)
{
	sched_yield();
}

// Allocate a new environment.
// Returns envid of new environment, or < 0 on error.  Errors are:
//	-E_NO_FREE_ENV if no free environment is available.
//	-E_NO_MEM on memory exhaustion.
static envid_t
sys_exofork(void)
{
	// Create the new environment with env_alloc(), from kern/env.c.
	// It should be left as env_alloc created it, except that
	// status is set to ENV_NOT_RUNNABLE, and the register set is copied
	// from the current environment -- but tweaked so sys_exofork
	// will appear to return 0.

	// LAB 4: Your code here.
	struct Env* new_env;
	int result;
	static envid_t last_id;
	struct Env* cur_env = curenv;

	if ((result = env_alloc(&new_env, curenv->env_id)) < 0) {
		return result;
	}
	new_env->env_status = ENV_NOT_RUNNABLE;
	new_env->env_tf = cur_env->env_tf;
	new_env->env_tf.tf_regs.reg_eax = 0;
	// cprintf("new_env: %d\n", new_env->env_id);
	return new_env->env_id;
}

// Set envid's env_status to status, which must be ENV_RUNNABLE
// or ENV_NOT_RUNNABLE.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if status is not a valid status for an environment.
static int
sys_env_set_status(envid_t envid, int status)
{
	// Hint: Use the 'envid2env' function from kern/env.c to translate an
	// envid to a struct Env.
	// You should set envid2env's third argument to 1, which will
	// check whether the current environment has permission to set
	// envid's status.

	// LAB 4: Your code here.
	struct Env* new_env;
	int result;

	if ((result = envid2env(envid, &new_env, 1)) < 0){
		return result;
	}
	if (status != ENV_RUNNABLE && status != ENV_NOT_RUNNABLE) {
		return -E_INVAL;
	}
	new_env->env_status = status;

	return 0;
}

// Set envid's trap frame to 'tf'.
// tf is modified to make sure that user environments always run at code
// protection level 3 (CPL 3), interrupts enabled, and IOPL of 0.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_set_trapframe(envid_t envid, struct Trapframe *tf)
{
	// LAB 5: Your code here.
	// Remember to check whether the user has supplied us with a good
	// address!
	struct Env* e;
	int result;

	if ((result = envid2env(envid, &e, 1)) < 0){
		return result;
	}
	if (!tf) {
		return -E_INVAL;
	}
	e->env_tf = *tf;
	e->env_tf.tf_ds = GD_UD | 3;
	e->env_tf.tf_es = GD_UD | 3;
	e->env_tf.tf_ss = GD_UD | 3;
	e->env_tf.tf_cs = GD_UT | 3;
	e->env_tf.tf_eflags = e->env_tf.tf_eflags | FL_IF;
	e->env_tf.tf_eflags &= ~FL_IOPL_MASK; 
	return 0;
}

// Set the page fault upcall for 'envid' by modifying the corresponding struct
// Env's 'env_pgfault_upcall' field.  When 'envid' causes a page fault, the
// kernel will push a fault record onto the exception stack, then branch to
// 'func'.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_set_pgfault_upcall(envid_t envid, void *func)
{
	// LAB 4: Your code here.
	struct Env* new_env;
	int result;

	if ((result = envid2env(envid, &new_env, 1)) < 0){
		return result;
	}
	new_env->env_pgfault_upcall = func;
	return 0;
}

// Allocate a page of memory and map it at 'va' with permission
// 'perm' in the address space of 'envid'.
// The page's contents are set to 0.
// If a page is already mapped at 'va', that page is unmapped as a
// side effect.
//
// perm -- PTE_U | PTE_P must be set, PTE_AVAIL | PTE_W may or may not be set,
//         but no other bits may be set.  See PTE_SYSCALL in inc/mmu.h.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
//	-E_INVAL if perm is inappropriate (see above).
//	-E_NO_MEM if there's no memory to allocate the new page,
//		or to allocate any necessary page tables.
static int
sys_page_alloc(envid_t envid, void *va, int perm)
{
	// Hint: This function is a wrapper around page_alloc() and
	//   page_insert() from kern/pmap.c.
	//   Most of the new code you write should be to check the
	//   parameters for correctness.
	//   If page_insert() fails, remember to free the page you
	//   allocated!

	// LAB 4: Your code here.
	struct Env* new_env;
	int result;
	struct PageInfo *p = NULL;
	
	if ((uintptr_t)va >= UTOP || (uintptr_t)va % PGSIZE ) {
		return -E_INVAL;
	}
	if (perm & ~PTE_SYSCALL) {
		return -E_INVAL;
	}
	if ((result = envid2env(envid, &new_env, 1)) < 0) {
		return result;
	}
	if (!(p = page_alloc(ALLOC_ZERO))) {
		return -E_NO_MEM;
	}
	if ((result = page_insert(new_env->env_pgdir, p, va, perm | PTE_U)) < 0){
		page_free(p);
		return result;
	}
	return 0;
}

// Map the page of memory at 'srcva' in srcenvid's address space
// at 'dstva' in dstenvid's address space with permission 'perm'.
// Perm has the same restrictions as in sys_page_alloc, except
// that it also must not grant write access to a read-only
// page.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if srcenvid and/or dstenvid doesn't currently exist,
//		or the caller doesn't have permission to change one of them.
//	-E_INVAL if srcva >= UTOP or srcva is not page-aligned,
//		or dstva >= UTOP or dstva is not page-aligned.
//	-E_INVAL is srcva is not mapped in srcenvid's address space.
//	-E_INVAL if perm is inappropriate (see sys_page_alloc).
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in srcenvid's
//		address space.
//	-E_NO_MEM if there's no memory to allocate any necessary page tables.
static int
sys_page_map(envid_t srcenvid, void *srcva,
	     envid_t dstenvid, void *dstva, int perm)
{
	// Hint: This function is a wrapper around page_lookup() and
	//   page_insert() from kern/pmap.c.
	//   Again, most of the new code you write should be to check the
	//   parameters for correctness.
	//   Use the third argument to page_lookup() to
	//   check the current permissions on the page.

	// LAB 4: Your code here.
	struct Env* src_env;
	struct Env* dst_env;
	int result;
	struct PageInfo *p = NULL;
	pte_t *pte;
	
	if ((uintptr_t)srcva >= UTOP || (uintptr_t)srcva % PGSIZE || (uintptr_t)srcva >= UTOP || (uintptr_t)srcva % PGSIZE) {
		// cprintf("srcva >= UTOP or srcva is not page-aligned, or dstva >= UTOP or dstva is not page-aligned.");
		return -E_INVAL;
	}
	if (perm & ~PTE_SYSCALL) {
		return -E_INVAL;
	}
	if ((result = envid2env(srcenvid, &src_env, 1)) < 0){
		return result;
	}
	if ((result = envid2env(dstenvid, &dst_env, 1)) < 0){
		return result;
	}
	if (!(p = page_lookup(src_env->env_pgdir, srcva, &pte))) {
		return -E_INVAL;
	}
	if (!(*pte & PTE_W)  && (perm & PTE_W)) {
		return -E_INVAL;
	}
	if ((result = page_insert(dst_env->env_pgdir, p, dstva, perm | PTE_U)) < 0){
		return result;
	}
	return 0;
}

// Unmap the page of memory at 'va' in the address space of 'envid'.
// If no page is mapped, the function silently succeeds.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
static int
sys_page_unmap(envid_t envid, void *va)
{
	// Hint: This function is a wrapper around page_remove().

	// LAB 4: Your code here.
	struct Env* new_env;
	int result;

	if ((uintptr_t)va >= UTOP || (uintptr_t)va % PGSIZE ) {
		return -E_INVAL;
	}
	if ((result = envid2env(envid, &new_env, 1)) < 0){
		return result;
	}
	page_remove(new_env->env_pgdir, va);
	return 0;
}

// Try to send 'value' to the target env 'envid'.
// If srcva < UTOP, then also send page currently mapped at 'srcva',
// so that receiver gets a duplicate mapping of the same page.
//
// The send fails with a return value of -E_IPC_NOT_RECV if the
// target is not blocked, waiting for an IPC.
//
// The send also can fail for the other reasons listed below.
//
// Otherwise, the send succeeds, and the target's ipc fields are
// updated as follows:
//    env_ipc_recving is set to 0 to block future sends;
//    env_ipc_from is set to the sending envid;
//    env_ipc_value is set to the 'value' parameter;
//    env_ipc_perm is set to 'perm' if a page was transferred, 0 otherwise.
// The target environment is marked runnable again, returning 0
// from the paused sys_ipc_recv system call.  (Hint: does the
// sys_ipc_recv function ever actually return?)
//
// If the sender wants to send a page but the receiver isn't asking for one,
// then no page mapping is transferred, but no error occurs.
// The ipc only happens when no errors occur.
//
// Returns 0 on success, < 0 on error.
// Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist.
//		(No need to check permissions.)
//	-E_IPC_NOT_RECV if envid is not currently blocked in sys_ipc_recv,
//		or another environment managed to send first.
//	-E_INVAL if srcva < UTOP but srcva is not page-aligned.
//	-E_INVAL if srcva < UTOP and perm is inappropriate
//		(see sys_page_alloc).
//	-E_INVAL if srcva < UTOP but srcva is not mapped in the caller's
//		address space.
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in the
//		current environment's address space.
//	-E_NO_MEM if there's not enough memory to map srcva in envid's
//		address space.
static int
sys_ipc_try_send(envid_t envid, uint32_t value, void *srcva, unsigned perm)
{
	// LAB 4: Your code here.
	struct Env* new_env;
	int result;
	struct Env* cur_env = curenv;

	if ((result = envid2env(envid, &new_env, 0)) < 0){
		return result;
	}
	if (!new_env->env_ipc_recving) {
		return -E_IPC_NOT_RECV;
	}
	if (srcva < (void*)UTOP && new_env->env_ipc_dstva) {
		//cprintf("map pages %08x", srcva);
		if ((size_t)srcva % PGSIZE) {
			cprintf("(size_t)srcva not PGSIZE");
			return -E_INVAL;
		}
		if (perm & ~PTE_SYSCALL) {
			cprintf("perm & ~PTE_SYSCALL");
			return -E_INVAL;
		}
		struct PageInfo *p = NULL;
		pte_t *pte;

		if (!(p = page_lookup(cur_env->env_pgdir, srcva, &pte))) {
			return -E_INVAL;
		}
		if (!(*pte & PTE_W)  && (perm & PTE_W)) {
			return -E_INVAL;
		}
		if ((result = page_insert(new_env->env_pgdir, p, new_env->env_ipc_dstva, perm | PTE_U)) < 0){
			return result;
		}
		new_env->env_ipc_perm = perm;
	} else {
		new_env->env_ipc_perm = 0;
	}
	new_env->env_ipc_recving = false;
	new_env->env_ipc_from = cur_env->env_id;
	new_env->env_ipc_value = value;
	new_env->env_status = ENV_RUNNABLE;
	new_env->env_tf.tf_regs.reg_eax = 0; 
	new_env->env_ipc_dstva = 0;
	//cprintf("sys_ipc_try_send to %08x %d %08x %d curenv id %08x\n", new_env->env_id, value, srcva, perm, cur_env->env_id);
	return 0;
}

// Block until a value is ready.  Record that you want to receive
// using the env_ipc_recving and env_ipc_dstva fields of struct Env,
// mark yourself not runnable, and then give up the CPU.
//
// If 'dstva' is < UTOP, then you are willing to receive a page of data.
// 'dstva' is the virtual address at which the sent page should be mapped.
//
// This function only returns on error, but the system call will eventually
// return 0 on success.
// Return < 0 on error.  Errors are:
//	-E_INVAL if dstva < UTOP but dstva is not page-aligned.
static int
sys_ipc_recv(void *dstva)
{
	// LAB 4: Your code here.
	int result;
	struct Env* cur_env = curenv;
	
	cur_env->env_ipc_recving = true;
	if (dstva < (void*)UTOP) {
		if ((size_t)dstva % PGSIZE) {
			cprintf("(size_t)dstva not PGSIZE");
			return -E_INVAL;
		}
		cur_env->env_ipc_dstva = dstva;
	}
	//cprintf("sys_ipc_recv %08x wait\n", cur_env->env_id);
	cur_env->env_tf.tf_regs.reg_eax = 0; 
	cur_env->env_ipc_from = 0;
	cur_env->env_ipc_value = 0;
	cur_env->env_status = ENV_NOT_RUNNABLE;
	sched_yield();
	return 0;
}

// Return the current time.
static int
sys_time_msec(void)
{
	// LAB 6: Your code here.
	// panic("sys_time_msec not implemented");
	return time_msec();
}

// Return the current time.
static int
sys_net_transmit(void *src, size_t length)
{
	// LAB 6: Your code here.
	user_mem_assert(curenv, src, length, 0);
	return transmit_packet(src, length);
}

static int
sys_net_receive(void *dst)
{
	// LAB 6: Your code here.
	//cprintf("sys_net_receive va %08x", dst);
	user_mem_assert(curenv, dst, 2048, PTE_U|PTE_W);
	return receive_packet(dst);
}

// Dispatches to the correct kernel function, passing the arguments.
int32_t
syscall(uint32_t syscallno, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
	// Call the function corresponding to the 'syscallno' parameter.
	// Return any appropriate return value.
	// LAB 3: Your code here.
	int32_t result = 0;

	// cprintf("syscall: %u para: %x %u %u %u %u\n", syscallno, a1, a2, a3, a4, a5);

	switch (syscallno) {
	case SYS_cputs:
		sys_cputs((const char*)a1, (size_t)a2);
		return 0;
	case SYS_cgetc:
		return sys_cgetc();
	case SYS_getenvid:
		return sys_getenvid();
	case SYS_env_destroy:
		return sys_env_destroy((envid_t)a1);
	case SYS_yield:
		sys_yield();
		return 0;
	case SYS_exofork:
		return sys_exofork();
	case SYS_env_set_status:
		return sys_env_set_status((envid_t)a1, (int)a2);
	case SYS_page_alloc:
		return sys_page_alloc((envid_t)a1, (void*)a2, (int)a3);
	case SYS_page_map:
		return sys_page_map((envid_t)a1, (void*)a2, (envid_t)a3, (void*)a4, (int)a5);
	case SYS_page_unmap:
		return sys_page_unmap((envid_t)a1, (void*)a2);
	case SYS_env_set_pgfault_upcall:
		return sys_env_set_pgfault_upcall((envid_t)a1, (void*)a2);
	case SYS_ipc_try_send:
		return sys_ipc_try_send((envid_t)a1, a2, (void*)a3, (unsigned int)a4);
	case SYS_ipc_recv:
		return sys_ipc_recv((void*)a1);
	case SYS_env_set_trapframe:
		return sys_env_set_trapframe((envid_t)a1, (struct Trapframe *)a2);
	case SYS_time_msec:
		return sys_time_msec();
	case SYS_net_transmit:
		return sys_net_transmit((void*)a1, (size_t)a2);
	case SYS_net_receive:
		return sys_net_receive((void*)a1);
	default:
		return -E_INVAL;
	}
}

