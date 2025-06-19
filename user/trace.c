#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]) // 主函数，argc 是命令行参数的数量，argv 是参数字符串数组
{
  int i;
  char *nargv[MAXARG]; // 用于存储要执行的命令及其参数

  // 检查命令行参数。trace 命令需要至少两个参数：一个掩码和要执行的命令。
  if (argc < 3 || (argv[1][0] < '0' || argv[1][0] > '9'))
  {
    fprintf(2, "Usage: %s mask command\n", argv[0]); // 如果参数不足或掩码不是数字，则打印用法提示
    exit(1); // 退出程序，返回状态 1 表示错误
  }

  // 调用 trace 系统调用，并传入掩码。
  // atoi(argv[1]) 将字符串格式的掩码转换为整数。
  if (trace(atoi(argv[1])) < 0) 
  {
    fprintf(2, "%s: trace failed\n", argv[0]); // 如果 trace 调用失败，则打印错误信息
    exit(1); // 退出程序
  }
  
  // 将要执行的命令及其参数复制到 nargv 数组中。
  // i 从 2 开始，跳过 "trace" 命令本身和掩码。
  for (i = 2; i < argc && i < MAXARG; i++)
  {
    nargv[i-2] = argv[i];
  }
  exec(nargv[0], nargv); // 执行指定的命令
  exit(0); // 正常退出
}
