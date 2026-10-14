/*
 * fm: a file manager for NLoader (foreground program, ABI 4).
 *
 * Browses the configured NLoader directory read-only, as
 * the camera's own menu pages (ui_list): one
 * directory at a time, directories first. OK opens a directory or shows a
 * file's size; back goes up, and leaves at the configured root. A directory longer
 * than one page continues on a "Next page" row; back from a later page
 * returns to the one before. An empty path names that root; paths never
 * contain a card letter or climb above it.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nlr.h"
#include "nlr_libc.h"

#define MAX_ENTRIES 2000
#define PAGE_ROWS 60          /* entries a page shows; one more row moves on */
#define PATH_MAX_FM (NLR_API_PATH_MAX + 1u)

static const struct nlr_api *api;

static char path[PATH_MAX_FM];          /* "" or "APPS\\DATA" */
static struct nlr_dirent *entries;
static int count;
static int list_error;                  /* the first fs_dir failure, or 0 */
static int truncated;

static char rows[NLR_LIST_ROWS_MAX][NLR_LIST_TEXT_MAX + 1];
static const char *row_ptrs[NLR_LIST_ROWS_MAX];

static void load(void)
{
    struct nlr_dirent e;
    int r = 0;

    count = 0;
    list_error = 0;
    truncated = 0;
    while (count < MAX_ENTRIES && (r = api->fs_dir(path, (uint32_t)count, &e)) == 0)
        entries[count++] = e;
    if (r != 0 && r != -NLR_API_ENOENT)
        list_error = r;
    if (count == MAX_ENTRIES) {
        r = api->fs_dir(path, (uint32_t)count, &e);
        truncated = r == 0;
        if (r < 0 && r != -NLR_API_ENOENT)
            list_error = r;
    }
}

static void size_text(char *out, size_t n, uint64_t s)
{
    if (s < 10000)
        snprintf(out, n, "%u B", (unsigned)s);
    else if (s < 10000ull * 1024)
        snprintf(out, n, "%u KB", (unsigned)(s >> 10));
    else if (s < 10000ull * 1024 * 1024)
        snprintf(out, n, "%u MB", (unsigned)(s >> 20));
    else
        snprintf(out, n, "%u GB", (unsigned)(s >> 30));
}

/* The page title: the path, its tail when it is too long for the bar. */
static const char *title_text(void)
{
    size_t n = strlen(path);

    return !n ? "NLoader files" : n <= NLR_LIST_TEXT_MAX ? path : path + n - NLR_LIST_TEXT_MAX;
}

/* Show one list and return the choice (a row, NLR_LIST_BACK, or < 0). */
static int32_t show(const char *title, int n, int selected)
{
    for (int i = 0; i < n; i++)
        row_ptrs[i] = rows[i];
    return api->ui_list(title, row_ptrs, (uint32_t)n, (uint32_t)(selected < 0 ? 0 : selected));
}

/* A file: its name above its size; back returns. */
static int show_file(const struct nlr_dirent *e)
{
    char sz[16];

    size_text(sz, sizeof sz, e->size);
    snprintf(rows[0], sizeof rows[0], "Size: %s", sz);
    int32_t r = show(e->name, 1, 0);

    return r < 0 ? (int)r : 0;
}

/* Up one level: "APPS\\DATA" -> "APPS", "APPS" -> "". Copies
 * the name left into `left`. */
static void up(char *left, size_t n)
{
    char *cut = strrchr(path, '\\');

    snprintf(left, n, "%s", cut ? cut + 1 : path);
    if (cut)
        *cut = 0;
    else
        path[0] = 0;
}

/* One directory, page by page, until the user goes up past it. Returns 0
 * at the configured root (leave) or < 0 on an API failure. */
static int browse(void)
{
    int page = 0, selected = 0;

    load();
    for (;;) {
        int first = page * PAGE_ROWS;
        int n = 0;

        if (count == 0) {
            snprintf(rows[n++], sizeof rows[0], "%s",
                     list_error ? "(directory read failed)" : "(empty or unavailable)");
        }
        for (int i = first; i < count && n < PAGE_ROWS; i++, n++) {
            const struct nlr_dirent *e = &entries[i];
            char sz[16];

            if (e->flags & NLR_DIRENT_DIR) {
                snprintf(rows[n], sizeof rows[0], "%s\\", e->name);
            } else {
                size_text(sz, sizeof sz, e->size);
                snprintf(rows[n], sizeof rows[0], "%.36s  %s", e->name, sz);
            }
        }
        int more = first + PAGE_ROWS < count;
        int next_row = -1;

        if (more) {
            next_row = n;
            snprintf(rows[n++], sizeof rows[0], "Next page (%d of %d)", page + 2,
                     (count + PAGE_ROWS - 1) / PAGE_ROWS);
        }
        if (list_error && count)
            snprintf(rows[n++], sizeof rows[0], "Directory read failed (%d)", list_error);
        if (truncated)
            snprintf(rows[n++], sizeof rows[0], "Only first %d entries shown", MAX_ENTRIES);
        int32_t r = show(title_text(), n, selected);

        if (r < 0)
            return (int)r;
        if (r == NLR_LIST_BACK) {
            char left[NLR_DIRENT_NAME_MAX];

            if (page > 0) {
                page--;
                selected = PAGE_ROWS - 1;
                continue;
            }
            if (!path[0])
                return 0;
            up(left, sizeof left);
            load();
            page = 0;
            selected = 0;
            for (int i = 0; i < count; i++) /* land on the directory left */
                if (!strcmp(entries[i].name, left)) {
                    page = i / PAGE_ROWS;
                    selected = i % PAGE_ROWS;
                    break;
                }
            continue;
        }
        if (more && r == next_row) {
            page++;
            selected = 0;
            continue;
        }
        int index = first + (int)r;

        selected = (int)r;
        if (count == 0 || r >= PAGE_ROWS || index >= count)
            continue;
        const struct nlr_dirent *e = &entries[index];
        size_t len = strlen(path);

        if (!(e->flags & NLR_DIRENT_DIR)) {
            int f = show_file(e);

            if (f < 0)
                return f;
            continue;
        }
        if (len + (len ? 1u : 0u) + strlen(e->name) >= sizeof path)
            continue;
        snprintf(path + len, sizeof path - len, "%s%s", len ? "\\" : "", e->name);
        load();
        page = 0;
        selected = 0;
    }
}

int32_t nlr_app_main(const struct nlr_api *table)
{
    if (!table || table->abi < 4 || !table->fs_dir || !table->ui_list ||
        !table->mem_alloc || !table->mem_free)
        return 1;
    api = table;
    nlr_libc_api = table;
    if (nlr_libc_arena(512u << 10, 256u << 10) != 0)
        return 2;
    entries = malloc(sizeof *entries * MAX_ENTRIES);
    if (!entries)
        return 3;
    path[0] = 0;
    int result = browse();
    free(entries);
    return result < 0 ? 5 : 0;
}
