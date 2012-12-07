/*
 * sfi.c - driver for parsing sfi mmap table and build e820 table
 *
 * Copyright (c) 2009, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "types.h"
#include "bootparam.h"
#include "bootstub.h"
#include "spi-uart.h"
#include "sfi.h"

#define bs_printk(x) { if (!*(int *)SPI_UART_SUPPRESSION) bs_spi_printk(x); }

#define SFI_BASE_ADDR		0x000E0000
#define SFI_LENGTH		0x00020000

static unsigned long sfi_search_mmap(unsigned long start, int len)
{
	unsigned long i = 0;
	char *pchar = (char *)start;

	for (i = 0; i < len; i++, pchar++) {
		if (pchar[0] == 'M'
			&& pchar[1] == 'M'
			&& pchar[2] == 'A'
			&& pchar[3] == 'P')
			return start + i;
	}
	return 0;
}

void sfi_setup_e820(struct boot_params *bp)
{
	struct sfi_table *sb;
	struct sfi_mem_entry *mentry;
	unsigned long long start, end, size;
	int i, num, type, total;

	bp->e820_entries = 0;
	total = 0;

	/* search for sfi mmap table */
	sb = (struct sfi_table *)sfi_search_mmap(SFI_BASE_ADDR, SFI_LENGTH);
	if (!sb) {
		bs_printk("Bootstub: failed to locate SFI MMAP table\n");
		return;
	}
	bs_printk("Bootstub: will use sfi mmap table for e820 table\n");
	num = SFI_GET_ENTRY_NUM(sb, sfi_mem_entry);
	mentry = (struct sfi_mem_entry *)sb->pentry;

	for (i = 0; i < num; i++) {
		start = mentry->phy_start;
		size = mentry->pages << 12;
		end = start + size;

		if (start > end)
			continue;

		/* translate SFI mmap type to E820 map type */
		switch (mentry->type) {
		case SFI_MEM_CONV:
			type = E820_RAM;
			break;
		case SFI_MEM_UNUSABLE:
		case SFI_RUNTIME_SERVICE_DATA:
			mentry++;
			continue;
		default:
			type = E820_RESERVED;
		}

		if (total == E820MAX)
			break;
		bp->e820_map[total].addr = start;
		bp->e820_map[total].size = size;
		bp->e820_map[total++].type = type;

		mentry++;
	}

	bp->e820_entries = total;
}
