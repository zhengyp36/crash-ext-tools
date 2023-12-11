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

#define EXTPY_WS_PREFIX   "crash_extpy_"
#define EXTPY_WS          "/tmp/"EXTPY_WS_PREFIX
#define EXTPY_PY_LIB_PATH "/usr/local/extpy/lib"
#define EXTPY_PY_BIN_PATH "/usr/local/extpy/bin"
#define EXTPY_SOCKET      "extpy.socket"
#define EXTPY_OUTPUT      "crash.stdout"
#define EXTPY_PY_PID      "python.pid"

#define WR_STR(print, buf, sz, fmt...)					\
	do {								\
		int __r = print(buf, sz, fmt);				\
		if (__r > 0 && __r < sz) {				\
			(buf) += __r;					\
			(sz) -= __r;					\
		}							\
	} while (0)

static int extpy_debug;
#define DEBUG(args...)							\
	do {								\
		if (extpy_debug)					\
			fprintf(stdout, args);				\
	} while (0)							\

#define CMD_CLEANUP_CRASH_TMP						\
	"prefix="EXTPY_WS_PREFIX";del=\"rm -rf /tmp/$prefix\";"		\
	"pids=($(ls /tmp/ | grep $prefix | cut -d '_' -f 3));"		\
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

typedef struct extpy_sock {
	int fd;
	uint32_t py_pid;
	struct sockaddr_un local;
} extpy_sock_t;

static jmp_buf saved_jmp_buf;
static int run_long_jmp = 0;

static int run_crash_cmd(const char *, const char *);

static const char *
join_path(char *buffer, size_t size, const char *relative_path, ...)
{
	static uint32_t pid;
	if (!pid)
		pid = (uint32_t)getpid();

	char *pbuf = buffer;
	size_t sz = size;
	WR_STR(snprintf, pbuf, sz, EXTPY_WS "%u", pid);

	if (relative_path) {
		va_list ap;
		va_start(ap, relative_path);
		WR_STR(snprintf, pbuf, sz, "/");
		WR_STR(vsnprintf, pbuf, sz, relative_path, ap);
		va_end(ap);
	}

	return (buffer);
}

static int
run_shell(const char *fmt, ...)
{
	char cmd[1024];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(cmd, sizeof(cmd), fmt, ap);
	va_end(ap);

	return (system(cmd));
}

static uint32_t
run_extpy_srcipt(const char *py, const char *py_args)
{
	char workspace[128], pid_path[128];
	join_path(workspace, sizeof(workspace), NULL);
	join_path(pid_path, sizeof(pid_path), EXTPY_PY_PID);

	const char *cmd_prefix = "";
	if (strchr(py, '/')) {
		cmd_prefix = "";
	} else if (!access(py, F_OK)) {
		cmd_prefix = "./";
	} else {
		cmd_prefix = "";
	}

	int rc = run_shell("PYTHONPATH=$PYTHONPATH:%s:%s PATH=$PATH:%s "
	    "CRASH_EXTEND_PYTHON_WORKSPACE=%s %s%s%s & "
	    "echo $! >%s && sleep 0.3",
	    EXTPY_PY_LIB_PATH, EXTPY_PY_BIN_PATH, EXTPY_PY_BIN_PATH,
	    workspace, cmd_prefix, py, py_args, pid_path);

	uint32_t pid = 0;
	if (!rc) {
		FILE *fp = fopen(pid_path, "r");
		if (fp) {
			if (fscanf(fp, "%u", &pid) == 1)
				DEBUG("extpy server pid is %u\n", pid);
			else
				printf("Error: *** read pid from %s error\n",
				    pid_path);
			fclose(fp);
		} else {
			printf("Error: *** open %s error\n", pid_path);
		}
	}

	return (pid);
}

static int
proc_alive(pid_t pid)
{
	return (pid > 0 &&
	    run_shell("kill -0 %u 2>/dev/null", (uint32_t)pid) == 0);
}

