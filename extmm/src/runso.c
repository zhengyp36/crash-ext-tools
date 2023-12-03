#include <errno.h>
#include <dlfcn.h>
#include <sys/extmm.h>
#include <crash/defs.h>
#include <crash/interface.h>

#define SEARCH_SO_PATH "/usr/local/extpy/release"

typedef struct runso_command {
	const char *name;
	void (*handler)(const struct runso_command*, int argc, char *argv[]);
	const char *help_info;
} rs_cmd_t;

#define RS_CMD_ENT_(n, f, h, ...) { .name = n, .handler = f, .help_info = h }
#define RS_CMD_ENT(nm, h, ...) RS_CMD_ENT_(nm, h, ##__VA_ARGS__, "")

static void cmd_rsrch_sym(const rs_cmd_t *, int argc, char *argv[]);
static void cmd_builtin(const rs_cmd_t *, int argc, char *argv[]);
static void cmd_srch_mem(const rs_cmd_t *, int argc, char *argv[]);

static rs_cmd_t rs_cmd_table[] = {
	RS_CMD_ENT("::rsrch_sym", cmd_rsrch_sym, "<addr>"),
	RS_CMD_ENT("::srch_mem", cmd_srch_mem, "<pattern> <addr>"),
	RS_CMD_ENT("::builtin", cmd_builtin),
	{ NULL, NULL, NULL}
};

static const rs_cmd_t *
find_cmd(const char *name)
{
	for (rs_cmd_t *cmd = rs_cmd_table; cmd->name && cmd->handler; cmd++)
		if (!strcmp(cmd->name, name))
			return (cmd);
	return (NULL);
}

static void
show_cmd_help(void)
{
	printf("Builtin-commands below:\n");
	for (rs_cmd_t *cmd = rs_cmd_table; cmd->name && cmd->handler; cmd++)
		printf("    runso %s %s\n", cmd->name, cmd->help_info);
}

static int
endswith(const char *target, const char *suffix)
{
	int target_len = strlen(target);
	int suffix_len = strlen(suffix);

	return (suffix_len > 0 && suffix_len <= target_len &&
	    !strcmp(&target[target_len - suffix_len], suffix));
}

static const char *
validate_so_path(const char *so, char *buf, size_t size)
{
	const char *SUFFIX = ".zfs.so";
	char tmp_buf[1024];

	if (!endswith(so, SUFFIX)) {
		snprintf(tmp_buf, sizeof(tmp_buf), "%s%s", so, SUFFIX);
		so = tmp_buf;
	}

	if (strchr(so, '/')) {
		snprintf(buf, size, "%s", so);
		return (buf);
	}

	if (!access(so, F_OK)) {
		snprintf(buf, size, "./%s", so);
		return (buf);
	}

	snprintf(buf, size, "%s/%s", SEARCH_SO_PATH, so);
	return (!access(buf, F_OK) ? buf : NULL);
}

static void
cmd_runso(void)
{
	if (argcnt <= 1)
		cmd_usage(pc->curcmd, SYNOPSIS);

	char so[1024] = {0};
	if (!validate_so_path(args[1], so, sizeof(so))) {
		const rs_cmd_t *cmd = find_cmd(args[1]);
		if (cmd)
			cmd->handler(cmd, argcnt - 1, &args[1]);
		else
			printf("Error: *** '%s' does not exist\n", args[1]);
		return;
	}

	void *handle = dlopen(so, RTLD_NOW);
	if (!handle) {
		printf("Error: *** open %s errno %d\n", so, errno);
		return;
	}

	void (*entry)(int,char*[]) = dlsym(handle, "__main__");
	if (!entry)
		printf("Error: *** lookup __main__ fail\n");
	else
		entry(argcnt - 1, &args[1]);
	dlclose(handle);
}

static char *help_run[] = {
	"runso",
	"Load external share-obj to run",

	"<cmd.so> [args...]",

	"  This command load specified so and lookup __main__ to run.",
	"The proto-type of __main__ is: void __main__(int argc, char *argv[]).",

	"\nEXAMPLES",
	"  #include <stdio.h>",
	"",
	"  void __main__(int argc, char *argv[])",
	"  {",
	"      for (int i = 0; i < argc; i++)",
	"          printf(\"[%%d]%%s\\n\", i, argv[i]);",
	"  }",

	"\nBUILTIN-COMMANDS",
	"  crash> runso ::builtin",

	NULL
};

static struct command_table_entry command_table[] = {
	{ "runso", cmd_runso, help_run, 0 },
	{ NULL }
};

static int
kread_impl(const void *kaddr, void *uaddr, size_t size)
{
	int ok = readmem((ulonglong)(uintptr_t)kaddr, KVADDR, uaddr, size,
	    "runso read kmem", RETURN_ON_ERROR);
	return (ok ? 0 : -1);
}

__attribute__((constructor,used)) static void
runso_init(void)
{
	extmm_init(kread_impl);
	register_extension(command_table);
}

__attribute__((destructor,used)) static void
runso_fini(void)
{
	extmm_cleanup();
}

