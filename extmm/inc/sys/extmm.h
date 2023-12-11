#ifndef _SYS_EXT_MM_H
#define _SYS_EXT_MM_H

#include <stdarg.h>
#include <sys/avlwrp.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * extmm: extend mmap - crash extend memory map between kernel and user space
 */

typedef int (pfn_mmread_t)(const void *kaddr, void *uaddr, size_t size);

enum {
	KINDEX,
	UINDEX
};

typedef struct mm_top {
	avl_tree_t	tree[2];
	pfn_mmread_t *	kread;
	int		inited;
} mmtop_t;

typedef struct mm_sub {
	uintptr_t	start[2];
	uintptr_t	size;

	mmtop_t *	top;
	avl_node_t	intop[2];
	avl_tree_t	tree;
} mmsub_t;

typedef struct map_node {
	struct map_node *next;
	uintptr_t	start;
	uintptr_t	size;
} mnode_t;

typedef struct mm_sub_node {
	uintptr_t	start;
	uintptr_t	size;

	mmsub_t *	sub;
	mnode_t *	maplist;
	avl_node_t	insub;
} mmsn_t;

void extmm_init(pfn_mmread_t *kread);
void extmm_mmap(uintptr_t kaddr, uintptr_t uaddr, size_t size);
void extmm_cleanup(void);

int extmm_is_kaddr(uintptr_t kaddr);
int extmm_is_uaddr(uintptr_t kaddr);

uintptr_t extmm_k2u(uintptr_t kaddr, size_t size);
uintptr_t extmm_u2k(uintptr_t uaddr, size_t size);
long extmm_pagesize(void);

int extmm_readmem(void *kaddr, void *buf, size_t size);

void extmm_vlog(const char *fmt, va_list ap);
void extmm_log(const char *fmt, ...);
	
#define EXTMM_LOG(fmt, ...)						\
	extmm_log(fmt "[%s,%d]\n", ##__VA_ARGS__, __FUNCTION__, __LINE__)

#ifdef __cplusplus
} // extern "C"
#endif

#endif // _SYS_EXT_MM_H