static void
wait_py_script_exit(pid_t pid_py)
{
	if (pid_py) {
		while (proc_alive(pid_py))
			usleep(300 * 1000);
	}
}

static int
extpy_sock_init(extpy_sock_t *sock, pid_t server_pid)
{
	sock->fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sock->fd < 0) {
		printf("Error: *** create socket error %d\n", sock->fd);
		return (-1);
	}

	char path[128];
	join_path(path, sizeof(path), EXTPY_SOCKET);

	sock->local.sun_family = AF_UNIX;
	strcpy(sock->local.sun_path, path);

	size_t len = strlen(sock->local.sun_path) +
	    sizeof(sock->local.sun_family);

	for (int i = 0; i < 3; i++) {
		if (proc_alive(server_pid))
			break;
		usleep(100 * 1000);
	}

	int rc = 0;
	for (int i = 0; i < 15; i++) {
		rc = connect(sock->fd, (struct sockaddr *)&sock->local, len);
		if (rc < 0 && proc_alive(server_pid))
			usleep(200 * 1000);
		else
			break;
	}

	if (rc < 0) {
		printf("Error: *** connect to server error %d\n", rc);
		close(sock->fd);
		sock->fd = -1;
		return (-1);
	}

	return (0);
}

static void
extpy_sock_fini(extpy_sock_t *sock)
{
	if (sock->fd >= 0) {
		close(sock->fd);
		sock->fd = -1;
	}
}

static void
convert_args(char *pbuf, size_t sz, int argc, char *argv[])
{
	pbuf[0] = '\0';
	for (int i = 0; i < argc; i++)
		WR_STR(snprintf, pbuf, sz, " %s", argv[i]);
}

static int
extpy_setup(int argc, char *argv[], extpy_sock_t *sock)
{
	char workspace[128];
	join_path(workspace, sizeof(workspace), NULL);

	CLEANUP_CRASH_TMP();
	int rc = run_shell("mkdir -p %s", workspace);
	if (rc) {
		printf("Error: *** mkdir %s error rc\n", workspace);
		return (rc);
	}

	const char *py = argv[0];

	char py_args[128];
	convert_args(py_args, sizeof(py_args), argc - 1, &argv[1]);

	pid_t pid = 0;
	if ((pid = run_extpy_srcipt(py, py_args)) == 0) {
		printf("Error: *** run expty(%s) error\n", py);
		run_shell("rm -rf %s", workspace);
		return (-1);
	}

	rc = extpy_sock_init(sock, pid);
	if (rc) {
		wait_py_script_exit(pid);
		run_shell("rm -rf %s", workspace);
		return (-1);
	}

	sock->py_pid = pid;
	return (0);
}

static void
extpy_command_loop(extpy_sock_t *sock)
{
	int is_stop;
	char outf[128];
	join_path(outf, sizeof(outf), EXTPY_OUTPUT);

	while (proc_alive(sock->py_pid)) {
		char buffer[4096] = {0};

		int rc = recv(sock->fd, buffer, sizeof(buffer)-1, 0);
		if (rc < 0) {
			printf("Error: *** recv command error %d\n", rc);
			break;
		}

		is_stop = !strcmp(buffer, "STOP");
		DEBUG("RecvCrashCmd[%d]: %s\n", rc, buffer);
		if (!is_stop)
			rc = run_crash_cmd(outf, buffer);

		snprintf(buffer, sizeof(buffer), rc ? "ERR" : "OK");
		rc = send(sock->fd, buffer, strlen(buffer), 0);
		if (rc < 0) {
			printf("Error: *** send ack error %d\n", rc);
			break;
		}

		if (is_stop)
			break;
	}
}

static void
extpy_cleanup(extpy_sock_t *sock)
{
	extpy_sock_fini(sock);
	wait_py_script_exit(sock->py_pid);
}

