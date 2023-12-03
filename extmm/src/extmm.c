#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/extmm.h>
#include <sys/debug.h>

#undef PAGE_SIZE
#define PAGE_SIZE extmm_pagesize()

#undef PAGE_MASK
#define PAGE_MASK (PAGE_SIZE - 1)

#ifndef MIN
#define MIN(x,y) ((x) < (y) ? (x) : (y))
#endif

#ifndef MAX
#define MAX(x,y) ((x) > (y) ? (x) : (y))
#endif

#define EXTMM_LOG_PATH "extmm.log"

void
extmm_vlog(const char *fmt, va_list ap)
{
	FILE *fp = fopen(EXTMM_LOG_PATH, "a+");
	if (fp) {
		vfprintf(fp, fmt, ap);
		fclose(fp);
	}
}

void
extmm_log(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	extmm_vlog(fmt, ap);
	va_end(ap);
}

static mmtop_t mmtop;

static inline mmtop_t *
extmm_top(void)
{
	mmtop_t *top = &mmtop;
	ASSERT(top->kread);
	return (&mmtop);
}

static inline boolean_t
intersectant(uintptr_t s1, uintptr_t e1, uintptr_t s2, uintptr_t e2)
{
	/* asume: s1 < e1, s2 < e2 */
	#define IN_RANGE(v,s,e) ((v) >= (s) && (v) < (e))
	return (IN_RANGE(s1,s2,e2) || IN_RANGE(s2,s1,e1));
}

static inline boolean_t
mergable(uintptr_t s1, uintptr_t e1, uintptr_t s2, uintptr_t e2)
{
	/* asume: s1 < e1, s2 < e2 */
	#define IN_OR_ADJ(v,s,e) ((v) >= (s) && (v) <= (e))
	return (IN_OR_ADJ(s1,s2,e2) || IN_OR_ADJ(s2,s1,e1));
}

static inline uintptr_t
calc_sum(uintptr_t x, uintptr_t y)
{
	const uintptr_t inf = (uintptr_t)-1;
	return (x < inf - y ? x + y : inf);
}

static inline int
sub_cmp(const mmsub_t *n1, const mmsub_t *n2, int ndx)
{
	boolean_t equal = intersectant(
	    n1->start[ndx], calc_sum(n1->start[ndx],n1->size),
	    n2->start[ndx], calc_sum(n2->start[ndx],n2->size));
	return (equal ? 0 : TREE_CMP(n1->start[ndx],n2->start[ndx]));
}

static int
ksub_cmp(const void *n1, const void *n2)
{
	return (sub_cmp(n1,n2,KINDEX));
}

static int
usub_cmp(const void *n1, const void *n2)
{
	return (sub_cmp(n1,n2,UINDEX));
}

static int
subn_cmp(const void *_n1, const void *_n2)
{
	const mmsn_t *n1 = (const mmsn_t*)_n1;
	const mmsn_t *n2 = (const mmsn_t*)_n2;
	boolean_t merg = mergable(n1->start, calc_sum(n1->start,n1->size),
	    n2->start, calc_sum(n2->start,n2->size));
	return (merg ? 0 : TREE_CMP(n1->start, n2->start));
}

static void
extmm_init_impl(mmtop_t *top, pfn_mmread_t *kread)
{
	top->inited = 1;
	top->kread = kread;
	AVL_CREATE(&top->tree[KINDEX], ksub_cmp, mmsub_t, intop[KINDEX]);
	AVL_CREATE(&top->tree[UINDEX], usub_cmp, mmsub_t, intop[UINDEX]);
}

void
extmm_init(pfn_mmread_t *kread)
{
	mmtop_t *top = &mmtop;
	if (top->inited)
		return;

	ASSERT(kread);
	extmm_init_impl(top, kread);
}

void
extmm_mmap(uintptr_t kaddr, uintptr_t uaddr, size_t size)
{
	mmtop_t *top = extmm_top();
	mmsub_t *sub = malloc(sizeof(*sub));
	ASSERT(sub);

	sub->start[KINDEX] = kaddr;
	sub->start[UINDEX] = uaddr;
	sub->size = size;
	sub->top = top;
	AVL_CREATE(&sub->tree, subn_cmp, mmsn_t, insub);

	avl_index_t kwhere, uwhere;
	void *n = avl_find(&top->tree[KINDEX], sub, &kwhere);
	ASSERT(!n);
	n = avl_find(&top->tree[UINDEX], sub, &uwhere);
	ASSERT(!n);

	avl_insert(&top->tree[KINDEX], sub, kwhere);
	avl_insert(&top->tree[UINDEX], sub, uwhere);
}

static inline mmsub_t *
lookupsub(mmtop_t *top, uintptr_t addr, int ndx, uintptr_t *addr_off)
{
	mmsub_t key = { .size = 1 };
	key.start[ndx] = addr;

	avl_index_t where;
	mmsub_t *sub = avl_find(&top->tree[ndx], &key, &where);
	if (sub)
		*addr_off = addr - sub->start[ndx];
	return (sub);
}

