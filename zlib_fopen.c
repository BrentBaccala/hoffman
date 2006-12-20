/* -*- mode: C; fill-column: 100; c-basic-offset: 4; -*-

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <zlib.h>

struct cookie {
    FILE *	file;
    z_stream	zstream;
    void *	buffer;
    int		bufsize;
};

static ssize_t reader(void *ptr, char *buffer, size_t size)
{
    struct cookie *cookie = ptr;
    int ret;

    cookie->zstream.next_out = (unsigned char *) buffer;
    cookie->zstream.avail_out = size;

    do {
	if (cookie->zstream.avail_in == 0) {
	    cookie->zstream.next_in = cookie->buffer;
	    cookie->zstream.avail_in = fread(cookie->buffer, 1, cookie->bufsize, cookie->file);
	    if (cookie->zstream.avail_in == 0) break;
	}
	if ((ret = inflate(&cookie->zstream, Z_NO_FLUSH)) != Z_OK) break;
    } while (cookie->zstream.avail_out > 0);

    return (size - cookie->zstream.avail_out);
}

static int read_cleaner (void *ptr)
{
    struct cookie *cookie = ptr;

    fclose(cookie->file);

    free(cookie->buffer);
    free(cookie);

    return 0;
}

static ssize_t writer(void *ptr, const char *buffer, size_t size)
{
    struct cookie *cookie = ptr;

    cookie->zstream.next_in = (unsigned char *) buffer;
    cookie->zstream.avail_in = size;
    cookie->zstream.next_out = cookie->buffer;
    cookie->zstream.avail_out = cookie->bufsize;

    do {
	if (deflate(&cookie->zstream, Z_NO_FLUSH) != Z_OK) break;

	if (cookie->zstream.avail_out < cookie->bufsize) {
	    fwrite(cookie->buffer, cookie->bufsize - cookie->zstream.avail_out, 1, cookie->file);
	    cookie->zstream.next_out = cookie->buffer;
	    cookie->zstream.avail_out = cookie->bufsize;
	}
    } while (cookie->zstream.avail_in > 0);

    return (size - cookie->zstream.avail_in);
}

static int write_cleaner (void *ptr)
{
    struct cookie *cookie = ptr;
    int ret;

    while ((ret = deflate(&cookie->zstream, Z_FINISH)) == Z_OK) {

	if (cookie->zstream.avail_out < cookie->bufsize) {
	    fwrite(cookie->buffer, cookie->bufsize - cookie->zstream.avail_out, 1, cookie->file);
	    cookie->zstream.next_out = cookie->buffer;
	    cookie->zstream.avail_out = cookie->bufsize;
	}
    }

    if (cookie->zstream.avail_out < cookie->bufsize) {
	fwrite(cookie->buffer, cookie->bufsize - cookie->zstream.avail_out, 1, cookie->file);
	cookie->zstream.next_out = cookie->buffer;
	cookie->zstream.avail_out = cookie->bufsize;
    }

    fclose(cookie->file);

    free(cookie->buffer);
    free(cookie);

    /* 0 is a good return; -1 is error */

    return (ret == Z_STREAM_END) ? 0 : -1;
}

static int read_seeker (void *ptr, off64_t *position, int whence)
{
    struct cookie *cookie = ptr;
    int ret;
    unsigned char buffer[16384];

    if (whence == SEEK_END) return -1;
    if (whence == SEEK_CUR) *position += cookie->zstream.total_out;

    if (*position < cookie->zstream.total_out) {
	/* rewind the underlying file and restart decompressing from the beginning */
	if (fseek(cookie->file, 0, SEEK_SET) == -1) return -1;
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
	    cookie->zstream.avail_in = fread(cookie->buffer, 1, cookie->bufsize, cookie->file);
	    if (cookie->zstream.avail_in == 0) break;
	}
	if ((ret = inflate(&cookie->zstream, Z_NO_FLUSH)) != Z_OK) break;
    }

    ret = (*position == cookie->zstream.total_out) ? 0 : -1;
    *position = cookie->zstream.total_out;

    return ret;
}

static cookie_io_functions_t read_functions = {reader, NULL, read_seeker, read_cleaner};
static cookie_io_functions_t write_functions = {NULL, writer, NULL, write_cleaner};

FILE * zlib_fopen(FILE *file, char *operation)
{
    /* this code could check for URLs or types in the 'url' and
       basicly use the real fopen() for standard files */

    struct cookie *cookie;

    cookie = (struct cookie *) malloc(sizeof(struct cookie));

    if (cookie == NULL) {
	errno = ENOMEM;
	return NULL;
    }

    memset(cookie, 0, sizeof(struct cookie));

    cookie->file = file;

    cookie->bufsize = 16384;
    cookie->buffer = malloc(cookie->bufsize);
    if (cookie->buffer == NULL) {
	errno = ENOMEM;
	return NULL;
    }

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

    if (operation[0] == 'r') {
	return fopencookie(cookie, operation, read_functions);
    } else {
	return fopencookie(cookie, operation, write_functions);
    }
}
