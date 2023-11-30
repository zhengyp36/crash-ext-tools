#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <setjmp.h>
#include <libgen.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <crash/defs.h>

static void
cmd_extpy(void)
{
	printf("Error: *** extpy is not implemented\n");
}

static char *help_extpy[] = {
	"extpy",
	"Execute external python scripts from crash",
	"<script.py> [args...]",
	"  This command executes external python scripts.",
	"\nEXAMPLE",
	"  crash> extpy demo.py arg0 arg1",
	"\nVERSION",
	"  crash-version : " EXT_CRASH_TO_STR(CRASH_VERSION),
	"  cpu-type : " EXT_CRASH_TO_STR(CRASH_CPU_TYPE),
	NULL
};

static struct command_table_entry command_table[] = {
	{ "extpy", cmd_extpy, help_extpy, 0},
	{ NULL },
};

static void __attribute__((constructor, used))
extpy_init(void)
{
	register_extension(command_table);
}

static void __attribute__((destructor, used))
extpy_fini(void)
{
}