static inline boolean_t
addr_space_contains(const mmsn_t *x, const mmsn_t *y)
{
	return (y->start >= x->start &&
	    calc_sum(y->start,y->size) <= calc_sum(x->start,x->size));
}

static inline void
merge_range(mmsn_t *dst, const mmsn_t *src)
{
	uintptr_t start = MIN(dst->start, src->start);
	uintptr_t end = MAX(calc_sum(dst->start,dst->size),
	    calc_sum(src->start,src->size));

	dst->start = start;
	dst->size = end - start;
}

static inline int
mnode_cmp(const mnode_t *x, const mnode_t *y)
{
	return (TREE_CMP(x->start, y->start));
}

static void
mnode_mv_one(mnode_t **head, mnode_t **tail, mnode_t **src)
{
	mnode_t *n = *src;
	*src = n->next;
	n->next = NULL;

	if (*tail) {
		(*tail)->next = n;
		*tail = n;
	} else {
		*head = *tail = n;
	}
}

static void
mnode_mv_all(mnode_t **head, mnode_t **tail, mnode_t **src)
{
	mnode_t *src_head, *src_tail;
	src_head = src_tail = *src;
	*src = NULL;

	while (src_tail->next)
		src_tail = src_tail->next;

	if (*tail) {
		(*tail)->next = src_head;
		*tail = src_tail;
	} else {
		*head = src_head;
		*tail = src_tail;
	}
}

static void
merge_list(mmsn_t *dst, mmsn_t *src)
{
	mnode_t *head = NULL;
	mnode_t *tail = NULL;

	while (dst->maplist && src->maplist) {
		int r = mnode_cmp(dst->maplist, src->maplist);
		ASSERT(r);
		mnode_mv_one(&head, &tail,
		    r < 0 ? &dst->maplist : &src->maplist);
	}

	if (dst->maplist)
		mnode_mv_all(&head, &tail, &dst->maplist);
	if (src->maplist)
		mnode_mv_all(&head, &tail, &src->maplist);

	dst->maplist = head;
}

static void
mmsn_merge(mmsn_t *dst, mmsn_t *src)
{
	merge_range(dst, src);
	merge_list(dst, src);
}

static void
mmsn_merge_nodes_intree(avl_tree_t *tree, mmsn_t *dst)
{
	mmsn_t *n;
	avl_index_t where;

	while (!!(n = avl_find(tree, dst, &where))) {
		avl_remove(tree, n);
		mmsn_merge(dst, n);
		free(n);
	}
}

static boolean_t
mmsn_readable(const mmsn_t *n)
{
	char buf[1];

	uintptr_t kaddr = n->sub->start[KINDEX] + n->start;
	void *start = (void*)kaddr;
	if (n->sub->top->kread(start, buf, sizeof(buf)))
		return (B_FALSE);

	if (n->size > 0) {
		void *end = (void*)(kaddr + n->size - 1);
		if (n->sub->top->kread(end, buf, sizeof(buf)))
			return (B_FALSE);
	}

	return (B_TRUE);
}

static mnode_t *
mmsn_map_slice(mmsub_t *sub, uintptr_t start, uintptr_t end)
{
	void *uaddr = (void*)(sub->start[UINDEX] + start);
	uintptr_t sz = end - start;

	const int props = PROT_READ | PROT_WRITE;
	const int flags = MAP_PRIVATE | MAP_ANONYMOUS;

	void *ret = mmap(uaddr, sz, props, flags, -1, 0);
	if (ret != uaddr)
		printf("Error: *** mmap err %d, addr=%p, sz=%lx, ret=%p\n",
		    errno, uaddr, sz, ret);
	VERIFY(ret == uaddr);

	void *kaddr = (void*)(sub->start[KINDEX] + start);
	int rc = sub->top->kread(kaddr, uaddr, sz);
	VERIFY(rc == 0);

	mnode_t *n = malloc(sizeof(*n));
	n->next = NULL;
	n->start = start;
	n->size = sz;

	return (n);
}

static void
mmsn_do_map(mmsub_t *sub, mmsn_t *n)
{
	mnode_t *head = NULL;
	mnode_t *tail = NULL;
	mnode_t *curr;

	uintptr_t start = n->start, end = calc_sum(start,n->size);
	while (start < end) {
		if (!n->maplist) {
			curr = mmsn_map_slice(sub, start, end);
			mnode_mv_one(&head, &tail, &curr);
			break;
		} else if (start != n->maplist->start) {
			ASSERT(start < n->maplist->start);
			curr = mmsn_map_slice(sub, start, n->maplist->start);
			mnode_mv_one(&head, &tail, &curr);
			start = n->maplist->start;
		} else {
			start += n->maplist->size;
			mnode_mv_one(&head, &tail, &n->maplist);
		}
	}

	VERIFY(n->maplist == NULL);
	VERIFY(head->start == n->start);
	VERIFY(tail->start + tail->size == end);

	n->maplist = head;
}

