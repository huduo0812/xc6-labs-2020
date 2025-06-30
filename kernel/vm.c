#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"

/*
 * xv6 虚拟内存管理实现文件。
 * 主要负责页表的创建、映射、查找、拷贝、释放等操作。
 * 支持RISC-V Sv39三级页表结构。
 */

/*
 * 内核页表，全局变量。
 * 用于内核空间的虚拟地址到物理地址的映射。
 */
pagetable_t kernel_pagetable;

extern char etext[];  // 由kernel.ld设置，指向内核代码的结尾。
extern char trampoline[]; // trap返回用的trampoline代码，映射到最高虚拟地址。

/*
 * 创建内核的直接映射页表。
 * 只在内核启动时调用一次。
 * 将设备寄存器、内核代码、内核数据、trampoline等映射到内核虚拟地址空间。
 */
void
kvminit()
{
  kernel_pagetable = (pagetable_t) kalloc(); // 分配一页作为顶级页表
  memset(kernel_pagetable, 0, PGSIZE);       // 清零

  // 映射串口UART寄存器
  kvmmap(UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // 映射virtio磁盘接口
  kvmmap(VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // 映射CLINT（定时器/软件中断控制器）
  kvmmap(CLINT, CLINT, 0x10000, PTE_R | PTE_W);

  // 映射PLIC（外部中断控制器）
  kvmmap(PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // 映射内核代码段（只读+可执行）
  kvmmap(KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // 映射内核数据段和物理内存（只读+可写）
  kvmmap((uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // 映射trampoline代码到最高虚拟地址
  kvmmap(TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
}

/*
 * 切换硬件页表寄存器到内核页表，并开启分页。
 * 每个CPU核启动时都要调用。
 */
void
kvminithart()
{
  w_satp(MAKE_SATP(kernel_pagetable)); // 设置satp寄存器，切换到内核页表
  sfence_vma(); // 刷新TLB
}

/*
 * 返回虚拟地址va在页表pagetable中的PTE指针。
 * 如果alloc!=0，则在需要时分配中间页表页。
 * Sv39三级页表，每级9位索引。
 * 返回的是最低一级（叶子）PTE的指针。
 */
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA)
    panic("walk");

  for(int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)]; // 取出当前级的PTE
    if(*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte); // 有效则进入下一层页表
    } else {
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE); // 新分配的页表清零
      *pte = PA2PTE(pagetable) | PTE_V; // 设置PTE为有效，指向新页表
    }
  }
  return &pagetable[PX(0, va)]; // 返回叶子PTE指针
}

/*
 * 查找虚拟地址va对应的物理地址。
 * 只用于查找用户页。
 * 如果没有映射，返回0。
 */
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if(pte == 0)
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

/*
 * 向内核页表添加一条映射。
 * 只在启动时使用。
 * va: 虚拟地址，pa: 物理地址，sz: 映射长度，perm: 权限
 */
void
kvmmap(uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(kernel_pagetable, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

/*
 * 将内核虚拟地址va转换为物理地址。
 * 只用于栈上的地址，假设va已页对齐。
 */
uint64
kvmpa(uint64 va)
{
  uint64 off = va % PGSIZE;
  pte_t *pte;
  uint64 pa;
  
  pte = walk(kernel_pagetable, va, 0);
  if(pte == 0)
    panic("kvmpa");
  if((*pte & PTE_V) == 0)
    panic("kvmpa");
  pa = PTE2PA(*pte);
  return pa+off;
}

/*
 * 在页表pagetable中为虚拟地址va开始的size字节创建映射，映射到物理地址pa。
 * va和size不一定页对齐。
 * perm为权限。
 * 返回0表示成功，-1表示walk分配页表失败。
 */
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  a = PGROUNDDOWN(va);
  last = PGROUNDDOWN(va + size - 1);
  for(;;){
    if((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    if(*pte & PTE_V)
      panic("remap");
    *pte = PA2PTE(pa) | perm | PTE_V; // 设置PTE，建立映射
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0)
      panic("uvmunmap: walk");
    if((*pte & PTE_V) == 0)
      panic("uvmunmap: not mapped");
    if(PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    if(do_free){
      uint64 pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
    *pte = 0;
  }
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t) kalloc();
  if(pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void
uvminit(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W|PTE_R|PTE_X|PTE_U);
  memmove(mem, src, sz);
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  char *mem;
  uint64 a;

  if(newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for(a = oldsz; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_W|PTE_X|PTE_R|PTE_U) != 0){
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

// 释放用户页表中oldsz到newsz之间的物理页。
// oldsz和newsz不需要页对齐，也不要求newsz<oldsz。
// 返回新的进程大小。
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;

  // 只释放页对齐部分，剩余部分不处理。
  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1); // 解除映射并释放物理内存
  }

  return newsz;
}

// 递归释放页表本身（不包括叶子节点映射的物理页）。
// 调用前应确保所有叶子映射已被移除。
void
freewalk(pagetable_t pagetable)
{
  // 每个页表有512个PTE（2^9）。
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    // 如果该PTE有效且不是叶子节点（即没有R/W/X权限），说明它指向下一层页表。
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // 递归释放下一层页表。
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0; // 清空PTE
    } else if(pte & PTE_V){
      // 如果是有效的叶子节点，说明调用者没有先解除映射，报错。
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable); // 释放本层页表占用的物理内存
}

// 释放用户内存（物理页），再递归释放页表本身。
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1); // 先释放所有物理页
  freewalk(pagetable); // 再释放所有页表
}

// 拷贝父进程的内存到子进程。
// 复制页表和物理内存。
// 成功返回0，失败返回-1（并释放已分配的资源）。
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char*)pa, PGSIZE); // 拷贝物理页内容
    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
      kfree(mem);
      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1); // 失败时释放已分配的物理页
  return -1;
}

