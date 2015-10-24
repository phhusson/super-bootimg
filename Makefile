CC=clang
CFLAGS=-Wall -std=gnu11

TARGETS=extract

all: $(TARGETS)

clean:
	rm -f *.o $(TARGETS)
