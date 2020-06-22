#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"

static char *argv0;
static char **fs;
static size_t fslen;

static void
usage(void)
{
	fprintf(stderr, "usage: %s [-p] [fspec...]\n", argv0);
	exit(1);
}

static void
fspec(char *buf, size_t len)
{
	if ((fslen & (fslen - 1)) == 0) {
		fs = reallocarray(fs, fslen ? fslen * 2 : 1, sizeof(fs[0]));
		if (!fs) {
			perror(NULL);
			exit(1);
		}
	}
	fs[fslen] = malloc(len + 1);
	if (!fs[fslen]) {
		perror(NULL);
		exit(1);
	}
	memcpy(fs[fslen], buf, len);
	fs[fslen][len] = '\0';
	++fslen;
}

static int
cmp(const void *p1, const void *p2)
{
	const char *r1 = *(const char **)p1, *r2 = *(const char **)p2;

	for (; *r1 == *r2 && *r1 != '\n' && *r2 != '\n'; ++r1, ++r2)
		;
	if (*r1 == *r2)
		return 0;
	if (*r1 == '\n')
		return -1;
	if (*r2 == '\n')
		return 1;
	if (*r1 == '/')
		return -1;
	if (*r2 == '/')
		return 1;
	return *r1 - *r2;
}

int
main(int argc, char *argv[])
{
	int pflag = 0;
	FILE *file;

	argv0 = argc ? argv[0] : "fspec-sort";
	ARGBEGIN {
	case 'p':
		pflag = 1;
		break;
	default:
		usage();
	} ARGEND

	if (argc == 0) {
		parse(stdin, fspec);
	} else {
		for (; argc > 0; --argc, ++argv) {
			file = fopen(*argv, "r");
			if (!file)
				fatal("open %s:", *argv);
			parse(file, fspec);
			fclose(file);
		}
	}
	qsort(fs, fslen, sizeof(fs[0]), cmp);
	for (size_t i = 0; i < fslen; ++i) {
		char *s = fs[i], *p = s, *q;

		if (pflag) {
			if (i) {
				q = fs[i - 1];
				while (*p++ == *q++)
					;
			}
			for (; p[1] != '\n'; ++p) {
				if (*p == '/')
					printf("%.*s\ntype=dir\nmode=0755\n\n", (int)(p - s + (p == s)), s);
			}
		}
		puts(s);
	}
	fflush(stdout);
	if (ferror(stdout))
		fatal("write:");
}
