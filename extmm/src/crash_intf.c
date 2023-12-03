#define __CRASH_INTF_IMPLEMENT__
#include <sys/extmm.h>
#include <crash/defs.h>
#include <crash/interface.h>

static const uint64_t ADDR_MASK = 0xFFFFF00000000000ULL;

static void *
crash_intf_lookup_symbol(const char *sym)
{
	struct syment *ent = symbol_search((char*)sym);
	return (ent ? (void*)(uintptr_t)ent->value : NULL);
}

static int try_add_addr_space(void *kaddr);

static void *
crash_intf_u2k(void *uaddr, size_t size)
{
	return ((void*)extmm_u2k((uintptr_t)uaddr, size));
}

static void *
crash_intf_k2u(void *kaddr, size_t size)
{
	extern int mm_convert_log, mm_convert_error;

	int previos = mm_convert_log;
	mm_convert_log = 0;
	void *uaddr = (void*)extmm_k2u((uintptr_t)kaddr, size);
	mm_convert_log = previos;
	int convert_err = mm_convert_error;

	if (!uaddr && try_add_addr_space(kaddr)) {
		uaddr = (void*)extmm_k2u((uintptr_t)kaddr, size);
		if (!uaddr && convert_err)
			EXTMM_LOG("[ERROR][K2U]convert %p error", kaddr);
	}
	return (uaddr);
}

static int crash_load_as_show = 0;

static void
crash_intf_load_as(as_ent_t *ent, int num)
{
	for (int i = 0; i < num; i++) {
		extmm_mmap(ent[i].kbase, ent[i].ubase, ent[i].size);
		if (crash_load_as_show)
			printf("[EXTMM]kbase=%lx,ubase=%lx,size=%lx\n",
			    ent[i].kbase, ent[i].ubase, ent[i].size);
	}
}

static long
crash_intf_pagesize(void)
{
	return (extmm_pagesize());
}

static int
crash_intf_is_kaddr(void *addr)
{
	return (extmm_is_kaddr((uintptr_t)addr));
}

static int
crash_intf_is_uaddr(void *addr)
{
	return (extmm_is_uaddr((uintptr_t)addr));
}

/*
 * typedef struct buf_hash_table {
 *     uint64_t ht_mask;
 *     arc_buf_hdr_t **ht_table;
 *     struct ht_lock ht_locks[BUF_LOCKS];
 * } buf_hash_table_t;
 * 
 * static buf_hash_table_t buf_hash_table;
 */

static int
crash_get_addr_space(void **kbss, void **kheap)
{
	void *kaddr[2] = {0};

	// The address of buf_hash_table stands for bss
	const char *ksym = "buf_hash_table";
	kaddr[0] = crash_intf_lookup_symbol(ksym);
	if (!kaddr[0]) {
		printf("Error: *** lookup symbol(%s) error\n", ksym);
		return (0);
	}

	// buf_hash_table.ht_table is kmalloc-address which stands for heap
	int rc = extmm_readmem(kaddr[0]+sizeof(uint64_t),
	    &kaddr[1], sizeof(kaddr[1]));
	if (rc) {
		printf("Error: *** readmem(%s:%p) error\n", ksym, kaddr[0]);
		return (0);
	}

	if (!kaddr[1]) {
		printf("Error: *** heap is null\n");
		return (0);
	}

	*kbss  = (void*)((uintptr_t)kaddr[0] & ADDR_MASK);
	*kheap = (void*)((uintptr_t)kaddr[1] & ADDR_MASK);
	if (!*kbss || !*kheap) {
		printf("Error: *** bss(%p) or heap(%p) is null\n",
		    *kbss, *kheap);
		return (0);
	}

	if (*kbss == *kheap) {
		printf("Warning: *** bss(%p) is equal to heap(%p) "
		    "and clear heap\n", *kbss, *kheap);
		*kheap = 0;
		return (1);
	}

	return (2);
}

static void crash_intf_reset_mmap(void);

typedef struct {
	int as_loaded;
	crash_intf_t intf;
} crash_intf_mgr_t;

