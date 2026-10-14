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
static unsigned alloc_live, alloc_peak, file_live, file_peak, begin_calls, end_calls, present_calls;
static size_t bytes_live, bytes_peak;
static struct
{
    void *p;
    size_t bytes;
} allocations[16];
static uint8_t pixels[320 * 200 * 2], alpha[320 * 200];
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
        present_calls<input_script[event_index].frame) return 0;
    event->key=input_script[event_index].key;
    event->down=input_script[event_index++].pressed;
    if (event->down) held_keys|=event->key; else held_keys&=~event->key;
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
    *s = (struct nlr_surface){.format=NLR_SURFACE_Y0Y1UV_A8,
        .width=320,.height=200,.stride=640,.pixels=pixels,.alpha=alpha,.alpha_stride=320};
    return 0;
}
static int end(void)
{
    ++end_calls;
    assert(lease);
    if (mode == 4)
        return -5;
    lease = 0;
    return 0;
}
static int present(void)
{
    ++present_calls;
    assert(lease && !end_calls);
    if (mode == 2)
        return -5;
    for (size_t i = 0; i < sizeof(alpha); ++i)
        assert(alpha[i] == 255);
    if (mode==3) {
        assert(present_calls<400); /* A broken natural Quit must fail, not hang. */
        assert(players[consoleplayer].mo);
        if (present_calls==100) {
            start_x=players[consoleplayer].mo->x;
            start_y=players[consoleplayer].mo->y;
            start_ammo=players[consoleplayer].ammo[am_clip];
        }
        if (present_calls==126)
            moved=players[consoleplayer].mo->x!=start_x || players[consoleplayer].mo->y!=start_y;
        if (present_calls==170)
            fired=players[consoleplayer].ammo[am_clip]<start_ammo;
    }
    return 0;
}
static uint32_t time_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)((uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u);
}
static void sleep_ms(uint32_t ms)
{
    assert(ms <= 1000);
    struct timespec t = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&t, NULL);
}
int main(int argc, char **argv)
{
    assert(argc == 4);
    wad_path = argv[1];
    mode = atoi(argv[3]);
    struct nlr_api api = {.abi = 4,
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
                          .ui_present = present,
                          .time_ms = time_ms,
                          .sleep_ms = sleep_ms};
    api.input_poll=input_poll;
    api.input_raw=input_raw;
    if (mode==5) api.abi=3;
    if (!setjmp(exit_target))
        program_exit = nlr_app_main(&api);
    assert(!alloc_live && !file_live && !bytes_live);
    if (!mode || mode==3)
    {
        assert(!program_exit && present_calls >= 90 && !lease);
        assert(begin_calls==1 && end_calls==1);
        if (mode==3) assert(present_calls>=192 && moved && fired);
        assert(nlr_doom_observation.magic==NLR_DOOM_OBSERVATION_MAGIC);
        assert(nlr_doom_observation.bytes==sizeof nlr_doom_observation);
        assert(!(nlr_doom_observation.sequence&1u));
        assert(nlr_doom_observation.frames==present_calls);
        assert(nlr_doom_observation.player_valid);
        if (mode==3) assert(nlr_doom_observation.input.presses>=5 &&
                           nlr_doom_observation.input.releases>=5);
        FILE *f = fopen(argv[2], "wb");
        assert(f);
        assert(fwrite(pixels, 1, sizeof(pixels), f) == sizeof(pixels));
        assert(fwrite(alpha, 1, sizeof(alpha), f) == sizeof(alpha));
        fclose(f);
    }
    else if (mode==5)
    {
        assert(program_exit==NLR_API_ENOSYS);
        assert(!begin_calls && !present_calls && !end_calls && !lease);
    }
    else
    {
        assert(program_exit == (mode == 1 ? 38 : 5));
        assert(begin_calls == 1 && end_calls == (mode == 1 ? 0 : 1));
        assert(present_calls==(mode==1 ? 0u : mode==4 ? 90u : 1u));
        assert(lease==(mode==4));
    }
    printf("\nPORT_RESULT code=%d allocations_peak=%u bytes_peak=%zu files_peak=%u begin=%u present=%u end=%u "
           "active_lease=%d reason=%s\n",
           program_exit, alloc_peak, bytes_peak, file_peak, begin_calls, present_calls, end_calls, lease,
           last_error);
    return 0;
}
