/* abi8test: the firmware by name (ABI 8), checked end to end on the camera
 * or the emulator. Started at boot (NLOADER/BOOT), it
 *   - asks what "gui.palette", "store.notify", "menu.leave" and a missing
 *     name are,
 *   - writes one palette entry, reads it back, and puts the table back,
 *   - hooks "store.notify", writes white balance through prop_set (which
 *     the settings service broadcasts), and counts the broadcasts its
 *     handler saw before it called the original,
 * and writes each result to APPS/ABI8LOG.TXT. It needs nothing but the API
 * table: no address of the firmware appears here. */
#include "nlr.h"

static const struct nlr_api *api;
static int log_fd = -1;

static void say(const char *text)
{
    uint32_t n = 0;
    while (text[n])
        n++;
    if (log_fd >= 0)
        api->fs_write(log_fd, text, n);
}

static void say_num(const char *label, int64_t value)
{
    char buf[48];
    uint32_t n = 0;
    while (label[n] && n < 24) {
        buf[n] = label[n];
        n++;
    }
    buf[n++] = ' ';
    uint64_t v = value < 0 ? (uint64_t)(-value) : (uint64_t)value;
    if (value < 0)
        buf[n++] = '-';
    char digits[24];
    uint32_t d = 0;
    do {
        digits[d++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v && d < sizeof digits);
    while (d)
        buf[n++] = digits[--d];
    buf[n++] = '\n';
    buf[n] = 0;
    say(buf);
}

/* the hook: count, then the camera's own function with every register
 * argument passed on as it came */
typedef uint64_t (*fn8)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                        uint64_t, uint64_t);
static volatile uint32_t broadcasts;
static fn8 original_changed;
static uint64_t changed(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
                        uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7)
{
    broadcasts++;
    /* on the firmware's task: post, return */
    struct nlr_event ev = { .code = 42, .value = (int32_t)a0 };
    api->queue_post(&ev);
    return original_changed(a0, a1, a2, a3, a4, a5, a6, a7);
}

int32_t nlr_app_main(const struct nlr_api *table)
{
    api = table;
    if (!api || api->abi < 8)
        return 1;
    log_fd = api->fs_open("APPS/ABI8LOG.TXT", NLR_OPEN_WRITE);
    say("abi8test\n");
    struct nlr_named n;
    int32_t r = api->named("gui.palette", &n);
    say_num("palette named", r);
    say_num("palette kind", n.kind);
    say_num("palette class", n.flags & 0xff);
    say_num("palette stock", (n.flags & NLR_NAMED_STOCK) != 0);
    say_num("palette size", n.size);
    say_num("missing named", api->named("no.such.name", &n));
    r = api->named("store.notify", &n);
    say_num("point named", r);
    say_num("point args", n.size);
    say_num("menu.leave found", api->fw_fn("menu.leave") != 0);

    uint32_t entry = 0, back = 0, now = 0x1766e3ffu;
    say_num("read 62", api->table_read("gui.palette", 62 * 4, &entry, 4));
    say_num("entry 62", entry);
    say_num("write 62", api->table_write("gui.palette", 62 * 4, &now, 4));
    say_num("read back", api->table_read("gui.palette", 62 * 4, &back, 4));
    say_num("back ok", back == now);
    r = api->named("gui.palette", &n);
    say_num("changed flag", (n.flags & NLR_NAMED_CHANGED) != 0);
    say_num("stock after write", (n.flags & NLR_NAMED_STOCK) != 0);
    say_num("restore", api->table_restore("gui.palette"));
    api->table_read("gui.palette", 62 * 4, &back, 4);
    say_num("restored ok", back == entry);
    say_num("write missing", api->table_write("no.such.name", 0, &now, 4));

    void *original = 0;
    r = api->hook_attach("store.notify", (void *)changed, &original);
    say_num("hook", r);
    if (!r && original) {
        original_changed = (fn8)original;
        uint32_t wb = 3;
        say_num("prop_set wb", api->prop_set(0x07000510u, &wb, 4));
        struct nlr_event got = {0};
        int32_t w = api->queue_wait(&got, 1000);
        while (w == 1 && got.type != NLR_EV_POSTED)
            w = api->queue_wait(&got, 1000);
        say_num("posted seen", w == 1 && got.type == NLR_EV_POSTED && got.code == 42);
        say_num("broadcasts", broadcasts);
        say_num("unhook", api->hook_detach("store.notify"));
        say_num("unhook again", api->hook_detach("store.notify"));
        wb = 0;
        api->prop_set(0x07000510u, &wb, 4);
    }
    /* the body IMU's WHO_AM_I (register 0x75) through the device's name */
    uint8_t reg = 0x75, who = 0;
    say_num("imu i2c", api->bus_i2c("i2c:imu", &reg, 1, &who, 1));
    say_num("imu who_am_i", who);
    say_num("no device", api->bus_i2c("i2c:none", &reg, 1, &who, 1));
    /* a RAM table by name, left changed on purpose: the loader must put
     * the stock bytes back when this foreground program ends */
    uint16_t row0 = 0, mark = 0x1234, back16 = 0;
    say_num("lut read", api->table_read("isp.lut1.nlog", 0, &row0, 2));
    say_num("lut row0", row0);
    say_num("lut write", api->table_write("isp.lut1.nlog", 0, &mark, 2));
    api->table_read("isp.lut1.nlog", 0, &back16, 2);
    say_num("lut now", back16);
    say_num("lut repack", api->table_write("codec.h265.modes", 0, &mark, 2));
    say("done\n");
    if (log_fd >= 0)
        api->fs_close(log_fd);
    api->sleep_ms(1500);
    return 0;
}
