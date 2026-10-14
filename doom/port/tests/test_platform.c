#include "nlr.h"
#include "doomgeneric.h"
#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern int nlr_doom_platform_ready(void);
extern void nlr_doom_platform_shutdown(void);
extern uint32_t nlr_doom_completed_frames(void);
const struct nlr_api *nlr_doom_api;
static uint32_t source[320u * 200u];
pixel_t *DG_ScreenBuffer = source;
static uint8_t pixels[(964u * 2u + 8u) * 604u];
static uint8_t alpha[(964u + 8u) * 604u];
static struct nlr_surface supplied;
static unsigned begins, presents, ends, sleeps, observed;
static uint32_t tick, last_sleep;
static int lease, begin_error, present_error, end_error, exit_code;
static jmp_buf exit_target;

int nlr_doom_input_start(void) { return 0; }
int nlr_doom_input_get(int *pressed, unsigned char *key)
{ (void)pressed; (void)key; return 0; }
void nlr_doom_observe(uint32_t frames) { observed = frames; }
static int begin(struct nlr_surface *out)
{
    ++begins;
    if (begin_error) return begin_error;
    assert(!lease);
    lease = 1;
    *out = supplied;
    return 0;
}
static int present(void)
{
    assert(lease && !ends);
    ++presents;
    return present_error;
}
static int end(void)
{
    assert(lease);
    ++ends;
    if (!end_error) lease = 0;
    return end_error;
}
static uint32_t time_ms(void) { return tick; }
static void sleep_ms(uint32_t ms) { ++sleeps; last_sleep = ms; }
static void leave(int code) { exit_code = code; longjmp(exit_target, 1); }
static const struct nlr_api api = {.abi=4,.gfx_begin=begin,.ui_present=present,
    .gfx_end=end,.time_ms=time_ms,.sleep_ms=sleep_ms,.exit=leave};

