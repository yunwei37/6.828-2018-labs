#  MIT 6.828 操作系统工程 lab4A：多处理器支持和协作多任务

>这篇是我自己探索实现 MIT 6.828 lab 的笔记记录，会包含一部分代码注释和要求的翻译记录，以及踩过的坑/个人的解决方案

这里是我实现的完整代码仓库，也包含其他笔记等等：[https://github.com/yunwei37/6.828-2018-labs](https://github.com/yunwei37/6.828-2018-labs)

<!-- TOC -->

- [MIT 6.828 操作系统工程 lab4A：多处理器支持和协作多任务](#mit-6828-操作系统工程-lab4a多处理器支持和协作多任务)
  - [实验 4 包含许多新的源文件:](#实验-4-包含许多新的源文件)
  - [多处理器支持](#多处理器支持)
    - [练习1：mmio_map_region](#练习1mmio_map_region)
  - [应用处理器引导程序](#应用处理器引导程序)
    - [练习2：page_init](#练习2page_init)
  - [每个 CPU 的状态和初始化](#每个-cpu-的状态和初始化)
    - [练习3：mem_init_mp](#练习3mem_init_mp)
    - [练习4：trap_init_percpu](#练习4trap_init_percpu)
  - [锁](#锁)
    - [练习 5： 通过在适当的位置调用lock_kernel()和unlock_kernel()，如上所述应用大内核锁](#练习-5-通过在适当的位置调用lock_kernel和unlock_kernel如上所述应用大内核锁)
  - [循环调度](#循环调度)
    - [练习 6](#练习-6)
  - [创建环境的系统调用](#创建环境的系统调用)
    - [练习 7 在kern/syscall.c 中实现上述系统调用](#练习-7-在kernsyscallc-中实现上述系统调用)

<!-- /TOC -->

## 实验 4 包含许多新的源文件:

- `kern/cpu.h`	多处理器支持的内核私有定义
- `kern/mpconfig.c`	读取多处理器配置的代码
- `kern/lapic.c`	驱动每个处理器中的本地 APIC 单元的内核代码
- `kern/mpentry.S`	非引导 CPU 的汇编语言入口代码
- `kern/spinlock.h`	自旋锁的内核私有定义，包括大内核锁
- `kern/spinlock.c`	实现自旋锁的内核代码
- `kern/sched.c`	您将要实现的调度程序的代码框架

## 多处理器支持

我们将让 JOS 支持“对称多处理”(SMP)，这是一种多处理器模型，在该模型中，所有 CPU 都具有对系统资源（例如内存和 I/O 总线）的同等访问权限。尽管 SMP 中所有 CPU 的功能都相同，但在引导过程中它们可以分为两种类型：引导处理器 (BSP) 负责初始化系统和引导操作系统；只有在操作系统启动并运行后，BSP 才会激活应用处理器（AP）。哪个处理器是 BSP 是由硬件和 BIOS 决定的。到目前为止，您现有的所有 JOS 代码都已在 BSP 上运行。

在 SMP 系统中，每个 CPU 都有一个伴随的本地 APIC (LAPIC) 单元。LAPIC 单元负责在整个系统中传送中断。LAPIC 还为其连接的 CPU 提供唯一标识符。在本实验中，我们使用 LAPIC 单元（在kern/lapic.c 中）的以下基本功能：

- 读取 LAPIC 标识符 (APIC ID) 以判断我们的代码当前正在哪个 CPU 上运行
- 发送STARTUP从BSP到AP间中断（IPI）去启动其他CPU
- 在 C 部分，我们对 LAPIC 的内置计时器进行编程以触发时钟中断以支持抢占式多任务处理

处理器使用内存映射 I/O (MMIO) 访问其 LAPIC。

### 练习1：mmio_map_region

```c
void *
mmio_map_region(physaddr_t pa, size_t size)
{
	// Where to start the next region.  Initially, this is the
	// beginning of the MMIO region.  Because this is static, its
	// value will be preserved between calls to mmio_map_region
	// (just like nextfree in boot_alloc).
	static uintptr_t base = MMIOBASE;

	// Reserve size bytes of virtual memory starting at base and
	// map physical pages [pa,pa+size) to virtual addresses
	// [base,base+size).  Since this is device memory and not
	// regular DRAM, you'll have to tell the CPU that it isn't
	// safe to cache access to this memory.  Luckily, the page
	// tables provide bits for this purpose; simply create the
	// mapping with PTE_PCD|PTE_PWT (cache-disable and
	// write-through) in addition to PTE_W.  (If you're interested
	// in more details on this, see section 10.5 of IA32 volume
	// 3A.)
	//
	// Be sure to round size up to a multiple of PGSIZE and to
	// handle if this reservation would overflow MMIOLIM (it's
	// okay to simply panic if this happens).
	//
	// Hint: The staff solution uses boot_map_region.
	//
	// Your code here:
	uintptr_t start = base;
	base += ROUNDUP(size, PGSIZE);
	boot_map_region(kern_pgdir, start, ROUNDUP(size, PGSIZE), pa, PTE_PCD|PTE_PWT|PTE_W);
	if (base > MMIOLIM) {
		panic("mmio_map_region overflows MMIOLIM");
	}
	return (void*)start;
}
```

## 应用处理器引导程序

在启动 AP 之前，BSP 应首先收集有关多处理器系统的信息，例如 CPU 总数、它们的 APIC ID 和 LAPIC 单元的 MMIO 地址。

### 练习2：page_init

```c
    ...
	cprintf("Init pages alloc...\n");

	size_t i;
	for (i = 1; i < npages_basemem; i++) {
		if (i * PGSIZE != MPENTRY_PADDR){
			pages[i].pp_ref = 0;
			pages[i].pp_link = page_free_list;
			page_free_list = &pages[i];
		}
	}
    ....
```

## 每个 CPU 的状态和初始化

在编写多处理器操作系统时，区分每个处理器私有的每个 CPU 状态和整个系统共享的全局状态很重要。

以下是您应该注意的每个 CPU 状态：

- 每 CPU 内核堆栈
- 每 CPU TSS 和 TSS 描述符
- 每个 CPU 当前环境指针
- 每个 CPU 系统寄存器

### 练习3：mem_init_mp

```c
static void
mem_init_mp(void)
{
	// Map per-CPU stacks starting at KSTACKTOP, for up to 'NCPU' CPUs.
	//
	// For CPU i, use the physical memory that 'percpu_kstacks[i]' refers
	// to as its kernel stack. CPU i's kernel stack grows down from virtual
	// address kstacktop_i = KSTACKTOP - i * (KSTKSIZE + KSTKGAP), and is
	// divided into two pieces, just like the single stack you set up in
	// mem_init:
	//     * [kstacktop_i - KSTKSIZE, kstacktop_i)
	//          -- backed by physical memory
	//     * [kstacktop_i - (KSTKSIZE + KSTKGAP), kstacktop_i - KSTKSIZE)
	//          -- not backed; so if the kernel overflows its stack,
	//             it will fault rather than overwrite another CPU's stack.
	//             Known as a "guard page".
	//     Permissions: kernel RW, user NONE
	//
	// LAB 4: Your code here:
	for (int i = 0; i < NCPU; ++i) {
		uintptr_t kstacktop = KSTACKTOP - i * (KSTKSIZE + KSTKGAP);
		boot_map_region(kern_pgdir, kstacktop - KSTKSIZE, KSTKSIZE, PADDR(percpu_kstacks[i]), PTE_W);
	}
}
```

### 练习4：trap_init_percpu

```c
// Initialize and load the per-CPU TSS and IDT
void
trap_init_percpu(void)
{
	// The example code here sets up the Task State Segment (TSS) and
	// the TSS descriptor for CPU 0. But it is incorrect if we are
	// running on other CPUs because each CPU has its own kernel stack.
	// Fix the code so that it works for all CPUs.
	//
	// Hints:
	//   - The macro "thiscpu" always refers to the current CPU's
	//     struct CpuInfo;
	//   - The ID of the current CPU is given by cpunum() or
	//     thiscpu->cpu_id;
	//   - Use "thiscpu->cpu_ts" as the TSS for the current CPU,
	//     rather than the global "ts" variable;
	//   - Use gdt[(GD_TSS0 >> 3) + i] for CPU i's TSS descriptor;
	//   - You mapped the per-CPU kernel stacks in mem_init_mp()
	//   - Initialize cpu_ts.ts_iomb to prevent unauthorized environments
	//     from doing IO (0 is not the correct value!)
	//
	// ltr sets a 'busy' flag in the TSS selector, so if you
	// accidentally load the same TSS on more than one CPU, you'll
	// get a triple fault.  If you set up an individual CPU's TSS
	// wrong, you may not get a fault until you try to return from
	// user space on that CPU.
	//
	// LAB 4: Your code here:

	// Setup a TSS so that we get the right stack
	// when we trap to the kernel.
	thiscpu->cpu_ts.ts_esp0 = (uintptr_t)percpu_kstacks[cpunum()];
	thiscpu->cpu_ts.ts_ss0 = GD_KD;
	thiscpu->cpu_ts.ts_iomb = sizeof(struct Taskstate);

	// Initialize the TSS slot of the gdt.
	gdt[(GD_TSS0 >> 3) + thiscpu->cpu_id] = SEG16(STS_T32A, (uint32_t) (&thiscpu->cpu_ts),
					sizeof(struct Taskstate) - 1, 0);
	gdt[(GD_TSS0 >> 3) + thiscpu->cpu_id].sd_s = 0;

	// Load the TSS selector (like other segment selectors, the
	// bottom three bits are special; we leave them 0)
	ltr(GD_TSS0 + (thiscpu->cpu_id << 3));

	// Load the IDT
	lidt(&idt_pd);
}
```

## 锁

在让 AP 更进一步之前，我们需要首先解决多个 CPU 同时运行内核代码时的竞争条件。实现这一点的最简单方法是使用大内核锁。大内核锁是一个全局锁，每当环境进入内核模式时都会持有，并在环境返回用户模式时释放。在这个模型中，用户态的环境可以在任何可用的 CPU 上并发运行，但内核态下只能运行一个环境；任何其他尝试进入内核模式的环境都被迫等待。

kern/spinlock.h声明了大内核锁，即 kernel_lock. 它还提供lock_kernel() 和unlock_kernel()，用于获取和释放锁。您应该在四个位置应用大内核锁：

- 在 中i386_init()，在 BSP 唤醒其他 CPU 之前获取锁。
- 在mp_main()，初始化AP后获取锁，然后调用sched_yield()在该AP上启动运行环境。
- 在trap()，从用户模式被困时获取锁。要确定陷阱发生在用户模式还是内核模式，请检查tf_cs.
- 在env_run()，在 切换到用户模式之前释放锁定。不要太早或太晚这样做，否则你会遇到竞争或僵局。

### 练习 5： 通过在适当的位置调用lock_kernel()和unlock_kernel()，如上所述应用大内核锁

（照上面写就行）

## 循环调度

您在本实验中的下一个任务是更改 JOS 内核，以便它可以以“循环”方式在多个环境之间交替。

### 练习 6

- sched_yield()

```c
// Choose a user environment to run and run it.
void
sched_yield(void)
{
	struct Env *idle;
	struct Env *cur_env = curenv;
	//cprintf("sched_yield cur_env: %08x\n",cur_env? cur_env->env_id:0);

	// Implement simple round-robin scheduling.
	//
	// Search through 'envs' for an ENV_RUNNABLE environment in
	// circular fashion starting just after the env this CPU was
	// last running.  Switch to the first such environment found.
	//
	// If no envs are runnable, but the environment previously
	// running on this CPU is still ENV_RUNNING, it's okay to
	// choose that environment.
	//
	// Never choose an environment that's currently running on
	// another CPU (env_status == ENV_RUNNING). If there are
	// no runnable environments, simply drop through to the code
	// below to halt the cpu.

	// LAB 4: Your code here.
	if (cur_env) {
		for (int i = ENVX(cur_env->env_id) + 1; i < NENV; i++) {
			if (envs[i].env_status == ENV_RUNNABLE) {
				env_run(&envs[i]);
			}
		}
		for (int i = 0; i < ENVX(cur_env->env_id); i++) {
			if (envs[i].env_status == ENV_RUNNABLE) {
				env_run(&envs[i]);
			}
		}
		if (cur_env->env_status == ENV_RUNNING) {
			env_run(cur_env);
		}
	} else {
		for (int i = 0; i < NENV; i++) {
			if (envs[i].env_status == ENV_RUNNABLE) {
				env_run(&envs[i]);
			}
		}
	}

	// sched_halt never returns
	sched_halt();
}
```

## 创建环境的系统调用

尽管您的内核现在能够在多个用户级环境之间运行和切换，但它仍然仅限于内核最初设置的运行环境。您现在将实现必要的 JOS 系统调用，以允许用户环境创建和启动其他新用户环境。

- sys_exofork：

    这个系统调用创建了一个几乎空白的新环境：在其地址空间的用户部分没有映射任何内容，并且它是不可运行的。新环境将在sys_exofork调用时与父环境具有相同的注册状态。在父级中，sys_exofork 将返回envid_t新创建的环境（如果环境分配失败，则返回负错误代码）。然而，sys_exofork在子进程中，它将返回 0。（由于子进程一开始被标记为不可运行，在父进程通过使用...标记子进程可运行来明确允许之前， 它实际上不会在子进程中返回。）

- sys_env_set_status：

    将指定环境的状态设置为ENV_RUNNABLE或ENV_NOT_RUNNABLE。这个系统调用通常用于标记一个新环境准备好运行，一旦它的地址空间和寄存器状态已经完全初始化。
- sys_page_alloc：
  
    分配一页物理内存并将其映射到给定环境地址空间中的给定虚拟地址。
    
- sys_page_map：
  
  将页面映射（不是页面的内容！）从一个环境的地址空间复制到另一个环境，留下内存共享安排，以便新旧映射都引用物理内存的同一页面。
  
- sys_page_unmap：

    取消映射在给定环境中的给定虚拟地址上映射的页面。

### 练习 7 在kern/syscall.c 中实现上述系统调用

```c
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

```

```c

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
```

```c

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
```

```c

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

```

```c

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

```