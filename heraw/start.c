/*
 * HERAW (resource extension, ABI 7).
 *
 * Runs once per boot, as early as NLoader starts its extensions. It gives
 * the camera's own menu five more options,
 *
 *   PHOTO SHOOTING MENU > Image quality settings > Image quality (HSFC):
 *       JPEG fine | JPEG normal | HE RAW 3.0 bpp | ... | HE RAW 7.0 bpp
 *
 * and with an HE RAW row chosen, every high-speed frame (C15/C30/C60/C120,
 * pre-release included) goes to the card as a NEF of the ring's own HE
 * RAW, kept at that many bits per pixel, written by the camera's own writer
 * under the camera's own name, instead of its JPEG (hsfc.c, nef.c). The
 * choice is a padding byte of the camera's settings store, saved with the
 * shooting bank. See README.md.
 *
 * It refuses, with the camera left exactly as it was, when the firmware is
 * not the one it knows, when the camera has no block to spare, or when a
 * row's text slot has no buffer. A refusal returns 0: a failing extension
 * holds NLoader's interruption marker and pauses the next start, which is
 * not what "this firmware is not mine" deserves.
 *
 * No writable static data: an extension that had any would cost a second
 * 2 MiB execution block. Everything the camera reads from this program is
 * in one block from the loader's allocator, which the callbacks find again
 * through the firmware's own .data.
 */
#include "hsfc.h"
#include "menu.h"

#define LOG_PATH "APPS/HERAW.LOG"
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
    if (!api || api->abi < 7u || !api->code_hook || !api->code_unhook ||
        !api->task_start)
        return -NLR_API_ENOSYS;

    struct line l;
    l.length = 0;
    l.bytes[0] = 0;
    put(&l, "heraw ");
    put_int(&l, (int32_t)api->time_ms());
    put(&l, " ms: ");

    /* A loader newer than this build: the table only grows at the end, so
     * the calls used here are still the calls they were, but say so. */
    if (api->abi > NLR_ABI_VERSION) {
        put(&l, "loader is ABI ");
        put_int(&l, (int32_t)api->abi);
        return refuse(api, &l, ", rebuild this program");
    }
    if (!menu_fingerprint())
        return refuse(api, &l, "not the firmware this build knows (menu)");
    if (!hsfc_fingerprint())
        return refuse(api, &l, "not the firmware this build knows (hooks)");
    if (!menu_slots_ready())
        return refuse(api, &l, "no text slot for the rows");

    /* The one allocation, taken before anything is installed: it stays for
     * the boot, as this program's code does. */
    void *block = api->mem_alloc(BLOCK_BYTES, 0);
    if (!block)
        return refuse(api, &l, "no memory");

    /* The menu first: the hooks find the block through its page record. */
    menu_install((uint64_t)(uintptr_t)block);
    int result = hsfc_install(api, (uint64_t)(uintptr_t)block);
    if (result != 0) {
        /* Any hook installed before the failure is dormant and has been
         * restored where possible. Remove the custom rows as well. */
        int32_t rollback = hsfc_rollback_status();
        int menu_result = menu_uninstall();
        put(&l, "HE RAW install failed (");
        put_int(&l, result);
        put(&l, "), rollback ");
        put_int(&l, rollback);
        put(&l, ", menu ");
        put_int(&l, menu_result);
        record(api, &l);
        return result;
    }
    put(&l, "HE RAW rows added, ");
    if (he_raw_on()) {
        put(&l, "tier ");
        put_int(&l, (int32_t)he_raw_tier());
    } else {
        put(&l, "off");
    }
    if (hsfc_tests()) {
        put(&l, "; HERAW.CFG ");
        put_int(&l, (int32_t)hsfc_tests());
    }
    record(api, &l);
    return 0;
}
