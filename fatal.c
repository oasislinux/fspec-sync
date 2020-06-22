#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"

void
fatal(const char *fmt, ...)
{
	va_list ap;
	int err = 1;

	if (fmt) {
		va_start(ap, fmt);
		vfprintf(stderr, fmt, ap);
		va_end(ap);
		err = fmt[0] && fmt[strlen(fmt) - 1] == ':';
		if (err)
			fputc(' ', stderr);
	}
	if (err)
		perror(NULL);
	else
		fputc('\n', stderr);
	exit(1);
}
