/*
 * doomgeneric on NLoader: DOOM as a foreground program on the camera.
 *
 * The five doomgeneric hooks map onto the loader's API table (ABI 3):
 *   DG_DrawFrame    the 320x200 frame, scaled 3x into the display surface
 *                   (gfx_begin), shown with ui_present;
 *   DG_GetKey       camera buttons (input_raw) turned into DOOM key events;
 *   DG_SleepMs      api->sleep_ms -- the program task outranks the camera's
 *                   GUI, so waiting must sleep, never spin;
 *   DG_GetTicksMs   api->time_ms;
 *   DG_SetWindowTitle  nothing to set.
 * The WAD is DOOM.WAD in the loader's card directory (A:\NLOADER\ in QEMU).
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "doomgeneric.h"
#include "doomkeys.h"
#include "nlr.h"
#include "nlr_libc.h"

#define SCALE 3

static const struct nlr_api *api;
static struct nlr_surface screen;
static int have_screen;
static uint32_t origin_x, origin_y;

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
    if (api->gfx_begin(&screen) == 0 && screen.format == NLR_SURFACE_Y0Y1UV_A8 &&
        screen.width >= DOOMGENERIC_RESX * SCALE &&
        screen.height >= DOOMGENERIC_RESY * SCALE) {
        have_screen = 1;
        origin_x = ((screen.width - DOOMGENERIC_RESX * SCALE) / 2) & ~1u;
        origin_y = (screen.height - DOOMGENERIC_RESY * SCALE) / 2;
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
        api->ui_present();
    } else {
        printf("doom: no screen (gfx_begin)\n");
    }
}

void DG_DrawFrame(void)
{
    if (!have_screen)
        return;
    for (uint32_t sy = 0; sy < DOOMGENERIC_RESY; sy++) {
        const uint32_t *src = DG_ScreenBuffer + sy * DOOMGENERIC_RESX;
        uint8_t *row0 = screen.pixels + (origin_y + sy * SCALE) * screen.stride +
                        origin_x * 2;

        /* Output pixel pairs: pixel x comes from source x / SCALE. */
        for (uint32_t ox = 0; ox < DOOMGENERIC_RESX * SCALE; ox += 2) {
            int y0, u0, v0, y1, u1, v1;

            to_yuv(src[ox / SCALE], &y0, &u0, &v0);
            to_yuv(src[(ox + 1) / SCALE], &y1, &u1, &v1);
            row0[ox * 2 + 0] = (uint8_t)y0;
            row0[ox * 2 + 1] = (uint8_t)y1;
            row0[ox * 2 + 2] = (uint8_t)((u0 + u1) >> 1);
            row0[ox * 2 + 3] = (uint8_t)((v0 + v1) >> 1);
        }
        for (int k = 1; k < SCALE; k++)
            memcpy(row0 + k * screen.stride, row0, DOOMGENERIC_RESX * SCALE * 2);
    }
    api->ui_present();
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
    { NLR_KEY_HALF, { KEY_FIRE, 0 } },
    { NLR_KEY_FULL, { KEY_FIRE, 0 } },
    { NLR_KEY_AF_ON, { KEY_FIRE, 0 } },
    { NLR_KEY_MENU, { KEY_ESCAPE, 0 } },
    { NLR_KEY_PLAY, { 'y', 0 } },
    { NLR_KEY_DELETE, { 'n', 0 } },
    { NLR_KEY_INFO, { KEY_TAB, 0 } },
    { NLR_KEY_ZOOM_IN, { KEY_RSHIFT, 0 } },   /* run */
    { NLR_KEY_ZOOM_OUT, { KEY_STRAFE_L, 0 } },
    { NLR_KEY_DISP, { KEY_STRAFE_R, 0 } },
};

static uint32_t held;
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

int DG_GetKey(int *pressed, unsigned char *doomKey)
{
    if (queue_head == queue_tail) {
        uint32_t now = api->input_raw(), changed = now ^ held;

        for (unsigned i = 0; changed && i < sizeof key_map / sizeof key_map[0]; i++) {
            if (!(changed & key_map[i].bit))
                continue;
            for (int k = 0; k < 2 && key_map[i].keys[k]; k++)
                queue(key_map[i].keys[k], !!(now & key_map[i].bit));
        }
        held = now;
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
    static char *argv[] = { "doom", "-iwad", "DOOM.WAD", 0 };

    if (!table || table->abi < 3)
        return 1;
    api = table;
    nlr_libc_api = table;
    if (nlr_libc_arena(24u << 20, 8u << 20) != 0)
        return 2;
    printf("doom: arena %u KiB\n", (unsigned)(nlr_libc_arena_size() >> 10));
    doomgeneric_Create(3, argv);
    for (;;)
        doomgeneric_Tick();
}
