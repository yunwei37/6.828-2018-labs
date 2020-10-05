#! https://zhuanlan.zhihu.com/p/258935003
# MIT 6.828 操作系统工程 lab1 通关指北

>这篇是我自己探索实现 MIT 6.828 lab1 的笔记记录，会包含一部分代码注释和要求的翻译记录，以及踩过的坑/个人的解决方案

这里是我实现的完整代码仓库，也包含其他笔记等等：[https://github.com/yunwei37/6.828-2018-labs](https://github.com/yunwei37/6.828-2018-labs)

## init

1. setup

    实验内容采用git分发：
    
    ```
    git clone https://pdos.csail.mit.edu/6.828/2018/jos.git lab
    ```

    测试的话可以使用：
    
    ```
    make grade

    ```

## Part 1: PC Bootstrap

- 需要了解x86汇编以及内联汇编的写法，参看：

    [http://www.delorie.com/djgpp/doc/brennan/brennan_att_inline_djgpp.html](http://www.delorie.com/djgpp/doc/brennan/brennan_att_inline_djgpp.html)
    [https://pdos.csail.mit.edu/6.828/2018/readings/pcasm-book.pdf](https://pdos.csail.mit.edu/6.828/2018/readings/pcasm-book.pdf)

- 运行 qemu

    ```
    cd lab
    make 
    make qemu

    ```

- PC的物理地址空间：

    ```
    +------------------+  <- 0xFFFFFFFF (4GB)
    |      32-bit      |
    |  memory mapped   |
    |     devices      |
    |                  |
    /\/\/\/\/\/\/\/\/\/\

    /\/\/\/\/\/\/\/\/\/\
    |                  |
    |      Unused      |
    |                  |
    +------------------+  <- depends on amount of RAM
    |                  |
    |                  |
    | Extended Memory  |
    |                  |
    |                  |
    +------------------+  <- 0x00100000 (1MB)
    |     BIOS ROM     |
    +------------------+  <- 0x000F0000 (960KB)
    |  16-bit devices, |
    |  expansion ROMs  |
    +------------------+  <- 0x000C0000 (768KB)
    |   VGA Display    |
    +------------------+  <- 0x000A0000 (640KB)
    |                  |
    |    Low Memory    |
    |                  |
    +------------------+  <- 0x00000000

    ```

- 使用 gdb 调试qemu：

打开新的窗口：

    cd lab
    make qemu-gdb

在另外一个终端：

    make
    make gdb

开始使用gdb调试，首先进入实模式；

- IBM PC从物理地址0x000ffff0开始执行，该地址位于为ROM BIOS保留的64KB区域的最顶部。
- PC从CS = 0xf000和IP = 0xfff0开始执行。
- 要执行的第一条指令是jmp指令，它跳转到分段地址 CS = 0xf000和IP = 0xe05b。

物理地址 = 16 *网段 + 偏移量

然后，BIOS所做的第一件事就是jmp倒退到BIOS中的较早位置；


## Part 2: The Boot Loader 引导加载程序

PC的软盘和硬盘分为512个字节的区域，称为扇区。

当BIOS找到可引导的软盘或硬盘时，它将512字节的引导扇区加载到物理地址0x7c00至0x7dff的内存中，然后使用jmp指令将CS：IP设置为0000：7c00，将控制权传递给引导程序装载机。

### 引导加载程序必须执行的两个主要功能：

- 将处理器从实模式切换到 32位保护模式；
- 通过x86的特殊I / O指令直接访问IDE磁盘设备寄存器，从硬盘读取内核；

### 引导加载程序的源代码：

boot/boot.S

```s
#include <inc/mmu.h>

# 启动CPU：切换到32位保护模式，跳至C代码；
# BIOS将该代码从硬盘的第一个扇区加载到
# 物理地址为0x7c00的内存，并开始以实模式执行
# %cs=0 %ip=7c00.

.set PROT_MODE_CSEG, 0x8         # 内核代码段选择器
.set PROT_MODE_DSEG, 0x10        # 内核数据段选择器
.set CR0_PE_ON,      0x1         # 保护模式启用标志

.globl start
start:
  .code16                     # 汇编为16位模式
  cli                         # 禁用中断
  cld                         # 字符串操作增量，将标志寄存器Flag的方向标志位DF清零。
                              # 在字串操作中使变址寄存器SI或DI的地址指针自动增加，字串处理由前往后。

  # 设置重要的数据段寄存器（DS，ES，SS）
  xorw    %ax,%ax             # 第零段
  movw    %ax,%ds             # ->数据段
  movw    %ax,%es             # ->额外段
  movw    %ax,%ss             # ->堆栈段

  # 启用A20：
  #   为了与最早的PC向后兼容，物理
  #   地址线20绑在低电平，因此地址高于
  #   1MB会被默认返回从零开始。  这边代码撤消了此操作。
seta20.1:
  inb     $0x64,%al               # 等待其不忙状态
  testb   $0x2,%al
  jnz     seta20.1

  movb    $0xd1,%al               # 0xd1 -> 端口 0x64
  outb    %al,$0x64

seta20.2:
  inb     $0x64,%al               # 等待其不忙状态
  testb   $0x2,%al
  jnz     seta20.2

  movb    $0xdf,%al               # 0xdf -> 端口 0x60
  outb    %al,$0x60

  # 使用引导GDT从实模式切换到保护模式
  # 并使用段转换以保证虚拟地址和它们的物理地址相同
  # 因此
  # 有效内存映射在切换期间不会更改。
  lgdt    gdtdesc
  movl    %cr0, %eax
  orl     $CR0_PE_ON, %eax
  movl    %eax, %cr0
  
  # 跳转到下一条指令，但还是在32位代码段中。
  # 将处理器切换为32位指令模式。
  ljmp    $PROT_MODE_CSEG, $protcseg

  .code32                     # 32位模式汇编
protcseg:
  # 设置保护模式数据段寄存器
  movw    $PROT_MODE_DSEG, %ax    # 我们的数据段选择器
  movw    %ax, %ds                # -> DS: 数据段
  movw    %ax, %es                # -> ES:额外段
  movw    %ax, %fs                # -> FS
  movw    %ax, %gs                # -> GS
  movw    %ax, %ss                # -> SS: 堆栈段
  
  # 设置堆栈指针并调用C代码，bootmain
  movl    $start, %esp
  call bootmain

  # 如果bootmain返回（不应该这样），则循环
spin:
  jmp spin

# Bootstrap GDT
.p2align 2                                # 强制4字节对齐 
gdt:
  SEG_NULL				# 空段
  SEG(STA_X|STA_R, 0x0, 0xffffffff)	# 代码段
  SEG(STA_W, 0x0, 0xffffffff)	        # 数据部分

gdtdesc:
  .word   0x17                            # sizeof(gdt) - 1
  .long   gdt                             # address gdt

```

boot/main.c

```c

#include <inc/x86.h>
#include <inc/elf.h>

/**********************************************************************
 * 这是一个简单的启动装载程序，唯一的工作就是启动
 * 来自第一个IDE硬盘的ELF内核映像。
 *
 * 磁盘布局
 *  * 此程序（boot.S和main.c）是引导加载程序。这应该
 *    被存储在磁盘的第一个扇区中。
 *
 *  * 第二个扇区开始保存内核映像。
 *
 *  * 内核映像必须为ELF格式。
 *
 * 启动步骤
 *  * 当CPU启动时，它将BIOS加载到内存中并执行
 *
 *  *  BIOS初始化设备，中断例程集以及
 *    读取引导设备的第一个扇区（例如，硬盘驱动器）
 *    进入内存并跳转到它。
 *
 *  * 假设此引导加载程序存储在硬盘的第一个扇区中
 *    此代码接管...
 *
 *  * 控制从boot.S开始-设置保护模式，
 *    和一个堆栈，然后运行C代码，然后调用bootmain（）
 *
 *  * 该文件中的bootmain（）会接管，读取内核并跳转到该内核。
 **********************************************************************/

#define SECTSIZE	512
#define ELFHDR		((struct Elf *) 0x10000) // /暂存空间

void readsect(void*, uint32_t);
void readseg(uint32_t, uint32_t, uint32_t);

void
bootmain(void)
{
	struct Proghdr *ph, *eph;

	// 从磁盘读取第一页
	readseg((uint32_t) ELFHDR, SECTSIZE*8, 0);

	// 这是有效的ELF吗？
	if (ELFHDR->e_magic != ELF_MAGIC)
		goto bad;

	// 加载每个程序段（忽略ph标志）
	ph = (struct Proghdr *) ((uint8_t *) ELFHDR + ELFHDR->e_phoff);
	eph = ph + ELFHDR->e_phnum;
	for (; ph < eph; ph++)
		// p_pa是该段的加载地址（同样
		// 是物理地址）
		readseg(ph->p_pa, ph->p_memsz, ph->p_offset);

	// 从ELF标头中调用入口点
	// 注意：不返回！
	((void (*)(void)) (ELFHDR->e_entry))();

bad:
	outw(0x8A00, 0x8A00);
	outw(0x8A00, 0x8E00);
	while (1)
		/* do nothing */;
}

// 从内核将“偏移”处的“计数”字节读取到物理地址“ pa”中。
// 复制数量可能超过要求
void
readseg(uint32_t pa, uint32_t count, uint32_t offset)
{
	uint32_t end_pa;

	end_pa = pa + count;

	// 向下舍入到扇区边界
	pa &= ~(SECTSIZE - 1);

	// 从字节转换为扇区，内核从扇区1开始
	offset = (offset / SECTSIZE) + 1;

	// 如果速度太慢，我们可以一次读取很多扇区。
	// 我们向内存中写入的内容超出了要求，但这没关系 --
	// 我们以递增顺序加载.
	while (pa < end_pa) {
		// 由于尚未启用分页，因此我们正在使用
		// 一个特定的段映射 (参阅 boot.S), 我们可以
		// 直接使用物理地址.  一旦JOS启用MMU
		// ，就不会这样了
		readsect((uint8_t*) pa, offset);
		pa += SECTSIZE;
		offset++;
	}
}

void
waitdisk(void)
{
	// 等待磁盘重新运行
	while ((inb(0x1F7) & 0xC0) != 0x40)
		/* do nothing */;
}

void
readsect(void *dst, uint32_t offset)
{
	// 等待磁盘准备好
	waitdisk();

	outb(0x1F2, 1);		// count = 1
	outb(0x1F3, offset);
	outb(0x1F4, offset >> 8);
	outb(0x1F5, offset >> 16);
	outb(0x1F6, (offset >> 24) | 0xE0);
	outb(0x1F7, 0x20);	// cmd 0x20 - 读取扇区

	// 等待磁盘准备好
	waitdisk();

	// 读取一个扇区
	insl(0x1F0, dst, SECTSIZE/4);
}


```

### 加载内核

- ELF二进制文件：

    可以将ELF可执行文件视为具有加载信息的标头，然后是几个程序段，每个程序段都是要在指定地址加载到内存中的连续代码或数据块。ELF二进制文件以固定长度的ELF标头开头，其后是可变长度的程序标头， 列出了要加载的每个程序段。

执行`objdump -h obj/kern/kernel`，查看内核可执行文件中所有部分的名称，大小和链接地址的完整列表：

- .text：程序的可执行指令。
- .rodata：只读数据，例如C编译器生成的ASCII字符串常量。
- .data：数据部分保存程序的初始化数据，例如用int x = 5等初始化程序声明的全局变量；

- VMA 链接地址，该节期望从中执行的内存地址。
- LMA 加载地址，

```
obj/kern/kernel:     file format elf32-i386

Sections:
Idx Name          Size      VMA       LMA       File off  Algn
  0 .text         00001acd  f0100000  00100000  00001000  2**4
                  CONTENTS, ALLOC, LOAD, READONLY, CODE
  1 .rodata       000006bc  f0101ae0  00101ae0  00002ae0  2**5
                  CONTENTS, ALLOC, LOAD, READONLY, DATA
  2 .stab         00004291  f010219c  0010219c  0000319c  2**2
                  CONTENTS, ALLOC, LOAD, READONLY, DATA
  3 .stabstr      0000197f  f010642d  0010642d  0000742d  2**0
                  CONTENTS, ALLOC, LOAD, READONLY, DATA
  4 .data         00009300  f0108000  00108000  00009000  2**12
                  CONTENTS, ALLOC, LOAD, DATA
  5 .got          00000008  f0111300  00111300  00012300  2**2
                  CONTENTS, ALLOC, LOAD, DATA
  6 .got.plt      0000000c  f0111308  00111308  00012308  2**2
                  CONTENTS, ALLOC, LOAD, DATA
  7 .data.rel.local 00001000  f0112000  00112000  00013000  2**12
                  CONTENTS, ALLOC, LOAD, DATA
  8 .data.rel.ro.local 00000044  f0113000  00113000  00014000  2**2
                  CONTENTS, ALLOC, LOAD, DATA
  9 .bss          00000648  f0113060  00113060  00014060  2**5
                  CONTENTS, ALLOC, LOAD, DATA
 10 .comment      00000024  00000000  00000000  000146a8  2**0
                  CONTENTS, READONLY

```

查看引导加载程序的.text部分：

objdump -h obj/boot/boot.out

```
obj/boot/boot.out:     file format elf32-i386

Sections:
Idx Name          Size      VMA       LMA       File off  Algn
  0 .text         0000019c  00007c00  00007c00  00000074  2**2
                  CONTENTS, ALLOC, LOAD, CODE
  1 .eh_frame     0000009c  00007d9c  00007d9c  00000210  2**2
                  CONTENTS, ALLOC, LOAD, READONLY, DATA
  2 .stab         00000870  00000000  00000000  000002ac  2**2
                  CONTENTS, READONLY, DEBUGGING
  3 .stabstr      00000940  00000000  00000000  00000b1c  2**0
                  CONTENTS, READONLY, DEBUGGING
  4 .comment      00000024  00000000  00000000  0000145c  2**0
                  CONTENTS, READONLY

```

引导加载程序使用ELF 程序标头来决定如何加载这些部分，程序标头指定要加载到内存中的ELF对象的哪些部分以及每个目标地址应占据的位置。

检查程序头：` objdump -x obj/kern/kernel`

ELF对象需要加载到内存中的区域是标记为“ LOAD”的区域。

```
Program Header:
    LOAD off    0x00001000 vaddr 0xf0100000 paddr 0x00100000 align 2**12
         filesz 0x00007dac memsz 0x00007dac flags r-x
    LOAD off    0x00009000 vaddr 0xf0108000 paddr 0x00108000 align 2**12
         filesz 0x0000b6a8 memsz 0x0000b6a8 flags rw-
   STACK off    0x00000000 vaddr 0x00000000 paddr 0x00000000 align 2**4
         filesz 0x00000000 memsz 0x00000000 flags rwx

```

查看内核程序的入口点`objdump -f obj/kern/kernel`：

```
obj/kern/kernel:     file format elf32-i386
architecture: i386, flags 0x00000112:
EXEC_P, HAS_SYMS, D_PAGED
start address 0x0010000c

```

- 在开始时，gdb会提示：The target architecture is assumed to be i8086
- 切换到保护模式之后(ljmpl  $0x8,$0xfd18f指令后)，提示: The target architecture is assumed to be i386

#### 练习6：

重置机器（退出QEMU / GDB并再次启动它们）。在BIOS进入引导加载程序时检查0x00100000处的8个内存字，然后在引导加载程序进入内核时再次检查。

进入引导加载程序:

```
(gdb) x/8x 0x00100000
0x100000:	0x00000000	0x00000000	0x00000000	0x00000000
0x100010:	0x00000000	0x00000000	0x00000000	0x00000000

```

设置断点： b *0x7d81

引导加载程序进入内核:

```
(gdb) x/8x 0x00100000
0x100000:	0x1badb002	0x00000000	0xe4524ffe	0x7205c766
0x100010:	0x34000004	0x2000b812	0x220f0011	0xc0200fd8

```

## Part 3: The Kernel 内核

### 使用虚拟内存解决位置依赖性

内核的链接地址（由objdump打印）与加载地址之间存在（相当大的）差异；操作系统内核通常喜欢被链接并在很高的虚拟地址（例如0xf0100000）上运行，以便将处理器虚拟地址空间的下部留给用户程序使用。

- 链接地址 f0100000 
- 加载地址 00100000 

许多机器在地址0xf0100000上没有任何物理内存，因此我们不能指望能够在其中存储内核；将使用处理器的内存管理硬件将虚拟地址0xf0100000（内核代码期望在其上运行的链接地址）映射到物理地址0x00100000（引导加载程序将内核加载到物理内存中）。

这样，尽管内核的虚拟地址足够高，可以为用户进程留出足够的地址空间，但是它将被加载到PC RAM中1MB点的BIOS ROM上方的物理内存中。

在这个阶段中，仅映射前4MB的物理内存；

映射：kern/entrypgdir.c 中手写，静态初始化的页面目录和页面表。
直到kern / entry.S设置了CR0_PG标志，内存引用才被视为物理地址。

-  将范围从0xf0000000到0xf0400000的虚拟地址转换为物理地址0x00000000到0x00400000
-  将虚拟地址0x00000000到0x00400000转换为物理地址0x00000000到0x00400000

- kern/entrypgdir.c：

```c
#include <inc/mmu.h>
#include <inc/memlayout.h>

pte_t entry_pgtable[NPTENTRIES];

// entry.S页面目录从虚拟地址KERNBASE开始映射前4MB的物理内存
// （也就是说，它映射虚拟地址
// 地址[KERNBASE，KERNBASE + 4MB）到物理地址[0，4MB）
// 我们选择4MB，因为这就是我们可以在一页的空间中映射的表
// 这足以使我们完成启动的早期阶段。我们也映射
// 虚拟地址[0，4MB）到物理地址[0，4MB）这个
// 区域对于entry.S中的一些指令至关重要，然后我们
// 不再使用它。
//
// 页面目录（和页面表）必须从页面边界开始，
// 因此是“ __aligned__”属性。 另外，由于限制
// 与链接和静态初始化程序有关, 我们在这里使用“ x + PTE_P”
// 而不是更标准的“ x | PTE_P”。  其他地方
// 您应该使用“ |”组合标志。
__attribute__((__aligned__(PGSIZE)))
pde_t entry_pgdir[NPDENTRIES] = {
	// 将VA的[0，4MB）映射到PA的[0，4MB）
	[0]
		= ((uintptr_t)entry_pgtable - KERNBASE) + PTE_P,
	// 将VA的[KERNBASE，KERNBASE + 4MB）映射到PA的[0，4MB）
	[KERNBASE>>PDXSHIFT]
		= ((uintptr_t)entry_pgtable - KERNBASE) + PTE_P + PTE_W
};

// 页表的条目0映射到物理页0，条目1映射到
// 物理页面1，依此类推
__attribute__((__aligned__(PGSIZE)))
pte_t entry_pgtable[NPTENTRIES] = {
	0x000000 | PTE_P | PTE_W,
	0x001000 | PTE_P | PTE_W,
	0x002000 | PTE_P | PTE_W,
	0x003000 | PTE_P | PTE_W,
	0x004000 | PTE_P | PTE_W,
	0x005000 | PTE_P | PTE_W,
  ................

```

- kern/entry.S

```s
/* See COPYRIGHT for copyright information. */

#include <inc/mmu.h>
#include <inc/memlayout.h>

# 逻辑右移
#define SRL(val, shamt)		(((val) >> (shamt)) & ~(-1 << (32 - (shamt))))


###################################################################
# 内核（此代码）链接到地址〜（KERNBASE + 1 Meg），
# 但引导加载程序会将其加载到地址〜1 Meg。
#	
# RELOC（x）将符号x从其链接地址映射到其在
# 物理内存中的实际位置（其加载地址）。	 
###################################################################

#define	RELOC(x) ((x) - KERNBASE)

#define MULTIBOOT_HEADER_MAGIC (0x1BADB002)
#define MULTIBOOT_HEADER_FLAGS (0)
#define CHECKSUM (-(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_HEADER_FLAGS))

###################################################################
# 进入点
###################################################################

.text

# Multiboot标头
.align 4
.long MULTIBOOT_HEADER_MAGIC
.long MULTIBOOT_HEADER_FLAGS
.long CHECKSUM

# '_start'指定ELF入口点。  既然当引导程序进入此代码时我们还没设置
# 虚拟内存，我们需要
# bootloader跳到入口点的*物理*地址。
.globl		_start
_start = RELOC(entry)

.globl entry
entry:
	movw	$0x1234,0x472			# 热启动

	# 我们尚未设置虚拟内存， 因此我们从
	# 引导加载程序加载内核的物理地址为：1MB
	# （加上几个字节）处开始运行.  但是，C代码被链接为在
	# KERNBASE+1MB 的位置运行。  我们建立了一个简单的页面目录，
	# 将虚拟地址[KERNBASE，KERNBASE + 4MB）转换为
	# 物理地址[0，4MB）。  这4MB区域
	# 直到我们在实验2 mem_init中设置真实页面表为止
	# 是足够的。

	# 将entry_pgdir的物理地址加载到cr3中。   entry_pgdir
	# 在entrypgdir.c中定义。
	movl	$(RELOC(entry_pgdir)), %eax
	movl	%eax, %cr3
	# 打开分页功能。
	movl	%cr0, %eax
	orl	$(CR0_PE|CR0_PG|CR0_WP), %eax
	movl	%eax, %cr0

	# 现在启用了分页，但是我们仍在低EIP上运行
	# （为什么这样可以？） 进入之前先跳到上方c代码中的
	# KERNBASE
	mov	$relocated, %eax
	jmp	*%eax
relocated:

	# 清除帧指针寄存器（EBP）
	# 这样，一旦我们调试C代码，
	# 堆栈回溯将正确终止。
	movl	$0x0,%ebp			# 空帧指针

	# 设置堆栈指针
	movl	$(bootstacktop),%esp

	# 现在转到C代码
	call	i386_init

	# 代码永远不会到这里，但如果到了，那就让它循环死机吧。
spin:	jmp	spin


.data
###################################################################
# 启动堆栈
###################################################################
	.p2align	PGSHIFT		# 页面对齐
	.globl		bootstack
bootstack:
	.space		KSTKSIZE
	.globl		bootstacktop   
bootstacktop:

```

不在这两个范围之一内的任何虚拟地址都将导致硬件异常：导致QEMU转储计算机状态并退出。

#### 练习7：

使用QEMU和GDB跟踪到JOS内核并在movl %eax, %cr0处停止。检查0x00100000和0xf0100000的内存。现在，使用stepiGDB命令单步执行该指令。同样，检查内存为0x00100000和0xf0100000。

在movl %eax, %cr0处停止：

```
(gdb) x 0x00100000
   0x100000:	add    0x1bad(%eax),%dh
(gdb) x 0xf0100000
   0xf0100000 <_start-268435468>:	add    %al,(%eax)
```

si：

```
0x00100028 in ?? ()
(gdb) x 0x00100000
   0x100000:	add    0x1bad(%eax),%dh
(gdb) x 0xf0100000
   0xf0100000 <_start-268435468>:	add    0x1bad(%eax),%dh

```

建立新映射后 的第一条指令是:

mov	$relocated, %eax

这时的eax是：

(gdb) info registers
eax            0xf010002f          -267386833


### 格式化打印到控制台：

- kern/printf.c

    内核的cprintf控制台输出的简单实现，
    基于printfmt（）和内核控制台的cputchar（）。

- lib/printfmt.c



```c
// 精简的基本printf样式格式化例程，
// 被printf，sprintf，fprintf等共同使用
// 内核和用户程序也使用此代码。

#include <inc/types.h>
#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/stdarg.h>
#include <inc/error.h>

/*
 * 数字支持空格或零填充和字段宽度格式。
 * 
 *
 * 特殊格式％e带有整数错误代码
 * 并输出描述错误的字符串。
 * 整数可以是正数或负数，
 * ，使-E_NO_MEM和E_NO_MEM等效。
 */

static const char * const error_string[MAXERROR] =
{
	[E_UNSPECIFIED]	= "unspecified error",
	[E_BAD_ENV]	= "bad environment",
	[E_INVAL]	= "invalid parameter",
	[E_NO_MEM]	= "out of memory",
	[E_NO_FREE_ENV]	= "out of environments",
	[E_FAULT]	= "segmentation fault",
};

/*
 * 使用指定的putch函数和关联的指针putdat
 * 以相反的顺序打印数字（基数<= 16）.
 */
static void
printnum(void (*putch)(int, void*), void *putdat,
	 unsigned long long num, unsigned base, int width, int padc)
{
	// 首先递归地打印所有前面的（更重要的）数字
	if (num >= base) {
		printnum(putch, putdat, num / base, base, width - 1, padc);
	} else {
		// 在第一个数字前打印任何需要的填充字符
		while (--width > 0)
			putch(padc, putdat);
	}

	// 然后打印此（最低有效）数字
	putch("0123456789abcdef"[num % base], putdat);
}

// 从varargs列表中获取各种可能大小的unsigned int，
// 取决于lflag参数。
static unsigned long long
getuint(va_list *ap, int lflag)
{
	if (lflag >= 2)
		return va_arg(*ap, unsigned long long);
	else if (lflag)
		return va_arg(*ap, unsigned long);
	else
		return va_arg(*ap, unsigned int);
}

// 与getuint相同
// 符号扩展
static long long
getint(va_list *ap, int lflag)
{
	if (lflag >= 2)
		return va_arg(*ap, long long);
	else if (lflag)
		return va_arg(*ap, long);
	else
		return va_arg(*ap, int);
}


// 用于格式化和打印字符串的主要函数
void printfmt(void (*putch)(int, void*), void *putdat, const char *fmt, ...);

void
vprintfmt(void (*putch)(int, void*), void *putdat, const char *fmt, va_list ap)
{
	register const char *p;
	register int ch, err;
	unsigned long long num;
	int base, lflag, width, precision, altflag;
	char padc;

	while (1) {
		while ((ch = *(unsigned char *) fmt++) != '%') {
			if (ch == '\0')
				return;
			putch(ch, putdat);
		}

		// 处理％转义序列
		padc = ' ';
		width = -1;
		precision = -1;
		lflag = 0;
		altflag = 0;
	reswitch:
		switch (ch = *(unsigned char *) fmt++) {

		// 标记以在右侧填充
		case '-':
			padc = '-';
			goto reswitch;

		// 标记以0代替空格
		case '0':
			padc = '0';
			goto reswitch;

		// 宽度字段
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			for (precision = 0; ; ++fmt) {
				precision = precision * 10 + ch - '0';
				ch = *fmt;
				if (ch < '0' || ch > '9')
					break;
			}
			goto process_precision;

		case '*':
			precision = va_arg(ap, int);
			goto process_precision;

		case '.':
			if (width < 0)
				width = 0;
			goto reswitch;

		case '#':
			altflag = 1;
			goto reswitch;

		process_precision:
			if (width < 0)
				width = precision, precision = -1;
			goto reswitch;

		// long标志（对long long加倍）
		case 'l':
			lflag++;
			goto reswitch;

		// 字符
		case 'c':
			putch(va_arg(ap, int), putdat);
			break;

		// 错误信息
		case 'e':
			err = va_arg(ap, int);
			if (err < 0)
				err = -err;
			if (err >= MAXERROR || (p = error_string[err]) == NULL)
				printfmt(putch, putdat, "error %d", err);
			else
				printfmt(putch, putdat, "%s", p);
			break;

		// 字符串
		case 's':
			if ((p = va_arg(ap, char *)) == NULL)
				p = "(null)";
			if (width > 0 && padc != '-')
				for (width -= strnlen(p, precision); width > 0; width--)
					putch(padc, putdat);
			for (; (ch = *p++) != '\0' && (precision < 0 || --precision >= 0); width--)
				if (altflag && (ch < ' ' || ch > '~'))
					putch('?', putdat);
				else
					putch(ch, putdat);
			for (; width > 0; width--)
				putch(' ', putdat);
			break;

		// （带符号）十进制
		case 'd':
			num = getint(&ap, lflag);
			if ((long long) num < 0) {
				putch('-', putdat);
				num = -(long long) num;
			}
			base = 10;
			goto number;

		// 无符号十进制
		case 'u':
			num = getuint(&ap, lflag);
			base = 10;
			goto number;

		// （无符号）八进制
		case 'o':
			num = getint(&ap, lflag);
			if ((long long) num < 0) {
				putch('-', putdat);
				num = -(long long) num;
			}
			base = 8;
			goto number;

		// 指针
		case 'p':
			putch('0', putdat);
			putch('x', putdat);
			num = (unsigned long long)
				(uintptr_t) va_arg(ap, void *);
			base = 16;
			goto number;

		// （无符号）十六进制
		case 'x':
			num = getuint(&ap, lflag);
			base = 16;
		number:
			printnum(putch, putdat, num, base, width, padc);
			break;

		// 跳过 %
		case '%':
			putch(ch, putdat);
			break;

		// 遇到不符合规范的%格式，跳过
		default:
			putch('%', putdat);
			for (fmt--; fmt[-1] != '%'; fmt--)
				/* do nothing */;
			break;
		}
	}
}


```

- kern/console.c

控制台IO相关代码；

#### 练习8：

我们省略了一小段代码-使用“％o”形式的模式打印八进制数字所必需的代码。查找并填写此代码片段。

```c
		case 'o':
			num = getint(&ap, lflag);
			if ((long long) num < 0) {
				putch('-', putdat);
				num = -(long long) num;
			}
			base = 8;
			goto number;
```

参考：[https://blog.csdn.net/weixin_30466039/article/details/97003339?utm_medium=distribute.pc_relevant.none-task-blog-OPENSEARCH-7.compare&depth_1-utm_source=distribute.pc_relevant.none-task-blog-OPENSEARCH-7.compare](https://blog.csdn.net/weixin_30466039/article/details/97003339?utm_medium=distribute.pc_relevant.none-task-blog-OPENSEARCH-7.compare&depth_1-utm_source=distribute.pc_relevant.none-task-blog-OPENSEARCH-7.compare)

1. 解释printf.c和 console.c之间的接口。
   
    console.c 提供了输入输出字符的功能，大部分都在处理IO接口相关。

2. 从console.c解释以下内容：

```
if (crt_pos >= CRT_SIZE) {
       int i;
        memcpy(crt_buf, crt_buf + CRT_COLS, (CRT_SIZE - CRT_COLS) * sizeof(uint16_t));
       for (i = CRT_SIZE - CRT_COLS; i < CRT_SIZE; i++)
               crt_buf[i] = 0x0700 | ' ';
       crt_pos -= CRT_COLS;
}

```

当crt_pos >= CRT_SIZE，其中CRT_SIZE = 80*25，由于我们知道crt_pos取值范围是0~(80*25-1)，那么这个条件如果成立则说明现在在屏幕上输出的内容已经超过了一页。所以此时要把页面向上滚动一行，即把原来的1~79号行放到现在的0~78行上，然后把79号行换成一行空格（当然并非完全都是空格，0号字符上要显示你输入的字符int c）。所以memcpy操作就是把crt_buf字符数组中1~79号行的内容复制到0~78号行的位置上。而紧接着的for循环则是把最后一行，79号行都变成空格。最后还要修改一下crt_pos的值。

3.  参考上述代码
4.  “Hello World”
5. 不确定值
6. 在vprintfmt中倒序处理参数


### 堆栈

在此过程中编写一个有用的新内核监视器函数，该函数将显示堆栈的回溯信息：保存的列表来自导致当前执行点的嵌套调用指令的指令指针（IP）值。

### 练习10：

[http://www.cnblogs.com/fatsheep9146/p/5079930.html](http://www.cnblogs.com/fatsheep9146/p/5079930.html)

### 练习11：

实现上述指定的backtrace函数。（默认参数下，并没有遇到文中的bug

先了解一下test_backtrace是做什么的；然后打印出堆栈信息和ebp函数调用链链信息，观察即可发现。


代码：

```c
int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	cprintf("Stack backtrace:\n");
	uint32_t *ebp;
	ebp = (uint32_t *)read_ebp();
	while(ebp!=0){
		cprintf("  ebp %08x",ebp);
		cprintf(" eip %08x  args",*(ebp+1));
		for(int i=2;i<7;++i)
			cprintf(" %08x",*(ebp+i));
		cprintf("\n");
		ebp = (uint32_t *)*ebp;
	}
	return 0;
}
```

打印输出：

```
  ebp f0110f18 eip f01000a5  args 00000000 00000000 00000000 f010004e f0112308
  ebp f0110f38 eip f010007a  args 00000000 00000001 f0110f78 f010004e f0112308
  ebp f0110f58 eip f010007a  args 00000001 00000002 f0110f98 f010004e f0112308
  ebp f0110f78 eip f010007a  args 00000002 00000003 f0110fb8 f010004e f0112308
  ebp f0110f98 eip f010007a  args 00000003 00000004 00000000 f010004e f0112308
  ebp f0110fb8 eip f010007a  args 00000004 00000005 00000000 f010004e f0112308
  ebp f0110fd8 eip f01000fc  args 00000005 00001aac 00000640 00000000 00000000
  ebp f0110ff8 eip f010003e  args 00000003 00001003 00002003 00003003 00004003

```

（为什么回溯代码无法检测到实际有多少个参数？如何解决此限制？）:可以利用后续的获取调试信息的方法；

### 练习12：

通过objdump打印出符号表信息，并尝试找到函数；

```
yunwei@ubuntu:~/lab$ objdump -G obj/kern/kernel | grep f01000
0      SO     0      0      f0100000 1      {standard input}
1      SOL    0      0      f010000c 18     kern/entry.S
2      SLINE  0      44     f010000c 0      
3      SLINE  0      57     f0100015 0      
4      SLINE  0      58     f010001a 0      
5      SLINE  0      60     f010001d 0      
6      SLINE  0      61     f0100020 0      
7      SLINE  0      62     f0100025 0      
8      SLINE  0      67     f0100028 0      
9      SLINE  0      68     f010002d 0      
10     SLINE  0      74     f010002f 0      
11     SLINE  0      77     f0100034 0      
12     SLINE  0      80     f0100039 0      
13     SLINE  0      83     f010003e 0      
14     SO     0      2      f0100040 31     kern/entrypgdir.c
72     SO     0      0      f0100040 0      
73     SO     0      2      f0100040 2889   kern/init.c
108    FUN    0      0      f0100040 2973   test_backtrace:F(0,25)
118    FUN    0      0      f01000aa 3014   i386_init:F(0,25)

```

看看`kdebug.h`里面的`debuginfo_eip`函数:

```c
#ifndef JOS_KERN_KDEBUG_H
#define JOS_KERN_KDEBUG_H

#include <inc/types.h>

// 调试有关特定指令指针的信息
struct Eipdebuginfo {
	const char *eip_file;		// EIP的源代码文件名
	int eip_line;			//  EIP的源代码行号

	const char *eip_fn_name;	// 包含EIP的函数的名称
					//  - 注意：不为空终止！
	int eip_fn_namelen;		// 函数名称的长度
	uintptr_t eip_fn_addr;		// 函数开始地址
	int eip_fn_narg;		// 函数参数的数量
};

int debuginfo_eip(uintptr_t eip, struct Eipdebuginfo *info);

#endif
```

由于包含EIP的函数的名称不为空终止，因此需要使用提示：

>提示：printf格式字符串为打印非空终止的字符串（如STABS表中的字符串）提供了一种简单而又晦涩的方法。 printf("%.*s", length, string)最多可打印的length字符string。查看printf手册页，以了解其工作原理。

在 mon_backtrace() 中继续修改，使用 debuginfo_eip 获取相关信息并打印：

```c
int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	cprintf("Stack backtrace:\n");
	uint32_t *ebp;
	int valid;
	struct Eipdebuginfo ei;
	ebp = (uint32_t *)read_ebp();
	while(ebp!=0){
		cprintf("  ebp %08x",ebp);
		cprintf(" eip %08x  args",*(ebp+1));
		valid = debuginfo_eip(*(ebp+1),&ei);
		for(int i=2;i<7;++i)
			cprintf(" %08x",*(ebp+i));
		cprintf("\n");
		if(valid == 0)
			cprintf("         %s:%d: %.*s+%d\n",ei.eip_file,ei.eip_line,ei.eip_fn_namelen,ei.eip_fn_name,*(ebp+1) - ei.eip_fn_addr);
		ebp = (uint32_t *)*ebp;
	}
	return 0;
}
```

可以参考 inc/stab.h:

```c
//JOS uses the N_SO, N_SOL, N_FUN, and N_SLINE types.
#define	N_SLINE		0x44	// text segment line number
```

知道我们需要使用N_SLINE进行搜索；以及stab的数据结构：

```c
// Entries in the STABS table are formatted as follows.
struct Stab {
	uint32_t n_strx;	// index into string table of name
	uint8_t n_type;         // type of symbol
	uint8_t n_other;        // misc info (usually empty)
	uint16_t n_desc;        // description field
	uintptr_t n_value;	// value of symbol
};
```

参考  的注释部分：

```c
// stab_binsearch(stabs, region_left, region_right, type, addr)
//
//	某些stab类型按升序排列在地址中
//	例如， N_FUN stabs ( n_type ==
//	N_FUN 的 stabs 条目), 标记了函数, 和 N_SO stabs,标记源文件。
//
//	给定指令地址，此函数查找单个 stab
//	条目， 包含该地址的'type'类型。
//
//	搜索在[* region_left，* region_right]范围内进行。
//	因此，要搜索整个N个stabs，可以执行以下操作：
//
//		left = 0;
//		right = N - 1;     /* rightmost stab */
//		stab_binsearch(stabs, &left, &right, type, addr);
//
```

在 kern/kdebug.c 中 debuginfo_eip 相应位置修改，添加行数搜索：

```c
	stab_binsearch(stabs, &lline, &rline, N_SLINE, addr);
	if(lline<=rline){
		info->eip_line = stabs[rline].n_value;
	}else{
		info->eip_line = 0;
		return -1;
	}

```

## pass

```
running JOS: (1.4s) 
  printf: OK 
  backtrace count: OK 
  backtrace arguments: OK 
  backtrace symbols: OK 
  backtrace lines: OK 
Score: 50/50


```

结果是：

```
Stack backtrace:
  ebp f0110f18 eip f01000a5  args 00000000 00000000 00000000 f010004e f0112308
         kern/init.c:6: test_backtrace+101
  ebp f0110f38 eip f010007a  args 00000000 00000001 f0110f78 f010004e f0112308
         kern/init.c:46: test_backtrace+58
  ebp f0110f58 eip f010007a  args 00000001 00000002 f0110f98 f010004e f0112308
         kern/init.c:46: test_backtrace+58
  ebp f0110f78 eip f010007a  args 00000002 00000003 f0110fb8 f010004e f0112308
         kern/init.c:46: test_backtrace+58
  ebp f0110f98 eip f010007a  args 00000003 00000004 00000000 f010004e f0112308
         kern/init.c:46: test_backtrace+58
  ebp f0110fb8 eip f010007a  args 00000004 00000005 00000000 f010004e f0112308
         kern/init.c:46: test_backtrace+58
  ebp f0110fd8 eip f01000fc  args 00000005 00001aac 00000640 00000000 00000000
         kern/init.c:70: i386_init+82
  ebp f0110ff8 eip f010003e  args 00000003 00001003 00002003 00003003 00004003
         kern/entry.S:-267386818: <unknown>+0

```

虽然似乎eip并不一定指向对应的行...

## 总结：

这两天大致搞清楚了boot的方式，然后浏览了一小部分的对应源代码（虽然也不是很多的样子），gdb还不算很熟练，大部分情况下还是使用cprintf打log；