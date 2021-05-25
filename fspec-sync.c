#define _XOPEN_SOURCE 700 /* for memccpy */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <blake3.h>
#include "common.h"

static char *argv0;
static char path[PATH_MAX];
static size_t baselen, pathlen;
static struct dir *dir;
static int dflag, fetchdir = AT_FDCWD;

struct dir {
	struct dirent **ent;
	size_t pos, len, pathlen;
	struct dir *next;
};

struct fetcher {
	pid_t pid;
	int rfd, wfd;
};

static void
usage(void)
{
	fprintf(stderr, "usage: %s [-d] rootdir [fspecfile]\n", argv0);
	exit(1);
}

static int
pathcmp(const char *p1, const char *p2, const char **end)
{
	char c1, c2;

	for (; *p1 == *p2 && *p1; ++p1, ++p2)
		;
	if (end)
		*end = p2;
	c1 = *p1, c2 = *p2;
	return c1 && c2 ? (c1 == '/' ? 0 : c1) - (c2 == '/' ? 0 : c2) : !c2 - !c1;
}

static int
dirnext(void)
{
	char *end;
	struct dir *d;

	while (dir && dir->pos == dir->len) {
		d = dir;
		dir = d->next;
		free(d->ent);
		free(d);
	}
	if (!dir)
		return 0;
	pathlen = dir->pathlen;
	path[pathlen] = '/';
	end = memccpy(path + pathlen + 1, dir->ent[dir->pos]->d_name, '\0', sizeof(path) - (pathlen + 1));
	if (!end)
		fatal("path is too long");
	pathlen = end - 1 - path;
	path[pathlen] = '\0';
	return 1;
}

static int
sel(const struct dirent *d)
{
	const char *n = d->d_name;

	return n[0] != '.' || (n[1] && (n[1] != '.' || n[2]));
}

static int
cmp(const struct dirent **d1, const struct dirent **d2)
{
	return strcmp((*d1)->d_name, (*d2)->d_name);
}

static void
dirpush(void)
{
	struct dir *d;
	int ret;

	d = malloc(sizeof(*d));
	if (!d)
		fatal(NULL);
	ret = scandir(path, &d->ent, sel, cmp);
	if (ret < 0)
		fatal("scandir %s:", path);
	d->pos = 0;
	d->len = ret;
	d->next = dir;
	dir = d;

	d->pathlen = pathlen;
}

static int
hexval(int c)
{
	if ('0' <= c && c <= '9')
		return c - '0';
	switch (c) {
	case 'a': case 'A': return 10;
	case 'b': case 'B': return 11;
	case 'c': case 'C': return 12;
	case 'd': case 'D': return 13;
	case 'e': case 'E': return 14;
	case 'f': case 'F': return 15;
	}
	return -1;
}

static int
hexdec(unsigned char *dst, const char *src, size_t len)
{
	int x1, x2;

	for (; len > 0; --len) {
		x1 = hexval(*src++);
		x2 = hexval(*src++);
		if (x1 == -1 || x2 == -1)
			return -1;
		*dst++ = x1 << 4 | x2;
	}
	return 0;
}

static void
randname(char *template)
{
	int i;
	struct timespec ts;
	unsigned long f;

	clock_gettime(CLOCK_REALTIME, &ts);
	f = ts.tv_nsec * 0x10001 ^ ((uintptr_t)&ts / 16 + (uintptr_t)template);
	for (i = 0; i < 6; ++i, f >>= 5)
		template[i] = 'A' + (f & 15) + (f & 16) * 2;
}

