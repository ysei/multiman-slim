#include <stdio.h>
#include <string.h>
#include <sys/process.h>

SYS_PROCESS_PARAM(1001, 0x10000)

int main(int argc, char **argv)
{
	(void) argc;
	char tn[128];
	char filename[128];

	sprintf(tn, "%s", argv[0]);
	char *pch=tn;
	char *pathpos=strrchr(pch,'/');
	pathpos[0]=0;

	sprintf(filename, "%s/RELOADED.BIN", tn);
	remove(filename);

	sprintf(filename, "%s/RELOAD.SELF", tn);
	sys_game_process_exitspawn2(filename, NULL, NULL, NULL, 0, 1200, SYS_PROCESS_PRIMARY_STACK_SIZE_1M);
}
