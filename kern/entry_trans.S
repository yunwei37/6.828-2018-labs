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
