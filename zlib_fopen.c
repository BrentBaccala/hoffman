/* -*- mode: C; fill-column: 100; c-basic-offset: 4; -*-
 *
 * I wrote this wrapper code around zlib with the intention of using GNU glibc's ability to fopen a
 * 'cookie' of arbitrary functions and thus treat a compressed file the same any other FILE *.  Then
 * I ported to Windows using cygwin, which doesn't using glibc, so I've had to remove my cookie
 * dependencies, loosing a lot of the original motivation for creating this code.
 *
 * You'll notice that I define some 64-bit seek functions that I never really use.  Why?  Well, for
 * one thing, we have to seek all the back to the beginning to rewind a zlib compressed stream, so
 * the only kind of seek that we need to pass in to zlib_open is a 32-bit one.  Also, though I've
 * created functions to seek to 64-bit offsets in a compressed stream, that seems a little bit silly
 * when you think about the performance implications of doing that to a zlib stream.  In any event,
 * since indices are (currently) 32-bit in hoffman, only its very largest tablebases with two-byte
 * DTM format would have any chance of overflowing this seek code.  And if we do change to 64-bit
 * indices to support even bigger tablebases, then we'll probably have to think real hard about
 * replacing zlib with something that supports random access better.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <zlib.h>
#include <fcntl.h>

#define STRICT_ZLIB_OPEN_DECLARATION 1

#include "zlib_fopen.h"

struct cookie {
    void *	ptr;

    ssize_t (*read) (void *ptr, char *buffer, size_t size);
    ssize_t (*write) (void *ptr, const char *buffer, size_t size);
    off_t (*seek) (void *ptr, off_t position, int whence);
    int (*close) (void *ptr);

    char	operation;

    z_stream	zstream;
    void *	buffer;
    int		bufsize;
};

static ssize_t fread_wrapper(void *ptr, char *buffer, size_t size)
{
    return fread(buffer, 1, size, (FILE *) ptr);
}

static ssize_t fwrite_wrapper(void *ptr, const char *buffer, size_t size)
{
    return fwrite(buffer, 1, size, (FILE *) ptr);
}

int zlib_read_int(void *ptr, char *buffer, int size)
{
    return zlib_read(ptr, buffer, size);
}

ssize_t zlib_read(void *ptr, char *buffer, size_t size)
{
    struct cookie *cookie = ptr;
    int ret;

    cookie->zstream.next_out = (unsigned char *) buffer;
    cookie->zstream.avail_out = size;

    do {
	if (cookie->zstream.avail_in == 0) {
	    cookie->zstream.next_in = cookie->buffer;
	    cookie->zstream.avail_in = cookie->read(cookie->ptr, cookie->buffer, cookie->bufsize);
	    if (cookie->zstream.avail_in == 0) break;
	}
	if ((ret = inflate(&cookie->zstream, Z_NO_FLUSH)) != Z_OK) break;
    } while (cookie->zstream.avail_out > 0);

    return (size - cookie->zstream.avail_out);
}

ssize_t zlib_write(void *ptr, const char *buffer, size_t size)
{
    struct cookie *cookie = ptr;

    cookie->zstream.next_in = (unsigned char *) buffer;
    cookie->zstream.avail_in = size;
    cookie->zstream.next_out = cookie->buffer;
    cookie->zstream.avail_out = cookie->bufsize;

    do {
	if (deflate(&cookie->zstream, Z_NO_FLUSH) != Z_OK) break;

	if (cookie->zstream.avail_out < cookie->bufsize) {
	    cookie->write(cookie->ptr, cookie->buffer, cookie->bufsize - cookie->zstream.avail_out);
	    cookie->zstream.next_out = cookie->buffer;
	    cookie->zstream.avail_out = cookie->bufsize;
	}
    } while (cookie->zstream.avail_in > 0);

    return (size - cookie->zstream.avail_in);
}

int zlib_flush (void *ptr)
{
    struct cookie *cookie = ptr;
    int ret = Z_STREAM_END;

    if (cookie->operation != 'r') {

	while ((ret = deflate(&cookie->zstream, Z_FINISH)) == Z_OK) {

	    if (cookie->zstream.avail_out < cookie->bufsize) {
		cookie->write(cookie->ptr, cookie->buffer, cookie->bufsize - cookie->zstream.avail_out);
		cookie->zstream.next_out = cookie->buffer;
		cookie->zstream.avail_out = cookie->bufsize;
	    }
	}

	if (cookie->zstream.avail_out < cookie->bufsize) {
	    cookie->write(cookie->ptr, cookie->buffer, cookie->bufsize - cookie->zstream.avail_out);
	    cookie->zstream.next_out = cookie->buffer;
	    cookie->zstream.avail_out = cookie->bufsize;
	}
    }

    /* 0 is a good return; -1 is error */

    return (ret == Z_STREAM_END) ? 0 : -1;
}

