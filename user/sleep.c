// user/sleep.c
// 包含内核定义的基本类型
#include "kernel/types.h"
// 包含文件系统相关的结构体和常量
#include "kernel/stat.h"
// 包含用户态程序可用的系统调用和库函数
#include "user/user.h"

// main函数是程序的入口点
// argc是命令行参数的数量
// argv是指向命令行参数字符串数组的指针
int main(int argc, char **argv) 
{
	// 检查命令行参数的数量
	// 如果参数少于2个（程序名 + sleep时长），则打印错误信息并退出
	if (argc < 2) 
    {
		// fprintf用于向指定的文件流输出格式化字符串
		// 2代表标准错误输出
		fprintf(2, "缺少参数，例如sleep 10\n");
		// exit(1)表示程序异常退出
		exit(1);
	}
	// 调用sleep函数，使其暂停指定的秒数
	// atoi函数将字符串参数转换为整数
	// argv[1]是第一个命令行参数，即sleep的时长
	sleep(atoi(argv[1]));
	// exit(0)表示程序正常退出
	exit(0);
}