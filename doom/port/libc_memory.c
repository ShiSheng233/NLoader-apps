#include "doom_port.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

struct allocation
{
    void *pointer;
    uint32_t bytes;
};
static struct allocation allocations[NLR_DOOM_ALLOCATIONS];
int errno;

void *malloc(size_t bytes)
{
    if (!nlr_doom_api || !bytes || bytes > UINT32_MAX)
    {
        errno = bytes > UINT32_MAX ? EOVERFLOW : EINVAL;
        return NULL;
    }
    for (unsigned i = 0; i < NLR_DOOM_ALLOCATIONS; ++i)
    {
        if (allocations[i].pointer)
            continue;
        void *pointer = nlr_doom_api->mem_alloc((uint32_t)bytes, 0);
        if (!pointer)
        {
            errno = ENOMEM;
            return NULL;
        }
        allocations[i] = (struct allocation){pointer, (uint32_t)bytes};
        return pointer;
    }
    errno = ENOMEM;
    return NULL;
}

void free(void *pointer)
{
    if (!pointer)
        return;
    for (unsigned i = 0; i < NLR_DOOM_ALLOCATIONS; ++i)
    {
        if (allocations[i].pointer != pointer)
            continue;
        nlr_doom_api->mem_free(pointer);
        allocations[i] = (struct allocation){0};
        return;
    }
    nlr_doom_fail(EINVAL, "DOOM invalid free");
}

void *calloc(size_t count, size_t bytes)
{
    if (bytes && count > SIZE_MAX / bytes)
    {
        errno = EOVERFLOW;
        return NULL;
    }
    size_t total = count * bytes;
    void *pointer = malloc(total);
    if (pointer)
        memset(pointer, 0, total);
    return pointer;
}

void *realloc(void *pointer, size_t bytes)
{
    if (!pointer)
        return malloc(bytes);
    if (!bytes)
    {
        free(pointer);
        return NULL;
    }
    for (unsigned i = 0; i < NLR_DOOM_ALLOCATIONS; ++i)
    {
        if (allocations[i].pointer != pointer)
            continue;
        size_t old_bytes = allocations[i].bytes;
        void *replacement = malloc(bytes);
        if (!replacement)
            return NULL;
        memcpy(replacement, pointer, bytes < old_bytes ? bytes : old_bytes);
        free(pointer);
        return replacement;
    }
    errno = EINVAL;
    return NULL;
}

char *strdup(const char *source)
{
    size_t bytes = strlen(source);
    if (bytes == SIZE_MAX)
    {
        errno = EOVERFLOW;
        return NULL;
    }
    char *result = malloc(bytes + 1);
    if (result)
        memcpy(result, source, bytes + 1);
    return result;
}

void nlr_doom_free_allocations(void)
{
    for (unsigned i = 0; i < NLR_DOOM_ALLOCATIONS; ++i)
        if (allocations[i].pointer)
            free(allocations[i].pointer);
}

_Noreturn void exit(int code)
{
    nlr_doom_platform_shutdown();
    nlr_doom_close_files();
    nlr_doom_free_allocations();
    (nlr_doom_api->exit)(code);
    __builtin_trap();
}

_Noreturn void abort(void) { nlr_doom_fail(EINVAL, "DOOM abort"); }
