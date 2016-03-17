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
	if(fd<0)
		return 1;
	off_t size = lseek(fd, 0, SEEK_END);
	if(size<=0)
		return 1;
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
	if(memcmp(base, BOOT_MAGIC, BOOT_MAGIC_SIZE) != 0)
		return 1;
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

int bootimg_parse_ramdisk(const char *decompressor, int flags, uint8_t *ptr, long size,
		int (*do_stuff)(const char *filename, int fd, long len)) {
	(void) flags;

	char ramdisk[] = "/tmp/bootimg.XXXXXX";
	int ramdiskfd = mkstemp(ramdisk);
	unlink(ramdisk);
	long off = 0;
	while(size > 0) {
		long ret = write(ramdiskfd, ptr + off, size);
		if(ret<=0)
			return __LINE__;
		size -= ret;
		off += ret;
	}
	lseek(ramdiskfd, 0, SEEK_SET);

	int p[2];
	if(pipe(p))
		return __LINE__;

	switch(fork()) {
		case -1:
			return __LINE__;
		case 0:
			dup2(ramdiskfd, 0);
			dup2(p[1], 1);
			execlp(decompressor, decompressor, "-d", "-", NULL);
			exit(0);
			break;
	};

	struct {
		char magic[6];
		char ino[8];
		char mode[8];
		char uid[8];
		char gid[8];
		char nlinks[8];
		char mtime[8];
		char filesize[8];
		char major[8];
		char minor[8];
		char rmajor[8];
		char rminor[8];
		char namesize[8];
		char chksum[8];
	} hdr;

	while(read(p[0], &hdr, sizeof(hdr)) == sizeof(hdr)) {
		if(strncmp(hdr.magic, "070701", 6) != 0)
			return __LINE__;

		long alignment = 0;
		char v[9];
		v[8]=0;

		memcpy(v, hdr.namesize, 8);
		long namesize = strtoll(v, NULL, 16);

		memcpy(v, hdr.filesize, 8);
		long filesize = strtoll(v, NULL, 16);

		char filename[namesize+1];
		if(read(p[0], filename, namesize) != namesize)
			return __LINE__;
		if(strcmp(filename, "TRAILER!!!")==0)
			break;

		alignment = 110 + namesize;
		alignment %= 4;
		if(alignment)
			read(p[0], v, 4-alignment);

		filename[namesize]=0;
		char tmpfile[] = "/tmp/bootimg.XXXXXX";
		int fd = mkstemp(tmpfile);
		unlink(tmpfile);

		{
			long s = filesize;
			char buf[1024];
			while(s>0) {
				long n = (sizeof(buf) < s) ? sizeof(buf) : s;
				long r = read(p[0], buf, n);
				if(write(fd, buf, r) != r)
					return __LINE__;
				s -= r;
				if(r <= 0)
					return __LINE__;
			}
		}
		lseek(fd, 0, SEEK_SET);
		if(do_stuff(filename, fd, filesize))
			return __LINE__;

		alignment = filesize;
		alignment %= 4;
		if(alignment)
			read(p[0], &alignment, 4-alignment);
		close(fd);
	}
	return 0;
}
