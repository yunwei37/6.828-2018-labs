#! https://zhuanlan.zhihu.com/p/260084031
# MIT 6.828 操作系统工程 lab3A:用户环境和异常处理

>这篇是我自己探索实现 MIT 6.828 lab3A 的笔记记录，会包含一部分代码注释和要求的翻译记录，以及踩过的坑/个人的解决方案

这里是我实现的完整代码仓库，也包含其他笔记等等：[https://github.com/yunwei37/6.828-2018-labs](https://github.com/yunwei37/6.828-2018-labs)

# 目录

<!-- TOC -->

- [MIT 6.828 操作系统工程 lab3A:用户环境和异常处理](#mit-6828-操作系统工程-lab3a用户环境和异常处理)
- [目录](#目录)
	- [记录一个奇怪的问题](#记录一个奇怪的问题)
	- [A部分：用户环境和异常处理](#a部分用户环境和异常处理)
		- [环境的状态](#环境的状态)
		- [分配环境数组](#分配环境数组)
		- [创建和运行环境](#创建和运行环境)
		- [处理中断和异常](#处理中断和异常)
		- [受保护控制转移的基础](#受保护控制转移的基础)
		- [异常和中断的类型](#异常和中断的类型)
		- [设置IDT](#设置idt)

<!-- /TOC -->

在本实验中，您将实现运行受保护的用户模式环境（即“进程”）所需的基本内核功能。您将增强JOS内核，以设置数据结构来跟踪用户环境，创建单个用户环境，将程序映像加载到其中并开始运行。您还将使JOS内核能够处理用户环境发出的任何系统调用并处理它引起的任何其他异常。

实验3包含新的源文件，可以先浏览一下:

- `inc/ env.h`        用户态环境的公共定义
    - `trap.h`        陷阱处理的公共定义
    - `syscall.h`     从用户态到内核的系统调用的公共定义
    - `lib.h`         用户态支持库的公共定义
- `kern/ env.h`       用户态环境的内核专用定义
    - `env.c`       实现用户态环境的内核代码
    - `trap.h`      内核专用陷阱处理定义
    - `trap.c`      陷阱处理代码
    - `trapentry.S` 汇编语言陷阱处理程序入口点
    - `syscall.h`   系统调用处理的内核专用定义
    - `syscall.c`   系统调用实现代码
- `lib/ Makefrag`    Makefile片段，用于构建用户态库
    - `entry.S`     用户环境的汇编语言入口点
    - `libmain.c`   从entry.S调用的用户态库设置代码
    - `syscall.c`   用户态系统调用存根函数
    - `console.c`   putchar和getchar的用户模式实现 ，提供控制台I/O
    - `exit.c`      exit的用户模式实现
    - `panic.c`     用户模式panic的实现
- `user/ 	*`      各种测试程序来检查内核 lab3 代码

课程主页也提到了内联汇编。

## 记录一个奇怪的问题

在开始阶段我把代码 merge 到 lab3 分支中，开始运行的时候，发现会出现：

`kernel panic at kern/pmap.c:154: PADDR called with invalid kva 00000000`

经过打 log，发现在 memset 之后，会把 kern_pgdir 的值覆盖掉；

```c
kern_pgdir = (pde_t *) boot_alloc(PGSIZE);
memset(kern_pgdir, 0, PGSIZE);
```

继续打log：`cprintf("%x\n",&kern_pgdir);` 发现 kern_pgdir 这个变量的地址是在 f018f00c...

然而 `extern char end[];` 的值是 f018f000....所以就会出现类似的问题，就是 `memset` 把 `kern_pgdir` 全局变量的值覆盖掉了；继续深入探讨，发现在 pmap.c 中如果是有 static 修饰的变量值是低于 end 的，如果没有修饰的话值是高于 end 的...在其他文件里面测试类似的变量也能得出类似的结果

但是从Git记录也可以看出，我们并没有修改过 kern/kernel.ld ，前面几个lab的实现也是正常的；

所以为什么会是这样呢（我也不懂）

不过对于 `kern/kernel.ld` 来说，我们可以看到文件记录中上一个版本有根据新版 GCC 做一些适配，因此修改了 kernel.ld，这里尝试回退一下：

```ld
	.bss : {
		PROVIDE(edata = .);
		*(.bss)
		BYTE(0)
	}

	PROVIDE(end = .);
```

看起来目前是可以用了。

## A部分：用户环境和异常处理

> 新的包含文件 inc/env.h 包含JOS中用户环境的基本定义，内核使用Env数据结构来跟踪每个用户环境。在本实验中，您最初将仅创建一个环境，但是您将需要设计JOS内核以支持多个环境；实验4将允许用户环境进入fork其他环境，从而利用此功能。

内核维护了与环境有关的三个主要全局变量：

- struct Env *envs = NULL;		// 所有环境
- struct Env *curenv = NULL;		// 当前环境
- static struct Env *env_free_list;	// 可用环境链表

下面是一些细节上的描述：

- 一旦JOS启动并运行，envs指针将指向Env代表系统中所有环境的结构数组。JOS内核将最多支持 NENV 个活动环境。
- JOS内核将所有非活动Env结构保留在env_free_list上。这种设计可以轻松分配和释放环境，因为只需将它们添加到空闲列表中或从空闲列表中删除.
- 内核使用该curenv符号在任何给定时间跟踪当前正在执行的环境。在启动期间，在运行第一个环境之前， curenv初始设置为NULL。

### 环境的状态

该 Env 结构在 `inc/env.h` 中定义如下：

```c
struct Env {
	struct Trapframe env_tf;	// Saved registers
	struct Env *env_link;		// Next free Env
	envid_t env_id;			// Unique environment identifier
	envid_t env_parent_id;		// env_id of this env's parent
	enum EnvType env_type;		// Indicates special system environments
	unsigned env_status;		// Status of the environment
	uint32_t env_runs;		// Number of times environment has run

	// Address space
	pde_t *env_pgdir;		// Kernel virtual address of page dir
};
```

以下是这些 Env 字段的用途：

- `env_tf：`

  在 `inc/trap.h` 中定义的该结构在该环境不运行时（即，在内核或其他环境正在运行时）保存该环境的已保存寄存器值。从用户模式切换到内核模式时，内核会保存这些设置，以便以后可以从中断的位置恢复环境。

- `env_link：`

  这是下一个链接Env上 env_free_list。 env_free_list指向列表中的第一个可用环境。

- `env_id：`

  内核在此处存储一个值，该值唯一地标识当前正在使用此Env结构的环境。

- `env_parent_id：`

  内核在此处存储env_id 创建该环境的环境的。这样，环境可以形成“家谱”，这对于制定允许哪些环境对谁做事的安全性决策很有用。

- `env_type：`

  这用于区分特殊环境。对于大多数环境，它将为ENV_TYPE_USER。在以后的实验中，我们将为特殊的系统服务环境引入更多类型。

- `env_status：`
  
  此变量保存以下值之一：

  - ENV_FREE：表示该Env结构处于非活动状态，因此位于 env_free_list 上。
  - ENV_RUNNABLE：指示该Env结构表示正在等待在处理器上运行的环境。
  - ENV_RUNNING：指示该Env结构代表当前正在运行的环境。
  - ENV_NOT_RUNNABLE：指示该Env结构表示当前处于活动状态的环境，但是当前尚未准备好运行：例如，它正在等待来自另一个环境的进程间通信（IPC）。
  - ENV_DYING：指示该Env结构表示僵尸环境。僵尸环境在下一次捕获到内核时将被释放。


- `env_pgdir：`
  
  此变量保存 此环境的页目录的内核虚拟地址。

> 像Unix进程一样，JOS环境将 “线程” 和 “地址空间” 的概念结合在一起。线程主要由保存的寄存器（env_tf字段）定义，地址空间由 env_pgdir 指向的页目录和页表定义。要运行环境，内核必须设置CPU、保存的寄存器和相应的地址空间。
> 
>我们 struct Env 类似于 xv6 的 struct proc。这两个结构都在一个结构中保留环境（即进程）的用户模式寄存器状态Trapframe 。在JOS中，各个环境不像xv6中的进程那样具有自己的内核堆栈。只能有一个JOS环境在内核中被激活，所以 JOS 只需要有一个 单一的内核堆栈。

### 分配环境数组

现在，您将需要进一步修改 mem_init() 以分配一个相似结构数组envs。这也是练习1的内容。

开始写代码：这部分也是和 lab2 类似，根据提示即可：

```c
...
envs = (struct Env*)boot_alloc(NENV * sizeof(struct Env));
...
boot_map_region(kern_pgdir, UENVS, NENV * sizeof(struct Env), PADDR(envs), PTE_U);
...
```

这部分结束之后，就能看到出现: `kernel panic at kern/env.c:461: env_run not yet implemented`

### 创建和运行环境

> 现在，您将在运行用户环境所需的kern / env.c中编写代码。因为我们还没有文件系统，所以我们将设置内核以加载嵌入在内核本身中的静态二进制映像。JOS将此二进制文件作为ELF可执行映像嵌入内核。
> 
> 在 kern/init.c 中的 i386_init() 中，可以看到在一个环境中执行这些二进制镜像的代码。但是，设置用户环境的关键功能还不完善。您将需要填写它们。

练习2. 在 env.c 中，完成对以下功能的编码：

- `env_init()`

    初始化数组 envs 中的所有 Env 结构并将它们添加到 env_free_list 中。还调用env_init_percpu，这会将分段硬件配置为具有特权级别0（内核）和特权级别3（用户）的单独段。

- `env_setup_vm()`

    为新环境分配页面目录，并初始化新环境的地址空间的内核部分。

- `region_alloc()`

    分配和映射环境的物理内存

- `load_icode()`

    您将需要像启动加载程序一样解析ELF二进制映像，并将其内容加载到新环境的用户地址空间中。

- `env_create()`

    使用 env_alloc 分配环境，然后调用load_icode将ELF二进制文件加载到其中。

- `env_run()`

    启动以用户模式运行的给定环境。

下面是代码的调用图，直到调用用户代码为止，可供参考：

- start (kern/entry.S)
- i386_init (kern/init.c)
  - cons_init
  - mem_init
  - env_init
  - trap_init (still incomplete at this point)
  - env_create
  - env_run
      - env_pop_tf

完成如果一切顺利，您的系统应进入用户空间并执行 hello二进制文件，直到使用该int指令进行系统调用为止。

这部分和 xv6 里面不太一样， xv6使用的是进程和线程的概念，所以大概不是很通用，不过还是有一点可以参考的。重要的还是理解执行环境这个概念，以及如何从内核态开始执行它的：

首先来看 `env_init()`，这个很简单，就是链表的插入，结合注释，注意顺序：

```c
void
env_init(void)
{
	// Set up envs array
	// LAB 3: Your code here.
	env_free_list = NULL;
	for(int i = NENV - 1; i >= 0; --i){
		envs[i].env_id = 0;
		envs[i].env_status = ENV_FREE;
		envs[i].env_link = env_free_list;
		env_free_list = &envs[i];
	}

	// Per-CPU part of the initialization
	env_init_percpu();
}

```

然后是 `env_setup_vm()`，只要 lab2 的设置是正确的，这里应该直接把内核的页表目录拿过来用就行：

```c
p->pp_ref++;
e->env_pgdir = page2kva(p);
memcpy(e->env_pgdir, kern_pgdir, PGSIZE);
```

`region_alloc()` 是一个给 `load_icode()` 用的辅助函数，这里和 lab2 的函数原理类似：

```c
void* end = ROUNDUP(va + len, PGSIZE);
for(void* i = ROUNDDOWN(va, PGSIZE); i < end; i+= PGSIZE) {
		struct PageInfo *p = NULL;
		assert(p = page_alloc(0));
		
		int result = page_insert(e->env_pgdir, p, i, PTE_W | PTE_U);
		assert(result >= 0);
}
```

`load_icode()` 稍微有点复杂，不过可以参考 xv6 里面加载 ELF 的相关函数，以及要了解一下 ELF 文件格式：

另外，这部分如果有一些实现不完善的地方并不会马上显现出来，但可能导致后续用户态的程序挂掉，所以如果之后有问题的的话可以再回过来看看。加载 ELF 的函数也可以最后写，先把下面两个写完；然后判断加载 ELF 基本正确的方式可以采用观察 triple fault 的时候 eip 的运行位置，如果是正确的话，对照反汇编的代码可以发现此时的 eip 应该正好是 trap 的代码位置。

```c
static void
load_icode(struct Env *e, uint8_t *binary)
{
	// LAB 3: Your code here.
	struct Elf *elf = (struct Elf *)binary;
	assert(elf->e_magic == ELF_MAGIC);

	struct Proghdr *ph, *eph;
	ph = (struct Proghdr *) ((uint8_t *) elf + elf->e_phoff);
	eph = ph + elf->e_phnum;
	for (; ph < eph; ph++){
    ph->p_pa, ph->p_offset, ph->p_flags);
		
		if(ph->p_type != ELF_PROG_LOAD)
			continue;
		assert(ph->p_memsz >= ph->p_filesz);
		
		region_alloc(e, (void*)ph->p_va, ph->p_memsz);
		lcr3(PADDR(e->env_pgdir));
		memset((void*)ph->p_va, 0, ph->p_memsz);
		memcpy((void*)ph->p_va, (void*)elf + ph->p_offset, ph->p_filesz);
		lcr3(PADDR(kern_pgdir));
	}

	// LAB 3: Your code here.
	region_alloc(e, (void*)USTACKTOP - PGSIZE, PGSIZE);
	e->env_tf.tf_esp = USTACKTOP;
	e->env_tf.tf_eip = elf->e_entry;
}
```

`env_create()` 很简单：

```c
void
env_create(uint8_t *binary, enum EnvType type)
{
	// LAB 3: Your code here.
	struct Env* newenv;
	env_alloc(&newenv, 0);
	load_icode(newenv, binary);
	newenv->env_type = type;
}
```

`env_run()` 也是，根据描述设置环境并调用 env_pop_tf 即可：

```c
void
env_run(struct Env *e)
{
	// LAB 3: Your code here.
	if (curenv) {
		curenv->env_status = ENV_RUNNABLE;
	}
	curenv = e;
	curenv->env_status = ENV_RUNNING;
	curenv->env_runs++;
	lcr3(PADDR(curenv->env_pgdir));

	// cprintf("start env_pop and running...\n");

	env_pop_tf(&curenv->env_tf);

	panic("env_run should not reach here");
}
```

这时运行 user_hello 的程序就会出现：

```
start env_pop and running...
EAX=00000000 EBX=00000000 ECX=0000000d EDX=eebfde88
ESI=00000000 EDI=00000000 EBP=eebfde60 ESP=eebfde54
EIP=00800bfe EFL=00000092 [--S-A--] CPL=3 II=0 A20=1 SMM=0 HLT=0
ES =0023 00000000 ffffffff 00cff300 DPL=3 DS   [-WA]
CS =001b 00000000 ffffffff 00cffa00 DPL=3 CS32 [-R-]
SS =0023 00000000 ffffffff 00cff300 DPL=3 DS   [-WA]
DS =0023 00000000 ffffffff 00cff300 DPL=3 DS   [-WA]
FS =0023 00000000 ffffffff 00cff300 DPL=3 DS   [-WA]
GS =0023 00000000 ffffffff 00cff300 DPL=3 DS   [-WA]
LDT=0000 00000000 00000000 00008200 DPL=0 LDT
TR =0028 f018fb80 00000067 00408900 DPL=0 TSS32-avl
GDT=     f011c300 0000002f
IDT=     f018f360 000007ff
CR0=80050033 CR2=00000000 CR3=003bc000 CR4=00000000
DR0=00000000 DR1=00000000 DR2=00000000 DR3=00000000 
DR6=ffff0ff0 DR7=00000400
EFER=0000000000000000
Triple fault.  Halting for inspection via QEMU monitor.
```

对照一下 eip:00800bfe 的汇编代码：

```s
sys_cputs(const char *s, size_t len)
{
  800be3:	f3 0f 1e fb          	endbr32 
  800be7:	55                   	push   %ebp
  800be8:	89 e5                	mov    %esp,%ebp
  800bea:	57                   	push   %edi
  800beb:	56                   	push   %esi
  800bec:	53                   	push   %ebx
	asm volatile("int %1\n"
  800bed:	b8 00 00 00 00       	mov    $0x0,%eax
  800bf2:	8b 55 08             	mov    0x8(%ebp),%edx
  800bf5:	8b 4d 0c             	mov    0xc(%ebp),%ecx
  800bf8:	89 c3                	mov    %eax,%ebx
  800bfa:	89 c7                	mov    %eax,%edi
  800bfc:	89 c6                	mov    %eax,%esi
  800bfe:	cd 30                	int    $0x30
	syscall(SYS_cputs, 0, (uint32_t)s, len, 0, 0, 0);
}
```

看起来我们成功运行到了 trap 的位置，就不要管该死的 gdb 啦。

### 处理中断和异常

> 在这一点上，int $0x30用户空间中的第一个系统调用指令是一个死胡同：一旦处理器进入用户模式，就无法退出。现在，您将需要实现基本的异常和系统调用处理，以便内核有可能从用户模式代码中恢复对处理器的控制。您应该做的第一件事是完全熟悉x86中断和异常机制。

练习3会要求你阅读一下相关的手册部分，不过我没看；这部分可以参考它的介绍我觉得就够完成 lab 了。

它还提到了一点概念的部分：

> 在本实验中，我们通常遵循Intel的有关中断，异常等的术语。但是，诸如异常，陷阱，中断，故障和中止之类的术语在整个体系结构或操作系统中没有标准含义，并且经常被使用而无视它们在特定体系结构（例如x86）上的细微差别。当您在本练习之外看到这些术语时，含义可能会略有不同。

### 受保护控制转移的基础

> 异常和中断都是“受保护的控制传递”，它们导致处理器从用户模式切换到内核模式（CPL = 0），而没有给用户模式代码任何干扰内核或其他环境功能的机会。用Intel的术语来说，中断是受保护的控制传输，它是由通常在处理器外部的异步事件引起的，例如，外部设备I / O活动的通知。一个例外，与此相反，是当前正在运行的代码同步地引起受保护的控制传输，例如由于通过零除法或一个无效的存储器访问。

在x86上，两种机制可以共同提供这种保护：

- `中断描述符表`
- `任务状态段`

### 异常和中断的类型

> x86处理器可以在内部生成的所有同步异常都使用0到31之间的中断向量，因此映射到IDT条目0-31。例如，页面错误总是通过向量14引起异常。大于31的中断向量仅由软件中断使用。

注意观察对应的例子，有两种类型，会对应着两种不同的堆栈压入参数，一般情况下是这样的：

```
                     +--------------------+ KSTACKTOP             
                     | 0x00000 | old SS   |     " - 4
                     |      old ESP       |     " - 8
                     |     old EFLAGS     |     " - 12
                     | 0x00000 | old CS   |     " - 16
                     |      old EIP       |     " - 20 <---- ESP 
                     +--------------------+             
	
```

有的时候也会多压入一个错误代码：

```
                     +--------------------+ KSTACKTOP             
                     | 0x00000 | old SS   |     " - 4
                     |      old ESP       |     " - 8
                     |     old EFLAGS     |     " - 12
                     | 0x00000 | old CS   |     " - 16
                     |      old EIP       |     " - 20
                     |     error code     |     " - 24 <---- ESP
                     +--------------------+             
```

这样的方式同样可以处理嵌套异常和中断，就是继续往堆栈里面压东西就好啦。

### 设置IDT

>现在，您应该具有设置IDT和处理JOS中的异常所需的基本信息。现在，您将设置IDT以处理中断向量0-31（处理器异常。您应该实现的总体控制流程如下所示：

```
      IDT                   trapentry.S         trap.c
   
+----------------+                        
|   &handler1    |---------> handler1:          trap (struct Trapframe *tf)
|                |             // do stuff      {
|                |             call trap          // handle the exception/interrupt
|                |             // ...           }
+----------------+
|   &handler2    |--------> handler2:
|                |            // do stuff
|                |            call trap
|                |            // ...
+----------------+
       .
       .
       .
+----------------+
|   &handlerX    |--------> handlerX:
|                |             // do stuff
|                |             call trap
|                |             // ...
+----------------+
```

每个异常或中断都应在trapentry.S中拥有自己的处理程序， 并trap_init()应使用这些处理程序的地址来初始化IDT。每个处理程序都应在堆栈上构建一个struct Trapframe （请参见inc/trap.h），并使用指向Trapframe的指针进行调用 trap()（在trap.c中）。 trap()然后处理异常/中断或调度到特定的处理函数。

练习4就是要编辑 `trapentry.S` 和 `trap.c` 并实现上述功能，它没有很明确地告诉你每个步骤，不过给了很多提示。

首先来看 trapentry.S，这部分其实和 xv6 里面基本一样，如果有什么不清楚的话照抄即可。需要注意的是：

- TRAPHANDLER_NOEC 对应着上述第一种堆栈，TRAPHANDLER 对应第二种，多加了一个 error code 参数；
- 堆栈里面整个内存布局和 struct Trapframe 有个一一对应的联系；
- pushal 很有用，去查一下；
- ds 和 es 寄存器是不能直接设置的，要中转。

```s
TRAPHANDLER_NOEC(handler0, T_DIVIDE)
TRAPHANDLER_NOEC(handler1, T_DEBUG)
TRAPHANDLER_NOEC(handler2, T_NMI)
TRAPHANDLER_NOEC(handler3, T_BRKPT)
TRAPHANDLER_NOEC(handler4, T_OFLOW)
TRAPHANDLER_NOEC(handler5, T_BOUND)
TRAPHANDLER_NOEC(handler6, T_ILLOP)
TRAPHANDLER_NOEC(handler7, T_DEVICE)
TRAPHANDLER(handler8, T_DBLFLT)

TRAPHANDLER(handler10, T_TSS)
TRAPHANDLER(handler11, T_SEGNP)
TRAPHANDLER(handler12, T_STACK)
TRAPHANDLER(handler13, T_GPFLT)
TRAPHANDLER(handler14, T_PGFLT)

TRAPHANDLER(handler16, T_FPERR)
TRAPHANDLER(handler17, T_ALIGN)
TRAPHANDLER(handler18, T_MCHK)
TRAPHANDLER(handler19, T_SIMDERR)

/*
 * Lab 3: Your code here for _alltraps
 */

.globl		_start
_alltraps:
	pushl	%ds
	pushl	%es
	pushal
	movw 	$(GD_KD), %ax
  	movw 	%ax, %ds
  	movw 	%ax, %es
	pushl 	%esp
	call	trap
```

trap.c 里面要对具体的 trap 进行处理，首先要了解一下 trap 处理的流程。

我们在内核初始化的时候调用了 trap_init 设置 IDT，这部分可以先写，注意使用 SETGATE 宏，以及部分特权级设置：

```c
void
trap_init(void)
{
	extern struct Segdesc gdt[];

	// LAB 3: Your code here.
	SETGATE(idt[0], 1, GD_KT, handler0, 0);
	SETGATE(idt[1], 1, GD_KT, handler1, 3);
	SETGATE(idt[2], 1, GD_KT, handler2, 0);
	SETGATE(idt[3], 1, GD_KT, handler3, 3);
	SETGATE(idt[4], 1, GD_KT, handler4, 0);
	SETGATE(idt[5], 1, GD_KT, handler5, 0);
	SETGATE(idt[6], 1, GD_KT, handler6, 0);
	SETGATE(idt[7], 1, GD_KT, handler7, 0);
	SETGATE(idt[8], 1, GD_KT, handler8, 0);

	SETGATE(idt[10], 1, GD_KT, handler10, 0);
	SETGATE(idt[11], 1, GD_KT, handler11, 0);
	SETGATE(idt[12], 1, GD_KT, handler12, 0);
	SETGATE(idt[13], 1, GD_KT, handler13, 0);
	SETGATE(idt[14], 1, GD_KT, handler14, 0);

	SETGATE(idt[16], 1, GD_KT, handler16, 0);
	SETGATE(idt[17], 1, GD_KT, handler17, 0);
	SETGATE(idt[18], 1, GD_KT, handler18, 0);
	SETGATE(idt[19], 1, GD_KT, handler19, 0);

	// Per-CPU setup 
	trap_init_percpu();
}
```

具体进入 trap 的时候，我们在 alltraps 之后调用了 trap 函数，通过 trap_dispatch 进行进一步处理，然后调用 env_run 返回用户态。主要处理逻辑在 trap_dispatch 里面。不过我们暂时还不用修改，这样就已经能通过 partA 啦。

问题：

1. 为每个异常/中断设置单独的处理函数的目的是什么？（即，如果所有异常/中断都传递给了同一处理程序，则无法提供当前实现中存在的功能？）

    首先，一部分处理程序是没有包含中断号的，这样可能会丢失一部分信息；我认为主要还是为了灵活性考虑，每种中断附带的信息都不一样，使用不同的处理程序可以让结果更灵活。当然，不这样设计应该也行。

2. 您是否需要做任何事情来使 softint 程序正常运行？等级脚本期望它会产生一般的保护故障（陷阱13），但是softint的代码说 int $14。 为什么要产生中断向量13？如果内核实际上允许softint的 int $14指令调用内核的页面错误处理程序（即中断向量14），会发生什么？

	不需要。用户态调用中断代码实际上就是软中断，所以不会直接进入对应中断处理程序，而是把如何处理通过软中断交给内核来决定；如果直接允许的话，可能会出现一些不可预料的恶意行为。

