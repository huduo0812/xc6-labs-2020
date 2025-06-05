// user/sleep.c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char **argv) 
{
	if (argc < 2) 
    {
		fprintf(2, "缺少参数，例如sleep 10\n");
		exit(1);
	}
	sleep(atoi(argv[1]));
	exit(0);
}