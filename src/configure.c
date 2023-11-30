#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	const char *cpu;
	const char *cflags;
} arch_conf_t;

static arch_conf_t arch_conf[] = {
	#define ARCH_CONF_ALPHA {"ALPHA",""}
	#ifdef __alpha__
	#define ARCH_CONF ARCH_CONF_ALPHA
	#endif
	ARCH_CONF_ALPHA,

	#define ARCH_CONF_X86 {"X86","-D_FILE_OFFSET_BITS=64"}
	#ifdef __i386__
	#define ARCH_CONF ARCH_CONF_X86
	#endif
	ARCH_CONF_X86,

	#define ARCH_CONF_PPC {"PPC","-D_FILE_OFFSET_BITS=64"}
	#ifdef __powerpc__
	#define ARCH_CONF ARCH_CONF_PPC
	#endif
	ARCH_CONF_PPC,

	#define ARCH_CONF_IA64 {"IA64",""}
	#ifdef __ia64__
	#define ARCH_CONF ARCH_CONF_IA64
	#endif
	ARCH_CONF_IA64,

	#define ARCH_CONF_S390 {"S390","-D_FILE_OFFSET_BITS=64"}
	#ifdef __s390__
	#define ARCH_CONF ARCH_CONF_S390
	#endif
	ARCH_CONF_S390,

	#define ARCH_CONF_S390X {"S390X",""}
	#ifdef __s390x__
	#define ARCH_CONF ARCH_CONF_S390X
	#endif
	ARCH_CONF_S390X,

	#define ARCH_CONF_PPC64 {"PPC64","-m64"}
	#ifdef __powerpc64__
	#define ARCH_CONF ARCH_CONF_PPC64
	#endif
	ARCH_CONF_PPC64,

	#define ARCH_CONF_X86_64 {"X86_64",""}
	#ifdef __x86_64__
	#define ARCH_CONF ARCH_CONF_X86_64
	#endif
	ARCH_CONF_X86_64,

	#define ARCH_CONF_ARM {"ARM","-D_FILE_OFFSET_BITS=64"}
	#ifdef __arm__
	#define ARCH_CONF ARCH_CONF_ARM
	#endif
	ARCH_CONF_ARM,

	#define ARCH_CONF_ARM64 {"ARM64",""}
	#ifdef __aarch64__
	#define ARCH_CONF ARCH_CONF_ARM64
	#endif
	ARCH_CONF_ARM64,

	#define ARCH_CONF_MIPS {"MIPS","-D_FILE_OFFSET_BITS=64"}
	#define ARCH_CONF_MIPS64 {"MIPS64",""}
	#ifdef __mips__
	#ifndef __mips64
	#define ARCH_CONF ARCH_CONF_MIPS
	#else
	#define ARCH_CONF ARCH_CONF_MIPS64
	#endif
	#endif
	ARCH_CONF_MIPS,
	ARCH_CONF_MIPS64,

	#define ARCH_CONF_SPARC64 {"SPARC64",""}
	#ifdef __sparc_v9__
	#define ARCH_CONF ARCH_CONF_SPARC64
	#endif
	ARCH_CONF_SPARC64,

	#define ARCH_CONF_RISCV64 {"RISCV64",""}
	#if defined(__riscv) && (__riscv_xlen == 64)
	#define ARCH_CONF ARCH_CONF_RISCV64
	#endif
	ARCH_CONF_RISCV64,

	#ifndef ARCH_CONF
	#error "CPU-TYPE is unknown or not-supported."
	#endif
	{NULL, NULL}
};

#define logerr(fmt,...) fprintf(stderr, fmt"\n", ##__VA_ARGS__)

static void
check_target(const char *target)
{
	for (const arch_conf_t *conf = arch_conf; conf->cpu; conf++)
		if (!strcmp(conf->cpu, target))
			return;

	logerr("Error: *** '%s' is not supported.", target);
	logerr("CPU-TYPEs supported below:");
	for (const arch_conf_t *conf = arch_conf; conf->cpu; conf++)
		logerr("\t%s", conf->cpu);

	exit(1);
}

static inline const char *
_getenv(const char *key)
{
	const char *val = getenv(key);
	return (val && val[0] ? val : NULL);
}

static void
config_machine_type(void)
{
	const char *conf[] = ARCH_CONF;
	if (_getenv("target")) {
		conf[0] = getenv("target");
		conf[1] = getenv("target_cflags");
	}

	check_target(conf[0]);
	printf("TARGET=%s\n", conf[0]);
	printf("TARGET_CFLAGS=%s\n", conf[1]);
}

static void
config_crash_version(void)
{
	const char *version = _getenv("crash_version");
	if (version) {
		printf("CRASH_VERSION=%s\n", version);
		return;
	}

	#define CONFIG_CRASH_VERSION_CMD				\
		"version=$("						\
			"crash -v | "					\
			"grep -E '^crash\\s+([0-9]+\\.){2}([0-9]+)' | "	\
			"awk '{print $2}' | "				\
			"awk -F '-' '{print $1}'"			\
		"); "							\
		"if [ -z \"$version\" ]; then "				\
			"echo \"Error: *** "				\
			    "Failed to get crash version.\" >&2; "	\
			"exit 1; "					\
		"fi; "							\
		"echo \"CRASH_VERSION=$version\"; "			\
									\
		"goal=$ORIG_MAKECMDGOALS; "				\
		"[ \"$goal\" = \"clean\" -o "				\
		"  \"$goal\" = \"clean_crash_headers\" ] && exit 0; "	\
									\
		"inc=$PWD/inc/crash; "					\
		"header=$inc/$version/crash/defs.h; "			\
		"if [ ! -f $header ]; then "				\
			"unzip -qo $inc/inc.zip -d $inc/ || exit; "	\
			"if [ ! -f $header ]; then "			\
				"echo \"Error: *** Failed to "		\
				    "unzip for $header.\" >&2; "	\
				"exit 1; "				\
			"fi; "						\
		"fi"
	int rc = system(CONFIG_CRASH_VERSION_CMD);
	if (rc)
		exit(rc);
}

static void
config_done(void)
{
	printf("CONFIGURED=1\n");
}

int
main(int argc, char *argv[])
{
	config_machine_type();
	config_crash_version();
	config_done();
	return (0);
}
