#include <ctype.h>
#include <assert.h>
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

/*
 * RUN CRASH-COMMANDS IN EXTPY-C-SOURCE:
 *
 * static void run_crash_cmd_start(void);
 *     @brief   : initialize crash_ctx_t
 *
 * static int run_crash_cmd(const char *output, const char *command);
 *     @brief   : run crash-or-gdb command
 *     @output  : redirect stdout into file <output>
 *     @command : 1) crash-command : NON_GDB <crash-cmd> [args...]
 *                2) gdb-command   : GDB <gdb-cmd> [args...]
 *                3) common-command: run as gdb-cmd if it's gdb-cmd
 *                                   else as crash-cmd
 *     @retval  : 0 ~ success; -1 ~ failure
 *
 * static void run_crash_cmd_done(void);
 *     @brief   : finalize crash_ctx_t
 */

typedef struct crash_ctx {
	FILE *	old_fp;
	FILE *	new_fp;
	int	bak_fd;
	int	isGDB;
	int	jmp_buf_is_saved;
	char	command_line[sizeof(pc->command_line)];
	char	command_bak[sizeof(pc->command_line)];
	jmp_buf	saved_jmp_buf;
} crash_ctx_t;

static crash_ctx_t crash_ctx;

static inline int
strequ(const char *s1, const char *s2)
{
	return (strcmp(s1,s2) == 0);
}

static int
parse_cmd_prefix(int *isGDB, int *cmdoff, const char *cmdline)
{
	int start = 0, end = argcnt;
	*isGDB = 0;
	*cmdoff = 0;

	// Wrong if having two or more prefixes
	while (start < end && start < 2) {
		const char *cmd = args[start];
		if (strequ(cmd, "NON_GDB")) {
			start++;
			*isGDB = 0;
		} else if (strequ(cmd, "GDB")) {
			start++;
			*isGDB = 1;
		} else if (strequ(cmd, "gdb")) {
			start++;
			*isGDB = 1;
		} else {
			break;
		}
	}

	if (start == 2) {
		printf("Error: *** prefix-cmds(%s vs %s) "
		    "repeat or conflict\n", args[0], args[1]);
		return (-1);
	} else if (start == 1) {
		int real_cnt = argcnt - start;
		for (int i = 0; i < real_cnt; i++)
			args[i] = args[start + i];
		for (int i = real_cnt; i < argcnt; i++)
			args[i] = NULL;
		argcnt = real_cnt;
	}

	if (argcnt > 0) {
		int has_prefix = start > 0;
		if (!has_prefix)
			*isGDB = is_gdb_command(FALSE, FAULT_ON_ERROR);
		else if (*isGDB && !is_gdb_command(FALSE, FAULT_ON_ERROR)) {
			printf("Error: *** cmd(%s) is not gdb-cmd.\n", args[0]);
			return (-1);
		} else if (*isGDB) {
			const char *cmd_start = strstr(cmdline, args[0]);
			assert(cmd_start);
			assert(cmd_start > cmdline);
			*cmdoff = cmd_start - cmdline;
		}
	} else {
		printf("Error: *** cmd is missing.\n");
		return (-1);
	}

	return (0);
}

static int
pass_crash_cmd(crash_ctx_t *ctx, const char *orig_cmd)
{
	snprintf(ctx->command_line, sizeof(ctx->command_line), "%s", orig_cmd);
	snprintf(ctx->command_bak, sizeof(ctx->command_bak), "%s", orig_cmd);
	argcnt = parse_line(ctx->command_line, args);

	int cmdoff = 0;
	int rc = parse_cmd_prefix(&ctx->isGDB, &cmdoff, ctx->command_bak);
	if (!rc && ctx->isGDB)
		snprintf(ctx->command_line, sizeof(ctx->command_line), "%s",
		    orig_cmd + cmdoff);
	return (rc);
}

static int
redirect_stdout(crash_ctx_t *ctx, const char *outf)
{
	ctx->new_fp = fopen(outf, "w");
	if (!ctx->new_fp) {
		printf("Error: *** Failed to open %s\n", outf);
		return (-1);
	}
	setbuf(ctx->new_fp, NULL);

	ctx->old_fp = fp;
	fp = pc->ofile = ctx->new_fp;

	fflush(stdout);
	ctx->bak_fd = dup(1);
	dup2(fileno(ctx->new_fp), 1);

	return (0);
}

static void
resume_stdout(crash_ctx_t *ctx)
{
	fflush(stdout);
	dup2(ctx->bak_fd, 1);
	close(ctx->bak_fd);

	pc->ofile = NULL;
	fp = stdout;

	fclose(ctx->new_fp);
}

static inline void
run_crash_cmd_start(void)
{
	memset(&crash_ctx, 0, sizeof(crash_ctx));
}

static inline void
run_crash_cmd_done(void)
{
	memset(&crash_ctx, 0, sizeof(crash_ctx));
}

