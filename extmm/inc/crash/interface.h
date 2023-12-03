#ifndef _CRASH_INTERFACE_H
#define _CRASH_INTERFACE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct address_space_entry {
	uintptr_t kbase;
	uintptr_t ubase;
	size_t    size;
} as_ent_t;

#define KU_MAP {							\
	{								\
		/* FFFF00... -> FFFF0F... */				\
		.kbase = 0xFFFF000000000000UL,				\
		.ubase = 0x0000100000000000UL,				\
		.size  = 0x00000F0000000000UL				\
	}, {								\
		/* FFFF80... -> FFFF8F... */				\
		.kbase = 0xFFFF800000000000UL,				\
		.ubase = 0x0000200000000000UL,				\
		.size  = 0x00000F0000000000UL				\
	}, {								\
		/* FFFF90... -> FFFF9F... */				\
		.kbase = 0xFFFF900000000000UL,				\
		.ubase = 0x0000300000000000UL,				\
		.size  = 0x00000F0000000000UL				\
	}, {								\
		/* FFFFFFFF.0... -> FFFFFFFF.F... */			\
		.kbase = 0xFFFFFFFF00000000UL,				\
		.ubase = 0x0000400000000000UL,				\
		.size  = 0x00000000F0000000UL				\
	}								\
}

typedef struct crash_interface {
	void  (*reset_mmap)(void);
	void  (*add_mmap)(uintptr_t kaddr, uintptr_t uaddr, size_t size);
	void  (*load_as)(as_ent_t*, int num);
	void* (*u2k)(void *uaddr, size_t size);
	void* (*k2u)(void *kaddr, size_t size);
	int   (*is_kaddr)(void *addr);
	int   (*is_uaddr)(void *addr);
	void* (*lookup_symbol)(const char *symbol);
	long  (*get_pagesize)(void);
} crash_intf_t;

const crash_intf_t * get_crash_intf(void);

static inline const crash_intf_t *
extas(int *inited)
{
	if (inited && !*inited) {
		get_crash_intf()->reset_mmap();
		*inited = 1;
	}
	return (get_crash_intf());
}

#define KSYM_REF(name) (*KSYM_NAME(name))
#define KSYM_NAME(name) ___KSYM_IMPORT_ADDR_USR_3578___##name##___2495___
#define KVAR_IMPORT(type,name) type * KSYM_NAME(name)

void extas_import_quiet(int quiet);

int extas_import_(int *inited, void **pvar, const char *name, size_t size);
#define extas_import(inited, var)					\
	extas_import_(inited,						\
	    (void**)&KSYM_NAME(var), #var, sizeof(KSYM_REF(var)))

#define EXTAS extas(&__crash_intf_inited__)
#define EXTAS_IMPORT(var) extas_import(&__crash_intf_inited__, var)

#ifndef __CRASH_INTF_IMPLEMENT__
int __crash_intf_inited__;
#endif

#ifdef __cplusplus
} // extern "C"
#endif
#endif // _CRASH_INTERFACE_H