void zlib_free (void *ptr)
{
    struct cookie *cookie = ptr;

    if (cookie->operation == 'r') {
	inflateEnd(&cookie->zstream);
    } else {
	deflateEnd(&cookie->zstream);
    }

    free(cookie->buffer);
    free(cookie);
}

int zlib_close (void *ptr)
{
    struct cookie *cookie = ptr;
    int ret;

    ret = zlib_flush(ptr);

    cookie->close(cookie->ptr);

    zlib_free(ptr);

    return ret;
}

#ifdef O_LARGEFILE

int zlib_seekptr64 (void *ptr, off64_t *position, int whence)
{
    struct cookie *cookie = ptr;
    int ret;
    unsigned char buffer[16384];

    if (cookie->operation != 'r') return -1;
    if (whence == SEEK_END) return -1;
    if (whence == SEEK_CUR) *position += cookie->zstream.total_out;

    if (*position < cookie->zstream.total_out) {
	/* rewind the underlying file and restart decompressing from the beginning */
	if (cookie->seek(cookie->ptr, 0, SEEK_SET) == -1) return -1;
	memset(&cookie->zstream, 0, sizeof(z_stream));
	if (inflateInit2(&cookie->zstream, 32 + MAX_WBITS) != Z_OK) return -1;
    }

    while (*position > cookie->zstream.total_out) {
	cookie->zstream.next_out = buffer;
	if (*position - cookie->zstream.total_out > sizeof(buffer)) {
	    cookie->zstream.avail_out =  sizeof(buffer);
	} else {
	    cookie->zstream.avail_out = (*position - cookie->zstream.total_out);
	}

	if (cookie->zstream.avail_in == 0) {
	    cookie->zstream.next_in = cookie->buffer;
	    cookie->zstream.avail_in = cookie->read(cookie->ptr, cookie->buffer, cookie->bufsize);
	    if (cookie->zstream.avail_in == 0) break;
	}
	if ((ret = inflate(&cookie->zstream, Z_NO_FLUSH)) != Z_OK) break;
    }

    ret = (*position == cookie->zstream.total_out) ? 0 : -1;
    *position = cookie->zstream.total_out;

    return ret;
}

off64_t zlib_seek64 (void *ptr, off64_t position, int whence)
{
    if (zlib_seekptr64(ptr, &position, whence) == -1) return (off64_t)-1;
    else return position;
}

#endif

