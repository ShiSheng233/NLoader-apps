/*
 * doomgeneric on NLoader: DOOM as a foreground program on the camera.
 *
 * The five doomgeneric hooks map onto the loader's API table (ABI 4):
 *   DG_DrawFrame    the 320x200 frame, scaled to fit the display surface
 *                   (gfx_begin), shown with ui_present;
 *   DG_GetKey       camera button changes turned into DOOM key events;
 *   DG_SleepMs      api->sleep_ms -- the program task outranks the camera's
 *                   GUI, so waiting must sleep, never spin;
 *   DG_GetTicksMs   api->time_ms;
 *   DG_SetWindowTitle  nothing to set.
 * The WAD is GAME.WAD in the configured card directory. This generic name
 * lets the engine identify Doom 1/2 from its actual E1M1/MAP01 contents.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doomgeneric.h"
#include "doomkeys.h"
#include "nlr.h"
#include "nlr_libc.h"

#define MAX_SCALE 3u

static const struct nlr_api *api;
static struct nlr_surface screen;
static int have_screen;
static uint32_t origin_x, origin_y;
static uint32_t scale;

static void release_screen(void)
{
    if (have_screen) {
        int result = api->gfx_end();
        if (result == 0)
            have_screen = 0;
        else
            printf("doom: screen release failed (%d)\n", result);
    }
}

static _Noreturn void fail(int code, const char *reason)
{
    printf("doom: %s\n", reason);
    if (api->ui_draw_text)
        api->ui_draw_text(0, 0, reason);
    release_screen();
    exit(code);
}

/* XRGB -> Y, U, V (BT.601, studio swing as the display's matrix expects). */
static inline void to_yuv(uint32_t p, int *y, int *u, int *v)
{
    int r = (p >> 16) & 0xff, g = (p >> 8) & 0xff, b = p & 0xff;

    *y = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
    *u = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
    *v = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
}

void DG_Init(void)
{
    /* Reserve the drawing channel before the engine arena. The engine
     * calls this hook again after Create; reuse the same valid screen. */
    if (have_screen)
        return;
    int begun = api->gfx_begin(&screen);
    if (begun != 0) {
        char reason[40];
        snprintf(reason, sizeof reason, "screen begin failed (%d)", begun);
        fail(5, reason);
    }
    have_screen = 1;
    if (screen.format != NLR_SURFACE_Y0Y1UV_A8)
        fail(5, "screen format unsupported");
    if (!screen.pixels)
        fail(5, "screen pixels unavailable");
    if (screen.width < DOOMGENERIC_RESX || screen.height < DOOMGENERIC_RESY)
        fail(5, "screen dimensions too small");
    if ((screen.width & 1u) || screen.width > UINT32_MAX / 2u)
        fail(5, "screen width unsupported");
    if (screen.stride < screen.width * 2u)
        fail(5, "screen stride unsupported");
    if (screen.alpha && screen.alpha_stride < screen.width)
        fail(5, "alpha stride unsupported");
    scale = screen.width / DOOMGENERIC_RESX;
    if (scale > screen.height / DOOMGENERIC_RESY)
        scale = screen.height / DOOMGENERIC_RESY;
    if (scale > MAX_SCALE)
        scale = MAX_SCALE;
    origin_x = ((screen.width - DOOMGENERIC_RESX * scale) / 2) & ~1u;
    origin_y = (screen.height - DOOMGENERIC_RESY * scale) / 2;
    /* Black, opaque, everywhere: the border around the picture too. */
    for (uint32_t y = 0; y < screen.height; y++) {
        uint8_t *row = screen.pixels + y * screen.stride;

        for (uint32_t x = 0; x < screen.width; x += 2) {
            row[2 * x + 0] = 16;
            row[2 * x + 1] = 16;
            row[2 * x + 2] = 128;
            row[2 * x + 3] = 128;
        }
        if (screen.alpha)
            memset(screen.alpha + y * screen.alpha_stride, 255, screen.width);
    }
    if (api->ui_present() != 0)
        fail(5, "screen present failed");
}

void DG_DrawFrame(void)
{
    if (!have_screen)
        return;
    for (uint32_t sy = 0; sy < DOOMGENERIC_RESY; sy++) {
        const uint32_t *src = DG_ScreenBuffer + sy * DOOMGENERIC_RESX;
        uint8_t *row0 = screen.pixels + (origin_y + sy * scale) * screen.stride +
                        origin_x * 2;

        /* Output pixel pairs: pixel x comes from source x / scale. */
        for (uint32_t ox = 0; ox < DOOMGENERIC_RESX * scale; ox += 2) {
            int y0, u0, v0, y1, u1, v1;

            to_yuv(src[ox / scale], &y0, &u0, &v0);
            to_yuv(src[(ox + 1) / scale], &y1, &u1, &v1);
            row0[ox * 2 + 0] = (uint8_t)y0;
            row0[ox * 2 + 1] = (uint8_t)y1;
            row0[ox * 2 + 2] = (uint8_t)((u0 + u1) >> 1);
            row0[ox * 2 + 3] = (uint8_t)((v0 + v1) >> 1);
        }
        for (uint32_t k = 1; k < scale; k++)
            memcpy(row0 + k * screen.stride, row0, DOOMGENERIC_RESX * scale * 2);
    }
    if (api->ui_present() != 0)
        fail(5, "screen present failed");
}

