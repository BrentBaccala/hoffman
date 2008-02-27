/* exported functions */

#if STRICT_ZLIB_OPEN_DECLARATION
void * zlib_open(void *ptr,
		 ssize_t (*read)(void *, char *, size_t), ssize_t (*write)(void *, const char *, size_t),
		 off_t (*seek)(void *, off_t, int), int (*close)(void *), char *operation);
#else
void * zlib_open(void *ptr, void *read, void *write, void *seek, void *close, char *operation);
#endif

ssize_t zlib_read(void *ptr, char *buffer, size_t size);
int zlib_read_int(void *ptr, char *buffer, int size);
#ifdef O_LARGEFILE
int zlib_seekptr64 (void *ptr, off64_t *position, int whence);
off64_t zlib_seek64 (void *ptr, off64_t position, int whence);
#endif
int zlib_seekptr (void *ptr, off_t *position, int whence);
off_t zlib_seek (void *ptr, off_t position, int whence);
ssize_t zlib_write(void *ptr, const char *buffer, size_t size);
int zlib_close (void *ptr);

#ifdef __GLIBC__
FILE * zlib_fopen(FILE *file, char *operation);
#endif
