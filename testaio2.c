
#define _GNU_SOURCE		/* to get O_DIRECT */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>	/* for memset() */
#include <unistd.h>	/* for _PC_REC_XFER_ALIGN */
#include <fcntl.h>
#include </usr/include/aio.h>

#define NUMAIOS 100

#define BUFFER_BYTES (1<<20)

struct aiocb aiocb[NUMAIOS];
void *buffer[NUMAIOS];

main()
{
    int fd;
    int i;
    int alignment;

    fd = open("testfile", O_RDONLY | O_DIRECT);
    alignment = fpathconf(fd, _PC_REC_XFER_ALIGN);

    for (i=0; i<NUMAIOS; i++) {
	if (posix_memalign(&buffer[i], alignment, BUFFER_BYTES) != 0) {
	    fprintf(stderr, "Can't posix_memalign\n");
	}
#if 0
	free(buffer[i]);
	if (posix_memalign(&buffer[i], alignment, BUFFER_BYTES) != 0) {
	    fprintf(stderr, "Can't posix_memalign\n");
	}
#endif
    }

    fprintf(stderr, "Enqueues starting\n");

    for (i=0; i<NUMAIOS; i++) {

	memset(&aiocb[i], 0, sizeof(struct aiocb));

	aiocb[i].aio_fildes = fd;
	aiocb[i].aio_buf = buffer[i];
	aiocb[i].aio_nbytes = BUFFER_BYTES;
	aiocb[i].aio_sigevent.sigev_notify = SIGEV_NONE;
	aiocb[i].aio_offset = BUFFER_BYTES * i;
	/* aiocb[i].aio_offset = 0; */

	if (aio_read(& aiocb[i]) != 0) {
	    fprintf(stderr, "Can't enqueue aio_read %d\n", i);
	}
    }

    fprintf(stderr, "Enqueues complete\n");

#if 1
    for (i=0; i<NUMAIOS; i++) {
	free(buffer[i]);
	if (posix_memalign(&buffer[i], alignment, BUFFER_BYTES) != 0) {
	    fprintf(stderr, "Can't posix_memalign\n");
	}
    }
#endif

#if 1
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
	fprintf(stderr, "Can't enqueue aio_read %d\n", i);
      }
    }
#endif

    for (i=0; i<NUMAIOS; i++) {
      const struct aiocb * aiocbs[1];
      aiocbs[0] = &aiocb[i];
      aio_suspend(aiocbs, 1, NULL);
    }
}
