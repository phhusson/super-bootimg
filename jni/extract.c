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

#include <libbootimg.h>

static void dump(uint8_t *ptr, size_t size, const char* filename) {
	unlink(filename);
	int ofd = open(filename, O_WRONLY|O_CREAT, 0644);
	assert(ofd >= 0);
	int ret = write(ofd, ptr, size);
	assert(ret == size);
	close(ofd);
}

static int do_stuff(int flags, uint8_t *base, long size) {
	switch( flags&BOOT_TYPE ) {
		case BOOT_HEADER: {
			if(flags&BOOT_CHROMEOS)
					dump(base, 0, "chromeos");
			if(flags&BOOT_UNKNOWN_SIGN)
					dump(base, 0, "secure");
			if(flags&BOOT_RK)
					dump(base, 0, "rkcrc");
			break;
		}
		case BOOT_KERNEL: {
			dump(base, size, "kernel");
		}
		case BOOT_RAMDISK: {
		   const char *filename = "ramdisk";
		   if(flags&BOOT_GZIP)
			   filename = "ramdisk.gz";
		   dump(base, size, filename);
		   if(flags&BOOT_MTK)
			   dump(base, 0, "ramdisk-mtk");
		   break;
		}
		case BOOT_SECOND:
			dump(base, size, "second");
			break;
		case BOOT_QCDT:
			dump(base, size, "dt");
			break;
	}
	return 0;
}

int main(int argc, char **argv) {
	assert(argc == 2);

	int v = bootimg_parse(argv[1], do_stuff);

	return v;
}
