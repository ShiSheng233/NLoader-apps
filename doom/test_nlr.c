#include "nlr.h"
#include "doomgeneric.h"
#include "doomkeys.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int32_t nlr_app_main(const struct nlr_api *);
const struct nlr_api *nlr_libc_api;
pixel_t *DG_ScreenBuffer;
static jmp_buf end_program;
static unsigned char pixels[320u * 200u * 2u], alpha[320u * 200u];
static pixel_t frame[320u * 200u];
static unsigned presents, events, exit_code, delivered;
static unsigned creations, begins, ends, arena_calls;
static char published[64];
static int scenario;
static unsigned char delivered_keys[16], delivered_down[16];
static const struct nlr_key_event input[] = {
    {NLR_KEY_OK, 0}, {NLR_KEY_UP, 1}, {NLR_KEY_UP, 0},
    {NLR_KEY_HALF, 1}, {NLR_KEY_FULL, 1}, {NLR_KEY_HALF, 0}, {NLR_KEY_FULL, 0}
};
_Noreturn void nlr_test_exit(int code)
{
    exit_code = (unsigned)code;
    longjmp(end_program, 1);
}
int nlr_libc_arena(size_t want, size_t floor)
{
    assert(want == 4u << 20 && floor == 5u << 19);
    assert(begins == 1 && presents == 1);
    ++arena_calls;
    return scenario == 3 ? -1 : 0;
}
size_t nlr_libc_arena_size(void) { return 3u << 20; }
static int draw_text(int x, int y, const char *text)
{
    assert(x == 0 && y == 0);
    snprintf(published, sizeof published, "%s", text);
    return 0;
}
static void *allocate(uint32_t n, uint32_t attrs) { assert(!attrs); return malloc(n); }
static void release(void *p) { free(p); }
static int open_file(const char *p, uint32_t m) { (void)p; (void)m; return -5; }
static int read_file(int f, void *p, uint32_t n) { (void)f; (void)p; (void)n; return -5; }
static int seek_file(int f, int64_t o, int w) { (void)f; (void)o; (void)w; return -5; }
static int close_file(int f) { (void)f; return -1; }
static int begin(struct nlr_surface *out)
{
    ++begins;
    if (scenario == 1)
        return -NLR_API_ENOSYS;
    *out = (struct nlr_surface){.format = NLR_SURFACE_Y0Y1UV_A8,
                               .width = 320, .height = 200, .stride = 640,
                               .pixels = pixels, .alpha = alpha, .alpha_stride = 320};
    if (scenario == 5)
        out->format = 99;
    if (scenario == 6)
        out->width = 160;
    return 0;
}
static int end(void) { ++ends; return 0; }
static int present(void)
{
    ++presents;
    return (scenario == 2 && presents == 1) || (scenario == 4 && presents == 2)
        ? -NLR_API_EIO : 0;
}
static int input_poll(struct nlr_key_event *out)
{
    if (events < sizeof input / sizeof input[0]) {
        *out = input[events++];
        return 1;
    }
    if (events++ == sizeof input / sizeof input[0])
        return -NLR_API_EOVERFLOW;
    if (events == sizeof input / sizeof input[0] + 2u)
        return -NLR_API_EBUSY;
    return 0;
}
static uint32_t input_raw(void)
{
    return !events ? NLR_KEY_OK : NLR_KEY_RIGHT;
}
static uint32_t ticks(void) { return 1234; }
static void sleep_ms(uint32_t ms) { assert(ms == 10); }
void doomgeneric_Create(int argc, char **argv)
{
    assert(argc == 12 && !strcmp(argv[2], "GAME.WAD"));
    assert(!strcmp(argv[3], "-mb") && !strcmp(argv[4], "2"));
    assert(!strcmp(argv[5], "-nosound") && !strcmp(argv[6], "-nomusic"));
    assert(!strcmp(argv[7], "-warp") && !strcmp(argv[8], "1") && !strcmp(argv[9], "1"));
    assert(!strcmp(argv[10], "-skill") && !strcmp(argv[11], "2"));
    assert(argv[argc] == NULL);
    ++creations;
    assert(begins == 1 && presents == 1 && arena_calls == 1);
    DG_ScreenBuffer = frame;
    DG_Init();
    assert(begins == 1 && presents == 1);
}
void doomgeneric_Tick(void)
{
    for (unsigned i = 0; i < sizeof frame / sizeof frame[0]; ++i)
        frame[i] = 0x00ff0000u;
    DG_DrawFrame();
    assert(presents == 2);
    assert(pixels[0] == 82 && pixels[1] == 82 && pixels[2] == 90 && pixels[3] == 240);
    for (unsigned i = 0; i < sizeof alpha; ++i)
        assert(alpha[i] == 255);
    for (unsigned i = 0; i < 12; ++i) {
        int pressed;
        unsigned char key;
        if (DG_GetKey(&pressed, &key)) {
            assert(delivered < sizeof delivered_keys);
            delivered_keys[delivered] = key;
            delivered_down[delivered++] = (unsigned char)pressed;
        }
    }
    assert(delivered == 6);
    assert(delivered_keys[0] == KEY_UPARROW && delivered_down[0] == 1);
    assert(delivered_keys[1] == KEY_UPARROW && delivered_down[1] == 0);
    assert(delivered_keys[2] == KEY_FIRE && delivered_down[2] == 1);
    assert(delivered_keys[3] == KEY_FIRE && delivered_down[3] == 0);
    assert(delivered_keys[4] == KEY_RIGHTARROW && delivered_down[4] == 1);
    assert(delivered_keys[5] == KEY_RIGHTARROW && delivered_down[5] == 0);
    assert(DG_GetTicksMs() == 1234);
    DG_SleepMs(10);
    nlr_test_exit(0);
}
int main(void)
{
    struct nlr_api api = {.abi = 3, .mem_alloc = allocate, .mem_free = release,
                         .fs_open = open_file, .fs_read = read_file, .fs_seek = seek_file,
                         .fs_close = close_file, .gfx_begin = begin, .gfx_end = end,
                         .ui_draw_text = draw_text,
                         .ui_present = present, .input_poll = input_poll,
                         .input_raw = input_raw, .time_ms = ticks, .sleep_ms = sleep_ms,
                         .exit = nlr_test_exit};
    assert(nlr_app_main(&api) == 1 && presents == 0);
    api.abi = 4;
    for (scenario = 0; scenario <= 6; ++scenario) {
        presents = events = delivered = exit_code = 0;
        creations = begins = ends = arena_calls = 0;
        published[0] = 0;
        if (!setjmp(end_program)) {
            int result = nlr_app_main(&api);
            assert(scenario == 3 && result == 2);
        } else {
            assert(exit_code == (scenario ? 5u : 0u));
        }
        assert(begins == 1);
        if (scenario == 1) {
            assert(!arena_calls && !creations && !presents && !ends);
            assert(!strcmp(published, "screen begin failed (-38)"));
        } else if (scenario == 2) {
            assert(!arena_calls && !creations && presents == 1 && ends == 1);
            assert(!strcmp(published, "screen present failed"));
        } else if (scenario == 3) {
            assert(arena_calls == 1 && !creations && presents == 1 && ends == 1);
            assert(!strcmp(published, "memory unavailable"));
        } else if (scenario == 5 || scenario == 6) {
            assert(!arena_calls && !creations && !presents && ends == 1);
            assert(!strcmp(published, scenario == 5 ? "screen format unsupported"
                                                    : "screen dimensions too small"));
        } else {
            assert(arena_calls == 1 && creations == 1 && presents == 2);
            assert(ends == (scenario ? 1u : 0u));
        }
    }
    puts("NLoader DOOM adapter: screen-first ordering, distinct failures, release, small-memory arguments, colour and input passed");
}
