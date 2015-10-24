CC=clang
CFLAGS=-Wall -std=gnu11

TARGETS=extract repack sepolicy-inject/sepolicy-inject

all: $(TARGETS)

sepolicy-inject/sepolicy-inject:
	make -C sepolicy-inject

clean:
	rm -f *.o $(TARGETS)
