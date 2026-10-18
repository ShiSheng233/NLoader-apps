/* BOOTDEMO: the boot program of the Z 8 QEMU fixture tests/qemu/boot_start.py
 * (NLOADER/BOOT names it). It does nothing but say, on the card, that its
 * entry ran and how long it kept running, appending to APPS/BOOTLOG.TXT:
 *
 *   start <abi>
 *   alive <seconds>      every two seconds
 *   done
 *
 * and returns 0 after BOOTDEMO_SECONDS -- past the loader's ten seconds
 * (NLR_BOOT_GOOD_MS), so the boot record's OK is written at one of its
 * waits while it still runs, and the normal end follows. Each line is
 * written and closed on its own, so the log on the card is complete up to
 * the moment the session stops. It takes neither the screen nor the
 * buttons; MENU+DELETE still stops it.
 *
 * State lives on the stack: a program with no writable static data gets
 * one memory region instead of two. */
#include "nlr.h"

#define BOOTDEMO_SECONDS 16u

static void put_dec(char *out, uint32_t *at, uint32_t value)
{
    char digits[12];
    uint32_t n = 0;
    do {
        digits[n++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value);
    while (n)
        out[(*at)++] = digits[--n];
}

static void put_text(char *out, uint32_t *at, const char *text)
{
    while (*text)
        out[(*at)++] = *text++;
}

static void log_line(const struct nlr_api *api, const char *text, int with_value,
                     uint32_t value)
{
    char line[32];
    uint32_t at = 0;
    put_text(line, &at, text);
    if (with_value) {
        line[at++] = ' ';
        put_dec(line, &at, value);
    }
    line[at++] = '\n';
    int fd = api->fs_open("APPS/BOOTLOG.TXT", NLR_OPEN_UPDATE);
    if (fd < 0)
        fd = api->fs_open("APPS/BOOTLOG.TXT", NLR_OPEN_WRITE);
    if (fd < 0)
        return;
    api->fs_seek(fd, 0, NLR_SEEK_END);
    api->fs_write(fd, line, at);
    api->fs_close(fd);
}

int32_t nlr_app_main(const struct nlr_api *api)
{
    if (!api || api->abi < 5u)
        return 0;
    log_line(api, "start", 1, api->abi);
    for (uint32_t seconds = 2; seconds <= BOOTDEMO_SECONDS; seconds += 2) {
        api->sleep_ms(2000);
        log_line(api, "alive", 1, seconds);
    }
    log_line(api, "done", 0, 0);
    return 0;
}