static void reset(uint32_t width, uint32_t height)
{
    begins=presents=ends=sleeps=observed=0;
    begin_error=present_error=end_error=exit_code=lease=0;
    tick=last_sleep=0;
    memset(source, 0, sizeof source);
    memset(pixels, 0xa5, sizeof pixels);
    memset(alpha, 0xa5, sizeof alpha);
    supplied=(struct nlr_surface){.format=NLR_SURFACE_Y0Y1UV_A8,
        .width=width,.height=height,.stride=width*2u+8u,.pixels=pixels,
        .alpha=alpha,.alpha_stride=width+8u};
    nlr_doom_api=&api;
    assert(nlr_doom_platform_ready()==0);
}
static void padding_and_alpha(void)
{
    for (uint32_t y=0;y<supplied.height;++y)
    {
        const uint8_t *row=pixels+(size_t)y*supplied.stride;
        const uint8_t *a=alpha+(size_t)y*supplied.alpha_stride;
        for (uint32_t x=0;x<supplied.width;++x) assert(a[x]==255u);
        for (uint32_t x=supplied.width*2u;x<supplied.stride;++x) assert(row[x]==0xa5u);
        for (uint32_t x=supplied.width;x<supplied.alpha_stride;++x) assert(a[x]==0xa5u);
    }
}
static void native_planes(void)
{
    reset(320,200);
    source[0]=0xffffffu; source[1]=0u;
    source[2]=source[3]=0xff0000u;
    source[4]=0x00ff00u; source[5]=0x0000ffu;
    DG_Init();
    assert(begins==1 && !presents && !ends && lease);
    const uint8_t black_pair[]={16,16,128,128};
    assert(!memcmp(pixels,black_pair,sizeof black_pair));
    padding_and_alpha();
    DG_DrawFrame();
    /* Independent BT.601 values: white/black, red/red, green/blue. */
    const uint8_t color_pairs[]={235,16,128,128,82,82,90,240,144,41,147,72};
    assert(!memcmp(pixels,color_pairs,sizeof color_pairs));
    assert(presents==1 && begins==1 && !ends && lease && observed==1);
    DG_DrawFrame();
    assert(presents==2 && begins==1 && !ends && observed==2);
    assert(nlr_doom_completed_frames()==2);
    padding_and_alpha();
    tick=UINT32_MAX;
    assert(DG_GetTicksMs()==UINT32_MAX);
    tick=0;
    assert(DG_GetTicksMs()==0);
    DG_SleepMs(0); assert(sleeps==1 && !last_sleep);
    DG_SleepMs(1000); assert(sleeps==2 && last_sleep==1000);
    nlr_doom_platform_shutdown();
    nlr_doom_platform_shutdown();
    assert(ends==1 && !lease);
}
static void scaled_planes(void)
{
    reset(964,604);
    source[0]=0xffffffu;
    DG_Init();
    DG_DrawFrame();
    const uint8_t expected[]={235,235,128,128,235,16,128,128,16,16,128,128};
    const uint8_t black_pair[]={16,16,128,128};
    const uint8_t *row=pixels+2u*supplied.stride+4u;
    assert(!memcmp(row,expected,sizeof expected));
    assert(!memcmp(row+supplied.stride,expected,sizeof expected));
    assert(!memcmp(row+2u*supplied.stride,expected,sizeof expected));
    assert(!memcmp(pixels,black_pair,sizeof black_pair));
    assert(!memcmp(row-4u,black_pair,sizeof black_pair));
    assert(!memcmp(row+960u*2u,black_pair,sizeof black_pair));
    padding_and_alpha();
    nlr_doom_platform_shutdown();
    assert(ends==1 && !lease);
}
static void rejected_surfaces(void)
{
    for (unsigned n=0;n<7;++n)
    {
        reset(320,200);
        switch (n)
        {
            case 0: supplied.width=319; break;
            case 1: supplied.height=199; break;
            case 2: supplied.stride=639; break;
            case 3: supplied.alpha_stride=319; break;
            case 4: supplied.alpha=0; break;
            case 5: supplied.format=2; break;
            default: supplied.reserved=1; break;
        }
        if (!setjmp(exit_target)) { DG_Init(); assert(!"unsupported surface accepted"); }
        assert(exit_code==NLR_API_EINVAL && !presents && ends==1 && !lease);
    }
}
static void failures_and_services(void)
{
    reset(320,200);
    begin_error=-NLR_API_ENOSYS;
    if (!setjmp(exit_target)) { DG_Init(); assert(!"begin failure accepted"); }
    assert(exit_code==NLR_API_ENOSYS && begins==1 && !ends && !lease);
    reset(320,200);
    DG_Init();
    present_error=-NLR_API_EIO;
    if (!setjmp(exit_target)) { DG_DrawFrame(); assert(!"present failure accepted"); }
    assert(exit_code==NLR_API_EIO && presents==1 && ends==1 && !lease && !observed);
    reset(320,200);
    DG_Init();
    end_error=-NLR_API_EIO;
    if (!setjmp(exit_target)) { nlr_doom_platform_shutdown(); assert(!"release failure accepted"); }
    assert(exit_code==NLR_API_EIO && ends==1 && lease);
    nlr_doom_platform_shutdown();
    assert(ends==1); /* Uncertain ownership must never be reused or released twice. */
    struct nlr_api missing=api;
    missing.abi=3;
    nlr_doom_api=&missing;
    assert(nlr_doom_platform_ready()==-NLR_API_ENOSYS);
    missing=api; missing.ui_present=0;
    assert(nlr_doom_platform_ready()==-NLR_API_ENOSYS);
    missing=api; missing.time_ms=0;
    assert(nlr_doom_platform_ready()==-NLR_API_ENOSYS);
    missing=api; missing.sleep_ms=0;
    assert(nlr_doom_platform_ready()==-NLR_API_ENOSYS);
}
int main(void)
{
    native_planes(); scaled_planes(); rejected_surfaces(); failures_and_services();
    puts("DOOM_PLATFORM ABI4 planes, colors, stride, scaling, clock wrap, presentation and release passed");
}
