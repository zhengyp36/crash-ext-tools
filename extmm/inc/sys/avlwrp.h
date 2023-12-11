#ifndef _AVL_WRAPPER_H
#define _AVL_WRAPPER_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef offsetof
#define offsetof(t,m) ((size_t)&((t*)0)->m)
#endif

typedef unsigned long ulong_t;
typedef enum { B_FALSE, B_TRUE } boolean_t;

#define AVL_CREATE(tree, cmp, type, member)				\
	avl_create(tree, cmp, sizeof(type), offsetof(type, member))

#ifdef __cplusplus
} // extern "C"
#endif

#include <stdint.h>
#include <sys/avl.h>
#include <sys/avl_impl.h>

#endif // _AVL_WRAPPER_H
