#define _FILE_OFFSET_BITS 64
#define _XOPEN_SOURCE 500
#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#define BITS_PHYS_MAX 256
static char  *bits_filename   = "bits_file";
static char  *bits_physdir    = ".";
static size_t bits_blocksize  = 1024 * 32;
static size_t bits_blockcount = 1024 * 10;
static off_t  bits_totalsize;
static int    bits_dirmode    = 0750;
static int    bits_filemode   = 0660;

/* static char  bits_pathbuf[3 * sizeof(off_t) + 1]; */

static int bits_isroot(const char *path) {
	return (strcmp(path, "/") == 0);
}

static int bits_isfile(const char *path) {
	return ((path[0] == '/') && (strcmp(path + 1, bits_filename) == 0));
}

/* path has to be of size BITS_PHYS_MAX. */
static int bits_otop(off_t offset, const char *path, int create) {
	char *pathpos = path;
	strcpy(pathpos, bits_physdir);
	pathpos += strlen(bits_physdir);
	for (int i = 0; i < sizeof(off_t); i++) {
		sprintf(pathpos, "/%02x",
		        ((offset >> (8 * (sizeof(off_t) - i - 1))) & 0xff));
		pathpos += 3;
		if (create && i < sizeof(off_t) - 1) {
			if (mkdir(path, bits_dirmode) < 0 && errno != EEXIST)
				return (-errno);
		}
	}
	return (0);
}

static int bits_getattr(const char *path, struct stat *stbuf) {
	/* Wipe buffer. */
	memset(stbuf, 0, sizeof(struct stat));
	if (bits_isroot(path)) {
		stbuf->st_mode = S_IFDIR | bits_dirmode;
		stbuf->st_nlink = 2;
	} else if (bits_isfile(path)) {
		stbuf->st_mode = S_IFREG | bits_filemode;
		stbuf->st_nlink = 1;
		stbuf->st_size = bits_totalsize;
	} else {
		return (-ENOENT);
	}
	return (0);
}

static int bits_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                        off_t offset, struct fuse_file_info *fi) {
	/* The only supported path is the root directory. */
	if (!bits_isroot(path))
		return (-ENOENT);
	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);
	filler(buf, bits_filename, NULL, 0);
	return (0);
}

static int bits_open(const char *path, struct fuse_file_info *fi) {
	if (bits_isroot(path))
		return (-EISDIR);
	if (!bits_isfile(path))
		return (-ENOENT);
	/* FIXME: Check permissions. */
	return (0);
}

static int bits_read(const char *path, char *buf, size_t size, off_t offset,
                     struct fuse_file_info *fi) {
	char *bufpos = buf;
	int counter = 0;
	/* The only valid file is bits_file. */
	if (!bits_isfile(path))
		return (-ENOENT);
	/* If offset is outside our filesystem size, return nothing (EOF). */
	if (offset >= bits_totalsize)
		return (0);
	/* If needed, reduce the size down to our filesystem end. */
	/* FIXME: What if offset + size overflows? */
	if (offset + size > bits_totalsize)
		size = bits_totalsize - offset;
	/* If the offset does not start at the beginning of a small, we have to
	   skip some data at the first read. */
	size_t skip = offset % bits_blocksize;
	/* Find the beginning of the correct small. */
	off_t small = offset - skip;
	/* Read the individual smalls. */
	while (size > 0) {
		/* The number of bytes to read is min(bs-skip,size). */
		size_t readhere = ((size > bits_blocksize - skip) ?
		                   (bits_blocksize - skip) : (size));
		/* Generate the small file's name and open it. */
		const char phys[BITS_PHYS_MAX];
		bits_otop(small, phys, 0);
		int fd = open(phys, O_RDONLY);
		#ifdef BITS_DEBUG
		printf("I'm in ur %s, reading from %d...\n", phys, fd);
		#endif
		if (fd < 0) {
			/* We don't require all files to exist, non-existing
			   ones will act as if there were zeroes inside.
			   However, if any other error than ENOENT occurs, pass
			   it on. */
			if (errno != ENOENT)
				return (-errno);
			/* Fill the buffer with the correct number of zeroes. */
			memset(bufpos, 0, readhere);
			/* Advance bufpos. */
			bufpos += readhere;
		} else {
			/* If we have to skip, skip. */
			if (skip > 0) {
				if (lseek(fd, skip, SEEK_SET) == (off_t) -1)
					return (-errno);
			}
			/* Usually this loop should run only once. */
			int reallyread = 0;
			while (reallyread < readhere) {
				/* Read into the buffer. */
				int r = read(fd, bufpos, readhere - reallyread);
				if (r == 0) {
					/* We read nothing, probably EOF.
					   Fill up with zeroes. */
					r = readhere - reallyread;
					memset(bufpos, 0, r);
				} else if (r < 0) {
					/* FIXME: What happens on EINTR? */
					return (-errno);
				}
				/* Advance counter and bufpos. */
				reallyread += r;
				bufpos += r;
			}
			#ifdef BITS_DEBUG
			printf("close(%d): %d\n", fd, close(fd));
			#else
			close(fd);
			#endif
		}
		/* Probably read the next small. */
		small   += bits_blocksize;
		/* Decrease size by number of bytes read, increase counter. */
		size    -= readhere;
		counter += readhere;
		/* No skips after the first read. */
		skip = 0;
	}
	/* Since we loop until size bytes have been read, we can assume: */
	return (counter);
}

