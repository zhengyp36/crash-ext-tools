#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <auto/zfs_crash.h>
#include <crash/interface.h>

static tsd_hash_table_t *tsd_hash_table;

static void
tsd_hash_table_setup(void)
{
	void *addr = EXTAS->lookup_symbol("tsd_hash_table");
	addr = EXTAS->k2u(addr, sizeof(void*));
	
	tsd_hash_table_t *table = *(void**)addr;
	tsd_hash_table = EXTAS->k2u(table, sizeof(tsd_hash_table_t));
	table = tsd_hash_table;

	int size = 1 << table->ht_bits;
	table->ht_bins = EXTAS->k2u(table->ht_bins, sizeof (tsd_hash_bin_t) * size);
}

static uint32_t
traverse_list(tsd_hash_bin_t *bin, uint32_t key, uint32_t *cnt)
{
	struct hlist_node *node;
	size_t off = offsetof(tsd_hash_entry_t, he_list);

	uint32_t curr_cnt = 0;
	for (node = bin->hb_head.first; node; node = node->next) {
		tsd_hash_entry_t *entry = EXTAS->k2u((char*)node - off, sizeof(tsd_hash_entry_t));
		if (!entry) {
			printf("Error: *** convert error\n");
			break;
		}
		node = &entry->he_list;
		if (entry->he_key == key) {
			printf("entry: pid=%u, key=%u\n", entry->he_pid, entry->he_key);
			if (++*cnt > 10)
				break;
		}
		curr_cnt++;
	}

	return (curr_cnt);
}

static void
tsd_hash_table_search(uint32_t key)
{
	tsd_hash_table_t *table = tsd_hash_table;

	uint32_t cnt = 0;
	uint32_t print_curr_cnt = 0;
	int size = 1 << table->ht_bits;
	for (int i = 0; i < size; i++) {
		uint32_t curr_cnt = traverse_list(&table->ht_bins[i], key, &cnt);
		if (print_curr_cnt)
			printf("[%04d] %u\n", i, curr_cnt);
	}
}

void
__main__(int argc, char *argv[])
{
	tsd_hash_table_setup();
	tsd_hash_table_t *table = tsd_hash_table;
	printf("table->ht_bins = %p\n", table->ht_bins);
	tsd_hash_table_search(5); /* rrw_tsd_key */
	/*
		key:
			taskq_tsd:线程key，taskq_thread创建线程使用；
			zfs_async_io_key：Used for TSD for processing completed asynchronous I/Os.
			rrw_tsd_key(5)：读写锁key，rrw_enter()使用。
			zfs_geom_probe_vdev_key：
			    * Thread local storage used to indicate when a thread is probing geoms
			    * for their guids.  If NULL, this thread is not tasting geoms.  If non NULL,
			    * it is looking for a replacement for the vdev_t* that is its value.
			zfs_allow_log_key：操作历史记录key；
			zfs_fsyncer_key：zfs_log_write()使用，zfs日志key。
	*/
}
