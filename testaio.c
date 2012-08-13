
#define _GNU_SOURCE		/* to get O_DIRECT */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>	/* for memset() */
#include <unistd.h>	/* for _PC_REC_XFER_ALIGN */
#include <asm/unistd.h>
#include <fcntl.h>
#include <aio.h>
#include <errno.h>

#define NUMAIOS 100

#define BUFFER_BYTES (1<<20)

struct iocb iocb[NUMAIOS];
void *buffer[NUMAIOS];
struct timeval timings[NUMAIOS];

aio_context_t aio_default_context;

void subtract_timeval(struct timeval *dest, struct timeval *src)
{
    dest->tv_sec -= src->tv_sec;
    if (dest->tv_usec > src->tv_usec) {
	dest->tv_usec -= src->tv_usec;
    } else {
	dest->tv_usec = 1000000 + dest->tv_usec - src->tv_usec;
	dest->tv_sec --;
    }
}

void add_timeval(struct timeval *dest, struct timeval *src)
{
    dest->tv_sec += src->tv_sec;
    dest->tv_usec += src->tv_usec;
    if (dest->tv_usec >= 1000000) {
	dest->tv_usec -= 1000000;
	dest->tv_sec ++;
    }
}

void sprint_timeval(char *strbuf, struct timeval *timevalp)
{
    if (timevalp->tv_sec < 60) {
	sprintf(strbuf, "%ld.%06lds", timevalp->tv_sec, timevalp->tv_usec);
    } else if (timevalp->tv_sec < 3600) {
	sprintf(strbuf, "%ldm%02ld.%03lds", timevalp->tv_sec/60, timevalp->tv_sec%60,
		timevalp->tv_usec/1000);
    } else {
	sprintf(strbuf, "%ldh%02ldm%02ld.%03lds", timevalp->tv_sec/3600,
		(timevalp->tv_sec/60)%60, timevalp->tv_sec%60, timevalp->tv_usec/1000);
    }
}

main()
{
    int fd;
    int i;
    int alignment;
    struct timeval tv1, tv2;
    struct iocb * iocbp[1];
    char strbuf[256];

    fd = open("testfile", O_RDONLY | O_DIRECT | O_NONBLOCK);
    alignment = fpathconf(fd, _PC_REC_XFER_ALIGN);

    for (i=0; i<NUMAIOS; i++) {
	if (posix_memalign(&buffer[i], alignment, BUFFER_BYTES) != 0) {
	    fprintf(stderr, "Can't posix_memalign\n");
	}
    }

    io_setup(1024, &aio_default_context);

    fprintf(stderr, "Enqueues starting\n");
    gettimeofday(&tv1, NULL);

    for (i=0; i<NUMAIOS; i++) {

	memset(&iocb[i], 0, sizeof(struct iocb));

	iocb[i].aio_lio_opcode = IOCB_CMD_PREAD;
	iocb[i].aio_fildes = fd;
	iocb[i].aio_buf = (unsigned long) buffer[i];
	iocb[i].aio_nbytes = BUFFER_BYTES;
	iocb[i].aio_offset = BUFFER_BYTES * i;
	/* aiocb[i].aio_offset = 0; */

	iocbp[0] = &iocb[i];
	if (io_submit(aio_default_context, 1, iocbp) != 1) {
	    perror("");
	    fprintf(stderr, "Can't enqueue aio_read %d\n", i);
	}
	gettimeofday(&timings[i], NULL);
    }

    gettimeofday(&tv2, NULL);
    subtract_timeval(&tv2, &tv1);
    sprint_timeval(strbuf, &tv2);
    fprintf(stderr, "Enqueues complete in %s\n", strbuf);

    for (i=0; i<NUMAIOS; i++) {
      subtract_timeval(&timings[i], &tv1);
      sprint_timeval(strbuf, &timings[i]);
      fprintf(stderr, "%d: %s", i, strbuf);
      if (i > 0) {
	subtract_timeval(&timings[i], &timings[i-1]);
	sprint_timeval(strbuf, &timings[i]);
	fprintf(stderr, "  %s", strbuf);
	add_timeval(&timings[i], &timings[i-1]);
      }
      fprintf(stderr, "\n");
    }
}