static crash_intf_mgr_t crash_intf_mgr = {
	.intf = {
		.reset_mmap	= crash_intf_reset_mmap,
		.add_mmap	= extmm_mmap,
		.load_as	= crash_intf_load_as,
		.is_kaddr	= crash_intf_is_kaddr,
		.is_uaddr	= crash_intf_is_uaddr,
		.u2k		= crash_intf_u2k,
		.k2u		= crash_intf_k2u,
		.lookup_symbol	= crash_intf_lookup_symbol,
		.get_pagesize	= crash_intf_pagesize
	}
};

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))
#endif

static uintptr_t UBASE      = 0;
static uintptr_t UBASE_STEP = 0x0000100000000000UL;
static uintptr_t UBASE_MAX  = 0x0000700000000000UL;

static void append_mmconf(const as_ent_t *);

static inline void
validate_mmconf_ent(as_ent_t *ent)
{
	const uintptr_t max_val = (uintptr_t)-1;
	if (max_val - ent->size < ent->kbase) {
		size_t new_size = ent->size -
		    crash_intf_mgr.intf.get_pagesize();
		printf("Warning: *** change addr-space "
		    "[k:%lx,u:%lx,s:%lx]=>[k:%lx,u:%lx,s:%lx]\n",
		    ent->kbase, ent->ubase, ent->size,
		    ent->kbase, ent->ubase, new_size);
		ent->size = new_size;
	}
}

static int
try_add_addr_space(void *kaddr)
{
	if (UBASE > UBASE_MAX)
		return (0);

	void *kbase = (void*)((uintptr_t)kaddr & ADDR_MASK);
	if (kbase && !crash_intf_is_kaddr(kbase)) {
		as_ent_t ent = {
			.kbase = (uintptr_t)kbase,
			.ubase = (uintptr_t)UBASE,
			.size  = 0x0000100000000000UL
		};
		validate_mmconf_ent(&ent);
		extmm_mmap(ent.kbase, ent.ubase, ent.size);
		append_mmconf(&ent);
		UBASE += UBASE_STEP;
		return (1);
	}

	return (0);
}

#define MAX_AS_ENT_NUM 128
#define CRASH_INTF_MMAP_CONF "extmm.conf"

typedef struct mmconf {
	int ent_num;
	as_ent_t *ent_tbl;
} mmconf_t;

static void
append_mmconf(const as_ent_t *ent)
{
	FILE *fp = fopen(CRASH_INTF_MMAP_CONF, "a+");
	if (!fp) {
		printf("Error: *** open %s error\n", CRASH_INTF_MMAP_CONF);
		return;
	}

	fprintf(fp, "0x%016lx 0x%016lx 0x%016lx\n",
	    ent->kbase, ent->ubase, ent->size);
	printf("Info: append [k:%lx,u:%lx,s:%lx] mmconf %s success\n",
	    ent->kbase, ent->ubase, ent->size,
	    CRASH_INTF_MMAP_CONF);

	fclose(fp);
}

static void
dump_mmconf(const mmconf_t *conf)
{
	FILE *fp = fopen(CRASH_INTF_MMAP_CONF, "w");
	if (!fp) {
		printf("Error: *** open %s error\n", CRASH_INTF_MMAP_CONF);
		return;
	}

	fprintf(fp, "%18s %18s %18s\n", "KBASE", "UBASE", "SIZE");
	for (int i = 0; i < conf->ent_num; i++) {
		as_ent_t *ent = &conf->ent_tbl[i];
		fprintf(fp, "0x%016lx 0x%016lx 0x%016lx\n",
		    ent->kbase, ent->ubase, ent->size);
	}

	printf("Info: dump mmconf %s success\n", CRASH_INTF_MMAP_CONF);
	fclose(fp);
}

