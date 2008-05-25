/* -*- mode: C; fill-column: 100; c-basic-offset: 4; -*-
 *
 * I wrote this wrapper code around ftplib with the intention of using GNU glibc's ability to fopen
 * a 'cookie' of arbitrary functions and thus treat an FTP connection the same any other FILE *.
 * Then I ported to Windows using cygwin, which doesn't using glibc, so I've had to remove my cookie
 * dependencies, loosing a lot of the original motivation for creating this code.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include <ftplib.h>

#include <sys/types.h>
#include <regex.h>

#include "ftp_fopen.h"

struct cookie {
    char	operation;
    char	filename[255];

    netbuf *	control;
    netbuf *	data;
};

ssize_t ftp_read(void *ptr, char *buffer, size_t size)
{
    struct cookie *cookie = ptr;

    return FtpRead(buffer, size, cookie->data);
}

ssize_t ftp_write(void *ptr, const char *buffer, size_t size)
{
    struct cookie *cookie = ptr;

    return FtpWrite(buffer, size, cookie->data);
}

int ftp_close (void *ptr)
{
    struct cookie *cookie = ptr;

    FtpClose(cookie->data);
    FtpQuit(cookie->control);

    free(cookie);

    /* 0 is a good return; -1 is error */

    return 0;
}

#ifdef O_LARGEFILE

int ftp_seekptr64 (void *ptr, off64_t *position, int whence)
{
    struct cookie *cookie = ptr;

    if (cookie->operation != 'r') return -1;
    if (whence != SEEK_SET) return -1;
    if (*position != 0) return -1;

    FtpClose(cookie->data);
    if (!FtpAccess(cookie->filename, (cookie->operation == 'r') ? FTPLIB_FILE_READ : FTPLIB_FILE_WRITE,
		   FTPLIB_IMAGE, cookie->control, &cookie->data)) {
	return -1;
    }

    return 0;
}

off64_t ftp_seek64 (void *ptr, off64_t position, int whence)
{
    if (ftp_seekptr64(ptr, &position, whence) == -1) return (off64_t)-1;
    else return position;
}

#endif

int ftp_seekptr (void *ptr, off_t *position, int whence)
{
    struct cookie *cookie = ptr;

    if (cookie->operation != 'r') return -1;
    if (whence != SEEK_SET) return -1;
    if (*position != 0) return -1;

    FtpClose(cookie->data);
    if (!FtpAccess(cookie->filename, (cookie->operation == 'r') ? FTPLIB_FILE_READ : FTPLIB_FILE_WRITE,
		   FTPLIB_IMAGE, cookie->control, &cookie->data)) {
	return -1;
    }

    return 0;
}

off_t ftp_seek (void *ptr, off_t position, int whence)
{
    if (ftp_seekptr(ptr, &position, whence) == -1) return (off_t)-1;
    else return position;
}

void * ftp_open(char *hostname, char *filename, char *operation)
{
    struct cookie *cookie;

    if (strlen(filename) > 254) {
	errno = ENAMETOOLONG;
	return NULL;
    }

    cookie = (struct cookie *) malloc(sizeof(struct cookie));

    if (cookie == NULL) {
	errno = ENOMEM;
	return NULL;
    }

    memset(cookie, 0, sizeof(struct cookie));

    cookie->operation = operation[0];

    strncpy(cookie->filename, filename, 255);

    FtpInit();

    if (!FtpConnect(hostname, &cookie->control)) {
	free(cookie);
	errno = EACCES;
	return NULL;
    }
    if (!FtpLogin("anonymous", "hoffman@freesoft.org", cookie->control)) {
	free(cookie);
	errno = EACCES;
	return NULL;
    }
    if (!FtpAccess(filename, (operation[0] == 'r') ? FTPLIB_FILE_READ : FTPLIB_FILE_WRITE,
		   FTPLIB_IMAGE, cookie->control, &cookie->data)) {
	free(cookie);
	errno = EACCES;
	return NULL;
    }

    return cookie;
}

void * ftp_openurl(char *url, char *operation) {
    regex_t pattern;
    regmatch_t matches[3];
    char hostname[256];

    if ((regcomp(&pattern, "ftp://\\([^/]*\\)\\(.*\\)", 0) != 0) || (regexec(&pattern, url, 3, matches, 0) != 0)) {
	errno = EINVAL;
	return NULL;
    }

    strncpy(hostname, url + matches[1].rm_so, matches[1].rm_eo - matches[1].rm_so);
    hostname[matches[1].rm_eo - matches[1].rm_so] = '\0';

    /* fprintf(stderr, "hostname = %s; filename = %s\n", hostname, url + matches[2].rm_so); */

    return ftp_open(hostname, url + matches[2].rm_so, operation);
}

#ifdef __GLIBC__

static cookie_io_functions_t read_functions = {ftp_read, NULL, ftp_seekptr64, ftp_close};
static cookie_io_functions_t write_functions = {NULL, ftp_write, NULL, ftp_close};

FILE * ftp_fopen(char *hostname, char *filename, char *operation)
{
    struct cookie *cookie;

    cookie = ftp_open(hostname, filename, operation);

    if (operation[0] == 'r') {
	return fopencookie(cookie, operation, read_functions);
    } else {
	return fopencookie(cookie, operation, write_functions);
    }
}

#endif