// 将某个虚拟地址对应的PTE的用户访问权限去掉。
// 用于exec时设置用户栈保护页。
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U; // 清除用户访问权限
}

// 从内核拷贝数据到用户空间。
// 将src的len字节拷贝到pagetable的dstva虚拟地址处。
// 成功返回0，失败返回-1。
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n); // 拷贝数据

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// 从用户空间拷贝数据到内核。
// 将pagetable的srcva虚拟地址处的len字节拷贝到dst。
// 成功返回0，失败返回-1。
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n); // 拷贝数据

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// 从用户空间拷贝以\0结尾的字符串到内核。
// 最多拷贝max字节，遇到\0提前结束。
// 成功返回0，失败返回-1。
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  uint64 n, va0, pa0;
  int got_null = 0;

  while(got_null == 0 && max > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > max)
      n = max;

    char *p = (char *) (pa0 + (srcva - va0));
    while(n > 0){
      if(*p == '\0'){
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if(got_null){
    return 0;
  } else {
    return -1;
  }
}


// kernel/vm.c
// 递归打印页表
int pgtblprint(pagetable_t pagetable, int depth) {
    // there are 2^9 = 512 PTEs in a page table.
    for (int i = 0; i < 512; i++) {
        pte_t pte = pagetable[i];

        if (pte & PTE_V) {      // 如果页表项有效，按格式打印页表项
            printf("..");
            for (int j = 0;j < depth;++j)
                printf(" ..");
            printf("%d: pte %p pa %p\n", i, pte, PTE2PA(pte));


            // 如果该节点不是叶节点，递归打印子节点
            if ((pte & (PTE_R | PTE_W | PTE_X)) == 0) {
                // this PTE points to a lower-level page table.
                uint64 child = PTE2PA(pte);
                pgtblprint((pagetable_t)child, depth + 1);
            }
        }
    }

    return 0;
}

// 打印页表
int vmprint(pagetable_t pagetable) {
    printf("page table %p\n", pagetable);
    return pgtblprint(pagetable, 0);
}