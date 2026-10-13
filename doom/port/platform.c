#include "doom_port.h"
#include "doomgeneric.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static uint32_t completed_frames;

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
#if NLR_ABI_VERSION >= 3 && defined(NLR_SURFACE_RGBA8888)
    if (!nlr_doom_api->clock_ms || !nlr_doom_api->sleep_ms || !nlr_doom_api->gfx_begin ||
        !nlr_doom_api->gfx_end)
        return -NLR_API_ENOSYS;
    uint64_t now;
    int result = nlr_doom_api->clock_ms(&now);
    if (!result && !NLR_DOOM_DIAGNOSTIC_FRAMES)
        result = nlr_doom_input_start();
    return result;
#else
    return -NLR_API_ENOSYS;
#endif
}

void DG_Init(void)
{
    completed_frames = 0;
    if (!DG_ScreenBuffer)
        nlr_doom_fail(ENOMEM, "DOOM pixel memory failed");
}

void DG_DrawFrame(void)
{
#if NLR_ABI_VERSION >= 3 && defined(NLR_SURFACE_RGBA8888)
    struct nlr_surface surface = {0};
    int result = nlr_doom_api->gfx_begin(&surface);
    if (result != 0)
        nlr_doom_fail(api_error(result), "DOOM frame begin failed");
    if (!surface.pixels || surface.width != NLR_DOOM_WIDTH || surface.height != NLR_DOOM_HEIGHT ||
        surface.stride_bytes != NLR_DOOM_WIDTH * 4u || surface.format != NLR_SURFACE_RGBA8888)
        nlr_doom_fail(EINVAL, "DOOM surface unsupported");
    /* The renderer owns its ARGB integer buffer. Publish a copy into the
     * current loader lease, so it cannot reuse bytes still read by the GUI. */
    unsigned char *destination = surface.pixels;
    for (uint32_t i = 0; i < NLR_DOOM_WIDTH * NLR_DOOM_HEIGHT; ++i)
    {
        uint32_t pixel = DG_ScreenBuffer[i];
        destination[4u * i] = (unsigned char)(pixel >> 16);
        destination[4u * i + 1u] = (unsigned char)(pixel >> 8);
        destination[4u * i + 2u] = (unsigned char)pixel;
        destination[4u * i + 3u] = 255u;
    }
    result = nlr_doom_api->gfx_end();
    if (result != 0)
        nlr_doom_fail(api_error(result), "DOOM frame submit failed");
    ++completed_frames;
    nlr_doom_observe(completed_frames);
#else
    nlr_doom_fail(ENOSYS, "DOOM graphics unsupported");
#endif
}

uint32_t DG_GetTicksMs(void)
{
#if NLR_ABI_VERSION >= 3
    uint64_t now = 0;
    int result = nlr_doom_api->clock_ms(&now);
    if (result != 0)
        nlr_doom_fail(api_error(result), "DOOM clock failed");
    return (uint32_t)now;
#else
    nlr_doom_fail(ENOSYS, "DOOM clock unsupported");
#endif
}

void DG_SleepMs(uint32_t milliseconds)
{
#if NLR_ABI_VERSION >= 3
    int result = nlr_doom_api->sleep_ms(milliseconds);
    if (result != 0)
        nlr_doom_fail(api_error(result), "DOOM wait failed");
#else
    (void)milliseconds;
    nlr_doom_fail(ENOSYS, "DOOM wait unsupported");
#endif
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
void nlr_doom_platform_shutdown(void) { /* No frame lease is reused or resubmitted on exit. */ }
