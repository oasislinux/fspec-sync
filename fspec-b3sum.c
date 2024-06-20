#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <blake3.h>
#include "common.h"

static char *argv0;

int cflag = 0;

static void
usage(void)
{
	fprintf(stderr, "usage: %s\n", argv0);
	exit(1);
}

static void
fspec(char *pos, size_t len)
{
	char *path, *source, *end, *hashline;
	int reg = 0;

	end = memchr(pos, '\n', len);
	assert(end);
	if (fwrite(pos, 1, end - pos + 1, stdout) != end - pos + 1)
		fatal("write:");
	*end = 0;

	hashline = 0;
	path = pos;
	source = pos + 1;
	len -= end + 1 - pos;
	pos = end + 1;

	while (len > 0) {
		end = memchr(pos, '\n', len);
		assert(end);
		if (fwrite(pos, 1, end - pos + 1, stdout) != end - pos + 1)
			fatal("write:");
		*end = 0;
		if (len >= 5 && memcmp(pos, "type=", 5) == 0) {
			reg = strcmp(pos + 5, "reg") == 0;
		} else if (len >= 7 && memcmp(pos, "source=", 7) == 0) {
			source = pos + 7;
		} else if (len >= 7 && memcmp(pos, "blake3=", 7) == 0) {
			hashline = pos + 7;
		}
		len -= end + 1 - pos;
		pos = end + 1;
	}

	if (reg && (!hashline || cflag)) {
		FILE *file;
		blake3_hasher ctx;
		char buf[16384];
		char hexsum[BLAKE3_OUT_LEN*2+1];
		unsigned char sum[BLAKE3_OUT_LEN];
		size_t len;

		file = fopen(source, "rb");
		if (!file)
			fatal("open %s:", source);
		blake3_hasher_init(&ctx);
		do {
			len = fread(buf, 1, sizeof(buf), file);
			blake3_hasher_update(&ctx, buf, len);
		} while (len == sizeof(buf));
		if (ferror(file))
			fatal("read %s:", source);
		fclose(file);

		blake3_hasher_finalize(&ctx, sum, sizeof(sum));
		for (size_t i = 0; i < sizeof(sum); ++i) {
			static char *h = "0123456789abcdef";
			hexsum[i*2] = h[(sum[i]&0xf0)>>4];
			hexsum[i*2+1] = h[sum[i]&0x0f];
		}
		hexsum[sizeof(hexsum) - 1] = 0;
		if (hashline && cflag && strcmp(hashline, hexsum)) {
			fprintf(stderr, "b3sum check failed:\n");
			fprintf(stderr, "  path=%s\n", path);
			fprintf(stderr, "  source=%s\n", source);
			fprintf(stderr, "  expected=%s\n", hashline);
			fprintf(stderr, "  got=%s\n", hexsum);
			exit(1);
		}

		if (!hashline)
			fprintf(stdout, "blake3=%s\n", hexsum);
	}
	fputc('\n', stdout);
}

int
main(int argc, char *argv[])
{
	argv0 = argc ? argv[0] : "fspec-b3sum";

	ARGBEGIN {
	case 'c':
		cflag = 1;
		break;
	} ARGEND

	if (argc)
		++argv, --argc;
	if (argc)
		usage();

	parse(stdin, fspec);
	fflush(stdout);
	if (ferror(stdout))
		fatal("write:");
}
