/* abi8test: the firmware by name (ABI 8), checked end to end on the camera
 * or the emulator. Started at boot (NLOADER/BOOT) or opened from Apps, it
 *   - asks what "gui.palette", "store.notify", "menu.leave", "i2c:imu",
 *     "isp.lut1.nlog", "codec.h265.modes" and a missing name are,
 *   - writes one palette entry, reads it back, and puts the table back,
 *   - hooks "store.notify", writes a setting through prop_set (which the
 *     settings service broadcasts), and counts the broadcasts its handler
 *     saw before it called the original,
 *   - talks to the body IMU by name, writes the N-Log row of Lut1 and a
 *     repack-only table,
 * and writes each result to APPS/ABI8LOG.TXT.
 *
 * The bodies differ in what their profiles declare: the Z 6III 2.00 has
 * every name above, its Lut1 rows in RAM; the Z 8 3.11 has no store.notify,
 * no i2c:imu and no codec.h265.modes, and its Lut1 rows are in flash
 * (class repack). So each call's expected answer is taken from what
 * named() says about the name on this body, never from the body itself:
 * a missing name must be refused, a repack table must answer -EACCES and
 * keep its bytes, a RAM or pointer table must take the write. Every check
 * is logged as "ok" or "FAIL", and the last line counts the failures.
 *
 * The setting the hook test writes is body-specific (a store item id,
 * not an address); build.sh passes it for the body the declaration names
 * (ABI8_NOTIFY_ID, the tagged id, and ABI8_NOTIFY_VALUE / _RESET). With
 * ABI8_NOTIFY_ID 0 the hook is attached and detached but nothing is
 * written. No address of the firmware appears here. */
#include "nlr.h"

#ifndef ABI8_NOTIFY_ID
#define ABI8_NOTIFY_ID 0u
#endif
#ifndef ABI8_NOTIFY_VALUE
#define ABI8_NOTIFY_VALUE 0u
#endif
#ifndef ABI8_NOTIFY_RESET
#define ABI8_NOTIFY_RESET 0u
#endif

static const struct nlr_api *api;
static int log_fd = -1;
static uint32_t failures;

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

