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
 * STRING-BUFFER: BUILD STRINGS
 *
 * void strbuf_init(strbuf_t*);
 * const char * strbuf_str(strbuf_t*);
 * int strbuf_wr(strbuf_t*, print, ...);
 * void strbuf_clear(strbuf_t*);
 * void strbuf_destroy(strbuf_t*);
 */

typedef struct strbuf {
	char *	str;
	char *	pbuf;
	size_t	size;
	size_t	remain;
} strbuf_t;

static inline void
strbuf_init(strbuf_t *sb)
{
	sb->str = sb->pbuf = NULL;
	sb->size = sb->remain = 0;
}

static inline void
strbuf_destroy(strbuf_t *sb)
{
	if (sb->str)
		free(sb->str);
	strbuf_init(sb);
}

static inline size_t
strbuf_strlen(const strbuf_t *sb)
{
	return (sb->pbuf - sb->str);
}

static inline const char *
strbuf_str(const strbuf_t *sb)
{
	return (strbuf_strlen(sb) > 0 ? sb->str : "");
}

/*static*/ void
strbuf_expand(strbuf_t *sb, size_t size)
{
	const size_t page_size = 4096;
	size_t new_size = (sb->size + size + page_size - 1)
	    / page_size * page_size;
	if (!new_size)
		new_size = page_size;

	char *buf = malloc(new_size);
	assert(buf);

	int len = strbuf_strlen(sb);
	if (len > 0) {
		memcpy(buf, sb->str, len + 1);
		free(sb->str);
	} else
		buf[0] = '\0';

	sb->str = buf;
	sb->pbuf = buf + len;
	sb->size = new_size;
	sb->remain = new_size - len;
}

static void
strbuf_clear(strbuf_t *sb)
{
	if (strbuf_strlen(sb) > 0) {
		sb->str[0] = '\0';
		sb->pbuf = sb->str;
		sb->remain = sb->size;
	}
}

#define strbuf_wr(sb, print, fmt...)					\
	do {								\
		typeof(sb) _sb = sb;					\
									\
		int _r = print(_sb->pbuf, _sb->remain, fmt);		\
		if (_r < 0)						\
			strbuf_expand(_sb, 4096);			\
		else if (_r >= _sb->remain)				\
			strbuf_expand(_sb, _r);				\
		else {							\
			_sb->pbuf += _r;				\
			_sb->remain -= _r;				\
			break;						\
		}							\
									\
		_r = print(_sb->pbuf, _sb->remain, fmt);		\
		if (_r > 0 && _r < _sb->remain) {			\
			_sb->pbuf += _r;				\
			_sb->remain -= _r;				\
		}							\
	} while (0)

#undef strbuf_wr
#define strbuf_wr(sb, print, fmt...)					\
	do {								\
		if ((sb)->size == 0)					\
			strbuf_expand(sb, 4096);			\
		int __r = print((sb)->pbuf, (sb)->remain, fmt);		\
		if (__r > 0 && __r < (sb)->remain) {			\
			(sb)->pbuf += __r;				\
			(sb)->remain -= __r;				\
		}							\
	} while (0)

static int
run_shell(const char *fmt, ...)
{
	strbuf_t str;
	strbuf_init(&str);

	va_list ap;
	va_start(ap, fmt);
	strbuf_wr(&str, vsnprintf, fmt, ap);
	va_end(ap);

	int rc = system(str.str);
	strbuf_destroy(&str);

	return (rc);
}

/*
 * EXTEND-PYTHON WORKSPACE & USEFUL FILES
 *
 * workspace  : /tmp/crash_extpy_<pid>
 * socket     : /tmp/crash_extpy_<pid>/extpy.socket
 * stdout     : /tmp/crash_extpy_<pid>/crash.stdout
 * stdout     : /tmp/crash_extpy_<pid>/crash.stdout
 * python_pid : /tmp/crash_extpy_<pid>/python.pid
 */

