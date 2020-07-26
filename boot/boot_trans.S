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


