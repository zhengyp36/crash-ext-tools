#ifndef __ADAPT_SYS_DEBUG_H__
#define __ADAPT_SYS_DEBUG_H__

#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <sys/avlwrp.h>

#define ASSERT(cond)							\
({									\
	boolean_t __cond = !!(cond);					\
	if (!__cond) {							\
		printf("assert %s fail @%s,%d\n",			\
		    #cond, __FUNCTION__, __LINE__);			\
		assert(0);						\
	}								\
	!__cond;							\
})

#define VERIFY(cond) ASSERT(cond)

#define	ASSERT3P(LEFT, OP, RIGHT)					\
do {									\
	const uintptr_t __left = (uintptr_t)(LEFT);			\
	const uintptr_t __right = (uintptr_t)(RIGHT);			\
	if (!(__left OP __right)) {					\
		printf("assert %s %s %s fail @%s,%d\n",			\
		    #LEFT, #OP, #RIGHT, __FUNCTION__, __LINE__);	\
		    assert(0);						\
	}								\
} while (0)

#define	ASSERT3U(LEFT, OP, RIGHT)					\
do {									\
	const uint64_t __left = (uint64_t)(LEFT);			\
	const uint64_t __right = (uint64_t)(RIGHT);			\
	if (!(__left OP __right)) {					\
		printf("assert %s %s %s fail @%s,%d\n",			\
		    #LEFT, #OP, #RIGHT, __FUNCTION__, __LINE__);	\
		    assert(0);						\
	}								\
} while (0)

#endif // __ADAPT_SYS_DEBUG_H__