#define EXTPY_WS_PREFIX   "crash_extpy_"
#define EXTPY_WS          "/tmp/"EXTPY_WS_PREFIX
#define EXTPY_SOCKET      "extpy.socket"
#define EXTPY_OUTPUT      "crash.stdout"
#define EXTPY_PY_PID      "python.pid"

#define EXTPY_PY_LIB_PATH "/usr/local/extpy/lib"
#define EXTPY_PY_BIN_PATH "/usr/local/extpy/bin"

static const char *
join_path(strbuf_t *str, const char *relative_path, ...)
{
	static uint32_t pid;
	if (!pid)
		pid = (uint32_t)getpid();

	strbuf_clear(str);
	strbuf_wr(str, snprintf, EXTPY_WS "%u", pid);

	if (relative_path) {
		va_list ap;
		va_start(ap, relative_path);
		strbuf_wr(str, snprintf, "/");
		strbuf_wr(str, vsnprintf, relative_path, ap);
		va_end(ap);
	}

	return (str->str);
}

static inline int
proc_alive(pid_t pid)
{
	return (pid > 0 &&
	    run_shell("kill -0 %u 2>/dev/null", (uint32_t)pid) == 0);
}

static uint32_t
run_extpy_script(const char *ws, const char *py, const char *py_args)
{
	uint32_t pid = 0;
	strbuf_t str_pid, str_cmd;
	strbuf_init(&str_pid);
	strbuf_init(&str_cmd);

	join_path(&str_pid, EXTPY_PY_PID);
	const char *pid_path = strbuf_str(&str_pid);

	const char *cmd_prefix = "";
	if (strchr(py, '/')) {
		cmd_prefix = "";
	} else if (!access(py, F_OK)) {
		cmd_prefix = "./";
	} else {
		cmd_prefix = "";
	}

	strbuf_wr(&str_cmd, snprintf,
	    "PYTHONPATH=$PYTHONPATH:%s:%s PATH=$PATH:%s "
	    "CRASH_EXTEND_PYTHON_WORKSPACE=%s %s%s%s & echo $! >%s",
	    EXTPY_PY_LIB_PATH, EXTPY_PY_BIN_PATH, EXTPY_PY_BIN_PATH, ws,
	    cmd_prefix, py, py_args, pid_path);
	const char *cmd = strbuf_str(&str_cmd);

	int rc = system(cmd);
	if (rc) {
		printf("Error: *** Failed to run cmd{%s}\n", cmd);
		goto done;
	}

	FILE *fp = fopen(pid_path, "r");
	if (!fp) {
		printf("Error: *** Failed to open '%s'.\n", pid_path);
		goto done;
	}

	if (fscanf(fp, "%u", &pid) != 1)
		printf("Error: *** Failed to read pid from '%s'\n", pid_path);
	else if (pid == 0)
		printf("Error: *** Get invalid pid(zero-pid)\n");
	fclose(fp);

done:
	strbuf_destroy(&str_pid);
	strbuf_destroy(&str_cmd);
	return (pid == 0 ? -1 : pid);
}

static pid_t
expty_startup(int argc, char *argv[])
{
	pid_t pid = 0;

	strbuf_t ws_str, args_str;
	strbuf_init(&ws_str);
	strbuf_init(&args_str);

	join_path(&ws_str, NULL);
	const char *workspace = strbuf_str(&ws_str);
	int rc = run_shell("mkdir -p %s", workspace);
	if (rc) {
		printf("Error: *** Failed to mkdir %s\n", workspace);
		goto done;
	}

	for (int i = 1; i < argc; i++)
		strbuf_wr(&args_str, snprintf, " %s", argv[i]);

	const char *py = argv[0];
	const char *py_args = strbuf_str(&args_str);
	pid = run_extpy_script(workspace, py, py_args);
	if (pid == 0)
		printf("Error: *** Failed to run cmd{%s %s}\n", py, py_args);

done:
	strbuf_destroy(&ws_str);
	strbuf_destroy(&args_str);
	return (pid);
}

