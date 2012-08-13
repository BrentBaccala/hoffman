/* exported functions */

void * ftp_open(char *hostname, char *filename, const char *operation);
void * ftp_openurl(char *url, const char *operation);

ssize_t ftp_read(void *ptr, char *buffer, size_t size);
#ifdef O_LARGEFILE
int ftp_seekptr64 (void *ptr, off64_t *position, int whence);
off64_t ftp_seek64 (void *ptr, off64_t position, int whence);
#endif
int ftp_seekptr (void *ptr, off_t *position, int whence);
off_t ftp_seek (void *ptr, off_t position, int whence);
ssize_t ftp_write(void *ptr, const char *buffer, size_t size);
int ftp_close (void *ptr);

#ifdef __GLIBC__
FILE * ftp_fopen(char *hostname, char *filename, const char *operation);
#endif