static off_t
fetch(char *tmp, size_t tmplen, const char *src, unsigned char hash[static BLAKE3_OUT_LEN])
{
	blake3_hasher ctx;
	char buf[8192], *pos;
	int srcfd, dstfd;
	size_t len;
	ssize_t ret;
	off_t size;

	/* TODO: support fetch over HTTP */

	if (tmplen < baselen + 9)
		fatal("path is too long");
	memcpy(tmp, path, baselen);
	strcpy(tmp + baselen, "/.XXXXXX");
	dstfd = mkstemp(tmp);
	if (dstfd < 0)
		fatal("mkstemp:");
	srcfd = openat(fetchdir, src, O_RDONLY);
	if (srcfd < 0)
		fatal("open %s:", src);
	blake3_hasher_init(&ctx);
	size = 0;
	while ((ret = read(srcfd, buf, sizeof(buf))) > 0) {
		size += ret;
		blake3_hasher_update(&ctx, buf, ret);
		for (len = ret, pos = buf; len > 0; len -= ret, pos += ret) {
			ret = write(dstfd, buf, len);
			if (ret <= 0)
				fatal("write %s:", tmp);
		}
	}
	close(srcfd);
	close(dstfd);
	blake3_hasher_finalize(&ctx, hash, BLAKE3_OUT_LEN);
	return size;
}

static void
infostring(char buf[static 19], size_t len, mode_t mode, off_t size)
{
	char *pos;

	memset(buf, '-', 10);
	switch (mode & S_IFMT) {
	case S_IFREG: buf[0] = '-'; break;
	case S_IFDIR: buf[0] = 'd'; break;
	case S_IFLNK: buf[0] = 'l'; break;
	case S_IFCHR: buf[0] = 'c'; break;
	case S_IFBLK: buf[0] = 'b'; break;
	case S_IFSOCK: buf[0] = 's'; break;
	}
	if (mode & S_IRUSR) buf[1] = 'r';
	if (mode & S_IWUSR) buf[2] = 'w';
	if (mode & S_IXUSR) buf[3] = 'x';
	if (mode & S_IRGRP) buf[4] = 'r';
	if (mode & S_IWGRP) buf[5] = 'w';
	if (mode & S_IXGRP) buf[6] = 'x';
	if (mode & S_IROTH) buf[7] = 'r';
	if (mode & S_IWOTH) buf[8] = 'w';
	if (mode & S_IXOTH) buf[9] = 'x';
	if (mode & S_ISUID) buf[3] = buf[3] == 'x' ? 's' : 'S';
	if (mode & S_ISGID) buf[6] = buf[6] == 'x' ? 's' : 'S';
	if (mode & S_ISVTX) buf[9] = buf[9] == 'x' ? 't' : 'T';
	buf[10] = '\0';
	if (S_ISREG(mode)) {
		off_t s, r = 0, x;
		char unit[] = "\0KMGT", *u = unit;
		int i;

		for (s = size; s > 1024 && u[1]; r = s, s /= 1024, ++u)
			;
		for (i = 0, x = s; x; x /= 10, ++i)
			;
		if (i > 4)
			return;
		buf[10] = ' ';
		pos = buf + 19;
		*--pos = '\0';
		if (*u) {
			*--pos = *u;
			r = (r % 1024) * 1000 / 1024;
			while (r > 10)
				r /= 10;
			*--pos = '0' + r % 10;
			*--pos = '.';
		}
		do *--pos = '0' + s % 10, s /= 10;
		while (s);
		while (pos > buf + 10)
			*--pos = ' ';
	}
}

static void delete(void);

static void
deleteunder(void)
{
	DIR *dir;
	struct dirent *d;
	char *end;
	size_t oldlen;

	dir = opendir(path);
	if (!dir)
		fatal("opendir %s:", path);
	oldlen = pathlen;
	path[pathlen++] = '/';
	while (errno = 0, (d = readdir(dir))) {
		if (!sel(d))
			continue;
		end = memccpy(path + oldlen + 1, d->d_name, '\0', sizeof(path) - oldlen - 1);
		if (!end)
			fatal("path is too large");
		pathlen = end - 1 - path;
		delete();
	}
	if (errno)
		fatal("readdir %s:", path);
	pathlen = oldlen;
	path[pathlen] = '\0';
}

