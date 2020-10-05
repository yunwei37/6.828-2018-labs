#! https://zhuanlan.zhihu.com/p/258939864
# MIT 6.828 操作系统工程 lab2 通关指南

>这篇是我自己探索实现 MIT 6.828 lab2 的笔记记录，会包含一部分代码注释和要求的翻译记录，以及踩过的坑/个人的解决方案

这里是我实现的完整代码仓库，也包含其他笔记等等：[https://github.com/yunwei37/6.828-2018-labs](https://github.com/yunwei37/6.828-2018-labs)

## 目录

<!-- TOC -->

- [MIT 6.828 操作系统工程 lab2 通关指南](#mit-6828-操作系统工程-lab2-通关指南)
	- [目录](#目录)
	- [第1部分：物理页面管理](#第1部分物理页面管理)
	- [第2部分：虚拟内存](#第2部分虚拟内存)
		- [虚拟，线性和物理地址](#虚拟线性和物理地址)
		- [引用计数](#引用计数)
		- [页表管理](#页表管理)
	- [第3部分：内核地址空间](#第3部分内核地址空间)
		- [权限和故障隔离](#权限和故障隔离)
		- [初始化内核地址空间](#初始化内核地址空间)
		- [地址空间布局选择](#地址空间布局选择)

<!-- /TOC -->

lab2 主要是关于内存管理的部分。内存管理包含两个组件：

- 内核的物理内存分配器：
  - 任务将是维护数据结构，该数据结构记录哪些物理页是空闲的，哪些是已分配的，以及多少进程正在共享每个分配的页。您还将编写例程来分配和释放内存页面。
- 虚拟内存
  - 您将根据我们提供的规范修改JOS以设置MMU的页表。


实验2包含以下新的源文件:

- inc/memlayout.h：描述了必须通过修改pmap.c来实现的虚拟地址空间的布局
- kern/pmap.c
- kern/pmap.h：PageInfo 用于跟踪哪些物理内存页可用的结构
- kern/kclock.h：操纵PC的电池供电时钟和CMOS RAM硬件，其中BIOS记录PC包含的物理内存量。
- kern/kclock.c

## 第1部分：物理页面管理

> 操作系统必须跟踪物理RAM的哪些部分空闲以及当前正在使用哪些部分，现在，您将编写物理页面分配器：它使用struct PageInfo对象的链接列表（与xv6不同，它们不嵌入在空闲页面中）跟踪哪些页面是空闲的，每个对象都对应于一个物理页面。

那么接下来就进入练习1的内容，我们可以先去看看需要做什么再回过来看代码：

练习1：在kern/pmap.c文件中，为以下功能实现代码:

- boot_alloc()
- mem_init()
- page_init()
- page_alloc()
- page_free()

这两个部分的测试函数在 check_page_free_list() 和  check_page_alloc()，也许可以添加一点 assert() 进行验证。

这部分需要做不少了解性的工作，但我觉得帮助比较大的方向还是直接去看相应函数里面的提示和测试用例；毕竟这些写的都已经比较详细了：

先从  boot_alloc() 开始。它是一个简单的物理内存分配器，仅在JOS设置其虚拟内存系统时使用。这里的分配地址，实际上就是简单的更新地址值，在看完注释之后应该很快就可以开始写：

```c
static void *
boot_alloc(uint32_t n)
{
	static char *nextfree;
	char *result;

	if (!nextfree) {
		extern char end[];
		nextfree = ROUNDUP((char *) end, PGSIZE);
	}

	if (n == 0) {
		return nextfree;
	} else if (n > 0) {
		result = nextfree;
		nextfree += ROUNDUP(n, PGSIZE);
		return result;
	}

	return NULL;
}

```

mem_init() 需要我们设置一个两层的页表，实际上这部分的内容不仅仅只包含在物理页面分配中，也包含了lab2余下的部分。我们可以先取消掉 panic 试试看：

很不幸，立马爆个 `Triple fault. ` 出来了...不过还是能得到一部分有用的信息，它可以告诉我们有多少物理内存空间：

```
Physical memory: 131072K available, base = 640K, extended = 130432K
```

接下来我们就继续把这个 panic 取消掉，然后一步步调试。

根据 mem_init() 里面的下一步描述，我们需要使用 boot_alloc 分配一个 struct PageInfo 的数组，这一部分应该也很简单：

```c
pages = (struct PageInfo*)boot_alloc(npages * sizeof(struct PageInfo));
memset(pages, 0, npages * sizeof(struct PageInfo));
```

（注意看对应英文的注释）

下一步就是 page_init() 函数，这一步我觉得它的注释比较混乱，但实际上需要注意的部分就是各个内存片段节点之间的顺序：

我们可以用打印log的方式打印出相关信息查看：

- npages: 32768
- npages_basemem: 160
- PGNUM(PADDR(kern_pgdir)): 279 
- PGNUM(boot_alloc(0)): 344
- PGNUM((void*)EXTPHYSMEM): 256
- PGNUM((void*)IOPHYSMEM): 160

这几个之间一部分是IO的空洞，一部分是内核代码和我们分配记录的page信息，这部分要注意留空不分配；再仔细观察一下 check_page_free_list，尝试测试驱动开发：

（余下的一部分可用的工具类函数记得查询一下相关头文件）

```c
void
page_init(void)
{
	size_t i;
	for (i = 1; i < PGNUM(IOPHYSMEM); i++) {
		pages[i].pp_ref = 0;
		pages[i].pp_link = page_free_list;
		page_free_list = &pages[i];
	}

	for (i = PGNUM(PADDR(boot_alloc(0))); i < npages; i++) {
		pages[i].pp_ref = 0;
		pages[i].pp_link = page_free_list;
		page_free_list = &pages[i];
	}
	
}
```

接下来的两个函数就很简单了，无非就是链表头结点的插入和删除而已，把它当做一个栈来用：

page_alloc()

```c
struct PageInfo *
page_alloc(int alloc_flags)
{
	// Fill this function in
	struct PageInfo *result;
	if (page_free_list){
		result = page_free_list;
		page_free_list = page_free_list->pp_link;
		if (alloc_flags & ALLOC_ZERO) {
			memset(page2kva(result),0,PGSIZE);
		}
		result->pp_link = NULL;
		result->pp_ref = 0;
		return result;
	} else {
		return NULL;
	}
}
```

page_free()

```c
void
page_free(struct PageInfo *pp)
{
	assert(pp->pp_ref == 0);
	assert(!pp->pp_link);

	pp->pp_link = page_free_list;
	page_free_list = pp;
}
```

然后目前就可以通过这两个测试用例啦！

## 第2部分：虚拟内存

这些介绍的部分观看一下，可能对实验会有比较大的帮助；如果理解了具体的页表机制，那么实现起来也是一件很简单的事情了。

>在进行其他操作之前，请熟悉x86的保护模式内存管理架构：`分段`和`页面转换`（不过我没看）。练习二希望你去阅读一下相关内容。

### 虚拟，线性和物理地址

>在x86术语中，`虚拟地址`由段选择器和段内的偏移量组成: 一个线性地址 是段转换之后但页面翻译之前得到的东西，物理地址是转换完全之后得到的。

可以参考下面这张图：

```
           Selector  +--------------+         +-----------+
          ---------->|              |         |           |
                     | Segmentation |         |  Paging   |
Software             |              |-------->|           |---------->  RAM
            Offset   |  Mechanism   |         | Mechanism |
          ---------->|              |         |           |
                     +--------------+         +-----------+
            Virtual                   Linear                Physical
```

> 在 boot/boot.S中，我们安装了全局描述符表（GDT），该表通过将所有段基址设置为0并将限制设置为来有效地禁用段转换0xffffffff。因此，“选择器”无效，线性地址始终等于虚拟地址的偏移量。在实验3中，我们将需要与分段进行更多的交互才能设置特权级别，但是对于 lab2 内存转换，我们可以在整个JOS实验中忽略分段，而只关注页面转换。

练习3提供了一些帮助性的工具：

- xp 命令在 qemu 里面检查物理内存；
- x 在 gdb 里面可以看虚拟内存；
- info pg 看页表；
- info mem 映射了哪些虚拟地址范围以及具有哪些权限的概述。

（其实这部分基本的实验还是比较简单的，这些工具我都没用到）

这一点很关键：

>从CPU上执行的代码开始，一旦进入保护模式（我们在boot/boot.S中完成的第一件事），就无法直接使用线性或物理地址。 所有内存引用都被解释为虚拟地址，并由MMU转换，这意味着C中的所有指针都是虚拟地址。

另外需要注意的一点就是虚拟地址和物理地址的类型，我们只能对虚拟地址解引用，对物理地址解引用会得到未定义的结果：

 C type |	Address type 
 -|-
 T*  |	Virtual
uintptr_t | 	Virtual
physaddr_t  |	Physical

> Question: 这里的结果应该是虚拟地址

如果这里我们要将一个虚拟地址和物理地址相互转换的话，可以使用定义在 `pmap.h` 里面的：

- `KADDR(pa)`
- `PADDR(va)`

### 引用计数

这部分可供参考：

> 在未来的 lab 中，您经常会在多个虚拟地址上同时（或在多个环境的地址空间中）映射相同的物理页面。您将在 struct PageInfo 与物理页面相对应的字段 pp_ref 中保留对每个物理页面的引用数。当物理页面的计数变为零时，该页面可以被释放，因为不再使用该页面。

> 使用 page_alloc 时要小心。它返回的页面将始终具有0的引用计数，因此一旦对返回的页面进行了某些操作（例如将其插入到页面表中），则应将pp_ref递增。

### 页表管理

到了最关键的一步啦！

注意：这部分的练习还是需要对于页表的原理和具体存储的信息有一个比较深入的了解（可以参考 mmu.h ），以及要用好之前写过的代码，不必重复造轮子；还有注意阅读注释提示。

练习4：

在文件 kern/pmap.c 中，您必须实现以下功能的代码：

- pgdir_walk（）
- page_lookup（）
- page_remove（）
- page_insert（）
- boot_map_region（）

mem_init() 调用 check_page()，测试您的页表管理例程。

这些函数互相之间是有依赖关系的，最好按照上面的顺序实现。我们来一步步看看：

首先是 pgdir_walk（）:

> 给定“ pgdir”（指向页面目录的指针），pgdir_walk返回指向线性地址“ va”的页表项（PTE）的指针。这需要遍历两级页面表结构。

这个实现几乎可以完全参考 xv6 里面的同名函数，跟着提示一步步来：

```c
pte_t *
pgdir_walk(pde_t *pgdir, const void *va, int create)
{
	pde_t *pde = &pgdir[PDX(va)];
	pte_t *pgtab;

	if (*pde & PTE_P){
		pgtab = (pte_t *)KADDR(PTE_ADDR(*pde));
	} else {
		if (!create)
			return NULL;
		struct PageInfo * newp = page_alloc(ALLOC_ZERO);
		if (!newp)
			return NULL;
		newp->pp_ref++;
		*pde = page2pa(newp) | PTE_P | PTE_W | PTE_U;
		pgtab = page2kva(newp);
	}
	return &pgtab[PTX(va)];
}
```

然后是 page_lookup（）：

> 返回映射到虚拟地址“ va”的页面。如果pte_store不为零，那么我们将此页面的 pte_t 地址存储在其中。

注意，题目提示使用 pgdir_walk 和 pa2page：

```c
struct PageInfo *
page_lookup(pde_t *pgdir, void *va, pte_t **pte_store)
{
	// Fill this function in
	pte_t* pte = pgdir_walk(pgdir, va, 0);
	if (!pte)
		return NULL;
	if (pte_store) {
		*pte_store = pte;
	}
	if(*pte & PTE_P){
		return pa2page(PTE_ADDR(*pte));
	} else {
		return NULL;
	}
}
```

page_remove（）:

> 在虚拟地址“ va”处取消对物理页面的映射。如果该地址上没有物理页面，则不执行任何操作。

题目提示使用 page_lookup 和 tlb_invalidate，page_decref：

```c
void
page_remove(pde_t *pgdir, void *va)
{
	// Fill this function in
	pte_t *pte_store;
	struct PageInfo *p = page_lookup(pgdir, va, &pte_store);
	if(!p)
		return;
	assert(p->pp_ref > 0);
	assert(*pte_store & PTE_P);
	page_decref(p);
	*pte_store = 0;
	tlb_invalidate(pgdir, va);
}

```

page_insert（）：

> 将物理页面“ pp”映射到虚拟地址“ va”。页表条目的权限（低12位）应该设置为'perm | PTE_P'。

要处理重新插入的 Corner-case， 只要在删除前增加引用计数防止页面被释放即可；

用好之前写过的函数很关键，可以用 pgdir_walk page_remove page2pa：

```c
int
page_insert(pde_t *pgdir, struct PageInfo *pp, void *va, int perm)
{
	// Fill this function in
	pte_t* pte = pgdir_walk(pgdir, va, 1);
	if (!pte)
		return -E_NO_MEM;
	pp->pp_ref++;
	if (*pte & PTE_P)
		page_remove(pgdir, va);
	*pte = page2pa(pp) | PTE_P | perm;
	return 0;
}
```

boot_map_region（）

> 在以pgdir为根的页表中将虚拟地址空间的[va，va + size）映射到物理[pa，pa + size）。 大小是PGSIZE的倍数，并且va和pa都是页面对齐的。

这个很简单，就是做个映射：

```c
static void
boot_map_region(pde_t *pgdir, uintptr_t va, size_t size, physaddr_t pa, int perm)
{
	// Fill this function in
	for(size_t i = 0; i < size; i += PGSIZE){
		tlb_invalidate(pgdir, (void *)va + i);
		pte_t* pte = pgdir_walk(pgdir, (const void *)va + i, 1);
		*pte = (pa + i) | PTE_P | perm;
	}
}
```

代码都很短，这样就结束啦x

## 第3部分：内核地址空间

> JOS将处理器的32位线性地址空间分为两部分。我们将在实验3中开始加载和运行的用户环境（进程）将控制下部的布局和内容，而内核始终保持对上部的完全控制。

这部分可以参考 memlayout.h 的JOS内存布局图：

```c

/*
 * Virtual memory map:                                Permissions
 *                                                    kernel/user
 *
 *    4 Gig -------->  +------------------------------+
 *                     |                              | RW/--
 *                     ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *                     :              .               :
 *                     :              .               :
 *                     :              .               :
 *                     |~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~| RW/--
 *                     |                              | RW/--
 *                     |   Remapped Physical Memory   | RW/--
 *                     |                              | RW/--
 *    KERNBASE, ---->  +------------------------------+ 0xf0000000      --+
 *    KSTACKTOP        |     CPU0's Kernel Stack      | RW/--  KSTKSIZE   |
 *                     | - - - - - - - - - - - - - - -|                   |
 *                     |      Invalid Memory (*)      | --/--  KSTKGAP    |
 *                     +------------------------------+                   |
 *                     |     CPU1's Kernel Stack      | RW/--  KSTKSIZE   |
 *                     | - - - - - - - - - - - - - - -|                 PTSIZE
 *                     |      Invalid Memory (*)      | --/--  KSTKGAP    |
 *                     +------------------------------+                   |
 *                     :              .               :                   |
 *                     :              .               :                   |
 *    MMIOLIM ------>  +------------------------------+ 0xefc00000      --+
 *                     |       Memory-mapped I/O      | RW/--  PTSIZE
 * ULIM, MMIOBASE -->  +------------------------------+ 0xef800000
 *                     |  Cur. Page Table (User R-)   | R-/R-  PTSIZE
 *    UVPT      ---->  +------------------------------+ 0xef400000
 *                     |          RO PAGES            | R-/R-  PTSIZE
 *    UPAGES    ---->  +------------------------------+ 0xef000000
 *                     |           RO ENVS            | R-/R-  PTSIZE
 * UTOP,UENVS ------>  +------------------------------+ 0xeec00000
 * UXSTACKTOP -/       |     User Exception Stack     | RW/RW  PGSIZE
 *                     +------------------------------+ 0xeebff000
 *                     |       Empty Memory (*)       | --/--  PGSIZE
 *    USTACKTOP  --->  +------------------------------+ 0xeebfe000
 *                     |      Normal User Stack       | RW/RW  PGSIZE
 *                     +------------------------------+ 0xeebfd000
 *                     |                              |
 *                     |                              |
 *                     ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *                     .                              .
 *                     .                              .
 *                     .                              .
 *                     |~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~|
 *                     |     Program Data & Heap      |
 *    UTEXT -------->  +------------------------------+ 0x00800000
 *    PFTEMP ------->  |       Empty Memory (*)       |        PTSIZE
 *                     |                              |
 *    UTEMP -------->  +------------------------------+ 0x00400000      --+
 *                     |       Empty Memory (*)       |                   |
 *                     | - - - - - - - - - - - - - - -|                   |
 *                     |  User STAB Data (optional)   |                 PTSIZE
 *    USTABDATA ---->  +------------------------------+ 0x00200000        |
 *                     |       Empty Memory (*)       |                   |
 *    0 ------------>  +------------------------------+                 --+
 *
 * (*) Note: The kernel ensures that "Invalid Memory" is *never* mapped.
 *     "Empty Memory" is normally unmapped, but user programs may map pages
 *     there if desired.  JOS user programs map pages temporarily at UTEMP.
 */

```

### 权限和故障隔离

> 由于内核和用户内存都存在于每个环境的地址空间中，因此我们将不得不使用x86页表中的权限位来允许用户代码仅访问地址空间的用户部分。否则，用户代码中的错误可能会覆盖内核数据，从而导致崩溃或更微妙的故障。用户代码也可能能够窃取其他环境的私有数据。请注意，可写权限位（PTE_W）同时影响用户代码和内核代码！

> 用户环境在 ULIM 之上将不具有上述任何内存的权限，而内核将能够读写此内存。对于地址范围 [UTOP,ULIM)，内核和用户环境都具有相同的权限：它们可以读取但不能写入该地址范围。该地址范围用于向用户环境公开某些内核数据结构。最后，下面的地址空间 UTOP供用户环境使用；用户环境将设置访问该内存的权限。

### 初始化内核地址空间

除了挑战之外最后一个要写代码的部分：

> Exercise 5. Fill in the missing code in mem_init() after the call to check_page().

还是一样，根据测试和注释驱动：

```c
void
mem_init(void)
{
	uint32_t cr0;
	size_t n;

	i386_detect_memory();

	kern_pgdir = (pde_t *) boot_alloc(PGSIZE);
	memset(kern_pgdir, 0, PGSIZE);

	kern_pgdir[PDX(UVPT)] = PADDR(kern_pgdir) | PTE_U | PTE_P;

	// Allocate an array of npages 'struct PageInfo's and store it in 'pages'.
	pages = (struct PageInfo*)boot_alloc(npages * sizeof(struct PageInfo));
	memset(pages, 0, npages * sizeof(struct PageInfo));

	page_init();

	check_page_free_list(1);
	check_page_alloc();
	check_page();

	// Map 'pages' read-only by the user at linear address UPAGES
	boot_map_region(kern_pgdir, UPAGES, npages * sizeof(struct PageInfo), PADDR(pages), PTE_U);

	// Use the physical memory that 'bootstack' refers to as the kernel
	boot_map_region(kern_pgdir, KSTACKTOP - KSTKSIZE, KSTKSIZE, PADDR(bootstack), PTE_W);

	// Map all of physical memory at KERNBASE.
	boot_map_region(kern_pgdir, KERNBASE, 0xffffffff - KERNBASE, 0, PTE_W);

	check_kern_pgdir();

	lcr3(PADDR(kern_pgdir));

	check_page_free_list(0);

	cr0 = rcr0();
	cr0 |= CR0_PE|CR0_PG|CR0_AM|CR0_WP|CR0_NE|CR0_MP;
	cr0 &= ~(CR0_TS|CR0_EM);
	lcr0(cr0);

	check_page_installed_pgdir();
}
```

Question:

> 2. 此时，页面目录中的哪些条目（行）已填写？他们映射什么地址，并指向何处？

- 顶端这些部分应该是映射到物理内存的顶部，即内核地址空间；底下几行还没写
- 应该参考一下上面那个 mem_init 函数就知道啦

> 3. 我们已经将内核和用户环境放置在相同的地址空间中。为什么用户程序无法读取或写入内核的内存？哪些特定机制可以保护内核内存？

这部分使用的就是特权位，就是我们设置的 PTE_U 

> 4. 此操作系统可以支持的最大物理内存量是多少？为什么？

可以看log：Physical memory: 131072K available, base = 640K, extended = 130432K

> 5. 如果我们实际拥有最大的物理内存量，那么管理内存有多少空间开销？这种开销如何减少？

主要是物理内存的页面数据结构和页表。页表可以用 4Mb 的巨页机制完成，但这里没做。参考第一个挑战。

> 6. 重新访问kern / entry.S和 kern / entrypgdir.c中的页表设置。打开分页后，EIP仍然是一个很小的数字（略大于1MB）。在什么时候我们要过渡到在KERNBASE之上的EIP上运行？在启用分页和开始在高于KERNBASE的EIP之间运行之间，有什么可能使我们能够以较低的EIP继续执行？为什么需要这种过渡？

这部分应该是回顾lab1的知识，有一个临时性的页表；

### 地址空间布局选择

> 我们在JOS中使用的地址空间布局不是唯一可能的一种。操作系统可能会将内核映射到低线性地址，而将线性地址空间的上部留给用户进程。x86内核通常不采用这种方法，因为x86的一种向后兼容模式（称为虚拟8086模式）已在处理器中“硬接线”以使用线性地址空间的底部.

挑战就暂时没做啦qwq（因为我没空，主要学 6.828 还是为了参考一下已有的实现的）