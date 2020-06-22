#include <stddef.h>
#include <stdio.h>

#define ARGBEGIN \
	for (;;) { \
		if (argc > 0) \
			++argv, --argc; \
		if (argc == 0 || (*argv)[0] != '-') \
			break; \
		if ((*argv)[1] == '-' && !(*argv)[2]) { \
			++argv, --argc; \
			break; \
		} \
		for (char *opt_ = &(*argv)[1], done_ = 0; !done_ && *opt_; ++opt_) { \
			switch (*opt_)

#define ARGEND \
		} \
	}

#define EARGF(x) \
	(done_ = 1, *++opt_ ? opt_ : argv[1] ? --argc, *++argv : ((x), abort(), (char *)0))

/* fatal.c */
void fatal(const char *, ...);

/* reallocarray.c */
void *reallocarray(void *, size_t, size_t);

/* parse.c */
void parse(FILE *, void (*)(char *, size_t));