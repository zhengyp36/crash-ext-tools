#include <stdio.h>
#include <string.h>
#include <auto/zfs_crash.h>
#include <crash/interface.h>

/*
 * gdb set $task = (struct scsi_task *)0xffffee3142809800
 * gdb set $sl = (sbd_lu_t *)$task->task_lu->lu_provider_private
 * gdb set $vp = $sl->sl_data_vp
 * gdb set $file = $vp->v_file
 *
 * gdb set $zv01 = (zvol_state_t*)0xfffff231830cb000
 * gdb set $zv02 = (zvol_state_t*)0xfffff2319aa65000
 * gdb set $zv = $zv01
 */

static uint8_t
sbd_calc_sum(uint8_t *buf, int size)
{
	uint8_t s = 0;

	while (size > 0)
		s += buf[--size];

	return (s);
}

#define DUMP_FIELD(s,w,m,fmt) printf("%-" #w "s " fmt "\n", #m, s->m)

/*
 * >>> dump sbd_meta_start_t
 * sm_magic           53554e5342444c55
 * sm_meta_size       20a
 * sm_meta_size_used  20a
 * sm_rsvd1           0
 * sm_rsvd2           0
 * sm_ver_major       1
 * sm_ver_minor       1
 * sm_ver_subminor    0
 * sm_flags           0
 * sm_chksum          8a
 */
static void
check_sbd_meta_start(sbd_meta_start_t *sm)
{
	printf(">>> dump sbd_meta_start_t\n");
	DUMP_FIELD(sm, 18, sm_magic, "%lx");
	DUMP_FIELD(sm, 18, sm_meta_size, "%lx");
	DUMP_FIELD(sm, 18, sm_meta_size_used, "%lx");
	DUMP_FIELD(sm, 18, sm_rsvd1, "%lx");
	DUMP_FIELD(sm, 18, sm_rsvd2, "%lx");
	DUMP_FIELD(sm, 18, sm_ver_major, "%x");
	DUMP_FIELD(sm, 18, sm_ver_minor, "%x");
	DUMP_FIELD(sm, 18, sm_ver_subminor, "%x");
	DUMP_FIELD(sm, 18, sm_flags, "%x");
	DUMP_FIELD(sm, 18, sm_chksum, "%x");

	uint8_t chksum = sbd_calc_sum((uint8_t *)sm, sizeof (*sm) - 1);
	if (chksum != sm->sm_chksum)
		printf("%-18s %x\n", "expect-chksum", chksum);
}

static uint8_t
sbd_calc_section_sum(sm_section_hdr_t *sm, uint32_t sz)
{
	uint8_t s, o;

	o = sm->sms_chksum;
	sm->sms_chksum = 0;
	s = sbd_calc_sum((uint8_t *)sm, sz);
	sm->sms_chksum = o;

	return (s);
}

static int
check_sm_section_hdr(sm_section_hdr_t *sm)
{
	uint8_t s = sbd_calc_section_sum(sm, sm->sms_size);
	return (s == sm->sms_chksum);
}

static sm_section_hdr_t *
dump_sm_section_hdr(sbd_meta_start_t *sm, uint16_t sms_id)
{
	printf(">>> dump sm_section_hdr_t\n");

	sm_section_hdr_t *r = NULL;
	sm_section_hdr_t *h = NULL;
	char *meta = (void*)sm;
	int i = 0;

	for (size_t off = sizeof (sbd_meta_start_t);
	    off < sm->sm_meta_size_used; off += h->sms_size) {
		h = (void*)&meta[off];
		if (h->sms_id == sms_id)
			r = h;
		printf("section[%d]: order=%02x, id=%04x, size=%x, chksum=%s\n",
		    i, h->sms_data_order, h->sms_id, h->sms_size,
		    check_sm_section_hdr(h) ? "ok" : "err");
		i++;
	}

	return (r);
}

static void
dump_sli(sbd_lu_info_1_1_t *sli)
{
	printf(">>> sli[%p,%p]\n", sli, EXTAS->u2k(sli, 1));
	DUMP_FIELD(sli, 30, sli_sms_header.sms_offset, "%lx");
	DUMP_FIELD(sli, 30, sli_sms_header.sms_size, "%x");
	DUMP_FIELD(sli, 30, sli_sms_header.sms_id, "%x");
	DUMP_FIELD(sli, 30, sli_sms_header.sms_data_order, "%x");
	DUMP_FIELD(sli, 30, sli_sms_header.sms_chksum, "%x");
	DUMP_FIELD(sli, 30, sli_flags, "%x");
	DUMP_FIELD(sli, 30, sli_lu_size, "%lx");
	DUMP_FIELD(sli, 30, sli_meta_fname_offset, "%lx");
	DUMP_FIELD(sli, 30, sli_data_fname_offset, "%lx");
	DUMP_FIELD(sli, 30, sli_serial_offset, "%lx");
	DUMP_FIELD(sli, 30, sli_alias_offset, "%lx");
	DUMP_FIELD(sli, 30, sli_data_blocksize_shift, "%x");
	DUMP_FIELD(sli, 30, sli_data_order, "%x");
	DUMP_FIELD(sli, 30, sli_serial_size, "%x");
	DUMP_FIELD(sli, 30, sli_rsvd1, "%x");
	DUMP_FIELD(sli, 30, sli_mgmt_url_offset, "%lx");
	printf("%-30s %s\n", "sli_buf", sli->sli_buf);
}

static void
dump_sl(sbd_lu_t *sl)
{
	sl = EXTAS->k2u(sl, sizeof(*sl));
	printf("sl: %p, %p\n", sl, EXTAS->u2k(sl, 1));

	// sbd_open_zfs_meta(): read data from zap into sl->sl_zfs_meta
	char *meta = EXTAS->k2u(sl->sl_zfs_meta, 2048);
	printf("meta: %p, %p\n", meta, EXTAS->u2k(meta,1));

	// sbd_load_meta_start(): read sbd_meta_start_t
	sbd_meta_start_t *sm = (void*)meta;
	check_sbd_meta_start(sm);

	// sbd_read_meta_section(): dump first & find sec by id
	uint16_t sms_id = 1; // SMS_ID_LU_INFO_1_1
	sm_section_hdr_t *h = dump_sm_section_hdr(sm, sms_id);

	sbd_lu_info_1_1_t *sli = (sbd_lu_info_1_1_t *)h;
	dump_sli(sli);
}

void
__main__(int argc, char *argv[])
{
	sbd_lu_t *sl_arr[] = {
		(void*)0xfffff031843fd0c0,
		(void*)0xfffff0318610a8c0,
	};

	int dump_cnt = 0, idx;
	for (int i = 1; i < argc; i++) {
		idx = !strcmp(argv[i], "0") ? 0 :
		    !strcmp(argv[i], "1") ? 1 : -1;
		if (idx == -1) {
			printf(">>> dump sl[%s] error\n", argv[i]);
		} else {
			printf(">>> dump sl[%d]=%p\n", idx, sl_arr[idx]);
			dump_sl(sl_arr[idx]);
			dump_cnt++;
		}
	}

	if (dump_cnt == 0) {
		idx = 0;
		printf(">>> dump sl[%d]=%p\n", idx, sl_arr[idx]);
		dump_sl(sl_arr[idx]);
	}
}
