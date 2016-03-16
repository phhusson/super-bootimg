#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <assert.h>
#include <string.h>

#include "bootimg.h"
#include <libbootimg.h>

//TODO: Search for other header types
static int parse_ramdisk(uint8_t *ptr, size_t size) {
	//GZip header
	if(memcmp(ptr, "\x1f\x8b\x08\x00", 4) == 0) {
		return BOOT_GZIP;
	//MTK header
	} else if(memcmp(ptr, "\x88\x16\x88\x58", 4) == 0) {
		return parse_ramdisk(ptr+512, size-512) | BOOT_MTK;
	} else {
		return BOOT_UNKNOWN_COMPR;
	}
}

static int search_security_hdr(uint8_t *buf, size_t size) {
	(void)size;
	if(memcmp(buf, "CHROMEOS", 8) == 0)
		return BOOT_CHROMEOS;

	if(memcmp(buf, BOOT_MAGIC, BOOT_MAGIC_SIZE) != 0)
		return BOOT_UNKNOWN_SIGN;

	return 0;
}

static int search_security(uint8_t *buf, size_t size, int pos) {
	//Rockchip signature
	if(memcmp(buf+1024, "SIGN", 4) == 0) {
		//Rockchip signature AT LEAST means the bootloader will check the crc
		//And it's possible there is a security too
		return BOOT_RK;
	}

	//If we didn't parse the whole file, it is highly likely there is a boot signature
	if(pos < size)
		return BOOT_UNKNOWN_SIGN;

	return 0;
}

/*
 * TODO:
 *  - At the moment we dump kernel + ramdisk + second + DT, it's likely we only want ramdisk
 *  - Error-handling via assert() is perhaps not the best
 */
int bootimg_parse(const char* filename, int do_stuff(int flags, uint8_t *ptr, int long)) {
	int fd = open(filename, O_RDONLY);
	off_t size = lseek(fd, 0, SEEK_END);
	lseek(fd, 0, SEEK_SET);
	uint8_t *orig = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
	uint8_t *base = orig;
	assert(base);

	int flags = 0;

	flags |= search_security_hdr(base, size);

	//We're searching for the header in the whole file, we could stop earlier.
	//At least HTC and nVidia have a signature header
	while(base<(orig+size)) {
		if(memcmp(base, BOOT_MAGIC, BOOT_MAGIC_SIZE) == 0)
			break;
		//We're searching every 256bytes, is it ok?
		base += 256;
	}
	void *kernel = NULL, *ramdisk = NULL, *second = NULL, *qcdt = NULL;

	struct boot_img_hdr *hdr = (struct boot_img_hdr*) base;
	if(!(hdr->page_size == 2048 ||
			hdr->page_size == 4096 ||
			hdr->page_size == 16384))
		return 1;

	long pos = hdr->page_size;
	kernel = base+pos;
	pos += hdr->kernel_size + hdr->page_size-1;
	pos &= ~(hdr->page_size-1L);

	ramdisk = base+pos;
	flags |= parse_ramdisk(ramdisk, hdr->ramdisk_size);
	pos += hdr->ramdisk_size + hdr->page_size-1;
	pos &= ~(hdr->page_size-1L);

	if(hdr->second_size) {
		if( (pos+hdr->second_size) > size)
			return 1;
		second = base+pos;
		pos += hdr->second_size + hdr->page_size-1;
		pos &= ~(hdr->page_size-1L);
	}

	//This is non-standard, so we triple check
	if( hdr->unused[0] &&
			pos < size &&
			(pos+hdr->unused[0]) <= size) {

		if(memcmp(base+pos, "QCDT", 4) == 0 ||
				memcmp(base+pos, "SPRD", 4) == 0) {
			qcdt = base+pos;
			pos += hdr->unused[0] + hdr->page_size-1;
			pos &= ~(hdr->page_size-1L);
		}
	}

	//If we think we find some security-related infos in the boot.img
	//create a "secure" flag to warn the user it is dangerous
	flags |= search_security(base, size, pos);

	if(do_stuff(flags|BOOT_HEADER, base, sizeof(struct boot_img_hdr)))
		return 1;

	if(do_stuff(flags|BOOT_KERNEL, kernel, hdr->kernel_size))
		return 1;

	if(do_stuff(flags|BOOT_RAMDISK, ramdisk, hdr->ramdisk_size))
		return 1;

	if(second) {
		if(do_stuff(flags|BOOT_SECOND, second, hdr->second_size))
			return 1;
	}

	if(qcdt) {
		if(do_stuff(flags|BOOT_QCDT, qcdt, hdr->unused[0]))
			return 1;
	}

	munmap(orig, size);
	close(fd);
	return 0;
}