/* One judged line: "check <label> ok" or "check <label> FAIL". */
static void check(const char *label, int good)
{
    say("check ");
    say(label);
    say(good ? " ok\n" : " FAIL\n");
    if (!good)
        failures++;
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

/* The GUI palette (a pointer table where declared): write one entry, read
 * it back, restore the table; logs and judges each step. */
static void palette_round_trip(void)
{
    struct nlr_named n;
    int32_t r = api->named("gui.palette", &n);
    say_num("palette named", r);
    if (r) {
        uint32_t v = 0;
        check("palette missing: write refused",
              api->table_write("gui.palette", 0, &v, 4) == -(int32_t)NLR_API_ENOENT);
        return;
    }
    say_num("palette kind", n.kind);
    say_num("palette class", n.flags & 0xff);
    say_num("palette stock", (n.flags & NLR_NAMED_STOCK) != 0);
    say_num("palette size", n.size);
    check("palette is a table", n.kind == NLR_NAMED_TABLE);
    check("palette holds stock bytes", (n.flags & NLR_NAMED_STOCK) != 0);
    /* entry 62: any entry would do; it is in both bodies' tables */
    uint32_t entry = 0, back = 0, now = 0x1766e3ffu;
    say_num("read 62", api->table_read("gui.palette", 62 * 4, &entry, 4));
    say_num("entry 62", entry);
    if ((n.flags & 0xffu) == NLR_TABLE_REPACK) {
        check("palette repack: write refused",
              api->table_write("gui.palette", 62 * 4, &now, 4) == -(int32_t)NLR_API_EACCES);
        return;
    }
    r = api->table_write("gui.palette", 62 * 4, &now, 4);
    say_num("write 62", r);
    check("palette write", r == 0);
    say_num("read back", api->table_read("gui.palette", 62 * 4, &back, 4));
    check("palette read back", back == now);
    api->named("gui.palette", &n);
    check("palette changed flag", (n.flags & NLR_NAMED_CHANGED) != 0);
    check("palette not stock after write", !(n.flags & NLR_NAMED_STOCK));
    r = api->table_restore("gui.palette");
    say_num("restore", r);
    check("palette restore", r == 0);
    back = 0;
    api->table_read("gui.palette", 62 * 4, &back, 4);
    check("palette restored entry", back == entry);
    api->named("gui.palette", &n);
    check("palette stock after restore", (n.flags & NLR_NAMED_STOCK) != 0);
}

static void hook_test(void)
{
    struct nlr_named n;
    int32_t missing = api->named("store.notify", &n);
    say_num("point named", missing);
    if (!missing)
        say_num("point args", n.size);
    void *original = 0;
    int32_t r = api->hook_attach("store.notify", (void *)changed, &original);
    say_num("hook", r);
    if (missing) {
        check("missing point: hook refused", r == -(int32_t)NLR_API_ENOENT);
        check("missing point: unhook refused", api->hook_detach("store.notify") < 0);
        return;
    }
    check("hook attach", r == 0 && original);
    if (r || !original)
        return;
    original_changed = (fn8)original;
    if (ABI8_NOTIFY_ID) {
        uint32_t value = ABI8_NOTIFY_VALUE;
        say_num("prop_set", api->prop_set(ABI8_NOTIFY_ID, &value, 4));
        struct nlr_event got = {0};
        int32_t w = api->queue_wait(&got, 1000);
        while (w == 1 && got.type != NLR_EV_POSTED)
            w = api->queue_wait(&got, 1000);
        check("posted seen", w == 1 && got.type == NLR_EV_POSTED && got.code == 42);
        say_num("broadcasts", broadcasts);
        check("broadcasts counted", broadcasts > 0);
    } else {
        say("no setting named for this body: nothing written\n");
    }
    check("unhook", api->hook_detach("store.notify") == 0);
    check("unhook again refused", api->hook_detach("store.notify") != 0);
    if (ABI8_NOTIFY_ID) {
        uint32_t value = ABI8_NOTIFY_RESET;
        api->prop_set(ABI8_NOTIFY_ID, &value, 4);
    }
}

static void function_test(void)
{
    struct nlr_named n;
    int32_t missing = api->named("menu.leave", &n);
    int found = api->fw_fn("menu.leave") != 0;
    say_num("menu.leave named", missing);
    say_num("menu.leave found", found);
    check("menu.leave as named says", missing ? !found : (found && n.kind == NLR_NAMED_FUNCTION));
}

/* the body IMU's WHO_AM_I (register 0x75) through the device's name */
static void device_test(void)
{
    struct nlr_named n;
    int32_t missing = api->named("i2c:imu", &n);
    uint8_t reg = 0x75, who = 0;
    int32_t r = api->bus_i2c("i2c:imu", &reg, 1, &who, 1);
    say_num("imu named", missing);
    say_num("imu i2c", r);
    if (missing) {
        check("missing device refused", r < 0);
    } else {
        check("imu transfer", r == 0);
        say_num("imu who_am_i", who);
    }
    check("no device refused", api->bus_i2c("i2c:none", &reg, 1, &who, 1) < 0);
}

/* A table by name: what a write must do follows from its class. A
 * foreground program's RAM writes are left in place on purpose: the
 * loader must put the stock bytes back when the program ends. */
static void table_test(const char *name, const char *label)
{
    struct nlr_named n;
    uint16_t row0 = 0, mark = 0x1234, back16 = 0;
    int32_t missing = api->named(name, &n);
    say(label);
    say_num(" named", missing);
    int32_t w;
    if (missing) {
        w = api->table_write(name, 0, &mark, 2);
        say_num("write", w);
        check("missing table: write refused", w == -(int32_t)NLR_API_ENOENT);
        return;
    }
    uint32_t cls = n.flags & 0xffu;
    say_num("class", cls);
    say_num("size", n.size);
    say_num("read", api->table_read(name, 0, &row0, 2));
    say_num("row0", row0);
    w = api->table_write(name, 0, &mark, 2);
    say_num("write", w);
    api->table_read(name, 0, &back16, 2);
    say_num("now", back16);
    if (cls == NLR_TABLE_REPACK) {
        check("repack table: write refused with -EACCES", w == -(int32_t)NLR_API_EACCES);
        check("repack table: bytes kept", back16 == row0);
    } else {
        check("table write", w == 0);
        check("table read back", back16 == mark);
    }
}

int32_t nlr_app_main(const struct nlr_api *table)
{
    api = table;
    if (!api || api->abi < 8)
        return 1;
    log_fd = api->fs_open("APPS/ABI8LOG.TXT", NLR_OPEN_WRITE);
    say("abi8test\n");
    say_num("abi", api->abi);
    struct nlr_named n;
    check("missing name refused",
          api->named("no.such.name", &n) == -(int32_t)NLR_API_ENOENT);
    uint32_t v = 0;
    check("write missing refused",
          api->table_write("no.such.name", 0, &v, 4) == -(int32_t)NLR_API_ENOENT);
    palette_round_trip();
    hook_test();
    function_test();
    device_test();
    table_test("isp.lut1.nlog", "lut");
    table_test("codec.h265.modes", "h265");
    say_num("failures", failures);
    say(failures ? "FAIL\n" : "PASS\n");
    if (log_fd >= 0)
        api->fs_close(log_fd);
    api->sleep_ms(1500);
    return failures ? 2 : 0;
}
