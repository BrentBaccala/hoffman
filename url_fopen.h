/* exported functions */

void * url_open(char *url,const char *operation);
ssize_t url_read(void *ptr, char *buffer, size_t size);
#ifdef O_LARGEFILE
int url_seekptr64 (void *ptr, off64_t *position, int whence);
off64_t url_seek64 (void *ptr, off64_t position, int whence);
#endif
off_t url_seek (void *ptr, off_t position, int whence);
ssize_t url_write(void *ptr, const char *buffer, size_t size);
int url_close (void *ptr);

#ifdef __GLIBC__
FILE *url_fopen(char *url,const char *operation);
#endif