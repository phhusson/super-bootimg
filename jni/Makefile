CC=clang
CFLAGS=-Wall -std=gnu11
LDFLAGS=-static

TARGETS=extract repack sepolicy-inject/sepolicy-inject strip-cpio

all: $(TARGETS)

install: $(TARGETS)
	mkdir -p ../scripts/bin/
	cp extract ../scripts/bin/bootimg-extract
	cp repack ../scripts/bin/bootimg-repack
	cp sepolicy-inject/sepolicy-inject ../scripts/bin/sepolicy-inject
	cp strip-cpio ../scripts/bin/strip-cpio

sepolicy-inject/sepolicy-inject:
	make -C sepolicy-inject

clean:
	rm -f *.o $(TARGETS)
