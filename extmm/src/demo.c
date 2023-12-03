#include <stdio.h>
#include <auto/zfs_crash.h>
#include <crash/interface.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))
#endif

#define KSYM_ENT(var)							\
	{								\
		.pvar = (void**)&KSYM_NAME(var),			\
		.name = #var,						\
		.size = sizeof(KSYM_REF(var))				\
	}

typedef struct ksym_ent {
	void **pvar;
	const char *name;
	size_t size;
} ksym_ent_t;

static int
setup_ksyms(ksym_ent_t tbl[], int cnt)
{

	const crash_intf_t *intf = get_crash_intf();
	for (int i = 0; i < cnt; i++) {
		void *kaddr = intf->lookup_symbol(tbl[i].name);
		if (!kaddr) {
			printf("Error: *** lookup symbol(%s) error\n",
			    tbl[i].name);
			return (-1);
		}
		void *uaddr = intf->k2u(kaddr, tbl[i].size);
		if (!uaddr) {
			printf("Error: *** convert symbol(%s) addr(%p) error\n",
			    tbl[i].name, kaddr);
			return (-1);
		}
		*tbl[i].pvar = uaddr;
		printf("%s: kaddr=%p, uaddr=%p, size=%lx\n",
		    tbl[i].name, kaddr, uaddr, tbl[i].size);
	}

	return (0);
}

KVAR_IMPORT(avl_tree_t, spa_namespace_avl);
KVAR_IMPORT(dbuf_hash_table_t, dbuf_hash_table);

static void
setup_avl(avl_tree_t *tree)
{
	// TODO:
}

static dmu_buf_impl_t *
setup_db_list(const crash_intf_t *intf, dmu_buf_impl_t *db, int *cnt, int *err)
{
	*err = 0;
	*cnt = 0;

	dmu_buf_impl_t *orig = intf->k2u(db, sizeof(*db));
	if (!orig) {
		*err = -1;
		return (NULL);
	}

	++*cnt;
	for (db = orig; db->db_hash_next; db = db->db_hash_next) {
		void *addr = intf->k2u(db->db_hash_next, sizeof(*db));
		if (!addr) {
			*err = -1;
			break;
		}
		db->db_hash_next = addr;
		++*cnt;
	}

	return (orig);
}

static void
setup_dbuf_table(void)
{
	const crash_intf_t *intf = get_crash_intf();

	dbuf_hash_table_t *h = &KSYM_REF(dbuf_hash_table);
	uint64_t hsize = h->hash_table_mask + 1;
	h->hash_table = intf->k2u(h->hash_table, hsize * sizeof(void*));

	int db_cnt = 0, err_cnt = 0;
	for (uint64_t i = 0; i < hsize; i++) {
		if (h->hash_table[i]) {
			int curr_cnt, err;
			void *addr = setup_db_list(intf, h->hash_table[i],
			    &curr_cnt, &err);
			if (addr)
				h->hash_table[i] = addr;
			if (err)
				err_cnt++;
			if (curr_cnt > 0)
				db_cnt += curr_cnt;
		}
	}

	printf("hash_table: size=%lx, db_cnt=%d, err_cnt=%d\n",
	    hsize, db_cnt, err_cnt);
}

void
__main__(int argc, char *argv[])
{
	get_crash_intf()->reset_mmap();

	ksym_ent_t tbl[] = {
		KSYM_ENT(spa_namespace_avl),
		KSYM_ENT(dbuf_hash_table),
	};
	int rc = setup_ksyms(tbl, ARRAY_SIZE(tbl));
	if (rc)
		return;

	setup_avl(&KSYM_REF(spa_namespace_avl));
	setup_dbuf_table();

	uint64_t *data = (void*)&KSYM_REF(spa_namespace_avl);
	printf("%p: %lx\n", data, data[0]);
}
