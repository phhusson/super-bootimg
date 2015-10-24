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

void dump(uint8_t *ptr, size_t size, char* filename) {
	unlink(filename);
	int ofd = open(filename, O_WRONLY|O_CREAT, 0644);
	assert(ofd >= 0);
	printf("pos = %p\n", ptr);
	int ret = write(ofd, ptr, size);
	assert(ret == size);
	close(ofd);
}

//TODO: Search for other header types
void dump_ramdisk(uint8_t *ptr, size_t size) {
	//GZip header
	if(memcmp(ptr, "\x1f\x8b\x08\x00", 4) == 0) {
		dump(ptr, size, "ramdisk.gz");
	//MTK header
	} else if(memcmp(ptr, "\x88\x16\x88\x58", 4) == 0) {
		dump_ramdisk(ptr+512, size-512);
	} else {
		//Since our first aim is to extract/repack ramdisk
		//Stop if we can't find it
		//Still dump it for debug purposes
		dump(ptr, size, "ramdisk");

		fprintf(stderr, "Unknown ramdisk type\n");
		abort();
	}
}

/*
 * TODO:
 *  - At the moment we dump kernel + ramdisk + second + DT, it's likely we only want ramdisk
 *  - Error-handling via assert() is perhaps not the best
 */
int main(int argc, char **argv) {
	assert(argc == 2);

	int fd = open(argv[1], O_RDONLY);
	off_t size = lseek(fd, 0, SEEK_END);
	lseek(fd, 0, SEEK_SET);
	uint8_t *orig = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
	uint8_t *base = orig;
	assert(base);

	//We're searching for the header in the whole file, we could stop earlier.
	//At least HTC and nVidia have a signature header
	while(base<(orig+size)) {
		if(memcmp(base, BOOT_MAGIC, BOOT_MAGIC_SIZE) == 0)
			break;
		//We're searching every 256bytes, is it ok?
		base += 256;
	}
	assert(base < (orig+size));

	struct boot_img_hdr *hdr = (struct boot_img_hdr*) base;
	assert(hdr->page_size == 2048);

	long pos = hdr->page_size;
	dump(base+pos, hdr->kernel_size, "kernel");
	pos += hdr->kernel_size + hdr->page_size-1;
	pos &= ~(hdr->page_size-1L);

	dump_ramdisk(base+pos, hdr->ramdisk_size);
	pos += hdr->ramdisk_size + hdr->page_size-1;
	pos &= ~(hdr->page_size-1L);

	if(hdr->second_size) {
		assert( (pos+hdr->second_size) <= size);
		dump(base+pos, hdr->second_size, "second");
		pos += hdr->second_size + hdr->page_size-1;
		pos &= ~(hdr->page_size-1L);
	}

	//This is non-standard, so we triple check
	if( hdr->unused[0] &&
			pos < size &&
			(pos+hdr->unused[0]) <= size) {

		if(memcmp(base+pos, "QCDT", 4) == 0 ||
				memcmp(base+pos, "SPRD", 4) == 0) {
			dump(base+pos, hdr->unused[0], "dt");
			pos += hdr->unused[0] + hdr->page_size-1;
			pos &= ~(hdr->page_size-1L);
		}
	}

	//Ensure we parsed the whole file
	printf("%ld => %ld\n", pos, size);
	assert(pos >= size);

	munmap(orig, size);
	close(fd);
}
