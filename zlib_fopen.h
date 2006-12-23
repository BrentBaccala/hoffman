/* exported functions */

#if STRICT_ZLIB_OPEN_DECLARATION
void * zlib_open(void *ptr,
		 ssize_t (*read)(void *, char *, size_t), ssize_t (*write)(void *, const char *, size_t),
		 off64_t (*seek)(void *, off64_t, int), int (*close)(void *), char *operation);
#else
void * zlib_open(void *ptr, void *read, void *write, void *seek, void *close, char *operation);
#endif

ssize_t zlib_read(void *ptr, char *buffer, size_t size);
int zlib_seekptr (void *ptr, off64_t *position, int whence);
off64_t zlib_seek64 (void *ptr, off64_t position, int whence);
ssize_t zlib_write(void *ptr, const char *buffer, size_t size);
int zlib_close (void *ptr);

#ifdef __GLIBC__
FILE * zlib_fopen(FILE *file, char *operation);
#endif
