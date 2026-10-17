/* DCIMTEST: the card's pictures as a program sees them (ABI 9). It lists
 * /DCIM, reads every file name of its first folder in one pass
 * (fs_dir_names), reads the first file, and tries to write there -- each
 * write must be refused with -NLR_API_EACCES and leave the card unchanged.
 * One line per step goes to APPS/DCIMTEST.LOG (relative to the loader's
 * directory, so it follows [storage].root):
 *
 *   dir /DCIM <index> <result> <name> <flags>
 *   names <path> <count> <name>,<name>,...
 *   read <path> <result> <first bytes as text>
 *   write <path> <mode> <result>
 *   open /DCIM <result>
 *   names APPS <count> ...
 *   done
 *
 * Nothing here names a firmware address or a drive letter. State lives on
 * the stack: a program with no writable static data gets one block. */
#include "nlr.h"

struct line {
    char text[200];
    uint32_t at;
};

static void put_text(struct line *l, const char *text)
{
    while (*text && l->at < sizeof l->text - 2u)
        l->text[l->at++] = *text++;
}

static void put_dec(struct line *l, int32_t v)
{
    char digits[12];
    uint32_t n = 0;
    uint32_t value = v < 0 ? (uint32_t)(-v) : (uint32_t)v;
    do {
        digits[n++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value);
    if (v < 0)
        put_text(l, "-");
    while (n && l->at < sizeof l->text - 2u)
        l->text[l->at++] = digits[--n];
}

static void emit(const struct nlr_api *api, struct line *l)
{
    l->text[l->at++] = '\n';
    int fd = api->fs_open("APPS/DCIMTEST.LOG", NLR_OPEN_UPDATE);
    if (fd < 0)
        fd = api->fs_open("APPS/DCIMTEST.LOG", NLR_OPEN_WRITE);
    if (fd >= 0) {
        api->fs_seek(fd, 0, NLR_SEEK_END);
        api->fs_write(fd, l->text, l->at);
        api->fs_close(fd);
    }
    l->at = 0;
}

static void copy(char *out, uint32_t cap, const char *a, const char *b)
{
    uint32_t n = 0;
    while (*a && n + 1u < cap)
        out[n++] = *a++;
    while (b && *b && n + 1u < cap)
        out[n++] = *b++;
    out[n] = 0;
}

static void names(const struct nlr_api *api, const char *path)
{
    char list[8 * NLR_DIRENT_NAME_MAX];
    struct line l;
    l.at = 0;
    int32_t count = api->fs_dir_names(path, list, 8);
    put_text(&l, "names ");
    put_text(&l, path);
    put_text(&l, " ");
    put_dec(&l, count);
    for (int32_t i = 0; i < count && i < 8; i++) {
        put_text(&l, i ? "," : " ");
        put_text(&l, list + (uint32_t)i * NLR_DIRENT_NAME_MAX);
    }
    emit(api, &l);
}

static void try_write(const struct nlr_api *api, const char *path, uint32_t mode)
{
    struct line l;
    l.at = 0;
    int fd = api->fs_open(path, mode);
    put_text(&l, "write ");
    put_text(&l, path);
    put_text(&l, mode == NLR_OPEN_WRITE ? " write " : " update ");
    put_dec(&l, fd);
    if (fd >= 0) {
        /* never expected: say so and touch nothing */
        api->fs_close(fd);
        put_text(&l, " NOT REFUSED");
    }
    emit(api, &l);
}

int32_t nlr_app_main(const struct nlr_api *api)
{
    if (!api || api->abi < 9 || !api->fs_dir_names)
        return 1;
    struct line l;
    l.at = 0;
    char folder[96];
    folder[0] = 0;
    for (uint32_t i = 0; i < 4u; i++) {
        struct nlr_dirent e;
        int r = api->fs_dir("/DCIM", i, &e);
        put_text(&l, "dir /DCIM ");
        put_dec(&l, (int32_t)i);
        put_text(&l, " ");
        put_dec(&l, r);
        if (r == 0) {
            put_text(&l, " ");
            put_text(&l, e.name);
            put_text(&l, " ");
            put_dec(&l, (int32_t)e.flags);
            if ((e.flags & NLR_DIRENT_DIR) && !folder[0])
                copy(folder, sizeof folder, "/DCIM/", e.name);
        }
        emit(api, &l);
        if (r != 0)
            break;
    }
    if (folder[0]) {
        names(api, folder);
        char list[NLR_DIRENT_NAME_MAX];
        if (api->fs_dir_names(folder, list, 1) == 1) {
            char file[160];
            copy(file, sizeof file, folder, "/");
            copy(file, sizeof file, file, list);
            char data[24];
            int fd = api->fs_open(file, NLR_OPEN_READ);
            int got = fd >= 0 ? api->fs_read(fd, data, sizeof data - 1u) : fd;
            if (fd >= 0)
                api->fs_close(fd);
            put_text(&l, "read ");
            put_text(&l, file);
            put_text(&l, " ");
            put_dec(&l, got);
            if (got > 0) {
                data[got] = 0;
                for (int k = 0; k < got; k++)
                    if (data[k] < ' ' || data[k] > '~')
                        data[k] = '.';
                put_text(&l, " ");
                put_text(&l, data);
            }
            emit(api, &l);
            try_write(api, file, NLR_OPEN_WRITE);
            try_write(api, file, NLR_OPEN_UPDATE);
        }
        char fresh[160];
        copy(fresh, sizeof fresh, folder, "/NEW.TXT");
        try_write(api, fresh, NLR_OPEN_WRITE);
    }
    try_write(api, "/DCIM/NEW.TXT", NLR_OPEN_WRITE);
    put_text(&l, "open /DCIM ");
    put_dec(&l, api->fs_open("/DCIM", NLR_OPEN_READ));
    emit(api, &l);
    names(api, "APPS");
    put_text(&l, "done");
    emit(api, &l);
    return 0;
}