void DG_SleepMs(uint32_t ms)
{
    api->sleep_ms(ms);
}

uint32_t DG_GetTicksMs(void)
{
    return api->time_ms();
}

/* Camera buttons to DOOM keys. OK both confirms in the menus and opens
 * doors; the shutter fires; MENU is Escape; PLAY answers the "are you
 * sure" prompts (y); i is the automap. */
static const struct {
    uint32_t bit;
    unsigned char keys[2];
} key_map[] = {
    { NLR_KEY_UP, { KEY_UPARROW, 0 } },
    { NLR_KEY_DOWN, { KEY_DOWNARROW, 0 } },
    { NLR_KEY_LEFT, { KEY_LEFTARROW, 0 } },
    { NLR_KEY_RIGHT, { KEY_RIGHTARROW, 0 } },
    { NLR_KEY_OK, { KEY_ENTER, KEY_USE } },
    { NLR_KEY_HALF | NLR_KEY_FULL | NLR_KEY_AF_ON, { KEY_FIRE, 0 } },
    { NLR_KEY_MENU, { KEY_ESCAPE, 0 } },
    { NLR_KEY_PLAY, { 'y', 0 } },
    { NLR_KEY_DELETE, { 'n', 0 } },
    { NLR_KEY_INFO, { KEY_TAB, 0 } },
    { NLR_KEY_ZOOM_IN, { KEY_RSHIFT, 0 } },   /* run */
    { NLR_KEY_ZOOM_OUT, { KEY_STRAFE_L, 0 } },
    { NLR_KEY_DISP, { KEY_STRAFE_R, 0 } },
};

static uint32_t held;
static uint32_t raw_held, startup_held;
static unsigned char queue_key[32];
static unsigned char queue_down[32];
static unsigned queue_head, queue_tail;

static void queue(unsigned char key, int down)
{
    if (queue_tail - queue_head < sizeof queue_key) {
        queue_key[queue_tail % sizeof queue_key] = key;
        queue_down[queue_tail % sizeof queue_key] = (unsigned char)down;
        queue_tail++;
    }
}

static void reconcile(uint32_t now)
{
    now &= ~startup_held;
    /* Several camera buttons fire. Releasing one must not release the
     * engine's key while another physical fire button remains held. */
    for (unsigned i = 0; i < sizeof key_map / sizeof key_map[0]; i++) {
        if (!!(now & key_map[i].bit) != !!(held & key_map[i].bit))
            for (int k = 0; k < 2 && key_map[i].keys[k]; k++)
                queue(key_map[i].keys[k], !!(now & key_map[i].bit));
    }
    held = now;
}

int DG_GetKey(int *pressed, unsigned char *doomKey)
{
    if (queue_head == queue_tail) {
        struct nlr_key_event event;
        int result = api->input_poll(&event);
        if (result == -NLR_API_EOVERFLOW) {
            raw_held = api->input_raw();
            startup_held &= raw_held;
            reconcile(raw_held);
        } else if (result == -NLR_API_EBUSY) {
            raw_held = startup_held = 0;
            reconcile(0);
        } else if (result < 0) {
            fail(6, "input unavailable");
        } else if (result == 1) {
            if (!event.key || (event.key & (event.key - 1u)) || event.down > 1u)
                fail(6, "invalid input event");
            startup_held &= ~event.key;
            if (event.down)
                raw_held |= event.key;
            else
                raw_held &= ~event.key;
            reconcile(raw_held);
        }
    }
    if (queue_head == queue_tail)
        return 0;
    *doomKey = queue_key[queue_head % sizeof queue_key];
    *pressed = queue_down[queue_head % sizeof queue_key];
    queue_head++;
    return 1;
}

void DG_SetWindowTitle(const char *title)
{
    (void)title;
}

int32_t nlr_app_main(const struct nlr_api *table)
{
    /* Match the measured small engine zone; the remaining arena stores the
     * frame and engine bookkeeping without requesting a large stock heap. */
    static char *argv[] = { "doom", "-iwad", "GAME.WAD", "-mb", "2",
                            "-nosound", "-nomusic", "-warp", "1", "1",
                            "-skill", "2", 0 };

    if (!table || table->abi < 4 || !table->mem_alloc || !table->mem_free ||
        !table->fs_open || !table->fs_read || !table->fs_seek || !table->fs_close ||
        !table->gfx_begin || !table->gfx_end || !table->ui_present ||
        !table->input_poll || !table->input_raw || !table->time_ms ||
        !table->sleep_ms || !table->exit)
        return 1;
    api = table;
    nlr_libc_api = table;
    have_screen = 0;
    DG_Init();
    raw_held = startup_held = api->input_raw();
    held = queue_head = queue_tail = 0;
    if (nlr_libc_arena(4u << 20, 5u << 19) != 0) {
        printf("doom: memory unavailable\n");
        if (api->ui_draw_text)
            api->ui_draw_text(0, 0, "memory unavailable");
        release_screen();
        return 2;
    }
    printf("doom: arena %u KiB\n", (unsigned)(nlr_libc_arena_size() >> 10));
    doomgeneric_Create((int)(sizeof argv / sizeof argv[0]) - 1, argv);
    for (;;)
        doomgeneric_Tick();
}
