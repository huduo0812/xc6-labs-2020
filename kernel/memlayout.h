// 
// xv6 内存布局头文件
// 定义了物理内存和虚拟内存中各个重要区域的地址。
//

// -------------------- 物理内存布局 (Physical Memory Layout) --------------------
//
// qemu -machine virt 模拟的RISC-V硬件平台的物理内存映射如下：
// (信息来源: qemu/hw/riscv/virt.c)
//
// 0x00001000 -- 引导ROM (Boot ROM)，由QEMU提供。
// 0x02000000 -- CLINT (Core Local Interruptor)，核心本地中断器，包含时钟等。
// 0x0c000000 -- PLIC (Platform-Level Interrupt Controller)，平台级中断控制器。
// 0x10000000 -- UART0 (Universal Asynchronous Receiver-Transmitter)，串口设备。
// 0x10001000 -- VIRTIO disk，virtio虚拟磁盘接口。
// 0x80000000 -- 物理内存(RAM)的起始地址。引导ROM会跳转到这里，内核也加载到这里。
// ...        -- 0x80000000 之后是可用的物理内存。

// 内核对物理内存的使用方式如下:
// 0x80000000 -- entry.S的入口点，紧接着是内核的代码段和数据段。
// end        -- (由链接脚本定义) 内核代码和数据段的结束位置，也是内核页分配区的开始。
// PHYSTOP    -- 内核可使用的物理内存的最高地址。

// -------------------- 设备物理地址定义 --------------------

// QEMU将UART串口设备的寄存器映射到这个物理地址。
#define UART0 0x10000000L
#define UART0_IRQ 10 // UART的中断号

// virtio 内存映射I/O (MMIO) 接口的物理地址。
#define VIRTIO0 0x10001000
#define VIRTIO0_IRQ 1 // virtio磁盘的中断号

// CLINT (核心本地中断器) 的物理地址，包含机器模式的计时器。
#define CLINT 0x2000000L
#define CLINT_MTIMECMP(hartid) (CLINT + 0x4000 + 8*(hartid)) // 每个CPU核心的计时器比较寄存器
#define CLINT_MTIME (CLINT + 0xBFF8) // 当前计时器的值（自启动以来的时钟周期数）

// PLIC (平台级中断控制器) 的物理地址，处理外部设备中断。
#define PLIC 0x0c000000L
#define PLIC_PRIORITY (PLIC + 0x0)
#define PLIC_PENDING (PLIC + 0x1000)
#define PLIC_MENABLE(hart) (PLIC + 0x2000 + (hart)*0x100)
#define PLIC_SENABLE(hart) (PLIC + 0x2080 + (hart)*0x100)
#define PLIC_MPRIORITY(hart) (PLIC + 0x200000 + (hart)*0x2000)
#define PLIC_SPRIORITY(hart) (PLIC + 0x201000 + (hart)*0x2000)
#define PLIC_MCLAIM(hart) (PLIC + 0x200004 + (hart)*0x2000)
#define PLIC_SCLAIM(hart) (PLIC + 0x201004 + (hart)*0x2000)

// -------------------- 内核虚拟地址空间布局 --------------------

// 内核期望物理地址从0x80000000到PHYSTOP的RAM用于内核和用户页。
// KERNBASE定义了内核的基地址，内核的虚拟地址和物理地址从这里开始是相同的（直接映射）。
#define KERNBASE 0x80000000L
// 定义了物理内存的最高点。这里设置为基地址+128MB。
#define PHYSTOP (KERNBASE + 128*1024*1024)

// 将trampoline页面映射到虚拟地址空间的最高处。
// 这个页面在用户和内核空间都会被映射，用于用户态和内核态的切换。
#define TRAMPOLINE (MAXVA - PGSIZE)

// 将每个进程的内核栈映射到trampoline页面的下方。
// 每个栈的大小是一个页面，并且上下各有一个无效的保护页（Guard Page）来防止栈溢出。
// (p) 是进程在proc数组中的索引。
#define KSTACK(p) (TRAMPOLINE - ((p)+1)* 2*PGSIZE)

// -------------------- 用户进程虚拟地址空间布局 --------------------
//
// 用户内存布局从虚拟地址0开始，向上增长：
//   text (代码段)
//   original data and bss (初始化和未初始化的数据段)
//   fixed-size stack (固定大小的用户栈)
//   expandable heap (可扩展的堆，通过sbrk系统调用增长)
//   ...
//   TRAPFRAME (p->trapframe，位于trampoline下方，用于保存陷入内核时的用户上下文)
//   TRAMPOLINE (与内核映射的是同一个物理页面)

// TRAPFRAME的地址，正好在TRAMPOLINE页的下方。
// 当从用户态陷入内核态时，trampoline代码会在这里保存用户的寄存器。
#define TRAPFRAME (TRAMPOLINE - PGSIZE)
