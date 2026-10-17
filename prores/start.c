/*
 * PRORES (resource extension, ABI 5).
 *
 * Runs once per boot, as early as NLoader starts its extensions. It opens
 * the three ProRes 422 grades the Z 6III's encoder can produce but its
 * menus never offer -- 422, LT and Proxy -- and gives the camera's own
 * menus the two rows that drive them:
 *
 *   VIDEO RECORDING MENU, after "Video quality (N-RAW)":
 *       Video quality (ProRes)        HQ | 422 | LT | PROXY
 *
 * The choice is stored in a padding byte of the camera's own settings
 * store, so it is saved with the shooting bank and survives a power
 * cycle. Nothing else of the camera is changed: six instructions of the
 * record service, the demuxer and the shooting display, a call in the
 * sample-description builder, and pointers in .data -- every one of them
 * checked against the word this build was compiled for first.
 *
 * It refuses, with the camera left exactly as it was, when the firmware is
 * not the one it knows, when the camera has no block to spare, or when the
 * menu's text slots have no buffers. A refusal returns 0:
 * a failing extension holds NLoader's interruption marker and pauses the
 * next start, which is not what "this firmware is not mine" deserves.
 *
 * No writable static data: an extension that had any would cost a second
 * 2 MiB execution block. Everything the camera reads from this program is
 * in one block from the loader's allocator, which the callbacks find again
 * through the firmware's own .data, and the two settings live in the
 * camera's settings store.
 */
#include "block.h"
#include "codec.h"
#include "menu.h"

#define LOG_PATH "APPS/PRORES.LOG"
#define LINE_MAX 200u

struct line {
    char bytes[LINE_MAX + 1];
    uint32_t length;
};

static void put(struct line *l, const char *text)
{
    while (text && *text && l->length < LINE_MAX)
        l->bytes[l->length++] = *text++;
    l->bytes[l->length] = 0;
}

static void put_int(struct line *l, int32_t value)
{
    char digits[12];
    uint32_t n = 0;
    uint32_t u = value < 0 ? (uint32_t)-value : (uint32_t)value;
    if (value < 0)
        put(l, "-");
    do {
        digits[n++] = (char)('0' + u % 10u);
        u /= 10u;
    } while (u && n < sizeof digits);
    char text[13];
    for (uint32_t i = 0; i < n; i++)
        text[i] = digits[n - 1u - i];
    text[n] = 0;
    put(l, text);
}

/* The record is a courtesy; failing to write it changes nothing. */
static void record(const struct nlr_api *api, const struct line *l)
{
    api->ui_draw_text(0, 0, l->bytes);
    if (!api->fs_write)
        return;
    int fd = api->fs_open(LOG_PATH, NLR_OPEN_UPDATE);
    if (fd >= 0 && api->fs_seek(fd, 0, NLR_SEEK_END) < 0) {
        api->fs_close(fd);
        fd = -1;
    }
    if (fd < 0)
        fd = api->fs_open(LOG_PATH, NLR_OPEN_WRITE);
    if (fd < 0)
        return;
    uint32_t done = 0;
    while (done < l->length) {
        int written = api->fs_write(fd, l->bytes + done, l->length - done);
        if (written <= 0)
            break;
        done += (uint32_t)written;
    }
    api->fs_write(fd, "\n", 1);
    api->fs_close(fd);
}

static int32_t refuse(const struct nlr_api *api, struct line *l,
                      const char *why)
{
    put(l, why);
    put(l, ", camera unchanged");
    record(api, l);
    return 0;
}

int32_t nlr_extension_init(const struct nlr_api *api)
{
    if (!api || api->abi < 5u || !api->code_patch)
        return -NLR_API_ENOSYS;

    struct line l;
    l.length = 0;
    l.bytes[0] = 0;
    put(&l, "prores ");
    put_int(&l, (int32_t)api->time_ms());
    put(&l, " ms: ");

    /* A loader newer than this build: the table only grows at the end, so
     * the calls used here are still the calls they were, but say so. */
    if (api->abi > NLR_ABI_VERSION) {
        put(&l, "loader is ABI ");
        put_int(&l, (int32_t)api->abi);
        return refuse(api, &l, ", rebuild this program");
    }
    if (!codec_fingerprint())
        return refuse(api, &l, "not the firmware this build knows (codec)");
    if (!menu_fingerprint())
        return refuse(api, &l, "not the firmware this build knows (menus)");

    /* The grade the user left there last time, read back before anything
     * is installed: a byte the store carries but no item claims, so a
     * camera that never ran this plugin has 0 there -- 422 HQ, which is
     * what the menus have always meant. */
    uint32_t grade = prores_grade();

    /* The one allocation, taken before anything is installed: it stays for
     * the boot, as this program's code does. */
    void *block = api->mem_alloc(BLOCK_BYTES, 0);
    if (!block)
        return refuse(api, &l, "no memory for the tables");

    int result = codec_install(api, (uint64_t)(uintptr_t)block);
    if (result != 0) {
        /* Some of the six instructions may be in place: the camera is in a
         * state only a restart clears, so this is the one failure worth
         * holding the marker for. */
        put(&l, "codec install failed (");
        put_int(&l, result);
        put(&l, "), POWER THE CAMERA OFF AND ON");
        record(api, &l);
        return 8;
    }
    put(&l, "ProRes 422 HQ/422/LT/Proxy open, grade ");
    put(&l, codec_grade_short(grade));

    if (!menu_slots_ready()) {
        put(&l, "; no text slots, rows left out");
        record(api, &l);
        return 0;
    }
    result = menu_install(api, (uint64_t)(uintptr_t)block);
    if (result != 0) {
        put(&l, "; rows failed (");
        put_int(&l, result);
        put(&l, ")");
        record(api, &l);
        return 0;
    }
    put(&l, "; rows added");

    record(api, &l);
    return 0;
}
