#include <stdio.h>
#include <string.h>
#include <auto/zfs_crash.h>
#include <crash/interface.h>

KVAR_IMPORT(dbuf_hash_table_t, dbuf_hash_table);

static dmu_buf_impl_t *
setup_db_list(dmu_buf_impl_t *db, int *cnt, int *err)
{
	*err = 0;
	*cnt = 0;

	dmu_buf_impl_t *orig = EXTAS->k2u(db, sizeof(*db));
	if (!orig) {
		printf("[ERR]db=%p, convert error\n", db);
		*err = -1;
		return (NULL);
	}

	++*cnt;
	for (db = orig; db->db_hash_next; db = db->db_hash_next) {
		void *addr = EXTAS->k2u(db->db_hash_next, sizeof(*db));
		if (!addr) {
			*err = -1;
			printf("[ERR]db=%p/%p,db_hash_next=%p\n",
			    EXTAS->u2k(db,1), db, db->db_hash_next);
			break;
		}
		db->db_hash_next = addr;
		++*cnt;
	}

	return (orig);
}

int
setup_dbuf_table(void)
{
	int rc = EXTAS_IMPORT(dbuf_hash_table);
	if (rc)
		return (rc);

	dbuf_hash_table_t *h = &KSYM_REF(dbuf_hash_table);
	if (!h) {
		printf("Error: *** dbuf_hash_table is null\n");
		return (-1);
	}

	uint64_t hsize = h->hash_table_mask + 1;
	void *addr = EXTAS->k2u(h->hash_table, hsize * sizeof(void*));
	if (!addr) {
		printf("Error: *** convert kaddr(%p) error\n", h->hash_table);
		return (-1);
	}
	h->hash_table = addr;

	int db_cnt = 0, err_cnt = 0;
	for (uint64_t i = 0; i < hsize; i++) {
		if (h->hash_table[i]) {
			int curr_cnt, err;
			void *addr = setup_db_list(h->hash_table[i],
			    &curr_cnt, &err);
			if (addr)
				h->hash_table[i] = addr;
			if (err)
				err_cnt++;
			if (curr_cnt > 0)
				db_cnt += curr_cnt;
		}
	}

	printf("Info: db_cnt=%d, err_cnt=%d\n", db_cnt, err_cnt);
	return (0);
}

#define kmem_cache_t spl_kmem_cache_t

#define	SPA_MINBLOCKSHIFT	9
#define	SPA_OLD_MAXBLOCKSHIFT	17
#define	SPA_MAXBLOCKSHIFT	24
#define	SPA_MINBLOCKSIZE	(1ULL << SPA_MINBLOCKSHIFT)
#define	SPA_OLD_MAXBLOCKSIZE	(1ULL << SPA_OLD_MAXBLOCKSHIFT)
#define	SPA_MAXBLOCKSIZE	(1ULL << SPA_MAXBLOCKSHIFT)

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr)/sizeof(arr[0]))
#endif

static kmem_cache_t *zio_buf_cache[SPA_MAXBLOCKSIZE >> SPA_MINBLOCKSHIFT];

#define CHECK_MAX_LIST_CNT 2000

static boolean_t
check_list(struct list_head *head, uint32_t *pcnt, int ndx)
{
	uint32_t cnt = 0, max_cnt = CHECK_MAX_LIST_CNT;

	struct list_head **n;
	for (n = &head->next; *n && *n != head; n = &(*n)->next) {
		struct list_head *tmp = EXTAS->k2u(*n, sizeof(**n));
		if (!tmp) {
			if (*n)
				printf("CONVERT-LIST[%d]\n", ndx);
			break;
		}
		*n = tmp;

		cnt++;
		if (cnt >= max_cnt)
			break;
	}

	*pcnt = cnt;
	return (!!*n && cnt < max_cnt);
}

static boolean_t
check_zio_buf_cache(int ndx, int *err_cnt,
    struct list_head **last, boolean_t *last_ok)
{
	kmem_cache_t *kc = EXTAS->k2u(zio_buf_cache[ndx], sizeof(*kc));
	if (zio_buf_cache[ndx] && !kc) {
		++*err_cnt;
		printf("CONVERT-KC[%d]\n", ndx);
		return (B_FALSE);
	}

	struct list_head *head = &kc->skc_partial_list;
	zio_buf_cache[ndx] = kc;

	uint32_t cnt = 0;
	boolean_t ok = check_list(head, &cnt, ndx);
	if (!ok) {
		if (cnt < CHECK_MAX_LIST_CNT) {
			++*err_cnt;
			if (head != *last) {
				printf("zio_buf_cache[%d]=%p, head=%p, "
				   "last=%p[%s], check=err, cnt=%u\n",
				    ndx, zio_buf_cache[ndx], head, *last,
				    (!*last ? "ok" : (*last_ok) ? "ok" : "err"),
				    cnt);
			}
		}
	} else if (*last && !*last_ok) {
		printf("zio_buf_cache[%d]=%p, head=%p, "
		   "last=%p[err], check=ok, cnt=%u\n",
		    ndx, zio_buf_cache[ndx], head, *last, cnt);
	}