static int
run_crash_cmd(const char *outf, const char *orig_cmd)
{
	crash_ctx_t *ctx = &crash_ctx;
	if (!ctx->jmp_buf_is_saved) {
		memcpy(ctx->saved_jmp_buf, pc->main_loop_env, sizeof(jmp_buf));
		ctx->jmp_buf_is_saved = 1;
	}

	int rc = pass_crash_cmd(ctx, orig_cmd);
	if (rc)
		return (rc);

	rc = redirect_stdout(ctx, outf);
	if (rc)
		return (rc);

	if (setjmp(pc->main_loop_env)) {
		ctx = &crash_ctx;
		memcpy(pc->main_loop_env, ctx->saved_jmp_buf, sizeof(jmp_buf));
		resume_stdout(ctx);
		return (-1);
	}

	if (ctx->isGDB) {
		int ok = gdb_pass_through(ctx->command_line,
		    ctx->new_fp, GNU_RETURN_ON_ERROR);
		rc = ok ? 0 : -1;
	} else
		exec_command();

	memcpy(pc->main_loop_env, ctx->saved_jmp_buf, sizeof(jmp_buf));
	resume_stdout(ctx);

	return (rc);
}

/*
 * PASS EXTPY ARGS AND TEST FUNCTION : run_crash_cmd()
 * Example: extpy foreach bt ; bt ; ps
 */

#define WR_STR(print, buf, sz, fmt...)					\
	do {								\
		int __r = print(buf, sz, fmt);				\
		if (__r > 0 && __r < sz) {				\
			(buf) += __r;					\
			(sz) -= __r;					\
		}							\
	} while (0)

typedef struct {
	char	buffer[4096];
	char	cmdbuffer[4096];
	char *	cmdvec[32];
	char *	pbuf;
	char *	pcmdbuf;
	char *	sep;
	char *	pcmd;
	int	size;
	int	cmdbufsz;
	int	argidx;
	int	cmdcnt;
} command_t;

static inline void
command_reset(command_t *cmd)
{
	cmd->cmdcnt++;
	if (cmd->cmdcnt > 0) {
		assert(cmd->pcmd);
		cmd->cmdvec[cmd->cmdcnt-1] = cmd->pcmdbuf;
		WR_STR(snprintf, cmd->pcmdbuf, cmd->cmdbufsz, "%s", cmd->pcmd);
		if (cmd->cmdbufsz > 0) {
			cmd->pcmdbuf++;
			cmd->cmdbufsz--;
			*cmd->pcmdbuf = '\0';
		}
	}

	cmd->pbuf = cmd->buffer;
	cmd->pbuf[0] = '\0';
	cmd->sep = "";
	cmd->pcmd = NULL;
	cmd->size = sizeof(cmd->buffer);
}

static inline void
command_init(command_t *cmd)
{
	cmd->argidx = 1;
	cmd->cmdcnt = -1;
	cmd->pcmdbuf = cmd->cmdbuffer;
	cmd->cmdbuffer[0] = '\0';
	cmd->cmdbufsz = sizeof(cmd->cmdbuffer);
	command_reset(cmd);
}

static int
command_parse(command_t *cmd)
{
	if (cmd->argidx > argcnt)
		return (0);

	int has_cmd = 0;
	if (cmd->argidx == argcnt)
		has_cmd = strlen(cmd->buffer) > 0;
	else if (strequ(args[cmd->argidx], ";"))
		has_cmd = strlen(cmd->buffer) > 0;
	else {
		if (cmd->pcmd)
			command_reset(cmd);
		WR_STR(snprintf, cmd->pbuf, cmd->size, "%s%s",
		    cmd->sep, args[cmd->argidx]);
		cmd->sep = " ";
	}
	cmd->argidx++;

	if (has_cmd)
		cmd->pcmd = cmd->buffer;

	return (1);
}

static void
command_run(command_t *cmd, int cmdid)
{
	char outf[128];
	snprintf(outf, sizeof(outf), "crash.stdout.%d.txt", cmdid);
	printf("run[%d]/> %s\n", cmdid, cmd->cmdvec[cmdid]);

	int rc = run_crash_cmd(outf, cmd->cmdvec[cmdid]);
	if (!rc)
		printf("Info : run cmd[%d] {%s} success\n",
		    cmd->cmdcnt, cmd->cmdvec[cmdid]);
	else
		printf("Error: run cmd[%d] {%s} failure\n",
		    cmd->cmdcnt, cmd->cmdvec[cmdid]);
}

static void
cmd_extpy(void)
{
	command_t cmd;
	command_init(&cmd);

	while (command_parse(&cmd))
		if (cmd.pcmd)
			command_reset(&cmd);

	run_crash_cmd_start();
	for (int i = 0; i < cmd.cmdcnt; i++)
		command_run(&cmd, i);
	run_crash_cmd_done();
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
