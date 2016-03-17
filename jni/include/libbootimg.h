#ifndef LIB_BOOTIMG_H
#define LIB_BOOTIMG_H

#ifdef _cplusplus
extern "C" {
#endif

#include <stdint.h>

enum {
	BOOT_HEADER		= 0x00000001,
	BOOT_KERNEL		= 0x00000002,
	BOOT_RAMDISK		= 0x00000003,
	BOOT_SECOND		= 0x00000004,
	BOOT_QCDT		= 0x00000005,
	BOOT_TYPE		= 0x0000000f,

	BOOT_GZIP		= 0x00000010,
	BOOT_LZ4		= 0x00000020,
	BOOT_UNKNOWN_COMPR	= 0x00000800,
	BOOT_COMPRESSION 	= 0x00000ff0,

	BOOT_MTK		= 0x00010000,
	BOOT_RK			= 0x00020000,
	BOOT_CHROMEOS		= 0x00030000,
	BOOT_PLATFORM		= 0x000f0000,

	BOOT_UNKNOWN_SIGN	= 0x80000000, // do not try to repack
	BOOT_SPECICAL		= 0xffff0000,
};

extern int bootimg_parse(const char* filename, int do_stuff(int flags, uint8_t *ptr, int long));
extern int bootimg_parse_ramdisk(const char *decompressor, int flags, uint8_t *ptr, long size, int (*do_stuff)(const char *filename, int fd, long len));

#ifdef _cplusplus
};
#endif
#endif
