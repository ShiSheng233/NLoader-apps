#include "doom_port.h"
#include "doomgeneric.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static uint32_t completed_frames;
static struct nlr_surface screen;
static uint32_t scale, origin_x, origin_y;
static int screen_owned;

static int api_error(int result) { return result < 0 && result >= -4095 ? -result : EIO; }

void nlr_doom_log_bytes(const char *bytes, size_t length)
{
    if (!nlr_doom_api || !nlr_doom_api->log)
        return;
    while (length)
    {
        char line[NLR_API_LOG_MAX];
        size_t part = length < sizeof(line) - 1 ? length : sizeof(line) - 1;
        memcpy(line, bytes, part);
        line[part] = 0;
        nlr_doom_api->log("%s", line);
        bytes += part;
        length -= part;
    }
}

_Noreturn void nlr_doom_fail(int code, const char *reason)
{
    if (nlr_doom_api && nlr_doom_api->ui_draw_text)
        (void)nlr_doom_api->ui_draw_text(0, 0, reason);
    exit(code > 0 ? code : EIO);
}

int nlr_doom_platform_ready(void)
{
    if (!nlr_doom_api || nlr_doom_api->abi < 4u || NLR_ABI_VERSION < 4u ||
        !nlr_doom_api->time_ms || !nlr_doom_api->sleep_ms || !nlr_doom_api->gfx_begin ||
        !nlr_doom_api->ui_present || !nlr_doom_api->gfx_end)
        return -NLR_API_ENOSYS;
    return NLR_DOOM_DIAGNOSTIC_FRAMES ? 0 : nlr_doom_input_start();
}

void DG_Init(void)
{
    completed_frames = 0;
    if (!DG_ScreenBuffer)
        nlr_doom_fail(ENOMEM, "DOOM pixel memory failed");
    screen = (struct nlr_surface){0};
    int result = nlr_doom_api->gfx_begin(&screen);
    if (result != 0)
        nlr_doom_fail(api_error(result), "DOOM frame begin failed");
    screen_owned = 1;
    if (!screen.pixels || !screen.alpha || screen.width < NLR_DOOM_WIDTH ||
        screen.height < NLR_DOOM_HEIGHT || (screen.width & 1u) ||
        screen.width > UINT32_MAX / 2u || screen.stride < screen.width * 2u ||
        screen.alpha_stride < screen.width ||
        screen.height > SIZE_MAX / screen.stride ||
        screen.height > SIZE_MAX / screen.alpha_stride ||
        screen.format != NLR_SURFACE_Y0Y1UV_A8 || screen.reserved)
        nlr_doom_fail(EINVAL, "DOOM surface unsupported");
    scale = screen.width / NLR_DOOM_WIDTH;
    if (scale > screen.height / NLR_DOOM_HEIGHT)
        scale = screen.height / NLR_DOOM_HEIGHT;
    origin_x = ((screen.width - NLR_DOOM_WIDTH * scale) / 2u) & ~1u;
    origin_y = (screen.height - NLR_DOOM_HEIGHT * scale) / 2u;
    /* Native Y0 Y1 U V planes use studio-range BT.601. Keep the letterbox
     * opaque black and leave padding outside the visible rows untouched. */
    for (uint32_t y = 0; y < screen.height; ++y)
    {
        uint8_t *row = screen.pixels + (size_t)y * screen.stride;
        for (uint32_t x = 0; x < screen.width; x += 2u)
        {
            row[2u * x] = row[2u * x + 1u] = 16u;
            row[2u * x + 2u] = row[2u * x + 3u] = 128u;
        }
        memset(screen.alpha + (size_t)y * screen.alpha_stride, 255, screen.width);
    }
}

static void to_yuv(uint32_t pixel, int *y, int *u, int *v)
{
    int r = (pixel >> 16) & 255u, g = (pixel >> 8) & 255u, b = pixel & 255u;
    *y = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
    *u = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
    *v = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
}

void DG_DrawFrame(void)
{
    if (!screen_owned)
        nlr_doom_fail(EINVAL, "DOOM frame not acquired");
    /* ui_present waits for consumption. Retain the surface until shutdown;
     * never treat gfx_end as the ABI 3 per-frame submit operation. */
    uint32_t width = NLR_DOOM_WIDTH * scale;
    for (uint32_t y = 0; y < NLR_DOOM_HEIGHT; ++y)
    {
        const uint32_t *source = DG_ScreenBuffer + y * NLR_DOOM_WIDTH;
        uint8_t *row = screen.pixels + (size_t)(origin_y + y * scale) * screen.stride +
                       origin_x * 2u;
        for (uint32_t x = 0; x < width; x += 2u)
        {
            int y0, u0, v0, y1, u1, v1;
            to_yuv(source[x / scale], &y0, &u0, &v0);
            to_yuv(source[(x + 1u) / scale], &y1, &u1, &v1);
            row[2u * x] = (uint8_t)y0;
            row[2u * x + 1u] = (uint8_t)y1;
            row[2u * x + 2u] = (uint8_t)((u0 + u1) >> 1);
            row[2u * x + 3u] = (uint8_t)((v0 + v1) >> 1);
        }
        for (uint32_t copy = 1; copy < scale; ++copy)
            memcpy(row + (size_t)copy * screen.stride, row, width * 2u);
    }
    int result = nlr_doom_api->ui_present();
    if (result != 0)
        nlr_doom_fail(api_error(result), "DOOM frame submit failed");
    ++completed_frames;
    nlr_doom_observe(completed_frames);
}

uint32_t DG_GetTicksMs(void)
{
    return nlr_doom_api->time_ms();
}

void DG_SleepMs(uint32_t milliseconds)
{
    nlr_doom_api->sleep_ms(milliseconds);
}

int DG_GetKey(int *pressed, unsigned char *key)
{
    if (NLR_DOOM_DIAGNOSTIC_FRAMES)
        return 0;
    int result = nlr_doom_input_get(pressed, key);
    if (result < 0)
        nlr_doom_fail(api_error(result), "DOOM input failed");
    return result;
}
void DG_SetWindowTitle(const char *title) { nlr_doom_log_bytes(title, strlen(title)); }
uint32_t nlr_doom_completed_frames(void) { return completed_frames; }
void nlr_doom_platform_shutdown(void)
{
    if (!screen_owned)
        return;
    /* Clear our access before release. A failed release must not recurse
     * into gfx_end, reuse its planes, or turn normal exit into success. */
    screen_owned = 0;
    screen = (struct nlr_surface){0};
    int result = nlr_doom_api->gfx_end();
    if (result)
        nlr_doom_fail(api_error(result), "DOOM frame release failed");
}
