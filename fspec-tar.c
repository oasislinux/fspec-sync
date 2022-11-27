#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"

enum {
	REGTYPE = '0',
	SYMTYPE = '2',
	DIRTYPE = '5',
};

static char *argv0;

static void
usage(void)
{
	fprintf(stderr, "usage: %s\n", argv0);
	exit(1);
}

static unsigned long
decnum(const char *s, size_t l, int *err)
{
	unsigned long n;

	n = 0;
	for (; l > 0; --l, ++s) {
		if (*s < '0' || *s > '9') {
			if (err)
				*err = 1;
			return 0;
		}
		n = n * 10 + (*s - '0');
	}
	if (err)
		*err = 0;
	return n;
}

static int
isodigit(int c)
{
	return '0' <= c && c <= '7';
}

static void
fspec(char *pos, size_t reclen)
{
	const char *name, *mode = NULL, *source = NULL;
	char hdr[512] = {0};
	char *end;
	size_t len, i;
	int ret;
	unsigned long chksum;
	long size;
	FILE *f = NULL;

	memset(hdr + 108, '0', 7);     /* uid */
	memset(hdr + 116, '0', 7);     /* gid */
	memset(hdr + 124, '0', 11);    /* size */
	memset(hdr + 136, '0', 11);    /* mtime */
	memset(hdr + 148, ' ', 8);     /* chksum */
	memcpy(hdr + 257, "ustar", 6); /* magic */
	memcpy(hdr + 263, "00", 2);    /* version */

	/* name, prefix */
	name = pos;
	end = memchr(pos, '\n', reclen);
	assert(end);
	*end = 0;
	len = end - pos;
	reclen -= len + 1;
	if (len > 100) {
		i = len < 155 ? len : 155;
		memcpy(hdr + 345, pos, i);
		while (i > 0 && hdr[345 + --i] != '/')
			hdr[345 + i] = 0;
		hdr[345 + i] = 0;
	} else {
		i = 0;
	}
	if (len > 100)
		fatal("path is too long");
	if (i > 0)
		++i;
	memcpy(hdr + 0, pos + i, len - i);
	pos = end + 1;

	for (; reclen > 0; pos = end + 1) {
		end = memchr(pos, '\n', reclen);
		assert(end);
		*end = 0;
		len = end - pos;
		reclen -= len + 1;

		if (len >= 5 && memcmp(pos, "type=", 5) == 0) {
			pos += 5, len -= 5;
			if (len == 3 && memcmp(pos, "reg", 3) == 0)
				hdr[156] = REGTYPE;
			else if (len == 3 && memcmp(pos, "sym", 3) == 0)
				hdr[156] = SYMTYPE;
			else if (len == 3 && memcmp(pos, "dir", 3) == 0)
				hdr[156] = DIRTYPE;
			else
				fatal("file '%s' has unsupported type '%s'", name, pos);
		} else if (len >= 5 && memcmp(pos, "mode=", 5) == 0) {
			pos += 5, len -= 5;
			if (len != 4 || !isodigit(pos[0]) || !isodigit(pos[1]) || !isodigit(pos[2]) || !isodigit(pos[3]))
				fatal("file '%s' has invalid mode '%s'", name, pos);
			mode = pos;
		} else if (len >= 7 && memcmp(pos, "source=", 7) == 0) {
			source = pos + 7;
		} else if (len >= 7 && memcmp(pos, "target=", 7) == 0) {
			pos += 7, len -= 7;
			if (len > 100)
				fatal("symlink '%s' target is too long", name);
			memcpy(hdr + 157, pos, len);
		} else if (len >= 4 && (memcmp(pos, "uid=", 4) == 0 || memcmp(pos, "gid=", 4) == 0)) {
			const char *key;
			unsigned long id;
			int err;

			key = pos;
			pos[3] = 0;
			pos += 4, len -= 4;
			id = decnum(pos, len, &err);
			if (err)
				fatal("file '%s' has invalid %s '%s'", name, key, pos);
			ret = snprintf(hdr + (key[0] == 'u' ? 108 : 116), 8, "%07lo", id);
			if (ret < 0 || ret >= 8)
				fatal("file '%s' uid is too large");
		}
	}
	if (!hdr[156])
		fatal("file '%s' is missing 'type' attribute", name);
	if (!mode) {
		switch (hdr[156]) {
		case REGTYPE: mode = "0644"; break;
		case SYMTYPE: mode = "0777"; break;
		case DIRTYPE: mode = "0755"; break;
		}
	}

	/* mode */
	memset(hdr + 100, '0', 3);
	memcpy(hdr + 100 + 3, mode, 4);

	if (!source)
		source = name + 1;
	if (hdr[156] == REGTYPE) {
		f = fopen(source, "rb");
		if (!f)
			fatal("open %s:", source);
		if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) == -1 || fseek(f, 0, SEEK_SET) != 0)
			fatal("seek:");
		ret = snprintf(hdr + 124, 12, "%011lo", size);
		if (ret < 0 || ret >= 12)
			fatal("file '%s' is too large", name);
	}

	chksum = 0;
	for (i = 0; i < sizeof(hdr); ++i)
		chksum += (unsigned char)hdr[i];
	snprintf(hdr + 148, 8, "%07lo", chksum);
	if (fwrite(hdr, 1, sizeof(hdr), stdout) != sizeof(hdr))
		fatal("write:");

	if (f) {
		char buf[16384];

		do {
			len = fread(buf, 1, sizeof(buf), f);
			if (len != sizeof(buf) && ferror(f))
				fatal("read %s:", source);
			if (len > size)
				break;
			if (len > 0 && fwrite(buf, 1, len, stdout) != len)
				fatal("write:");
			size -= len;
		} while (!feof(f));
		if (size > 0)
			fatal("file '%s' changed size when reading %lu", source, size);
		len = -len & 511;
		memset(hdr, 0, len);
		if (fwrite(buf, 1, len, stdout) != len)
			fatal("write:");
		fclose(f);
	}
}

int
main(int argc, char *argv[])
{
	char buf[1024] = {0};

	argv0 = argc ? argv[0] : "fspec-tar";
	if (argc)
		++argv, --argc;
	if (argc)
		usage();

	parse(stdin, fspec);
	fwrite(buf, 1, sizeof(buf), stdout);
	fflush(stdout);
	if (ferror(stdout))
		fatal("write:");
}
