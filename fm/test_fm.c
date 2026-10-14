#include "nlr.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int32_t nlr_app_main(const struct nlr_api *);
const struct nlr_api *nlr_libc_api;
static unsigned lists, roots, nested;
static int large;
int nlr_libc_arena(size_t want, size_t floor)
{
    assert(want == 512u << 10 && floor == 256u << 10);
    return 0;
}
static void *allocate(uint32_t n, uint32_t attrs) { assert(!attrs); return malloc(n); }
static void release(void *p) { free(p); }
static int directory(const char *path, uint32_t index, struct nlr_dirent *out)
{
    assert(!strchr(path, ':') && !strstr(path, "..") && path[0] != '\\');
    memset(out, 0, sizeof *out);
    if (!*path) {
        ++roots;
        if (large) {
            if (index > 2000u)
                return -NLR_API_ENOENT;
            snprintf(out->name, sizeof out->name, "F%u", index);
            return 0;
        }
        if (index > 1u)
            return -NLR_API_ENOENT;
        strcpy(out->name, index ? "root.txt" : "APPS");
        out->flags = index ? 0u : NLR_DIRENT_DIR;
    } else if (!strcmp(path, "APPS")) {
        ++nested;
        if (index > 0u)
            return -NLR_API_ENOENT;
        strcpy(out->name, "DATA");
        out->flags = NLR_DIRENT_DIR;
    } else {
        assert(!strcmp(path, "APPS\\DATA"));
        ++nested;
        if (index > 0u)
            return -NLR_API_ENOENT;
        strcpy(out->name, "inside.txt");
    }
    return 0;
}
static int32_t list(const char *title, const char *const *rows,
                    uint32_t count, uint32_t selected)
{
    assert(selected < count && count <= NLR_LIST_ROWS_MAX);
    unsigned call = lists++;
    if (large) {
        assert(strstr(rows[count - 1u], "Only first 2000 entries shown"));
        if (!call) {
            assert(count == 62u && !strncmp(rows[60], "Next page", 9));
            return 60;
        }
        if (call == 1u)
            assert(!strncmp(rows[0], "F60 ", 4));
        return NLR_LIST_BACK;
    }
    switch (call) {
    case 0: assert(!strcmp(title, "NLoader files")); return 0;
    case 1: assert(!strcmp(title, "APPS")); return 0;
    case 2: assert(!strcmp(title, "APPS\\DATA")); return NLR_LIST_BACK;
    case 3: assert(!strcmp(title, "APPS")); return NLR_LIST_BACK;
    case 4: assert(!strcmp(title, "NLoader files")); return 1;
    case 5: assert(!strcmp(title, "root.txt")); return NLR_LIST_BACK;
    case 6: return NLR_LIST_BACK;
    default: abort();
    }
}
int main(void)
{
    struct nlr_api api = {.abi = 3u, .mem_alloc = allocate, .mem_free = release,
                         .fs_dir = directory, .ui_list = list};
    assert(nlr_app_main(&api) == 1 && !lists);
    api.abi = 4;
    assert(nlr_app_main(&api) == 0 && lists == 7 && roots && nested);
    lists = 0;
    large = 1;
    assert(nlr_app_main(&api) == 0 && lists == 3);
    puts("NLoader FM: configured-root navigation, ABI gate and directory limit passed");
}
