.POSIX:

BLAKE3_LDLIBS=-l blake3

-include config.mk

CFLAGS+=-Wall -Wpedantic

COMMON_OBJ=fatal.o parse.o reallocarray.o

.PHONY: all
all: fspec-hash fspec-sort fspec-sync fspec-tar

$(COMMON_OBJ) fspec-hash.o fspec-sort.o fspec-tar.o: common.h

libcommon.a: $(COMMON_OBJ)
	$(AR) $(ARFLAGS) $@ $(COMMON_OBJ)

fspec-hash: fspec-hash.o libcommon.a
	$(CC) $(LDFLAGS) -o $@ fspec-hash.o libcommon.a $(BLAKE3_LDLIBS)

fspec-sort: fspec-sort.o libcommon.a
	$(CC) $(LDFLAGS) -o $@ fspec-sort.o libcommon.a

fspec-sync: fspec-sync.o libcommon.a
	$(CC) $(LDFLAGS) -o $@ fspec-sync.o libcommon.a $(BLAKE3_LDLIBS)

fspec-tar: fspec-tar.o libcommon.a
	$(CC) $(LDFLAGS) -o $@ fspec-tar.o libcommon.a

.PHONY: clean
clean:
	rm -f\
		fspec-hash fspec-hash.o\
		fspec-sort fspec-sort.o\
		fspec-tar fspec-tar.o\
		libcommon.a $(COMMON_OBJ)
