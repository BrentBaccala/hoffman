
#define _GNU_SOURCE		/* to get O_DIRECT */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>	/* for memset() */
#include <unistd.h>	/* for _PC_REC_XFER_ALIGN */
#include <fcntl.h>
#include <aio.h>
#include <errno.h>

/* (1<<17) * 4 = (1<<19) * 2 = (1<<20) * 16 = 16 MB * 431 tables */

#define NUMPASSES (431*2*16)
#define NUMAIOS 4

#define BUFFER_BYTES (1<<17)

struct aiocb aiocb[NUMAIOS];
void *buffer[NUMAIOS];
struct timeval timings[NUMAIOS];

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
	sprintf(strbuf, "%ld.%03lds", timevalp->tv_sec, timevalp->tv_usec/1000);
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
    int pass;
    struct timeval tv1, tv2;
    struct iocb * iocbp[1];
    char strbuf[256];
    const struct aiocb * aiocbs[1];

    /* fd = open("testfile", O_RDONLY | O_DIRECT); */
    fd = open("testfile", O_RDWR | O_DIRECT);
    alignment = fpathconf(fd, _PC_REC_XFER_ALIGN);

    for (i=0; i<NUMAIOS; i++) {
	if (posix_memalign(&buffer[i], alignment, BUFFER_BYTES) != 0) {
	    fprintf(stderr, "Can't posix_memalign\n");
	}
    }

    fprintf(stderr, "Enqueues starting\n");
    gettimeofday(&tv1, NULL);

    for (i=0; i<NUMAIOS; i++) {

	memset(&aiocb[i], 0, sizeof(struct aiocb));

	aiocb[i].aio_fildes = fd;
	aiocb[i].aio_buf = buffer[i];
	aiocb[i].aio_nbytes = BUFFER_BYTES;
	aiocb[i].aio_sigevent.sigev_notify = SIGEV_NONE;
	aiocb[i].aio_offset = BUFFER_BYTES * i;
	/* aiocb[i].aio_offset = 0; */

	if (aio_read(& aiocb[i]) != 0) {
	    perror("");
	    fprintf(stderr, "Can't enqueue aio_read %d\n", i);
	}
    }

    for (pass=1; pass<NUMPASSES; pass++) {

#if 1
	/* wait for reads and queue writes */

	for (i=0; i<NUMAIOS; i++) {
	    const struct aiocb * aiocbs[1];
	    aiocbs[0] = &aiocb[i];
	    aio_suspend(aiocbs, 1, NULL);

	    memset(&aiocb[i], 0, sizeof(struct aiocb));

	    aiocb[i].aio_fildes = fd;
	    aiocb[i].aio_buf = buffer[i];
	    aiocb[i].aio_nbytes = BUFFER_BYTES;
	    aiocb[i].aio_sigevent.sigev_notify = SIGEV_NONE;
	    aiocb[i].aio_offset = BUFFER_BYTES * i;
	    /* aiocb[i].aio_offset = 0; */

	    if (aio_write(& aiocb[i]) != 0) {
		perror("");
		fprintf(stderr, "Can't enqueue aio_write %d\n", i);
	    }
	}
#endif

	/* wait for writes and queue reads */

	for (i=0; i<NUMAIOS; i++) {
	    const struct aiocb * aiocbs[1];
	    aiocbs[0] = &aiocb[i];
	    aio_suspend(aiocbs, 1, NULL);

	    memset(&aiocb[i], 0, sizeof(struct aiocb));

	    aiocb[i].aio_fildes = fd;
	    aiocb[i].aio_buf = buffer[i];
	    aiocb[i].aio_nbytes = BUFFER_BYTES;
	    aiocb[i].aio_sigevent.sigev_notify = SIGEV_NONE;
	    aiocb[i].aio_offset = BUFFER_BYTES * i;
	    /* aiocb[i].aio_offset = 0; */

	    if (aio_read(& aiocb[i]) != 0) {
		perror("");
		fprintf(stderr, "Can't enqueue aio_read %d\n", i);
	    }
	}
    }

    /* wait for last reads */

    for (i=0; i<NUMAIOS; i++) {
	const struct aiocb * aiocbs[1];
	aiocbs[0] = &aiocb[i];
	aio_suspend(aiocbs, 1, NULL);
    }
}
