#ifndef _CRASH_VERSION_H
#define _CRASH_VERSION_H

#define EXT_CRASH_TO_STR(v) EXT_CRASH_TO_STR_(v)
#define EXT_CRASH_TO_STR_(v) #v

__attribute__((used))
static const char *extension_crash_version =				\
	"crash_version=" EXT_CRASH_TO_STR(CRASH_VERSION) "; "		\
	"crash_cpu_type=" EXT_CRASH_TO_STR(CRASH_CPU_TYPE);

#endif // _CRASH_VERSION_H
