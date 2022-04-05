#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <blake3.h>
#include "common.h"

static char *argv0;

static void
usage(void)
{
	fprintf(stderr, "usage: %s\n", argv0);
	exit(1);
}

static void
fspec(char *pos, size_t len)
{
	char *source, *end;
	int reg = 0, hash = 1;

	end = memchr(pos, '\n', len);
	assert(end);
	if (fwrite(pos, 1, end - pos + 1, stdout) != end - pos + 1)
		fatal("write:");
	*end = 0;
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
			hash = 0;
		}
		len -= end + 1 - pos;
		pos = end + 1;
	}
	if (reg && hash) {
		FILE *file;
		blake3_hasher ctx;
		char buf[16384];
		size_t len;
		unsigned char out[BLAKE3_OUT_LEN];

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
		blake3_hasher_finalize(&ctx, out, sizeof(out));
		fclose(file);
		fputs("blake3=", stdout);
		for (size_t i = 0; i < sizeof(out); ++i)
			printf("%02x", out[i]);
		fputc('\n', stdout);
	}
	fputc('\n', stdout);
}

int
main(int argc, char *argv[])
{
	argv0 = argc ? argv[0] : "fspec-b3sum";
	if (argc)
		++argv, --argc;
	if (argc)
		usage();

	parse(stdin, fspec);
	fflush(stdout);
	if (ferror(stdout))
		fatal("write:");
}
