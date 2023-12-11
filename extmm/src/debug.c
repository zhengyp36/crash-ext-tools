#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/avlwrp.h>

static int
do_map(uintptr_t start, uintptr_t size)
{
	void *addr = (void*)start;
	size_t sz = (size_t)size;
	const int props = PROT_READ | PROT_WRITE;
	const int flags = MAP_PRIVATE | MAP_ANONYMOUS;

	void *ret = mmap(addr, sz, props, flags, -1, 0);
	if (ret == addr)
		return (0);

	printf("Error: ret=%p, addr=%p\n", ret, addr);
	if (ret != MAP_FAILED)
		munmap(ret, sz);
	return (-1);
}

#define MAP_START ((uintptr_t)0x300000000000ULL)

typedef struct {
	char data[4096];
} raw_t;

typedef struct {
	raw_t r[10];
} data_t;

int
main(int argc, char *argv[])
{
	data_t *data = (data_t*)MAP_START;
	uintptr_t start = MAP_START;
	uintptr_t step = 16 * sysconf(_SC_PAGESIZE);
	int ret;

	for (int i = 0; i < 10; i++, start += step) {
		ret = do_map(start, step);
		if (ret) {
			printf("Error: *** map [%d]%lx error %d[%s]\n",
			    i, start, errno, strerror(errno));
			sleep(3600);
			return (ret);
		}
		*(uintptr_t*)data->r[i].data = start;
		printf("map[%d]=%lx,size=%lx\n", i, start, step);
	}

	for (int i = 0; i < 10; i++) {
		printf("[%d] %lx\n", i, *(uintptr_t*)data->r[i].data);
	}

	printf("avl_create=%p\n", avl_create);
	return (0);
}
