#include "doom_port.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

struct nlr_doom_file
{
    int fd;
    int64_t length;
    int64_t position;
    unsigned used;
    unsigned console;
    unsigned error;
    unsigned eof;
};
static FILE files[NLR_DOOM_FILES];
static FILE console_input = {.fd = -1, .used = 1, .console = 1};
static FILE console_output = {.fd = -1, .used = 1, .console = 2};
static FILE console_error = {.fd = -1, .used = 1, .console = 2};
FILE *stdin = &console_input;
FILE *stdout = &console_output;
FILE *stderr = &console_error;

static int valid_file(const FILE *file)
{
    if (file == stdin || file == stdout || file == stderr)
        return 1;
    for (unsigned i = 0; i < NLR_DOOM_FILES; ++i)
        if (file == &files[i] && files[i].used)
            return 1;
    errno = EBADF;
    return 0;
}

FILE *fopen(const char *path, const char *mode)
{
    if (!path || !mode || (strcmp(mode, "r") && strcmp(mode, "rb")))
    {
        errno = mode && *mode != 'r' ? EROFS : EINVAL;
        return NULL;
    }
    FILE *file = NULL;
    for (unsigned i = 0; i < NLR_DOOM_FILES; ++i)
        if (!files[i].used)
        {
            file = &files[i];
            break;
        }
    if (!file || !nlr_doom_api)
    {
        errno = ENOMEM;
        return NULL;
    }
    int fd = nlr_doom_api->fs_open(path, 0);
    if (fd < 0)
    {
        errno = fd < -1 ? -fd : EIO;
        return NULL;
    }
    *file = (FILE){.fd = fd, .used = 1};
    int length = nlr_doom_api->fs_seek(fd, 0, NLR_SEEK_END);
    if (length < 0 || nlr_doom_api->fs_seek(fd, 0, NLR_SEEK_SET) != 0)
    {
        file->error = 1;
        (void)fclose(file);
        errno = EIO;
        return NULL;
    }
    file->length = length;
    return file;
}

int fclose(FILE *file)
{
    if (!valid_file(file))
        return EOF;
    if (file->console)
        return 0;
    if (nlr_doom_api->fs_close(file->fd) != 0)
    {
        file->error = 1;
        errno = EIO;
        return EOF;
    }
    *file = (FILE){0};
    return 0;
}

size_t fread(void *destination, size_t size, size_t count, FILE *file)
{
    if (!size || !count)
        return 0;
    if (!valid_file(file))
        return 0;
    if (!destination || count > SIZE_MAX / size)
    {
        file->error = 1;
        errno = EOVERFLOW;
        return 0;
    }
    if (file->console || file->position >= file->length)
    {
        file->eof = 1;
        return 0;
    }
    size_t requested = size * count;
    size_t remaining = (size_t)(file->length - file->position);
    size_t wanted = requested < remaining ? requested : remaining;
    size_t done = 0;
    while (done < wanted)
    {
        size_t part = wanted - done;
        if (part > 65536u)
            part = 65536u;
        int received =
            nlr_doom_api->fs_read(file->fd, (unsigned char *)destination + done, (uint32_t)part);
        if (received <= 0 || (size_t)received > part)
        {
            file->error = 1;
            errno = EIO;
            break;
        }
        done += (size_t)received;
        file->position += received;
    }
    if (done == wanted && wanted < requested)
        file->eof = 1;
    return done / size;
}

int fseek(FILE *file, long offset, int whence)
{
    if (!valid_file(file) || file->console)
    {
        errno = EBADF;
        return -1;
    }
    int64_t base;
    if (whence == SEEK_SET)
        base = 0;
    else if (whence == SEEK_CUR)
        base = file->position;
    else if (whence == SEEK_END)
        base = file->length;
    else
    {
        errno = EINVAL;
        return -1;
    }
    if (offset < -base || offset > INT_MAX - base)
    {
        errno = EOVERFLOW;
        return -1;
    }
    int64_t target = base + offset;
    int result = nlr_doom_api->fs_seek(file->fd, target, NLR_SEEK_SET);
    if (result < 0 || result != target)
    {
        file->error = 1;
        errno = EIO;
        return -1;
    }
    file->position = target;
    file->eof = 0;
    return 0;
}

long ftell(FILE *file)
{
    if (!valid_file(file) || file->console)
    {
        errno = EBADF;
        return -1;
    }
    return (long)file->position;
}
int feof(FILE *file) { return valid_file(file) ? (int)file->eof : 0; }
int ferror(FILE *file) { return valid_file(file) ? (int)file->error : 1; }
void clearerr(FILE *file)
{
    if (valid_file(file))
        file->error = file->eof = 0;
}
int fflush(FILE *file) { return !file || valid_file(file) ? 0 : EOF; }

size_t fwrite(const void *source, size_t size, size_t count, FILE *file)
{
    if (!size || !count)
        return 0;
    if (!valid_file(file))
        return 0;
    if (!source || count > SIZE_MAX / size)
    {
        file->error = 1;
        errno = EOVERFLOW;
        return 0;
    }
    if (file != stdout && file != stderr)
    {
        file->error = 1;
        errno = EROFS;
        return 0;
    }
    nlr_doom_log_bytes(source, size * count);
    return count;
}

int fgetc(FILE *file)
{
    unsigned char value;
    return fread(&value, 1, 1, file) == 1 ? value : EOF;
}
int getc(FILE *file) { return fgetc(file); }
int fputc(int character, FILE *file)
{
    unsigned char value = (unsigned char)character;
    return fwrite(&value, 1, 1, file) == 1 ? value : EOF;
}
int putchar(int character) { return fputc(character, stdout); }
int fputs(const char *text, FILE *file)
{
    size_t length = strlen(text);
    return fwrite(text, 1, length, file) == length ? 0 : EOF;
}
int puts(const char *text)
{
    if (fputs(text, stdout) == EOF)
        return EOF;
    return fputc('\n', stdout) == EOF ? EOF : 0;
}
char *fgets(char *buffer, int capacity, FILE *file)
{
    if (!buffer || capacity < 1)
    {
        errno = EINVAL;
        return NULL;
    }
    int used = 0;
    while (used < capacity - 1)
    {
        int c = fgetc(file);
        if (c == EOF)
            break;
        buffer[used++] = (char)c;
        if (c == '\n')
            break;
    }
    buffer[used] = 0;
    return used || capacity == 1 ? buffer : NULL;
}

int remove(const char *path)
{
    (void)path;
    errno = EROFS;
    return -1;
}
int rename(const char *from, const char *to)
{
    (void)from;
    (void)to;
    errno = EROFS;
    return -1;
}
int mkdir(const char *path, unsigned mode)
{
    (void)path;
    (void)mode;
    errno = EROFS;
    return -1;
}
int system(const char *command)
{
    (void)command;
    errno = ENOSYS;
    return -1;
}
char *getenv(const char *name)
{
    (void)name;
    return NULL;
}

void nlr_doom_close_files(void)
{
    for (unsigned i = 0; i < NLR_DOOM_FILES; ++i)
        if (files[i].used)
            (void)fclose(&files[i]);
}
