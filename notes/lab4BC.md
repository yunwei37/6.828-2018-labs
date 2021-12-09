<!-- TOC -->

- [MIT 6.828 操作系统工程 lab4B：Copy-on-Write Fork](#mit-6828-操作系统工程-lab4bcopy-on-write-fork)
  - [用户级页面错误处理](#用户级页面错误处理)
    - [练习 8. 实现sys_env_set_pgfault_upcall系统调用](#练习-8-实现sys_env_set_pgfault_upcall系统调用)
  - [用户环境中的正常和异常堆栈](#用户环境中的正常和异常堆栈)
    - [练习 9.page_fault_handler](#练习-9page_fault_handler)
  - [用户模式页面错误入口点](#用户模式页面错误入口点)
    - [练习 10._pgfault_upcall](#练习-10_pgfault_upcall)
    - [练习 11.set_pgfault_handler()](#练习-11set_pgfault_handler)
  - [实现写时复制分叉](#实现写时复制分叉)
    - [练习 12 实现 fork,duppage 和 pgfault](#练习-12-实现-forkduppage-和-pgfault)
- [MIT 6.828 操作系统工程 lab4C: 抢占式多任务和进程间通信 (IPC)](#mit-6828-操作系统工程-lab4c-抢占式多任务和进程间通信-ipc)
  - [时钟中断和抢占](#时钟中断和抢占)
    - [练习13 初始化所述IDT中的相应条目](#练习13-初始化所述idt中的相应条目)
  - [处理时钟中断](#处理时钟中断)
  - [进程间通信 (IPC)](#进程间通信-ipc)
    - [练习 15](#练习-15)

<!-- /TOC -->

# MIT 6.828 操作系统工程 lab4B：Copy-on-Write Fork

>这篇是我自己探索实现 MIT 6.828 lab 的笔记记录，会包含一部分代码注释和要求的翻译记录，以及踩过的坑/个人的解决方案

这里是我实现的完整代码仓库，也包含其他笔记等等：[https://github.com/yunwei37/6.828-2018-labs](https://github.com/yunwei37/6.828-2018-labs)

如前所述，Unix 提供fork()系统调用作为其主要的进程创建原语。该fork()系统调用将调用进程的地址空间（父）创建一个新的进程（孩子）。

在本实验的下一部分中，您将实现一个“正确的”类 Unix fork() 和写时复制，作为用户空间库例程。

## 用户级页面错误处理

用户级写时复制fork()需要了解写保护页面上的页面错误，因此这是您首先要实现的。写时复制只是用户级页面错误处理的众多可能用途之一。

为了处理自己的页面错误，用户环境需要向JOS 内核注册一个页面错误处理程序入口点。用户环境通过新的sys_env_set_pgfault_upcall系统调用注册其页面错误入口点。

### 练习 8. 实现sys_env_set_pgfault_upcall系统调用

```c
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

```

## 用户环境中的正常和异常堆栈

在正常执行过程中，JOS用户环境将在运行正常的用户堆栈：它的ESP注册开始了在指向USTACKTOP且堆栈数据之间是推动在页面上驻留USTACKTOP-PGSIZE和USTACKTOP-1包容性。然而，当在用户模式下发生页面错误时，内核将重新启动用户环境，在不同的堆栈上运行指定的用户级页面错误处理程序，即用户异常堆栈。本质上，我们将让 JOS 内核代表用户环境实现自动“堆栈切换”，这与 x86处理器 在从用户模式转换到内核模式时已经代表 JOS 实现堆栈切换非常相似！

JOS 用户异常栈也是一页大小，其顶部定义为虚拟地址UXSTACKTOP.

### 练习 9.page_fault_handler

实现将页面错误分派到用户模式处理程序所需 的代码。写入异常堆栈时一定要采取适当的预防措施。

```c

void
page_fault_handler(struct Trapframe *tf)
{
	uint32_t fault_va;

	// Read processor's CR2 register to find the faulting address
	fault_va = rcr2();

	// Handle kernel-mode page faults.

	// LAB 3: Your code here.
	if ((tf->tf_cs & 3) != 3) {
		panic("[%08x] kernel fault va %08x ip %08x\n",
		curenv->env_id, fault_va, tf->tf_eip);
	}

	// We've already handled kernel-mode exceptions, so if we get here,
	// the page fault happened in user mode.

	// Call the environment's page fault upcall, if one exists.  Set up a
	// page fault stack frame on the user exception stack (below
	// UXSTACKTOP), then branch to curenv->env_pgfault_upcall.
	//
	// The page fault upcall might cause another page fault, in which case
	// we branch to the page fault upcall recursively, pushing another
	// page fault stack frame on top of the user exception stack.
	//
	// It is convenient for our code which returns from a page fault
	// (lib/pfentry.S) to have one word of scratch space at the top of the
	// trap-time stack; it allows us to more easily restore the eip/esp. In
	// the non-recursive case, we don't have to worry about this because
	// the top of the regular user stack is free.  In the recursive case,
	// this means we have to leave an extra word between the current top of
	// the exception stack and the new stack frame because the exception
	// stack _is_ the trap-time stack.
	//
	// If there's no page fault upcall, the environment didn't allocate a
	// page for its exception stack or can't write to it, or the exception
	// stack overflows, then destroy the environment that caused the fault.
	// Note that the grade script assumes you will first check for the page
	// fault upcall and print the "user fault va" message below if there is
	// none.  The remaining three checks can be combined into a single test.
	//
	// Hints:
	//   user_mem_assert() and env_run() are useful here.
	//   To change what the user environment runs, modify 'curenv->env_tf'
	//   (the 'tf' variable points at 'curenv->env_tf').

	// LAB 4: Your code here.
	
	if (curenv->env_pgfault_upcall) {
		struct UTrapframe *utf;
		user_mem_assert(curenv, curenv->env_pgfault_upcall, 1, PTE_P|PTE_U);

		if (curenv->env_tf.tf_esp <= UXSTACKTOP-1 && curenv->env_tf.tf_esp >= UXSTACKTOP - PGSIZE) {
			utf = (struct UTrapframe*)(curenv->env_tf.tf_esp - sizeof(size_t) - sizeof(*utf));
			if (utf < (struct UTrapframe*)(UXSTACKTOP - PGSIZE)) {
				cprintf("the exception stack overflows.");
				goto out;
			}
		} else {
			utf = (struct UTrapframe*)(UXSTACKTOP - sizeof(*utf));
		}
		user_mem_assert(curenv, utf, sizeof(*utf), PTE_P|PTE_U|PTE_W);
		utf->utf_regs = curenv->env_tf.tf_regs;
		utf->utf_esp = curenv->env_tf.tf_esp;
		utf->utf_eflags = curenv->env_tf.tf_eflags;
		utf->utf_eip = curenv->env_tf.tf_eip;
		utf->utf_err = curenv->env_tf.tf_err;
		utf->utf_fault_va = fault_va;

		curenv->env_tf.tf_eip = (uintptr_t)curenv->env_pgfault_upcall;
		curenv->env_tf.tf_esp = (uintptr_t)utf;

		env_run(curenv);
	}

out:
	// Destroy the environment that caused the fault.
	cprintf("[%08x] user fault va %08x ip %08x\n",
		curenv->env_id, fault_va, tf->tf_eip);
	print_trapframe(tf);
	env_destroy(curenv);
}

```

## 用户模式页面错误入口点

接下来，您需要实现汇编例程，该例程将负责调用 C 页错误处理程序并在原始错误指令处恢复执行。

### 练习 10._pgfault_upcall

```s
_pgfault_upcall:
	// Call the C page fault handler.
	pushl %esp			// function argument: pointer to UTF
	movl _pgfault_handler, %eax
	call *%eax
	addl $4, %esp			// pop function argument
	
	// Now the C page fault handler has returned and you must return
	// to the trap time state.
	// Push trap-time %eip onto the trap-time stack.
	//
	// Explanation:
	//   We must prepare the trap-time stack for our eventual return to
	//   re-execute the instruction that faulted.
	//   Unfortunately, we can't return directly from the exception stack:
	//   We can't call 'jmp', since that requires that we load the address
	//   into a register, and all registers must have their trap-time
	//   values after the return.
	//   We can't call 'ret' from the exception stack either, since if we
	//   did, %esp would have the wrong value.
	//   So instead, we push the trap-time %eip onto the *trap-time* stack!
	//   Below we'll switch to that stack and call 'ret', which will
	//   restore %eip to its pre-fault value.
	//
	//   In the case of a recursive fault on the exception stack,
	//   note that the word we're pushing now will fit in the
	//   blank word that the kernel reserved for us.
	//
	// Throughout the remaining code, think carefully about what
	// registers are available for intermediate calculations.  You
	// may find that you have to rearrange your code in non-obvious
	// ways as registers become unavailable as scratch space.
	//
	// LAB 4: Your code here.
	popl %eax
	popl %eax

	movl 0x20(%esp), %eax
	movl 0x28(%esp), %ebx
	subl $4, %ebx
	movl %ebx, 0x28(%esp)
	movl %eax, (%ebx)

	// Restore the trap-time registers.  After you do this, you
	// can no longer modify any general-purpose registers.
	// LAB 4: Your code here.
	popal

	// Restore eflags from the stack.  After you do this, you can
	// no longer use arithmetic operations or anything else that
	// modifies eflags.
	// LAB 4: Your code here.
	addl $4, %esp
	popf

	// Switch back to the adjusted trap-time stack.
	// LAB 4: Your code here.
	popl %esp

	// Return to re-execute the instruction that faulted.
	// LAB 4: Your code here.
	ret

```

### 练习 11.set_pgfault_handler()

```c
void
set_pgfault_handler(void (*handler)(struct UTrapframe *utf))
{
	int r;

	if (_pgfault_handler == 0) {
		// First time through!
		// LAB 4: Your code here.
		if ((r = sys_page_alloc(sys_getenvid(), (void*)(UXSTACKTOP - PGSIZE), PTE_P|PTE_U|PTE_W)) < 0)
			cprintf("set_pgfault_handler: sys_page_alloc: %e\n", r);
		if (sys_env_set_pgfault_upcall(sys_getenvid(), _pgfault_upcall) < 0){
			cprintf("set_pgfault_handler: sys_env_set_pgfault_upcall failed\n");
		}
	}

	// Save handler pointer for assembly to call.
	_pgfault_handler = handler;
}
```

## 实现写时复制分叉

### 练习 12 实现 fork,duppage 和 pgfault

一个并不是很完善的实现：

lib/fork.c
```c
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

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
	if (!(err & 2)) {
		panic("the faulting access was not write at %x", addr);
	}
	if (!(uvpt[PGNUM(addr)] & PTE_COW)) {
		panic("the faulting access was not to a copy-on-write page at %x", addr);
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
	cprintf("map in our own private writable copy %x\n", addr);
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
	// This is NOT what you should do in your fork implementation.
	for (addr = (uint8_t*) UTEXT; addr < end; addr += PGSIZE)
		duppage(envid, addr);

	// Also copy the stack we are currently running on.
	dump_duppage(envid, ROUNDDOWN(&addr, PGSIZE));

	// Copy our page fault handler setup to the child.
	if ((r = sys_page_alloc(envid, (void*)(UXSTACKTOP - PGSIZE), PTE_P|PTE_U|PTE_W)) < 0)
		cprintf("set_pgfault_handler: sys_page_alloc: %e\n", r);
	if (sys_env_set_pgfault_upcall(envid, _pgfault_upcall) < 0){
		cprintf("set_pgfault_handler: sys_env_set_pgfault_upcall failed\n");
	}
	dump_duppage(envid, ROUNDDOWN(&_pgfault_handler, PGSIZE));

	// Start the child environment running
	if ((r = sys_env_set_status(envid, ENV_RUNNABLE)) < 0)
		panic("sys_env_set_status: %e", r);

	return envid;
}

```

# MIT 6.828 操作系统工程 lab4C: 抢占式多任务和进程间通信 (IPC)

## 时钟中断和抢占

为了让内核抢占运行环境，强行夺回对 CPU 的控制，我们必须扩展 JOS 内核以支持来自时钟硬件的外部硬件中断。

外部中断（即设备中断）称为 IRQ。

### 练习13 初始化所述IDT中的相应条目

trapentry.S
```s
TRAPHANDLER_NOEC(handler32, IRQ_OFFSET + IRQ_TIMER)
TRAPHANDLER_NOEC(handler33, IRQ_OFFSET + IRQ_KBD)
TRAPHANDLER_NOEC(handler34, IRQ_OFFSET + IRQ_SERIAL)
TRAPHANDLER_NOEC(handler35, IRQ_OFFSET + IRQ_SPURIOUS)
TRAPHANDLER_NOEC(handler36, IRQ_OFFSET + IRQ_IDE)
TRAPHANDLER_NOEC(handler37, IRQ_OFFSET + IRQ_ERROR)
```

trap.c
```c
...
void handler32();
void handler33();
void handler34();
void handler35();
void handler36();
void handler37();

...
	SETGATE(idt[IRQ_OFFSET + IRQ_TIMER], 0, GD_KT, handler32, 0);
	SETGATE(idt[IRQ_OFFSET + IRQ_KBD], 0, GD_KT, handler33, 0);
	SETGATE(idt[IRQ_OFFSET + IRQ_SERIAL], 0, GD_KT, handler34, 0);
	SETGATE(idt[IRQ_OFFSET + IRQ_SPURIOUS], 0, GD_KT, handler35, 0);
	SETGATE(idt[IRQ_OFFSET + IRQ_IDE], 0, GD_KT, handler36, 0);
	SETGATE(idt[IRQ_OFFSET + IRQ_ERROR], 0, GD_KT, handler37, 0);
...
```
注意这里 setgate 的参数和之前不同，因为这里是 IRQ 不是 trap，进入 IRQ 时硬件会自动阻止中断

env_alloc.c
```c
	// Enable interrupts while in user mode.
	// LAB 4: Your code here.
	e->env_tf.tf_eflags = e->env_tf.tf_eflags | FL_IF;
```

## 处理时钟中断

我们需要对硬件进行编程以定期生成时钟中断，这将强制控制回到内核，在那里我们可以将控制切换到不同的用户环境。

修改内核的trap_dispatch()函数，使其sched_yield() 在发生时钟中断时调用查找并运行不同的环境。

```c

	// Handle clock interrupts. Don't forget to acknowledge the
	// interrupt using lapic_eoi() before calling the scheduler!
	// LAB 4: Your code here.
	if (tf->tf_trapno == IRQ_OFFSET + IRQ_TIMER) {
		//cprintf("IRQ_TIMER interrupt\n");
		lapic_eoi();
		sched_yield();
		return;
	}
```

## 进程间通信 (IPC)

操作系统的另一个重要服务是允许程序在需要时相互通信。让程序与其他程序交互是非常强大的。

您将实现一些额外的 JOS 内核系统调用，它们共同提供了一个简单的进程间通信机制。您将实现两个系统调用，sys_ipc_recv以及 sys_ipc_try_send. 然后，您将实现两个库包装器 ipc_recv和ipc_send.

### 练习 15 

sys_ipc_recv以及 sys_ipc_try_send

```c

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

```

ipc_recv和ipc_send

```c

	if(from_env_store)
		*from_env_store = thisenv->env_ipc_from;
	if(perm_store)
		*perm_store = thisenv->env_ipc_perm;
	//cprintf("ipc_recv from %08x to %08x value %d\n",thisenv->env_ipc_from, thisenv->env_id, thisenv->env_ipc_value);
	return thisenv->env_ipc_value;
}

// Send 'val' (and 'pg' with 'perm', if 'pg' is nonnull) to 'toenv'.
// This function keeps trying until it succeeds.
// It should panic() on any error other than -E_IPC_NOT_RECV.
//
// Hint:
//   Use sys_yield() to be CPU-friendly.
//   If 'pg' is null, pass sys_ipc_try_send a value that it will understand
//   as meaning "no page".  (Zero is not the right value.)
void
ipc_send(envid_t to_env, uint32_t val, void *pg, int perm)
{
	// LAB 4: Your code here.
	int result;

	do {
		result = sys_ipc_try_send(to_env, val, pg? pg: (void*)UTOP, perm);
		if (result != 0) {
			sys_yield();
		}
	} while(result == -E_IPC_NOT_RECV);
	if (result != 0) {
		panic("ipc_send failed: %d", result);
	}
}

```