static void
cmd_builtin(const rs_cmd_t *cmd, int argc, char *argv[])
{
	show_cmd_help();
}

static inline char
to_lower(char ch)
{
	return (ch | 0x20);
}

static int
str2u64(const char *str, uint64_t *r, uint8_t *len)
{
	const char *orig = str;
	uint16_t max_len = 16;
	char ch;

	*r = 0;
	*len = 0;
	if (str[0] == '0' && to_lower(str[1]) == 'x')
		str += 2;

	if (!*str) {
		printf("Error: *** invalid number(%s)\n", orig);
		return (-1);
	}

	while (*len <= max_len && !!(ch = *str++)) {
		if (ch >= '0' && ch <= '9')
			*r = *r * 16 + (ch - '0');
		else {
			ch = to_lower(ch);
			if (ch >= 'a' && ch <= 'f')
				*r = *r * 16 + (ch - 'a' + 10);
			else {
				printf("Error: *** invalid number(%s)\n", orig);
				return (-1);
			}
		}
		++*len;
	}

	if (*len > max_len) {
		printf("Error: *** number(%s) is too long\n", orig);
		return (-1);
	}

	return (0);
}

static void
search_symbol_by_addr(uint64_t addr)
{
	ulong off = 0;
	struct syment *ent = value_search_module((ulong)addr, &off);
	if (!ent)
		printf("Error: *** search symbol by addr(0x%lx) error\n", addr);
	else
		printf("0x%lx: %s, off=%lx\n", addr, ent->name, off);
}

static void
cmd_rsrch_sym(const rs_cmd_t *cmd, int argc, char *argv[])
{
	uint64_t addr;
	uint8_t len;

	if (argc == 1) {
		printf("Usage: runso %s %s\n", cmd->name, cmd->help_info);
		return;
	}

	for (int i = 1; i < argc; i++)
		if (str2u64(argv[i], &addr, &len))
			search_symbol_by_addr(addr);
}

static size_t
srch_mem_in_buf(void *_buf, size_t sz, uint64_t pattern, uint8_t pattern_len)
{
	char *start = (char*)_buf;
	char *end = &start[sz];
	char *buf = end - pattern_len;

	while (buf >= start) {
		if (memcmp(buf, &pattern, pattern_len) == 0)
			break;
		buf -= pattern_len;
	}

	return (buf >= start ? end - buf : 0);
}

#define AARCH_2ND_INSTRUCTION 0x910003fdUL
#define AARCH_2ND_INSTRUCTION_SIZE 4

static void
cmd_srch_mem(const rs_cmd_t *cmd, int argc, char *argv[])
{
	if (argc != 3) {
		printf("Usage: runso %s %s\n", cmd->name, cmd->help_info);
		printf("Example: runso %s ff55 0xfffff2319eee8000\n",
		    cmd->name);
		printf("Example: runso %s %lx ffff4e7612c084fc\n",
		    cmd->name, AARCH_2ND_INSTRUCTION);
		printf("Note: %lx is the 2nd-instruction "
		    "@function entry and instruction-size=%dB for aarch64.\n",
		    AARCH_2ND_INSTRUCTION, AARCH_2ND_INSTRUCTION_SIZE);
		return;
	}

	uint64_t pattern;
	uint8_t pattern_len;
	const char *str_pattern = argv[1];
	if (str2u64(str_pattern, &pattern, &pattern_len)) {
		printf("Error: *** pattern(%s) is invalid\n", str_pattern);
		return;
	}

	pattern_len = (pattern_len + 1) / 2;

	uint64_t addr;
	uint8_t addr_len;
	if (str2u64(argv[2], &addr, &addr_len)) {
		printf("Error: *** addr(%s) is invalid\n", argv[2]);
		return;
	}

	uint8_t byte;
	if (extmm_readmem((void*)addr, &byte, sizeof(byte))) {
		printf("Error: *** addr(0x%lx) is unreadable\n", addr);
		return;
	}

	long pgsz = extmm_pagesize();
	uint64_t start = addr & ~(pgsz - 1);

	size_t bufsz = addr - start;
	void *buf = malloc(bufsz);
	if (!buf) {
		printf("Error: *** outof memory for %lx\n", bufsz);
		return;
	}

	if (extmm_readmem((void*)start, buf, bufsz)) {
		printf("Error: *** read mem(0x%lx) error\n", start);
		free(buf);
		return;
	}

	size_t off = srch_mem_in_buf(buf, bufsz, pattern, pattern_len);
	if (off) {
		printf("Search pattern(%s) success @0x%lx\n",
		    str_pattern, addr - off);
		if (pattern == AARCH_2ND_INSTRUCTION &&
		    pattern_len == AARCH_2ND_INSTRUCTION_SIZE)
			printf("The function entry may be 0x%lx\n",
			    addr - off - AARCH_2ND_INSTRUCTION_SIZE);
	}
	else
		printf("Error: *** Search pattern(%s) fail @[0x%lx~0x%lx)\n",
		    str_pattern, addr - bufsz, addr);

	free(buf);
}
