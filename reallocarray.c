#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include "common.h"

void *
reallocarray(void *p, size_t n, size_t m)
{
	if (n > 0 && SIZE_MAX / n < m) {
		errno = ENOMEM;
		return NULL;
	}
	return realloc(p, n * m);
}