static int
read_mmconf(as_ent_t *ent_tbl, int max_num)
{
	FILE *fp = fopen(CRASH_INTF_MMAP_CONF, "r");
	if (!fp)
		return (0);

	int num = 0;
	char line[1024];
	while (num < max_num && fgets(line, sizeof(line), fp)) {
		unsigned long kbase, ubase, size;
		int r = sscanf(line,
		    "0x%lx 0x%lx 0x%lx", &kbase, &ubase, &size);
		if (r != 3)
			continue;

		ent_tbl[num].kbase = (uintptr_t)kbase;
		ent_tbl[num].ubase = (uintptr_t)ubase;
		ent_tbl[num].size  = (size_t)size;
		num++;
	}

	if (num >= max_num)
		printf("Error: *** %s has too many conf more than %d\n",
		    CRASH_INTF_MMAP_CONF, max_num-1);
	assert(num < max_num);

	fclose(fp);
	return (num);
}

static void
validate_mmconf(mmconf_t *conf)
{
	for (int i = 0; i < conf->ent_num; i++)
		validate_mmconf_ent(&conf->ent_tbl[i]);
}

static const mmconf_t *
get_mmconf(void)
{
	static as_ent_t local_ent_tbl[MAX_AS_ENT_NUM];
	static mmconf_t mmconf = {};

	mmconf_t *conf = &mmconf;
	conf->ent_tbl = local_ent_tbl;
	conf->ent_num = read_mmconf(local_ent_tbl, MAX_AS_ENT_NUM);
	if (conf->ent_num > 0)
		return (conf);

	as_ent_t default_ent_tbl[] = KU_MAP;
	as_ent_t dynamic_ent_tbl[2] = {
		{
			.kbase = 0,
			.ubase = 0x0000200000000000UL,
			.size  = 0x0000100000000000UL
		}, {
			.kbase = 0,
			.ubase = 0x0000300000000000UL,
			.size  = 0x0000100000000000UL
		}
	};

	as_ent_t *ent_tbl = default_ent_tbl;
	int ent_tbl_num = ARRAY_SIZE(default_ent_tbl);

	void *bss, *heap;
	int rc = crash_get_addr_space(&bss, &heap);
	if (rc > 0) {
		dynamic_ent_tbl[0].kbase = (uintptr_t)bss;
		dynamic_ent_tbl[1].kbase = (uintptr_t)heap;
		assert(rc <= 2);

		ent_tbl = dynamic_ent_tbl;
		ent_tbl_num = rc;
	}

	conf->ent_num = ent_tbl_num;
	assert(ent_tbl_num < MAX_AS_ENT_NUM);
	memcpy(conf->ent_tbl, ent_tbl, sizeof(conf->ent_tbl[0]) * ent_tbl_num);

	validate_mmconf(conf);
	dump_mmconf(conf);
	return (conf);
}

static crash_intf_t *
do_load_as(void)
{
	crash_intf_mgr_t *mgr = &crash_intf_mgr;
	const mmconf_t * conf = get_mmconf();

	mgr->intf.load_as(conf->ent_tbl, conf->ent_num);
	mgr->as_loaded = 1;
	UBASE = conf->ent_tbl[conf->ent_num-1].ubase + UBASE_STEP;

	return (&mgr->intf);
}

static void
crash_intf_reset_mmap(void)
{
	extmm_cleanup();
	crash_intf_mgr.as_loaded = 0;
}

const crash_intf_t *
get_crash_intf(void)
{
	crash_intf_mgr_t *mgr = &crash_intf_mgr;
	return (mgr->as_loaded ? &mgr->intf : do_load_as());
}

static int import_quiet = 0;

void
extas_import_quiet(int quiet)
{
	import_quiet = quiet;
}

int
extas_import_(int *inited, void **pvar, const char *name, size_t size)
{
	void *kaddr = extas(inited)->lookup_symbol(name);
	if (!kaddr) {
		printf("Error: *** lookup symbol(%s) error\n",
		    name);
		return (-1);
	}

	void *uaddr = extas(inited)->k2u(kaddr, size);
	if (!uaddr) {
		printf("Error: *** convert kaddr(%s:%p) error\n",
		    name, kaddr);
		return (-1);
	}

	*pvar = uaddr;
	if (!import_quiet)
		printf("import symbol[%s] %p=>%p\n", name, kaddr, uaddr);

	return (0);
}
