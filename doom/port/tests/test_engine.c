#include "nlr.h"
#include "doomstat.h"
#include "../observation.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static unsigned alloc_live, alloc_peak, file_live, file_peak, begin_calls, end_calls;
static size_t bytes_live, bytes_peak;
static struct
{
    void *p;
    size_t bytes;
} allocations[16];
static uint8_t pixels[320 * 200 * 4];
static int lease, mode, program_exit;
static char last_error[80];
static const char *wad_path;
static jmp_buf exit_target;
static uint32_t held_keys;
static unsigned event_index;
static int32_t start_x,start_y;
static int start_ammo, moved, fired;
static const struct { unsigned frame; uint32_t key, pressed; } input_script[]={
    {100,NLR_KEY_UP,1},{125,NLR_KEY_UP,0},
    {130,NLR_KEY_AF_ON,1},{165,NLR_KEY_AF_ON,0},
    {180,NLR_KEY_DISP,1},{181,NLR_KEY_DISP,0},
    {184,NLR_KEY_UP,1},{185,NLR_KEY_UP,0},
    {188,NLR_KEY_OK,1},{189,NLR_KEY_OK,0},
    {192,NLR_KEY_OK,1},{193,NLR_KEY_OK,0}
};
static int input_poll(struct nlr_key_event *event)
{
    if (mode!=3 || event_index==sizeof input_script/sizeof input_script[0] ||
        end_calls<input_script[event_index].frame) return 0;
    event->key=input_script[event_index].key;
    event->pressed=input_script[event_index++].pressed;
    if (event->pressed) held_keys|=event->key; else held_keys&=~event->key;
    return 1;
}
static uint32_t input_raw(void) { return held_keys; }
static void *alloc(uint32_t n, uint32_t attr)
{
    assert(!attr);
    for (unsigned i = 0; i < 16; i++)
        if (!allocations[i].p)
        {
            void *p = malloc(n);
            if (!p)
                return NULL;
            allocations[i].p = p;
            allocations[i].bytes = n;
            ++alloc_live;
            bytes_live += n;
            if (alloc_live > alloc_peak)
                alloc_peak = alloc_live;
            if (bytes_live > bytes_peak)
                bytes_peak = bytes_live;
            return p;
        }
    return NULL;
}
static void release(void *p)
{
    for (unsigned i = 0; i < 16; i++)
        if (allocations[i].p == p)
        {
            --alloc_live;
            bytes_live -= allocations[i].bytes;
            allocations[i].p = NULL;
            free(p);
            return;
        }
    assert(!"unknown free");
}
static int open_file(const char *path, uint32_t mode_)
{
    assert(!mode_);
    if (strcmp(path, "GAME.WAD"))
        return -5;
    int fd = open(wad_path, O_RDONLY);
    if (fd >= 0)
    {
        ++file_live;
        if (file_live > file_peak)
            file_peak = file_live;
    }
    return fd;
}
static int close_file(int fd)
{
    int r = close(fd);
    if (!r)
        --file_live;
    return r;
}
static int read_file(int fd, void *p, uint32_t n)
{
    if (n > 4096)
        n = 4096;
    ssize_t r = read(fd, p, n);
    return r < 0 ? -5 : (int)r;
}
static int seek_file(int fd, int64_t off, int whence)
{
    assert(whence == 0 || whence == 2);
    off_t r = lseek(fd, off, whence == 0 ? SEEK_SET : SEEK_END);
    return r < 0 || r > INT_MAX ? -5 : (int)r;
}
static int text(int x, int y, const char *s)
{
    (void)x;
    (void)y;
    snprintf(last_error, sizeof(last_error), "%s", s);
    return 0;
}
static void logger(const char *format, ...)
{
    va_list v;
    va_start(v, format);
    vprintf(format, v);
    va_end(v);
}
static void leave(int code)
{
    program_exit = code;
    longjmp(exit_target, 1);
}
static int begin(struct nlr_surface *s)
{
    ++begin_calls;
    if (mode == 1)
        return -38;
    assert(!lease);
    lease = 1;
    *s = (struct nlr_surface){pixels, 320, 200, 1280, NLR_SURFACE_RGBA8888};
    return 0;
}
static int end(void)
{
    ++end_calls;
    assert(lease);
    if (mode == 2)
        return -5;
    for (size_t i = 3; i < sizeof(pixels); i += 4)
        assert(pixels[i] == 255);
    lease = 0;
    if (mode==3) {
        assert(end_calls<400); /* A broken natural Quit must fail, not hang. */
        assert(players[consoleplayer].mo);
        if (end_calls==100) {
            start_x=players[consoleplayer].mo->x;
            start_y=players[consoleplayer].mo->y;
            start_ammo=players[consoleplayer].ammo[am_clip];
        }
        if (end_calls==126)
            moved=players[consoleplayer].mo->x!=start_x || players[consoleplayer].mo->y!=start_y;
        if (end_calls==170)
            fired=players[consoleplayer].ammo[am_clip]<start_ammo;
    }
    return 0;
}
static int clock_ms(uint64_t *out)
{
    struct timespec t;
    assert(out);
    clock_gettime(CLOCK_MONOTONIC, &t);
    *out = (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u;
    return 0;
}
static int sleep_ms(uint32_t ms)
{
    assert(ms <= 1000);
    struct timespec t = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&t, NULL);
    return 0;
}
int main(int argc, char **argv)
{
    assert(argc == 4);
    wad_path = argv[1];
    mode = atoi(argv[3]);
    struct nlr_api api = {.abi = 3,
                          .mem_alloc = alloc,
                          .mem_free = release,
                          .fs_open = open_file,
                          .fs_read = read_file,
                          .fs_seek = seek_file,
                          .fs_close = close_file,
                          .ui_draw_text = text,
                          .log = logger,
                          .exit = leave,
                          .gfx_begin = begin,
                          .gfx_end = end,
                          .clock_ms = clock_ms,
                          .sleep_ms = sleep_ms};
    api.input_poll=input_poll;
    api.input_raw=input_raw;
    if (mode==3) api.abi=4; /* Declared minimum ABI, compatible table prefix. */
    if (!setjmp(exit_target))
        program_exit = nlr_app_main(&api);
    assert(!alloc_live && !file_live && !bytes_live);
    if (!mode || mode==3)
    {
        assert(!program_exit && end_calls >= 90 && !lease);
        if (mode==3) assert(end_calls>=192 && moved && fired);
        assert(nlr_doom_observation.magic==NLR_DOOM_OBSERVATION_MAGIC);
        assert(nlr_doom_observation.bytes==sizeof nlr_doom_observation);
        assert(!(nlr_doom_observation.sequence&1u));
        assert(nlr_doom_observation.frames==end_calls);
        assert(nlr_doom_observation.player_valid);
        if (mode==3) assert(nlr_doom_observation.input.presses>=5 &&
                           nlr_doom_observation.input.releases>=5);
        FILE *f = fopen(argv[2], "wb");
        assert(f);
        assert(fwrite(pixels, 1, sizeof(pixels), f) == sizeof(pixels));
        fclose(f);
    }
    else
    {
        assert(program_exit == (mode == 1 ? 38 : 5));
        assert(begin_calls == 1 && end_calls == (mode == 1 ? 0 : 1));
    }
    printf("\nPORT_RESULT code=%d allocations_peak=%u bytes_peak=%zu files_peak=%u begin=%u end=%u "
           "active_lease=%d reason=%s\n",
           program_exit, alloc_peak, bytes_peak, file_peak, begin_calls, end_calls, lease,
           last_error);
    return 0;
}
