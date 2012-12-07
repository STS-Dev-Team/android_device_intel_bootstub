/*
 * bootstub 32 bit entry setting routings 
 *
 * Copyright (C) 2008-2010 Intel Corporation.
 * Author: Alek Du <alek.du@intel.com>
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
 *
 */

#include "types.h"
#include "bootstub.h"
#include "bootparam.h"
#include "spi-uart.h"
#include "sfi.h"

#define bs_printk(x) { if (! *(int *)SPI_UART_SUPPRESSION) bs_spi_printk(x);}

struct gdt_ptr {
        u16 len;
        u32 ptr;
} __attribute__((packed));

static void setup_gdt(void)
{
        static const u64 boot_gdt[] __attribute__((aligned(16))) = {
                /* CS: code, read/execute, 4 GB, base 0 */
                [GDT_ENTRY_BOOT_CS] = GDT_ENTRY(0xc09b, 0, 0xfffff),
                /* DS: data, read/write, 4 GB, base 0 */
                [GDT_ENTRY_BOOT_DS] = GDT_ENTRY(0xc093, 0, 0xfffff),
        };
        static struct gdt_ptr gdt;

        gdt.len = sizeof(boot_gdt)-1;
        gdt.ptr = (u32)&boot_gdt;

        asm volatile("lgdtl %0" : : "m" (gdt));
}

static void setup_idt(void)
{
        static const struct gdt_ptr null_idt = {0, 0};
        asm volatile("lidtl %0" : : "m" (null_idt));
}

static void *memcpy(void *dest, const void *src, size_t count)
{
        char *tmp = dest;
        const char *s = src;
	size_t _count = count / 4;

	while (_count--) {
		*(long *)tmp = *(long *)s;
		tmp += 4;
		s += 4;
	}
	count %= 4;
        while (count--)
                *tmp++ = *s++;
        return dest;
}

static void *memset(void *s, unsigned char c, size_t count)
{
        char *xs = s;
	size_t _count = count / 4;
	unsigned long  _c = c << 24 | c << 16 | c << 8 | c;
 
	while (_count--) {
		*(long *)xs = _c;
		xs += 4;
	}
	count %= 4;
        while (count--)
                *xs++ = c; 
        return s;
}

static size_t strnlen(const char *s, size_t maxlen)
{
        const char *es = s;
        while (*es && maxlen) {
                es++;
                maxlen--;
        }

        return (es - s);
}

static void setup_boot_params(struct boot_params *bp, struct setup_header *sh)
{
	u8 *initramfs;

	memset(bp, 0, sizeof (struct boot_params));
	bp->screen_info.orig_video_mode = 0;
	bp->screen_info.orig_video_lines = 0;
	bp->screen_info.orig_video_cols = 0;
	bp->alt_mem_k = 128*1024; // hard coded 128M mem here, since SFI will override it
	memcpy(&bp->hdr, sh, sizeof (struct setup_header));
	bp->hdr.cmd_line_ptr = CMDLINE_OFFSET;
	bp->hdr.cmdline_size = strnlen((const char *)CMDLINE_OFFSET, CMDLINE_SIZE);
	bp->hdr.type_of_loader = 0xff; //bootstub is unknown bootloader for kernel :)
	bp->hdr.ramdisk_size = *(u32 *)INITRD_SIZE_OFFSET;
	bp->hdr.ramdisk_image = (bp->alt_mem_k*1024 - bp->hdr.ramdisk_size) & 0xFFFFF000;
	bp->hdr.hardware_subarch = X86_SUBARCH_MRST;
	initramfs = (u8 *)BZIMAGE_OFFSET + *(u32 *)BZIMAGE_SIZE_OFFSET;
	if (*initramfs) {
		bs_printk("Relocating initramfs to high memory ...\n");
		memcpy((u8*)bp->hdr.ramdisk_image, initramfs, bp->hdr.ramdisk_size);
	} else {
		bs_printk("Won't relocate initramfs, are you in SLE?\n");
	}
	sfi_setup_e820(bp);
}

static int get_32bit_entry(unsigned char *ptr)
{
	while (1){
		if (*(u32 *)ptr == SETUP_SIGNATURE && *(u32 *)(ptr+4) == 0)
			break;
		ptr++;
	}
	ptr+=4;
	return (((unsigned int)ptr+511)/512)*512;
}

static inline void cpuid(u32 op, u32 *eax, u32 *ebx, u32 *ecx, u32 *edx)
{
	*eax = op;
	*ecx = 0;
	asm volatile("cpuid"
		: "=a" (*eax),
		"=b" (*ebx),
		"=c" (*ecx),
		"=d" (*edx)
		: "0" (*eax), "2" (*ecx));
}

enum cpuid_regs {
	CR_EAX = 0,
	CR_ECX,
	CR_EDX,
	CR_EBX
};

int mrst_identify_cpu(void)
{
	u32 regs[4];

	cpuid(1, &regs[CR_EAX], &regs[CR_EBX], &regs[CR_ECX], &regs[CR_EDX]);
	if ((regs[CR_EAX] & PENWELL_FAMILY) == PENWELL_FAMILY)
		return MRST_CPU_CHIP_PENWELL;
	else if ((regs[CR_EAX] & CLOVERVIEW_FAMILY) == CLOVERVIEW_FAMILY)
		return MRST_CPU_CHIP_CLOVERVIEW;
	return MRST_CPU_CHIP_LINCROFT;
}

static void setup_spi(void)
{
	if (!(*(int *)SPI_TYPE)) {
		if (mrst_identify_cpu() == MRST_CPU_CHIP_PENWELL) {
			*(int *)SPI_TYPE = 1;
			bs_printk("Penwell detected ...\n");
		} else if (mrst_identify_cpu() == MRST_CPU_CHIP_CLOVERVIEW) {
			*(int *)SPI_TYPE = 1;
			bs_printk("Cloverview detected ...\n");
		} else {
			*(int *)SPI_TYPE = 1;
			bs_printk("Lincroft detected ...\n");
		}
	}
}

int bootstub(void)
{
	setup_idt();
	setup_gdt();
	setup_spi();
	bs_printk("Bootstub Version: 1.2 ...\n");
	setup_boot_params((struct boot_params *)BOOT_PARAMS_OFFSET, 
		(struct setup_header *)SETUP_HEADER_OFFSET);
	bs_printk("Jump to kernel 32bit entry ...\n");
	return get_32bit_entry((unsigned char *)BZIMAGE_OFFSET);
}