	*last = head;
	*last_ok = ok;
	return (ok);
}

void
debug_zio_buf_cache(void)
{
	kmem_cache_t **knl = EXTAS->lookup_symbol("zio_buf_cache");
	kmem_cache_t **usr = EXTAS->k2u(knl, sizeof(zio_buf_cache));
	if (!usr) {
		printf("Error: *** setup zio_buf_cache error, knl=%p, usr=%p\n",
		    knl, usr);
		return;
	}
	memcpy(zio_buf_cache, usr, sizeof(zio_buf_cache));
	printf("Info: setup zio_buf_cache[%ld] success\n",
	    ARRAY_SIZE(zio_buf_cache));

	struct list_head *last = NULL;
	boolean_t last_ok = B_FALSE;

	int err_cnt = 0;
	for (int i = ARRAY_SIZE(zio_buf_cache) - 1; i >= 0; i--)
		check_zio_buf_cache(i, &err_cnt, &last, &last_ok);

	if (err_cnt)
		printf("There are %d errors or more\n", err_cnt);
}

int
check_mem(int ndx, void *knl, size_t size)
{
	void *usr = EXTAS->k2u(knl, size);
	if (!usr) {
		printf("Error: *** convert addr[%d] %p error\n", ndx, knl);
		return (-1);
	}

	uint64_t *data = usr;
	for (int i = 0; i < size / sizeof(uint64_t); i++) {
		if (data[i]) {
			printf("Error: *** addr[%d], knl=%p, usr=%p, "
			    "data=%lx\n", ndx,
			    EXTAS->u2k(&data[i],1), &data[i],
			    data[i]);
			return (-2);
		}
	}

	return (0);
}

#define	P2ROUNDUP_TYPED(x, align, type) \
	((((type)(x) - 1) | ((type)(align) - 1)) + 1)

static inline uint32_t
spl_obj_size(spl_kmem_cache_t *skc)
{
	uint32_t align = skc->skc_obj_align;

	return (P2ROUNDUP_TYPED(skc->skc_obj_size, align, uint32_t) +
	    P2ROUNDUP_TYPED(sizeof (spl_kmem_obj_t), align, uint32_t));
}

static inline uint32_t
spl_sks_size(spl_kmem_cache_t *skc)
{
	return (P2ROUNDUP_TYPED(sizeof (spl_kmem_slab_t),
	    skc->skc_obj_align, uint32_t));
}

static inline spl_kmem_obj_t *
spl_sko_from_obj(spl_kmem_cache_t *skc, void *obj)
{
	return (obj + P2ROUNDUP_TYPED(skc->skc_obj_size,
	    skc->skc_obj_align, uint32_t));
}

void
dump_sks_content(spl_kmem_cache_t *skc, spl_kmem_slab_t *sks, int cnt)
{
	skc = EXTAS->k2u(skc, sizeof(*skc));
	sks = EXTAS->k2u(sks, sizeof(*sks));

	uint32_t obj_size = spl_obj_size(skc);
	uint32_t sks_size = spl_sks_size(skc);

	printf("dump sks contents, skc[%p,%p], sks[%p,%p], cnt[%d], "
	    "obj_size[%u], sks_size[%u]...\n",
	    EXTAS->u2k(skc,1), skc,
	    EXTAS->u2k(sks,1), sks,
	    obj_size, sks_size, cnt);

	for (int i = 0; i < cnt; i++) {
		void *base = sks;
		void *obj = base + sks_size + (i * obj_size);
		spl_kmem_obj_t *sko = spl_sko_from_obj(skc, obj);
		printf("[%d]addr=%p,magic=%x,slab=%p,sko=%p\n",
		    i, sko->sko_addr, sko->sko_magic, sko->sko_slab,
		    EXTAS->u2k(sko,1));
	}
}

void
dump_sks(void)
{
	spl_kmem_cache_t *skc =
	    (spl_kmem_cache_t *)(uintptr_t) 0xfffff1319673a000;
	skc = EXTAS->k2u(skc, sizeof(spl_kmem_cache_t));
	uint32_t obj_size = spl_obj_size(skc);
	uint32_t sks_size = spl_sks_size(skc);

	const size_t pagesize = 64 * 1024;
	uint64_t mmap_size = ((uintptr_t)0xffff00002f99fff0 & ~(pagesize-1)) -
	    (uintptr_t)0xffff00002e980000;

	void *sks = (void*)(uintptr_t)0xffff00002e980000;
	void *usr = EXTAS->k2u(sks, pagesize + mmap_size);
	printf("dump sks knl=%p, usr=%p, obj_sz=%u, sks_sz=%u, sz=%lfMB\n",
	    sks, usr, obj_size, sks_size, (double)mmap_size/(1024 * 1024));

#if 1
	for (int i = 0; i < 10000; i++)
		if (check_mem(i, sks + i * pagesize, pagesize) == -1)
			break;
	dump_sks_content(skc, usr, 1);
#endif
}

void
__main__(int argc, char *argv[])
{
	// setup_dbuf_table();
	// debug_zio_buf_cache();
	dump_sks();
}