static void
cmd_extpy(void)
{
	if (argcnt < 2) {
		printf("Usage: extpy <script.py> [args...]\n");
		return;
	}

	extpy_sock_t sock;
	if (!extpy_setup(argcnt - 1, &args[1], &sock)) {
		memcpy(saved_jmp_buf, pc->main_loop_env, sizeof(jmp_buf));
		extpy_command_loop(&sock);
		extpy_cleanup(&sock);
		if (run_long_jmp) {
			run_long_jmp = 0;
			longjmp(pc->main_loop_env, 1);
		}
	}
}

static char *help_extpy[] = {
	"extpy",
	"Execute external python scripts from crash",
	"<script.py> [args...]",
	"  This command executes external python scripts.",
	"\nEXAMPLE",
	"  crash> extpy mod.py zfs spl",
	"\nExecute the command below to list some useful scripts:",
	"  crash> !ls "EXTPY_PY_BIN_PATH"",
	"\nExtendPython version:",
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
	char workspace[128];
	join_path(workspace, sizeof(workspace), NULL);
	run_shell("rm -rf %s", workspace);

	CLEANUP_CRASH_TMP();
}

typedef struct crash_ctx {
	FILE *	ofile;
	char	command_line[sizeof(pc->command_line)];
	char	command_bak[sizeof(pc->command_line)];
} crash_ctx_t;

static crash_ctx_t crash_ctx;

static int
run_crash_cmd(const char *outf, const char *orig_cmd)
{
	FILE *old_fp = fp;
	crash_ctx_t *ctx = &crash_ctx;

	FILE *ofile = crash_ctx.ofile = fopen(outf, "w+");
	if (!ofile) {
		printf("Error: *** open %s error\n", outf);
		return (-1);
	}

	#define GDB_CMD_PREFIX "gdb "
	int prefix_len = sizeof(GDB_CMD_PREFIX) - 1;
	int has_gdb_prefix = 0;
	const char *cmd = orig_cmd;
	if (!strncmp(cmd, GDB_CMD_PREFIX, prefix_len)) {
		has_gdb_prefix = 1;
		cmd += prefix_len;
	}

	int has_non_gdb_prefix = 0;
	if (!has_gdb_prefix) {
		#define NON_GDB_CMD_PREFIX "NON_GDB "
		prefix_len = sizeof(NON_GDB_CMD_PREFIX) - 1;
		if (!strncmp(cmd, NON_GDB_CMD_PREFIX, prefix_len)) {
			has_non_gdb_prefix = 1;
			cmd += prefix_len;
		}
	}

	setbuf(ofile, NULL);
	fp = pc->ofile = ofile;

	fflush(stdout);
	int bak = dup(1);
	dup2(fileno(ofile), 1);

	snprintf(ctx->command_bak, sizeof(ctx->command_bak), "%s", cmd);
	snprintf(ctx->command_line, sizeof(ctx->command_line), "%s", cmd);
	argcnt = parse_line(ctx->command_line, args);

	if (setjmp(pc->main_loop_env)) {
		ctx = &crash_ctx;
		printf("Error: *** execute '%s' error\n", ctx->command_bak);

		memcpy(pc->main_loop_env, saved_jmp_buf, sizeof(jmp_buf));

		fflush(stdout);
		dup2(bak, 1);
		close(bak);
		pc->ofile = NULL;
		fp = stdout;
		fclose(ofile);

		run_long_jmp = 1;
		return (-1);
	}

	int rc = 0;
	if (has_non_gdb_prefix ||
	    (!has_gdb_prefix && !is_gdb_command(FALSE, FAULT_ON_ERROR)))
		exec_command();
	else {
		snprintf(ctx->command_line, sizeof(ctx->command_line),
		    "%s", cmd);
		int ok = gdb_pass_through(ctx->command_line,
		    ofile, GNU_RETURN_ON_ERROR);
		if (!ok)
			rc = -1;
	}
	memcpy(pc->main_loop_env, saved_jmp_buf, sizeof(jmp_buf));

	fflush(stdout);
	dup2(bak, 1);
	close(bak);
	pc->ofile = NULL;
	fp = old_fp;
	fclose(ofile);

	return (rc);
}