static int bits_write(const char *path, const char *buf, size_t size, off_t offset,
                     struct fuse_file_info *fi) {
	char *bufpos = (char *) buf;
	int counter = 0;
	/* The only valid file is bits_file. */
	if (!bits_isfile(path))
		return (-EACCES);
	/* If offset is outside our filesystem size or size is longer than
	   possible, return "no space left on device". */
	/* FIXME: What if offset + size overflows? */
	if ((offset >= bits_totalsize) || (offset + size > bits_totalsize))
		return (-ENOSPC);
	/* If the offset does not start at the beginning of a small, we have to
	   skip some data at the first write. */
	size_t skip = offset % bits_blocksize;
	/* Find the beginning of the correct small. */
	off_t small = offset - skip;
	/* Write the individual smalls. */
	while (size > 0) {
		/* The number of bytes to write is min(bs-skip,size). */
		size_t writehere = ((size > bits_blocksize - skip) ?
		                    (bits_blocksize - skip) : (size));
		/* Generate the small file's name and open it. */
		const char phys[BITS_PHYS_MAX];
		int r = bits_otop(small, phys, 1);
		if (r < 0)
			return (r);
		int fd = open(phys, O_WRONLY | O_CREAT);
		#ifdef BITS_DEBUG
		printf("I'm in ur %s, writing to %d...\n", phys, fd);
		#endif
		if (fd < 0) {
			/* If this went wrong, abort. */
			return (-errno);
		}
		/* If we have to skip, skip. */
		if (skip > 0) {
			if (lseek(fd, skip, SEEK_SET) == (off_t) -1)
				return (-errno);
		}
		/* Usually this loop should run only once. */
		int reallywritten = 0;
		while (reallywritten < writehere) {
			/* Write the buffer. */
			int r = write(fd, bufpos, writehere);
			/* FIXME: What should happen on EINTR? */
			if (r < 0)
				return (-errno);
			/* Advance counter and bufpos. */
			reallywritten += r;
			bufpos += r;
		}
		#ifdef BITS_DEBUG
		printf("close(%d): %d\n", fd, close(fd));
		#else
		close(fd);
		#endif
		/* Probably write the next small. */
		small   += bits_blocksize;
		/* Decrease size by number of bytes written, increase counter. */
		size    -= writehere;
		counter += writehere;
		/* No skips after the first write. */
		skip = 0;
	}
	/* Since we loop until size bytes have been written, we can assume: */
	return (counter);
}

static int bits_chmod(const char *path, mode_t mode) {
	if (bits_isroot(path)) {
		bits_dirmode = mode;
	} else if (bits_isfile(path)) {
		bits_filemode = mode;
	} else {
		return (-ENOENT);
	}
	return (0);
}

static struct fuse_operations bits_oper = {
	.getattr = bits_getattr,
	.readdir = bits_readdir,
	.chmod   = bits_chmod,
	.open    = bits_open,
	.read    = bits_read,
	.write   = bits_write,
};

int main(int argc, char *argv[]) {
	if (strlen(bits_physdir) + 3 * sizeof(off_t) >= BITS_PHYS_MAX) {
		printf("physdir may not be larger than %lud\n",
		       BITS_PHYS_MAX - 3 * sizeof(off_t) - 1);
		return (EXIT_FAILURE);
	}
	bits_totalsize = bits_blockcount * bits_blocksize;
	return fuse_main(argc, argv, &bits_oper, NULL);
}