static int
extmm_load(mmsub_t *sub, uintptr_t addr, size_t size)
{
	uintptr_t start = addr & ~PAGE_MASK;
	uintptr_t end = (addr + size + PAGE_MASK) & ~PAGE_MASK;

	mmsn_t key = {
		.start = start,
		.size  = end - start,
		.sub   = sub,
	};

	avl_index_t where;
	mmsn_t *n = avl_find(&sub->tree, &key, &where);
	if (n && addr_space_contains(n, &key))
		return (0);

	if (!mmsn_readable(&key))
		return (-1);

	if (!n) {
		n = malloc(sizeof(*n));
		*n = key;
	} else {
		avl_remove(&sub->tree, n);
		mmsn_merge(n, &key);
		mmsn_merge_nodes_intree(&sub->tree, n);
	}

	mmsn_do_map(sub, n);

	mmsn_t *tmp = avl_find(&sub->tree, n, &where);
	ASSERT(!tmp);
	avl_insert(&sub->tree, n, where);

	return (0);
}

static inline int
convert_ndx(int ndx)
{
	/*
	 * KINDEX => UINDEX
	 * UINDEX => KINDEX
	 */
	return (1 - ndx);
}

int mm_convert_error = 0;
int mm_convert_log = 0;

static uintptr_t
mm_convert(mmtop_t *top, uintptr_t abs_addr, size_t size, int ndx)
{
	mm_convert_error = 0;
	if (abs_addr == 0)
		return (0);

	uintptr_t addr;
	mmsub_t *sub = lookupsub(top, abs_addr, ndx, &addr);
	if (!sub) {
		sub = lookupsub(top, abs_addr, convert_ndx(ndx), &addr);
		if (!sub) {
			mm_convert_error = 1;
			if (mm_convert_log)
				EXTMM_LOG("[ERROR][%s]convert %lx error",
				    (ndx == UINDEX ? "U2K" : "K2U"), abs_addr);
		}
		return (sub ? abs_addr : 0);
	}

	int ret = extmm_load(sub, addr, size);
	if (ret)
		return (0);

	return (sub->start[convert_ndx(ndx)] + addr);
}

static inline int
is_uk_addr(mmtop_t *top, uintptr_t abs_addr, int ndx)
{
	uintptr_t addr;
	return (!!lookupsub(top, abs_addr, ndx, &addr));
}

int
extmm_is_kaddr(uintptr_t kaddr)
{
	return (is_uk_addr(extmm_top(), kaddr, KINDEX));
}

int
extmm_is_uaddr(uintptr_t kaddr)
{
	return (is_uk_addr(extmm_top(), kaddr, UINDEX));
}

uintptr_t
extmm_k2u(uintptr_t kaddr, size_t size)
{
	uintptr_t uaddr = mm_convert(extmm_top(), kaddr, size, KINDEX);
	return (uaddr ? uaddr :
	    extmm_is_uaddr(kaddr) ? kaddr : 0);
}

uintptr_t
extmm_u2k(uintptr_t uaddr, size_t size)
{
	uintptr_t kaddr = mm_convert(extmm_top(), uaddr, size, UINDEX);
	return (kaddr ? kaddr :
	    extmm_is_kaddr(uaddr) ? uaddr : 0);
}

static void
mnode_destroy(uintptr_t base, mnode_t *n)
{
	void *addr = (void*)(base + n->start);
	munmap(addr, n->size);
	free(n);
}

static void
mmsn_destroy(mmsn_t *sn)
{
	mnode_t *n, *next;
	for (n = sn->maplist; n; n = next) {
		next = n->next;
		mnode_destroy(sn->sub->start[UINDEX], n);
	}

	free(sn);
}

static void
sub_destroy(mmsub_t *sub)
{
	void *cookie = NULL;
	mmsn_t *sn;

	while ((sn = avl_destroy_nodes(&sub->tree, &cookie)))
		mmsn_destroy(sn);
	free(sub);
}

void
extmm_cleanup(void)
{
	mmtop_t *top = extmm_top();
	void *cookie = NULL;

	mmsub_t *sub;
	while ((sub = avl_destroy_nodes(&top->tree[KINDEX], &cookie))) {
		avl_remove(&top->tree[UINDEX], sub);
		sub_destroy(sub);
	}

	extmm_init_impl(top, top->kread);
}

long
extmm_pagesize(void)
{
	return (sysconf(_SC_PAGESIZE));
}

int
extmm_readmem(void *kaddr, void *buf, size_t size)
{
	mmtop_t *top = extmm_top();
	return (top->kread(kaddr, buf, size));
}
