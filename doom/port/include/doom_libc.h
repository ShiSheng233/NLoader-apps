#ifndef NLR_DOOM_LIBC_H
#define NLR_DOOM_LIBC_H
/* Minimal freestanding libc used by the fixed doomgeneric source list. */
#pragma GCC visibility push(hidden)
#ifdef NLR_DOOM_HOST_TEST
#include "host_namespace.h"
#endif
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
typedef struct nlr_doom_file FILE;
extern FILE *stdin, *stdout, *stderr;
extern int errno;
#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define EISDIR 21
#define EINVAL 22
#define EIO 5
#define EBADF 9
#define ENOMEM 12
#define EROFS 30
#define ENOSYS 38
#define EOVERFLOW 75
#define ERANGE 34
#define F_OK 0
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define O_RDONLY 0
#define O_BINARY 0
#define RAND_MAX 2147483647
void *malloc(size_t);
void *calloc(size_t,size_t);
void *realloc(void *,size_t);
void free(void *);
_Noreturn void exit(int);
_Noreturn void abort(void);
int atexit(void (*)(void));
int atoi(const char *);
long atol(const char *);
long strtol(const char *,char **,int);
unsigned long strtoul(const char *,char **,int);
double atof(const char *);
double strtod(const char *,char **);
int abs(int);
long labs(long);
int rand(void);
void srand(unsigned);
char *getenv(const char *);
int system(const char *);
void qsort(void *,size_t,size_t,int (*)(const void *,const void *));
void *memcpy(void *,const void *,size_t);
void *memmove(void *,const void *,size_t);
void *memset(void *,int,size_t);
int memcmp(const void *,const void *,size_t);
void *memchr(const void *,int,size_t);
size_t strlen(const char *);
int strcmp(const char *,const char *);
int strncmp(const char *,const char *,size_t);
int strcasecmp(const char *,const char *);
int strncasecmp(const char *,const char *,size_t);
char *strcpy(char *,const char *);
char *strncpy(char *,const char *,size_t);
char *strcat(char *,const char *);
char *strncat(char *,const char *,size_t);
char *strchr(const char *,int);
char *strrchr(const char *,int);
char *strstr(const char *,const char *);
char *strdup(const char *);
char *strerror(int);
char *strtok(char *,const char *);
int tolower(int);
int toupper(int);
int isalnum(int);
int isalpha(int);
int isdigit(int);
int isprint(int);
int isspace(int);
int ispunct(int);
int isupper(int);
int islower(int);
FILE *fopen(const char *,const char *);
int fclose(FILE *);
size_t fread(void *,size_t,size_t,FILE *);
size_t fwrite(const void *,size_t,size_t,FILE *);
int fseek(FILE *,long,int);
long ftell(FILE *);
int feof(FILE *);
int ferror(FILE *);
void clearerr(FILE *);
int fflush(FILE *);
int fileno(FILE *);
int fgetc(FILE *);
int getc(FILE *);
int getchar(void);
int fputc(int,FILE *);
int putc(int,FILE *);
int putchar(int);
char *fgets(char *,int,FILE *);
int fputs(const char *,FILE *);
int puts(const char *);
int ungetc(int,FILE *);
int printf(const char *,...);
int fprintf(FILE *,const char *,...);
int sprintf(char *,const char *,...);
int snprintf(char *,size_t,const char *,...);
int vprintf(const char *,va_list);
int vfprintf(FILE *,const char *,va_list);
int vsprintf(char *,const char *,va_list);
int vsnprintf(char *,size_t,const char *,va_list);
int sscanf(const char *,const char *,...);
int fscanf(FILE *,const char *,...);
int remove(const char *);
int rename(const char *,const char *);
void perror(const char *);
int mkdir(const char *,unsigned int);
int isatty(int);
int access(const char *,int);
int open(const char *,int,...);
int close(int);
long read(int,void *,size_t);
long write(int,const void *,size_t);
long lseek(int,long,int);
int unlink(const char *);
char *getcwd(char *,size_t);
int usleep(unsigned int);
unsigned int sleep(unsigned int);
typedef long ssize_t;
typedef long off_t;
typedef unsigned int mode_t;
double fabs(double);
double sqrt(double);
double sin(double);
double cos(double);
double atan(double);
double atan2(double,double);
double pow(double,double);
double floor(double);
double ceil(double);
#define assert(condition) ((condition) ? (void)0 : abort())
#endif