int zlib_seekptr (void *ptr, off_t *position, int whence)
{
    struct cookie *cookie = ptr;
    int ret;
    unsigned char buffer[16384];

    if (cookie->operation != 'r') return -1;
    if (whence == SEEK_END) return -1;
    if (whence == SEEK_CUR) *position += cookie->zstream.total_out;

    if (*position < cookie->zstream.total_out) {
	/* rewind the underlying file and restart decompressing from the beginning */
	if (cookie->seek(cookie->ptr, 0, SEEK_SET) == -1) {
	    fprintf(stderr, "zlib_seekptr: can't reset underlying stream\n");
	    return -1;
	}
	inflateEnd(&cookie->zstream);
	memset(&cookie->zstream, 0, sizeof(z_stream));
	if (inflateInit2(&cookie->zstream, 32 + MAX_WBITS) != Z_OK) {
	    fprintf(stderr, "zlib_seekptr: can't inflatInit2\n");
	    return -1;
	}
    }

    while (*position > cookie->zstream.total_out) {
	cookie->zstream.next_out = buffer;
	if (*position - cookie->zstream.total_out > sizeof(buffer)) {
	    cookie->zstream.avail_out =  sizeof(buffer);
	} else {
	    cookie->zstream.avail_out = (*position - cookie->zstream.total_out);
	}

	if (cookie->zstream.avail_in == 0) {
	    cookie->zstream.next_in = cookie->buffer;
	    cookie->zstream.avail_in = cookie->read(cookie->ptr, cookie->buffer, cookie->bufsize);
	    if (cookie->zstream.avail_in == 0) {
		fprintf(stderr, "zlib_seekptr: underlying read failed\n");
		break;
	    }
	}
	if ((ret = inflate(&cookie->zstream, Z_NO_FLUSH)) != Z_OK) {
	    fprintf(stderr, "zlib_seekptr: inflate failed\n");
	    break;
	}
    }

    ret = (*position == cookie->zstream.total_out) ? 0 : -1;
    *position = cookie->zstream.total_out;

    return ret;
}

off_t zlib_seek (void *ptr, off_t position, int whence)
{
    if (zlib_seekptr(ptr, &position, whence) == -1) return (off_t)-1;
    else return position;
}

void * zlib_open(void *ptr,
		 ssize_t (*read)(void *, char *, size_t), ssize_t (*write)(void *, const char *, size_t),
		 off_t (*seek)(void *, off_t, int), int (*close)(void *), const char *operation)
{
    struct cookie *cookie;

    cookie = (struct cookie *) malloc(sizeof(struct cookie));

    if (cookie == NULL) {
	errno = ENOMEM;
	return NULL;
    }

    memset(cookie, 0, sizeof(struct cookie));

    cookie->ptr = ptr;
    cookie->read = read;
    cookie->write = write;
    cookie->seek = seek;
    cookie->close = close;

    cookie->bufsize = 16384;
    cookie->buffer = malloc(cookie->bufsize);
    if (cookie->buffer == NULL) {
	errno = ENOMEM;
	return NULL;
    }

    cookie->operation = operation[0];

    if (operation[0] == 'r') {
	cookie->zstream.next_in = cookie->buffer;
	if (inflateInit2(&cookie->zstream, 32 + MAX_WBITS) != Z_OK) {
	    errno = ENOMEM;
	    return NULL;
	}
    } else {
	if (deflateInit2(&cookie->zstream, Z_DEFAULT_COMPRESSION,
			 Z_DEFLATED, 16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
	    errno = ENOMEM;
	    return NULL;
	}
    }

    return cookie;
}

#ifdef __GLIBC__

#if O_LARGEFILE
static cookie_io_functions_t read_functions = {zlib_read, NULL, zlib_seekptr64, zlib_close};
#else
static cookie_io_functions_t read_functions = {zlib_read, NULL, zlib_seekptr, zlib_close};
#endif

static cookie_io_functions_t write_functions = {NULL, zlib_write, NULL, zlib_close};

/* Wrap fseek to get the types correct */

off_t fseek_off_t(void *stream, off_t offset, int whence)
{
    return fseek(stream, offset, whence);
}

FILE * zlib_fopen(FILE *file, char *operation)
{
    struct cookie *cookie;

    cookie = zlib_open(file, fread_wrapper, fwrite_wrapper,
		       fseek_off_t, (int (*)(void *)) fclose, operation);

    if (operation[0] == 'r') {
	return fopencookie(cookie, operation, read_functions);
    } else {
	return fopencookie(cookie, operation, write_functions);
    }
}

#endif