static int
extpy_connect(pid_t pid, int *fd)
{
	if (!proc_alive(pid))
		return (-1);

	*fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (*fd < 0) {
		printf("Error: *** Failed to create socket\n");
		return (-1);
	}

	strbuf_t str;
	strbuf_init(&str);
	join_path(&str, EXTPY_SOCKET);

	struct sockaddr_un local;
	local.sun_family = AF_UNIX;
	strcpy(local.sun_path, strbuf_str(&str));
	strbuf_destroy(&str);

	int alive;
	size_t len = strlen(local.sun_path) + sizeof(local.sun_family);
	while (!!(alive = proc_alive(pid))) {
		int rc = connect(*fd, (struct sockaddr*)&local, len);
		if (rc < 0)
			usleep(100 * 1000);
		else
			break;
	}

	if (!alive) {
		close(*fd);
		*fd = -1;
		return (-1);
	}

	return (0);
}

static int
extpy_handle_commands(pid_t pid, int *fd)
{
	int sock_fd = *fd;
	*fd = -1;

	strbuf_t str;
	strbuf_init(&str);
	join_path(&str, EXTPY_OUTPUT);
	const char *outf = strbuf_str(&str);

	run_crash_cmd_start();

	int alive;
	while (!!(alive = proc_alive(pid))) {
		char buffer[4096];
		buffer[0] = '\0';
		buffer[sizeof(buffer) - 1] = '\0';

		int rc = recv(sock_fd, buffer, sizeof(buffer) - 1, 0);
		if (rc <= 0)
			break;

		rc = run_crash_cmd(outf, buffer);
		snprintf(buffer, sizeof(buffer), rc ? "ERR" : "OK");

		rc = send(sock_fd, buffer, strlen(buffer), 0);
		if (rc)
			break;
	}

	run_crash_cmd_done();
	strbuf_destroy(&str);
	close(sock_fd);

	return (alive ? 0 : -1);
}

static void
extpy_loop(pid_t pid)
{
	int sock_fd = -1;

	// Wait proc running
	for (int i = 0; i < 15 && !proc_alive(pid); i++)
		usleep(100 * 1000);
	if (!proc_alive(pid)) {
		printf("Error: *** Python is not running.\n");
		return;
	}

	while (proc_alive(pid)) {
		if (sock_fd < 0) {
			if (extpy_connect(pid, &sock_fd))
				break;
			assert(sock_fd >= 0);
		}

		if (extpy_handle_commands(pid, &sock_fd))
			break;
	}
}

static void
cmd_extpy(void)
{
	if (argcnt < 2) {
		printf("Usage: extpy <script.py> [args...]\n");
		return;
	}

	pid_t pid = expty_startup(argcnt - 1, &args[1]);
	if (pid)
		extpy_loop(pid);
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

#define CMD_CLEANUP_CRASH_TMP						\
	"prefix="EXTPY_WS_PREFIX";del=\"rm -rf /tmp/$prefix\";"		\
	"pids=($(ls /tmp/ | grep ^$prefix | cut -d '_' -f 3));"		\
	"for pid in ${pids[@]}; do"					\
	"    kill -0 $pid 2>/dev/null;"					\
	"    if [ $? -ne 0 ]; then"					\
	"        ${del}$pid;"						\
	"    else"							\
	"        ps -ef | grep -v grep | grep -w $pid | "		\
	"            grep crash 1>/dev/null || ${del}$pid;"		\
	"    fi;"							\
	"done"
#define CLEANUP_CRASH_TMP() system(CMD_CLEANUP_CRASH_TMP)

static void __attribute__((constructor, used))
extpy_init(void)
{
	register_extension(command_table);
	CLEANUP_CRASH_TMP();
}

static void __attribute__((destructor, used))
extpy_fini(void)
{
	CLEANUP_CRASH_TMP();
}
