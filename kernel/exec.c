#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "elf.h"

static int loadseg(pde_t *pgdir, uint64 addr, struct inode *ip, uint offset, uint sz);

int
exec(char *path, char **argv)
{
  char *s, *last;
  int i, off;
  uint64 argc, sz = 0, sp, ustack[MAXARG+1], stackbase;
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  pagetable_t pagetable = 0, oldpagetable;
  struct proc *p = myproc();

  begin_op();

  // 1. 打开路径对应的可执行文件
  // namei() 会返回一个锁定的 inode
  if((ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);

  // 2. 检查 ELF 头部
  // readi() 从 inode 读取数据
  if(readi(ip, 0, (uint64)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto bad;
  // 检查 ELF 魔数，确认是 ELF 文件
  if(elf.magic != ELF_MAGIC)
    goto bad;

  // 3. 为新程序创建一个新的页表
  if((pagetable = proc_pagetable(p)) == 0)
    goto bad;

  // 4. 加载程序段到内存
  // 遍历 ELF 文件中的所有程序头（program header）
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    if(readi(ip, 0, (uint64)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    // 只处理可加载的段
    if(ph.type != ELF_PROG_LOAD)
      continue;
    if(ph.memsz < ph.filesz) // 内存大小不能小于文件大小
      goto bad;
    if(ph.vaddr + ph.memsz < ph.vaddr) // 检查地址溢出
      goto bad;
    uint64 sz1;
    // 为段分配内存
    if((sz1 = uvmalloc(pagetable, sz, ph.vaddr + ph.memsz)) == 0)
      goto bad;

    if(sz1 >= PLIC) // 防止程序内存大小超过 PLIC
      goto bad;
    sz = sz1;
    if(ph.vaddr % PGSIZE != 0) // 虚拟地址必须页对齐
      goto bad;
    // 调用 loadseg 将段内容从文件加载到内存
    if(loadseg(pagetable, ph.vaddr, ip, ph.off, ph.filesz) < 0)
      goto bad;
  }
  // 加载完成，解锁并释放 inode
  iunlockput(ip);
  end_op();
  ip = 0;

  p = myproc();
  uint64 oldsz = p->sz;

  // 5. 分配用户栈
  // 将程序大小向上取整到页边界
  sz = PGROUNDUP(sz);
  uint64 sz1;
  // 分配两页内存：一页用作栈，另一页作为保护页（guard page）防止栈溢出
  if((sz1 = uvmalloc(pagetable, sz, sz + 2*PGSIZE)) == 0)
    goto bad;
  sz = sz1;
  // 清空栈页内容
  uvmclear(pagetable, sz-2*PGSIZE);
  // sp (stack pointer) 初始化为栈的最高地址
  sp = sz;
  // 栈底地址，用于检查栈溢出
  stackbase = sp - PGSIZE;

  // 6. 准备命令行参数 (argv)
  // 将参数字符串逐个复制到栈上
  for(argc = 0; argv[argc]; argc++) {
    if(argc >= MAXARG) // 参数数量不能超过最大限制
      goto bad;
    // 为参数字符串分配空间
    sp -= strlen(argv[argc]) + 1;
    sp -= sp % 16; // RISC-V 要求栈指针16字节对齐
    if(sp < stackbase) // 检查栈是否溢出
      goto bad;
    // 将参数字符串从内核空间复制到用户栈
    if(copyout(pagetable, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    // 在 ustack 数组中保存每个参数在栈上的地址
    ustack[argc] = sp;
  }
  ustack[argc] = 0; // argv 数组以空指针结尾

  // 将 argv 指针数组复制到栈上
  sp -= (argc+1) * sizeof(uint64);
  sp -= sp % 16; // 保持16字节对齐
  if(sp < stackbase)
    goto bad;
  // 将 ustack (包含指向参数字符串的指针) 复制到用户栈
  if(copyout(pagetable, sp, (char *)ustack, (argc+1)*sizeof(uint64)) < 0)
    goto bad;

  // 7. 设置用户 main 函数的参数 (argc, argv)
  // argc 通过系统调用的返回值传递给 a0 寄存器
  // argv (指向指针数组的指针) 存入 a1 寄存器
  p->trapframe->a1 = sp;

  // 为调试目的，保存程序名
  for(last=s=path; *s; s++)
    if(*s == '/')
      last = s+1;
  safestrcpy(p->name, last, sizeof(p->name));
    
  // 8. 提交新的程序镜像
  // 切换到新的页表
  oldpagetable = p->pagetable;
  p->pagetable = pagetable;
  // 更新进程大小
  p->sz = sz;
  // 设置程序计数器为 ELF 文件的入口点 (通常是 _start)
  p->trapframe->epc = elf.entry;
  // 设置栈指针
  p->trapframe->sp = sp;
  // 释放旧的页表和内存
  proc_freepagetable(oldpagetable, oldsz);

  if (p->pid == 1) {
    vmprint(p->pagetable); // 打印 init 进程的页表
  }

  // 返回 argc，这个值会放在 a0 寄存器中，作为 main 的第一个参数
  return argc;

// 错误处理：如果上述任何步骤失败，则跳转到这里
bad:
  if(pagetable)
    proc_freepagetable(pagetable, sz); // 释放新创建的页表
  if(ip){
    iunlockput(ip); // 释放 inode 锁
    end_op();
  }
  return -1;
}

// 将一个程序段加载到页表中指定的虚拟地址 va
// va 必须是页对齐的
// 从 va 到 va+sz 的页面必须已经被映射
// 成功返回0，失败返回-1
static int
loadseg(pagetable_t pagetable, uint64 va, struct inode *ip, uint offset, uint sz)
{
  uint i, n;
  uint64 pa;

  if((va % PGSIZE) != 0)
    panic("loadseg: va must be page aligned");

  for(i = 0; i < sz; i += PGSIZE){
    pa = walkaddr(pagetable, va + i);
    if(pa == 0)
      panic("loadseg: address should exist");
    if(sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    if(readi(ip, 0, (uint64)pa, offset+i, n) != n)
      return -1;
  }
  
  return 0;
}