static void
delete(void)
{
	char info[19];
	struct stat st;

	if (lstat(path, &st) != 0)
		fatal("stat %s:", path);
	if (S_ISDIR(st.st_mode))
		deleteunder();
	infostring(info, sizeof(info), st.st_mode, st.st_size);
	printf("%-48s %-18s → delete\n", path + baselen, info);
	if (!dflag && unlinkat(AT_FDCWD, path, S_ISDIR(st.st_mode) ? AT_REMOVEDIR : 0) < 0)
		fatal("remove %s:", path);
}

static void
checkpath(const char *p1, const char *p2)
{
	const char *end;

	if (pathcmp(p1, p2, &end) >= 0)
		fatal("not sorted at %s", p2);
	end = strchr(end + 1, '/');
	if (end)
		fatal("missing directory %.*s", end - p2, p2);
}

static void
fspec(char *pos, size_t len)
{
	char tmp[PATH_MAX], old[19], new[19], *end;
	const char *name, *source, *target = NULL;
	unsigned char remotehash[BLAKE3_OUT_LEN], localhash[BLAKE3_OUT_LEN];
	mode_t mode = 0;
	off_t size = 0;
	struct stat st;
	int ret, replace;

	/* name */
	name = pos;
	end = memchr(pos, '\n', len);
	*end = 0;
	assert(name[0] == '/');
	source = name + 1;
	len -= end + 1 - pos;
	pos = end + 1;

	checkpath(path + baselen, name);

	/* delete files not present in manifest */
	ret = 1;
	while (dirnext()) {
		ret = pathcmp(path + baselen, name, NULL);
		if (ret <= 0)
			++dir->pos;
		if (ret >= 0)
			break;
		delete();
	}

	while (len > 0) {
		end = memchr(pos, '\n', len);
		assert(end);
		*end = 0;
		len -= end + 1 - pos;
		if (strncmp(pos, "type=", 5) == 0) {
			pos += 5;
			if (strcmp(pos, "reg") == 0)
				mode = (mode & ~S_IFMT) | S_IFREG;
			else if (strcmp(pos, "sym") == 0)
				mode = (mode & ~S_IFMT) | S_IFLNK;
			else if (strcmp(pos, "dir") == 0)
				mode = (mode & ~S_IFMT) | S_IFDIR;
			else
				fatal("file '%s' has unsupported type '%s'", name, pos);
		} else if (strncmp(pos, "mode=", 5) == 0) {
			pos += 5;
			mode = (mode & S_IFMT) | strtoul(pos, &end, 8);
			if (*end)
				fatal("file '%s' has unsupported mode '%s'", name, pos);
		} else if (strncmp(pos, "size=", 5) == 0) {
			pos += 5;
			size = strtoull(pos, &end, 10);
			if (*end)
				fatal("file '%s' has unsupported size '%s'", name, pos);
		} else if (strncmp(pos, "source=", 7) == 0) {
			pos += 7;
			source = pos;
		} else if (strncmp(pos, "target=", 7) == 0) {
			pos += 7;
			target = pos;
		} else if (strncmp(pos, "blake3=", 7) == 0) {
			pos += 7;
			if (end - pos != sizeof(remotehash) * 2 || hexdec(remotehash, pos, sizeof(remotehash)) != 0)
				fatal("file '%s' has invalid blake3 attribute", name);
		}
		pos = end + 1;
	}

	if (strcmp(name, "/") != 0) {
		end = memccpy(path + baselen, name, '\0', sizeof(path) - baselen);
		if (!end)
			fatal("path is too long");
		pathlen = end - 1 - path;
	}

	if (lstat(path, &st) == 0) {
		if (S_ISDIR(st.st_mode) && !S_ISDIR(mode))
			deleteunder();
		infostring(old, sizeof(old), st.st_mode, st.st_size);
	} else if (errno == ENOENT) {
		old[0] = '\0';
		st.st_mode = 0;
	} else {
		fatal("lstat %s:", name);
	}

	replace = 0;
	switch (mode & S_IFMT) {
	case S_IFREG:
		replace = 1;
		if (S_ISREG(st.st_mode)) {
			blake3_hasher ctx;
			char buf[16384];
			ssize_t ret;
			int fd;

			fd = open(path, O_RDONLY);
			if (fd < 0)
				fatal("open %s:", path);
			blake3_hasher_init(&ctx);
			while ((ret = read(fd, buf, sizeof(buf))) > 0)
				blake3_hasher_update(&ctx, buf, ret);
			close(fd);
			if (ret < 0)
				fatal("read %s:", path);
			blake3_hasher_finalize(&ctx, localhash, sizeof(localhash));
			if (memcmp(localhash, remotehash, sizeof(localhash)) == 0) {
				replace = 0;
				size = st.st_size;
			}
		}
		if (replace && !dflag) {
			size = fetch(tmp, sizeof(tmp), source, localhash);
			if (memcmp(localhash, remotehash, sizeof(localhash)) != 0)
				fatal("file '%s' has incorrect hash", name);
			if (chmod(tmp, mode & ~S_IFMT) != 0)
				fatal("chmod %s:", path);
		}
		break;
	case S_IFDIR:
		replace = !S_ISDIR(st.st_mode);
		break;
	case S_IFLNK:
		replace = 1;
		if (S_ISLNK(st.st_mode)) {
			char localtarget[PATH_MAX];
			ssize_t ret;

			ret = readlink(path, localtarget, sizeof(localtarget));
			if (ret > 0 && ret < sizeof(localtarget)) {
				localtarget[ret] = '\0';
				replace = strcmp(localtarget, target) != 0;
			}
		}
		if (replace && !dflag) {
			int retry;

			ret = snprintf(tmp, sizeof(tmp), "%.*s/.XXXXXX", (int)baselen, path);
			for (retry = 20; retry > 0; --retry) {
				randname(tmp + (ret - 6));
				if (symlink(target, tmp) == 0)
					break;
				if (errno != EEXIST)
					fatal("symlink %s:", tmp);
			}
			if (retry == 0)
				fatal("could not find temporary name");
		}
		break;
	default:
		fatal("file '%s' is missing type");
	}
	if (replace || (!S_ISLNK(mode) && mode != st.st_mode)) {
		infostring(new, sizeof(new), mode, size);
		if (old[0])
			printf("%-48s %-18s → %s\n", name, old, new);
		else
			printf("%-69s %s\n", name, new);
	}
	if (!dflag) {
		if (replace) {
			if (S_ISDIR(mode)) {
				if (st.st_mode && !S_ISDIR(st.st_mode) && unlink(path) != 0)
					fatal("unlink %s:", path);
				if (mkdir(path, mode & ~S_IFMT) != 0)
					fatal("mkdir %s:", path);
			} else {
				if (S_ISDIR(st.st_mode) && rmdir(path) != 0)
					fatal("rmdir %s:", path);
				if (rename(tmp, path) != 0)
					fatal("rename:");
			}
		} else if (!S_ISLNK(mode) && mode != st.st_mode) {
			if (chmod(path, mode & ~S_IFMT) != 0)
				fatal("chmod %s:", path);
		}
	}
	if (S_ISDIR(mode))
		dirpush();
}

int
main(int argc, char *argv[])
{
	char *end;

	argv0 = argc ? argv[0] : "fspec-sync";
	ARGBEGIN {
	case 'd':
		dflag = 1;
		break;
	} ARGEND
	if (argc == 2) {
		if (!freopen(argv[1], "r", stdin))
			fatal("open %s:", argv[1]);
		end = strrchr(argv[1], '/');
		if (end) {
			end[1] = '\0';
			fetchdir = open(argv[1], O_DIRECTORY | O_PATH | O_CLOEXEC);
			if (fetchdir < 0)
				fatal("open %s:", argv[1]);
		}
	} else if (argc != 1) {
		usage();
	}

	umask(0);
	end = memccpy(path, argv[0], '\0', sizeof(path));
	if (!end)
		fatal("path is too long");
	pathlen = baselen = end - 1 - path;

	parse(stdin, fspec);
	while (dirnext()) {
		delete();
		++dir->pos;
	}
}
