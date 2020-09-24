
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

