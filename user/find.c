// user/find.c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

// 递归查找函数，查找路径为 path 的目录下是否有目标文件 target
void find(char *path, char *target)
{
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;
    // 打开目录
    if ((fd = open(path, 0)) < 0)
    { // fd < 0 表示打开目录失败
        fprintf(2, "find: cannot open %s\n", path); // 向标准错误输出错误信息
        return;                                     // 结束当前函数调用
    }
    // 获取目录的状态信息
    // fstat 系统调用获取通过文件描述符 fd 打开的文件的状态信息，
    // 并将其存储在 st 指向的 stat 结构体中。
    // 这些信息包括文件类型、大小、inode 号等。
    if (fstat(fd, &st) < 0) // 通过文件描述符 fd 获取文件状态信息
    { // fstat 返回负值表示获取状态失败
        fprintf(2, "find: cannot stat %s\n", path); // 向标准错误输出错误信息
        close(fd);                                  // 关闭先前打开的文件描述符
        return;                                     // 结束当前函数调用
    }
    // 文件、目录分别处理
    switch (st.type) // 根据获取到的文件类型 (st.type) 进行分支处理
    {
    // 如果是文件，检查文件名是否与目标文件名匹配
    case T_FILE: // 当前路径指向一个文件
        // 比较路径 path 的末尾部分是否与 target 字符串相同。
        // target 在 main 函数中被构造成 "/filename" 的形式。
        // path + strlen(path) - strlen(target) 计算出 path 中一个位置，
        // 从这个位置开始的子字符串长度与 target 相同，用于和 target 比较。
        // 注意: 如果 strlen(path) < strlen(target)，这里可能存在越界访问风险。
        if (strcmp(path + strlen(path) - strlen(target), target) == 0)
        {
            printf("%s\n", path); // 若匹配，则打印文件的完整路径
        }
        else
        {
            printf("find: %s not found\n", target); // 如果不匹配，则打印未找到信息
        }
        break;
    // 如果是目录
    case T_DIR: // 当前路径指向一个目录
        // 检查路径长度是否超出缓冲区大小
        // 准备构造子目录/文件的完整路径，格式为 path + "/" + de.name + "\0"
        // DIRSIZ 是目录项中文件名的最大长度。
        // strlen(path) 当前路径长度
        // +1 for '/' 分隔符
        // + DIRSIZ 为可能的最大文件名长度
        // +1 for '\0' 字符串结束符
        if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf)
        {
            printf("find: path too long\n"); // 如果构造的路径长度可能超出buf大小，则打印错误信息
            break;                          // 终止当前目录的处理，跳出 switch
        }
        strcpy(buf, path);      // 将当前目录路径复制到缓冲区 buf
        p = buf + strlen(buf);  // 指针 p 指向 buf 中当前路径字符串的末尾
        *p++ = '/';             // 在路径末尾添加 '/' 分隔符，然后 p 指向分隔符之后的位置
        // 读取目录项
        // 循环读取目录 fd 中的每一个目录条目 (struct dirent de)
        // read 函数尝试读取 sizeof(de) 大小的数据到 de。
        // 若成功读取一个完整的目录条目，返回值等于 sizeof(de)。
        while (read(fd, &de, sizeof(de)) == sizeof(de))
        {
            if (de.inum == 0) // 跳过无效的目录项
                continue;
            memmove(p, de.name, DIRSIZ); // 将目录项中的文件名拷贝到缓冲区
            p[DIRSIZ] = 0;               // 添加字符串结束符
            // 获取目录项的状态信息
            if (stat(buf, &st) < 0)
            {
                printf("find: cannot stat %s\n", buf);
                continue;
            }
            // 排除 "." 和 ".." 目录
            if (strcmp(buf + strlen(buf) - 2, "/.") != 0 && strcmp(buf + strlen(buf) - 3, "/..") != 0)
            {
                find(buf, target); // 递归查找子目录
            }
        }
        break;
    }
    close(fd); // 关闭目录
}

int main(int argc, char *argv[])
{
    if (argc < 3)
    { // 如果参数不足，退出程序
        exit(0);
    }
    char target[512];
    target[0] = '/';             // 为查找的文件名添加 / 在开头
    strcpy(target + 1, argv[2]); // 将目标文件名存储在 target 中
    find(argv[1], target);       // 调用查找函数
    exit(0);
}