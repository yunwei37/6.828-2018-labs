# 6.828-labs 

MIT 6.828 operating system engineering 2018 fall 

这里是我自己探索实现 MIT 6.828 lab 的笔记记录，会包含一部分代码注释和要求的翻译记录，以及踩过的坑/个人的解决方案；顺带还有每个阶段的源代码目录。

MIT 6.828 官方网站：https://pdos.csail.mit.edu/6.828/2018/index.html 详细的资料和 lab 源代码可以在上面找到。

2020 年对于6.828进行了一下拆分，降低了一些难度，同时换成了 RISC-V 指令集。18 年的版本难度会稍微更大一点，主要关注于：

- x86 体系结构相关；
- SMP 设计

查看每个 lab 具体对应的源代码，请切换到对应分支。

## Documents

1. [工具链搭建](notes/工具链.md)
2. [lab1 启动PC](notes/lab1.md)
3. [lab2 内存管理](notes/lab2.md)
4. [lab3A 用户环境和异常处理](notes/lab3A.md)
5. [lab3B 页面错误，断点异常和系统调用](notes/lab3B.md)
6. [lab4A 多处理器支持和协作多任务](notes/lab4A.md)
7. [lab4B/C Copy-on-Write Fork/抢占式多任务和进程间通信 (IPC)](notes/lab4BC.md)
8. [Lab 5: File system, Spawn and Shell](notes/lab5.md)
9. [Lab 6: 网络驱动程序](notes/lab6.md)


