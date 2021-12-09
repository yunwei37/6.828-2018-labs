# MIT 6.828 操作系统工程 Lab5: File system, Spawn and Shell

>这篇是我自己探索实现 MIT 6.828 lab 的笔记记录，会包含一部分代码注释和要求的翻译记录，以及踩过的坑/个人的解决方案

这里是我实现的完整代码仓库，也包含其他笔记等等：[https://github.com/yunwei37/6.828-2018-labs](https://github.com/yunwei37/6.828-2018-labs)

实际上 lab5 可能是最简单的一个 lab 了（绝大多数的代码都已经写好了，就一点点完形填空要做）

lab5 需要仔细阅读的材料比较多：

实验室这部分的主要新组件是文件系统环境，位于新的fs目录中。浏览此目录中的所有文件以了解所有新内容。此外，在user和lib目录中有一些新的文件系统相关的源文件，

- fs/fs.c	mainipulates 文件系统的磁盘结构的代码。
- fs/bc.c	一个简单的块缓存建立在我们的用户级页面错误处理设施之上。
- fs/ide.c	最小的基于 PIO（非中断驱动）的 IDE 驱动程序代码。
- fs/serv.c	使用文件系统 IPC 与客户端环境交互的文件系统服务器。
- lib/fd.c	实现通用类 UNIX 文件描述符接口的代码。
- lib/file.c	磁盘文件类型的驱动程序，作为文件系统 IPC 客户端实现。
- lib/console.c	控制台输入/输出文件类型的驱动程序。
- lib/spawn.c	spawn库调用的代码框架。

## 文件系统

包含以下内容，需要仔细了解：

- 磁盘文件系统结构
- 扇区和块
- 超级块
- 文件元数据
- 目录与常规文件

## 磁盘访问

我们操作系统中的文件系统环境需要能够访问磁盘，但是我们还没有在我们的内核中实现任何磁盘访问功能。我们没有采用传统的“单体”操作系统策略，即向内核添加 IDE 磁盘驱动程序以及必要的系统调用以允许文件系统访问它，而是将 IDE 磁盘驱动程序实现为用户级文件的一部分系统环境。我们仍然需要稍微修改内核，以便进行设置，以便文件系统环境具有实现磁盘访问本身所需的权限。

### 练习 1 env_create

```c
void
env_create(uint8_t *binary, enum EnvType type)
{
	// LAB 3: Your code here.

	// If this is the file server (type == ENV_TYPE_FS) give it I/O privileges.
	// LAB 5: Your code here.
	struct Env* newenv;
	env_alloc(&newenv, 0);
	load_icode(newenv, binary);
	newenv->env_type = type;
	if (type == ENV_TYPE_FS) {
		newenv->env_tf.tf_eflags |= FL_IOPL_3;
	}
}
```

- 当您随后从一种环境切换到另一种环境时，您是否需要做任何其他事情来确保此 I/O 权限设置得到正确保存和恢复？为什么？

不需要，这个会自动被 trap frame 保存。

## 块缓存

在我们的文件系统中，我们将在处理器的虚拟内存系统的帮助下实现一个简单的“缓冲区缓存”（实际上只是一个块缓存）。块缓存的代码在fs/bc.c 中。

### 练习 2. 实现fs/bc.c 中的bc_pgfault和flush_block 函数

```c
	// Allocate a page in the disk map region, read the contents
	// of the block from the disk into that page.
	// Hint: first round addr to page boundary. fs/ide.c has code to read
	// the disk.
	//
	// LAB 5: you code here:
	if ((r = sys_page_alloc(0, ROUNDDOWN(addr, PGSIZE), PTE_SYSCALL)) < 0)
		panic("allocating at %x in page fault handler: %e", addr, r);
	
	r = ide_read(blockno * BLKSECTS, ROUNDDOWN(addr, PGSIZE), BLKSECTS);
	if (r < 0) {
		panic("bc_pgfault failed");
	}
```

```c
// Flush the contents of the block containing VA out to disk if
// necessary, then clear the PTE_D bit using sys_page_map.
// If the block is not in the block cache or is not dirty, does
// nothing.
// Hint: Use va_is_mapped, va_is_dirty, and ide_write.
// Hint: Use the PTE_SYSCALL constant when calling sys_page_map.
// Hint: Don't forget to round addr down.
void
flush_block(void *addr)
{
	uint32_t blockno = ((uint32_t)addr - DISKMAP) / BLKSIZE;

	if (addr < (void*)DISKMAP || addr >= (void*)(DISKMAP + DISKSIZE))
		panic("flush_block of bad va %08x", addr);

	// LAB 5: Your code here.
	if (!va_is_mapped(addr) || !va_is_dirty(addr)) {
		return;
	}
	int r = ide_write(blockno * BLKSECTS, ROUNDDOWN(addr, PGSIZE), BLKSECTS);
	if (r < 0) {
		panic("flush_block failed");
	}
	if ((r = sys_page_map(0, ROUNDDOWN(addr, PGSIZE), 0, ROUNDDOWN(addr, PGSIZE), uvpt[PGNUM(addr)] & PTE_SYSCALL)) < 0)
		panic("in flush_block, sys_page_map: %e", r);
}
```

## 块位图
### 练习3. alloc_block

```c

alloc_block(void)
{
	// The bitmap consists of one or more blocks.  A single bitmap block
	// contains the in-use bits for BLKBITSIZE blocks.  There are
	// super->s_nblocks blocks in the disk altogether.

	// LAB 5: Your code here.
	for (size_t i = 0; i < super->s_nblocks/32; i++){
		if (bitmap[i]) {
			for (size_t blockno = 0; blockno < 32; blockno++) {
				if (bitmap[i] & 1<<blockno) {
					bitmap[i] &= ~(1<<blockno);
					flush_block(bitmap + i);
					// cprintf("alloc_block %d\n", i*32 + blockno);
					return i*32 + blockno;
				}
			}
			panic("loop not here");
		}
	}
	return -E_NO_DISK;
}

```

## 文件操作

通读fs/fs.c 中的所有代码， 并确保在继续之前了解每个函数的作用。

### 练习 4. file_block_walk 和file_get_block

这里需要注意：

- 对文件索引进行的操作应该立刻被写会磁盘；
- 在对指针进行加减的时候需要注意一下系数问题；

```c

// Find the disk block number slot for the 'filebno'th block in file 'f'.
// Set '*ppdiskbno' to point to that slot.
// The slot will be one of the f->f_direct[] entries,
// or an entry in the indirect block.
// When 'alloc' is set, this function will allocate an indirect block
// if necessary.
//
// Returns:
//	0 on success (but note that *ppdiskbno might equal 0).
//	-E_NOT_FOUND if the function needed to allocate an indirect block, but
//		alloc was 0.
//	-E_NO_DISK if there's no space on the disk for an indirect block.
//	-E_INVAL if filebno is out of range (it's >= NDIRECT + NINDIRECT).
//
// Analogy: This is like pgdir_walk for files.
// Hint: Don't forget to clear any block you allocate.
static int
file_block_walk(struct File *f, uint32_t filebno, uint32_t **ppdiskbno, bool alloc)
{
       // LAB 5: Your code here.
       if (filebno >= NDIRECT + NINDIRECT) {
		   return -E_INVAL;
	   }
	   // cprintf("file_block_walk %d %08x %08x %08x\n", filebno, &(f->f_indirect), f->f_indirect, super->s_nblocks);
	   if (filebno < NDIRECT) {
		   if (ppdiskbno) {
			   *ppdiskbno = f->f_direct + filebno;
		   }
		   return 0;
	   } else {
		   if (!f->f_indirect) {
			   	if (!alloc) {
				   return -E_NOT_FOUND;
			   	}
	 		   	int r = alloc_block();
				if (r < 0) {
					// cprintf("there's no space on the disk for an indirect block");
					return r;
				}
				// cprintf("alloc_block in file_block_walk %08x\n", r);
				f->f_indirect = r;
				void* addr = diskaddr(f->f_indirect);
				memset(addr, 0, BLKSIZE);
				flush_block(addr);
		    }
		    if (ppdiskbno) {
				*ppdiskbno = diskaddr(f->f_indirect) + (filebno - NDIRECT)*4;
			}
			//cprintf("file_block_walk %08x  %08x %08x\n", &(f->f_indirect), f->f_indirect, **ppdiskbno);
		    return 0;
	   }
	   panic("not reach here");
}

// Set *blk to the address in memory where the filebno'th
// block of file 'f' would be mapped.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_NO_DISK if a block needed to be allocated but the disk is full.
//	-E_INVAL if filebno is out of range.
//
// Hint: Use file_block_walk and alloc_block.
int
file_get_block(struct File *f, uint32_t filebno, char **blk)
{
    int r;
	uint32_t *ppdiskbno;
	
	// cprintf("enter file_get_block");
	r = file_block_walk(f, filebno, &ppdiskbno, true);
	if (r < 0) {
		return r;
	}
	// cprintf("file_get_block start ppdiskbno %08x\n", *ppdiskbno);
	void* addr;
    if (!*ppdiskbno) {
		r = alloc_block();
		if (r < 0) {
			// cprintf("there's no space on the disk for an indirect block");
			return r;
		}
		addr = diskaddr(r);
		// cprintf("alloc_block in file_get_block %08x\n", r);
		*ppdiskbno = r;
		memset(addr, 0, BLKSIZE);
		flush_block(ppdiskbno);
	} else {
		addr = diskaddr(*ppdiskbno);
	}
	assert(blk);
	// cprintf("file_get_block success %08x\n", addr);
	*blk = addr;
	return 0;
}

```

## 文件系统界面

由于其他环境无法直接调用文件系统环境中的函数，我们将通过构建在 JOS 的 IPC 机制之上的远程过程调用或 RPC 抽象公开对文件系统环境的访问。

### 练习5 实现serve_read

```c

// Read at most ipc->read.req_n bytes from the current seek position
// in ipc->read.req_fileid.  Return the bytes read from the file to
// the caller in ipc->readRet, then update the seek position.  Returns
// the number of bytes successfully read, or < 0 on error.
int
serve_read(envid_t envid, union Fsipc *ipc)
{
	struct Fsreq_read *req = &ipc->read;
	struct Fsret_read *ret = &ipc->readRet;
	struct OpenFile *o;
	int r;
	int read_size;

	if (debug)
		cprintf("serve_read %08x %08x %08x\n", envid, req->req_fileid, req->req_n);

	// Lab 5: Your code here:
	if ((r = openfile_lookup(envid, req->req_fileid, &o)) < 0)
		return r;

	read_size = file_read(o->o_file, ret->ret_buf, req->req_n, o->o_fd->fd_offset);
	o->o_fd->fd_offset += read_size;
	if (debug)
		cprintf("serve_read success %08x %08x %08x %s\n", envid, read_size, o->o_fd->fd_offset, ret->ret_buf);

	return read_size;
}

```

### 练习6 实现serve_write

```c

// Write req->req_n bytes from req->req_buf to req_fileid, starting at
// the current seek position, and update the seek position
// accordingly.  Extend the file if necessary.  Returns the number of
// bytes written, or < 0 on error.
int
serve_write(envid_t envid, struct Fsreq_write *req)
{
	if (debug)
		cprintf("serve_write %08x %08x %08x\n", envid, req->req_fileid, req->req_n);

	// LAB 5: Your code here.
	struct OpenFile *o;
	int r;
	int write_size = 0;

	if ((r = openfile_lookup(envid, req->req_fileid, &o)) < 0)
		return r;
	write_size = file_write(o->o_file, req->req_buf, req->req_n, o->o_fd->fd_offset);
	o->o_fd->fd_offset += write_size;
	if (debug)
		cprintf("serve_write success %08x %08x %08x %s\n", envid, write_size, o->o_fd->fd_offset, req->req_buf);
	return write_size;
}

```

## spawn

我们实现spawn而不是 UNIX 风格 exec 因为spawn更容易以“外内核方式”从用户空间实现，无需内核的特殊帮助。

### 练习 7 sys_env_set_trapframe

```c
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
```

### 练习 8

copy_shared_pages
```c
// Copy the mappings for shared pages into the child address space.
static int
copy_shared_pages(envid_t child)
{	
	extern unsigned char end[];
	int r;
	uint8_t *addr = (uint8_t *)UTEXT;//(uint8_t *)ROUNDDOWN((uint8_t *)end, PGSIZE);

	//cprintf("copy_shared_pages %08x\n", addr);
	for (addr += PGSIZE; addr < (uint8_t*)USTACKTOP; addr += PGSIZE) {
		if ((uvpd[PDX(addr)] & PTE_P) && (uvpt[PGNUM(addr)] & PTE_P) && (uvpt[PGNUM(addr)] & PTE_SHARE)) {
			//cprintf("duppage PTE_SHARE %08x\n", addr);
			r = sys_page_map(0, addr, child, addr, PTE_SYSCALL);
			if (r < 0)
				panic("sys_page_map: %e", r);
		}
	}
	return 0;
}

```

fork
```c
	//cprintf("fork duppage dump_duppage page %08x\n", ROUNDDOWN(&addr, PGSIZE));
	for (addr; addr < (uint8_t*)USTACKTOP; addr += PGSIZE) {
		if ((uvpd[PDX(addr)] & PTE_P) && (uvpt[PGNUM(addr)] & PTE_P) && (uvpt[PGNUM(addr)] & PTE_SHARE)) {
			//cprintf("fork duppage PTE_SHARE page %08x\n", addr);
			r = sys_page_map(0, addr, envid, addr, PTE_SYSCALL);
			if (r < 0)
				panic("sys_page_map: %e", r);
		}
	}
```

## 键盘接口

为了让 shell 工作，我们需要一种方法来输入它。

### 练习 9. IRQ_OFFSET+IRQ_KBD和IRQ_OFFSET+IRQ_SERIAL

```c
	// Handle keyboard and serial interrupts.
	// LAB 5: Your code here.
	if (tf->tf_trapno == IRQ_OFFSET + IRQ_KBD) {
		//cprintf("IRQ_KBD interrupt\n");
		kbd_intr();
		return;
	}

	if (tf->tf_trapno == IRQ_OFFSET + IRQ_SERIAL) {
		//cprintf("IRQ_SERIAL interrupt\n");
		serial_intr();
		return;
	}
```
## shell

### 练习 10 shell I/O 重定向

```c
			// Open 't' for reading as file descriptor 0
			// (which environments use as standard input).
			// We can't open a file onto a particular descriptor,
			// so open the file as 'fd',
			// then check whether 'fd' is 0.
			// If not, dup 'fd' onto file descriptor 0,
			// then close the original 'fd'.
			if ((fd = open(t, O_RDONLY)) < 0) {
				cprintf("open %s for read: %e", t, fd);
				exit();
			}

			// LAB 5: Your code here.
			if (fd != 0) {
				dup(fd, 0);
				close(fd);
			}
```