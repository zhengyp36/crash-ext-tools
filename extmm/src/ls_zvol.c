#include <stdio.h>
#include <string.h>
#include <auto/zfs_crash.h>
#include <crash/interface.h>

KVAR_IMPORT(struct hlist_head *, zvol_htable);

#define	ZVOL_HT_SIZE	1024
struct hlist_head *zvol_htable;
#define	ZVOL_HT_HEAD(hash)	(&zvol_htable[(hash) & (ZVOL_HT_SIZE-1)])

#define	hlist_entry(ptr, type, field)   container_of(ptr, type, field)
#define	container_of(ptr, type, member)                         \
({                                                              \
        const __typeof(((type *)0)->member) *__p = (ptr);       \
        (type *)((uintptr_t)__p - offsetof(type, member));      \
})

#define	hlist_for_each(p, head)                                      \
	for (p = (head)->first; p; p = (p)->next)

void
__main__(int argc, char *argv[])
{
	int rc = EXTAS_IMPORT(zvol_htable);
	if (rc)
		return;

	zvol_htable = EXTAS->k2u(KSYM_REF(zvol_htable),
	    sizeof(zvol_htable) * ZVOL_HT_SIZE);
	if (!zvol_htable) {
		printf("Warning: *** zvol_htable is null\n");
		return;
	}

	printf("Imported: zvol_htable=%p\n", EXTAS->u2k(zvol_htable,1));
	for (int i = 0; i < ZVOL_HT_SIZE; i++) {
		struct hlist_node *p;
		int j = 0;
		hlist_for_each(p, ZVOL_HT_HEAD(i)) {
			zvol_state_t *zv = EXTAS->k2u(hlist_entry(p,
			    zvol_state_t, zv_hlink), sizeof(*zv));
			p = &zv->zv_hlink;
			printf("[tbl:%d][ndx:%d]zv=%p,%s\n",
			    i, j, EXTAS->u2k(zv,1), zv->zv_name);
		}
	}
}
