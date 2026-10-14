#include "nlr.h"
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct nlr_doom_file;
typedef struct nlr_doom_file DFILE;
extern void *nlr_doom_test_malloc(size_t);
extern void *nlr_doom_test_calloc(size_t, size_t);
extern void *nlr_doom_test_realloc(void *, size_t);
extern void nlr_doom_test_free(void *);
extern int nlr_doom_test_errno;
extern char *nlr_doom_test_strdup(const char *);
extern int nlr_doom_test_snprintf(char *, size_t, const char *, ...);
extern int nlr_doom_test_sscanf(const char *, const char *, ...);
extern DFILE *nlr_doom_test_fopen(const char *, const char *);
extern int nlr_doom_test_fclose(DFILE *);
extern size_t nlr_doom_test_fread(void *, size_t, size_t, DFILE *);
extern size_t nlr_doom_test_fwrite(const void *, size_t, size_t, DFILE *);
extern int nlr_doom_test_fseek(DFILE *, long, int);
extern long nlr_doom_test_ftell(DFILE *);
extern int nlr_doom_test_feof(DFILE *);
extern int nlr_doom_test_ferror(DFILE *);
extern void nlr_doom_test_clearerr(DFILE *);
extern int nlr_doom_test_remove(const char *);
extern int nlr_doom_test_rename(const char *, const char *);
extern int nlr_doom_test_mkdir(const char *, unsigned);
extern void nlr_doom_free_allocations(void);
extern void nlr_doom_close_files(void);
extern _Noreturn void nlr_doom_test_exit(int);
const struct nlr_api *nlr_doom_api;
static unsigned live_allocations, peak_allocations, open_calls, read_calls, close_calls;
static int fail_allocate, zero_read, fail_close, exit_code;
static int positions[32], used[32];
static jmp_buf exit_target;
static const unsigned char contents[] = "0123456789";
static void *allocate(uint32_t bytes, uint32_t attrs)
{
    assert(!attrs);
    if (fail_allocate)
        return NULL;
    void *p = malloc(bytes);
    if (p)
    {
        ++live_allocations;
        if (live_allocations > peak_allocations)
            peak_allocations = live_allocations;
    }
    return p;
}
static void release(void *p)
{
    assert(p && live_allocations);
    --live_allocations;
    free(p);
}
static int file_open(const char *path, uint32_t mode)
{
    ++open_calls;
    assert(mode == 0);
    if (strcmp(path, "DOOM.WAD"))
        return -5;
    for (int i = 0; i < 32; i++)
        if (!used[i])
        {
            used[i] = 1;
            positions[i] = 0;
            return i;
        }
    return -16;
}
static int file_read(int fd, void *out, uint32_t n)
{
    ++read_calls;
    assert(fd >= 0 && fd < 32 && used[fd]);
    if (zero_read)
        return 0;
    int left = 10 - positions[fd];
    if (left <= 0)
        return 0;
    if (n > 3)
        n = 3;
    if (n > (unsigned)left)
        n = (unsigned)left;
    memcpy(out, contents + positions[fd], n);
    positions[fd] += (int)n;
    return (int)n;
}
static int file_seek(int fd, int64_t offset, int whence)
{
    assert(fd >= 0 && fd < 32 && used[fd]);
    assert(whence == 0 || whence == 2);
    int64_t next = (whence == 2 ? 10 : 0) + offset;
    if (next < 0 || next > INT_MAX)
        return -22;
    positions[fd] = (int)next;
    return (int)next;
}
static int file_close(int fd)
{
    ++close_calls;
    assert(fd >= 0 && fd < 32 && used[fd]);
    if (fail_close)
        return -1;
    used[fd] = 0;
    return 0;
}
static void api_exit(int code)
{
    exit_code = code;
    longjmp(exit_target, 1);
}
void nlr_doom_platform_shutdown(void) {}
void nlr_doom_log_bytes(const char *b, size_t n)
{
    (void)b;
    (void)n;
}
_Noreturn void nlr_doom_fail(int code, const char *message)
{
    (void)message;
    nlr_doom_test_exit(code);
}
int main(void)
{
    struct nlr_api api = {.abi = 4,
                          .mem_alloc = allocate,
                          .mem_free = release,
                          .fs_open = file_open,
                          .fs_read = file_read,
                          .fs_seek = file_seek,
                          .fs_close = file_close,
                          .exit = api_exit};
    nlr_doom_api = &api;
    unsigned char *p = nlr_doom_test_calloc(17, 3);
    assert(p);
    for (int i = 0; i < 51; i++)
        assert(!p[i]);
    memcpy(p, "alive", 6);
    fail_allocate = 1;
    assert(!nlr_doom_test_realloc(p, 100));
    assert(!strcmp((char *)p, "alive"));
    fail_allocate = 0;
    p = nlr_doom_test_realloc(p, 100);
    assert(p && !strcmp((char *)p, "alive"));
    nlr_doom_test_free(p);
    assert(!live_allocations);
    assert(!nlr_doom_test_calloc(SIZE_MAX, 2));
    assert(!nlr_doom_test_malloc((size_t)UINT32_MAX + 1));
    void *slots[16];
    for (unsigned i = 0; i < 16; i++)
        assert((slots[i] = nlr_doom_test_malloc(8)));
    assert(!nlr_doom_test_malloc(1));
    for (unsigned i = 0; i < 16; i++)
        nlr_doom_test_free(slots[i]);
    assert(!live_allocations && peak_allocations == 16);
    char *copy = nlr_doom_test_strdup("owned");
    assert(copy && !strcmp(copy, "owned"));
    nlr_doom_test_free(copy);
    char text[80];
    assert(nlr_doom_test_snprintf(text, sizeof(text), "%08x/%d/%s/%.2f", 42, -9, "ok", 1.25) == 19);
    assert(!strcmp(text, "0000002a/-9/ok/1.25"));
    assert(nlr_doom_test_snprintf(text, 5, "abcdef") == 6 && !strcmp(text, "abcd"));
    assert(nlr_doom_test_snprintf(NULL, 0, "abc %d", 7) == 5);
    int value = 0;
    assert(nlr_doom_test_sscanf("0x7f", "%i", &value) == 1 && value == 127);
    assert(nlr_doom_test_sscanf("075", "%i", &value) == 1 && value == 61);
    assert(nlr_doom_test_sscanf("-17", "%d", &value) == 1 && value == -17);
    assert(nlr_doom_test_sscanf("junk", "%d", &value) == 0);
    unsigned before = open_calls;
    assert(!nlr_doom_test_fopen("DOOM.WAD", "wb"));
    assert(open_calls == before);
    assert(nlr_doom_test_remove("a") == -1 && nlr_doom_test_rename("a", "b") == -1 &&
           nlr_doom_test_mkdir("a", 0) == -1);
    DFILE *f = nlr_doom_test_fopen("DOOM.WAD", "rb");
    assert(f);
    assert(nlr_doom_test_fread(text, 4, 3, f) == 2);
    assert(!memcmp(text, contents, 10));
    assert(nlr_doom_test_ftell(f) == 10 && nlr_doom_test_feof(f));
    before = read_calls;
    assert(!nlr_doom_test_fread(text, 1, 1, f) && read_calls == before);
    assert(nlr_doom_test_fseek(f, -4, SEEK_CUR) == 0);
    assert(!nlr_doom_test_feof(f));
    assert(nlr_doom_test_fread(text, 1, 4, f) == 4 && !memcmp(text, "6789", 4));
    assert(nlr_doom_test_fseek(f, -11, SEEK_END) == -1);
    assert(nlr_doom_test_fseek(f, 0, SEEK_SET) == 0);
    zero_read = 1;
    assert(!nlr_doom_test_fread(text, 1, 1, f) && nlr_doom_test_ferror(f) &&
           !nlr_doom_test_feof(f));
    zero_read = 0;
    nlr_doom_test_clearerr(f);
    assert(!nlr_doom_test_ferror(f));
    assert(!nlr_doom_test_fwrite("x", 1, 1, f) && nlr_doom_test_ferror(f));
    fail_close = 1;
    assert(nlr_doom_test_fclose(f) == EOF);
    fail_close = 0;
    assert(nlr_doom_test_fclose(f) == 0);
    assert(!nlr_doom_test_fread(text, 1, 1, f));
    assert(nlr_doom_test_malloc(12));
    assert(nlr_doom_test_fopen("DOOM.WAD", "r"));
    if (!setjmp(exit_target))
        nlr_doom_test_exit(7);
    assert(exit_code == 7 && !live_allocations && close_calls == 3);
    for (int i = 0; i < 32; i++)
        assert(!used[i]);
    puts("DOOM libc: allocation ownership, 16-slot refusal, realloc retention, formatting, "
         "read-only I/O, short read, EOF, seek, failed close and exit cleanup passed");
    return 0;
}
