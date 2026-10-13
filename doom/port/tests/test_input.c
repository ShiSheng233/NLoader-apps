#include "../doom_port.h"
#include "doomkeys.h"
#include <assert.h>
#include <stdio.h>

const struct nlr_api *nlr_doom_api;
static struct nlr_key_event events[64];
static unsigned read_at, write_at, polls;
static uint32_t raw;
static int error, repeat;

static int poll_key(struct nlr_key_event *out)
{
    ++polls;
    if (error) { int result=error; error=0; return result; }
    if (repeat) { *out=(struct nlr_key_event){NLR_KEY_DOWN,1}; return 1; }
    if (read_at==write_at) return 0;
    *out=events[read_at++];
    return 1;
}
static uint32_t raw_keys(void) { return raw; }
static const struct nlr_api api={.abi=3,.input_poll=poll_key,.input_raw=raw_keys};
static void reset(uint32_t seed)
{
    read_at=write_at=polls=0;error=repeat=0;raw=seed;nlr_doom_api=&api;
    assert(nlr_doom_input_start()==0);
}
static void push(uint32_t key,uint32_t pressed)
{
    assert(write_at<64);events[write_at++]=(struct nlr_key_event){key,pressed};
}
static void expect(unsigned char wanted,int down)
{
    int pressed=-1;unsigned char key=0;
    assert(nlr_doom_input_get(&pressed,&key)==1);
    assert(key==wanted && pressed==down);
}
static void empty(void)
{
    int pressed=-1;unsigned char key=0;
    assert(nlr_doom_input_get(&pressed,&key)==0);
}
static void mappings_and_edges(void)
{
    const uint32_t physical[]={NLR_KEY_UP,NLR_KEY_DOWN,NLR_KEY_LEFT,NLR_KEY_RIGHT,
        NLR_KEY_OK,NLR_KEY_PLAYBACK,NLR_KEY_AF_ON,NLR_KEY_DISP,NLR_KEY_FN1,NLR_KEY_FN2,
        NLR_KEY_ZOOM_IN,NLR_KEY_ZOOM_OUT};
    const unsigned char engine[]={KEY_UPARROW,KEY_DOWNARROW,KEY_LEFTARROW,KEY_RIGHTARROW,
        KEY_ENTER,KEY_PAUSE,KEY_FIRE,KEY_ESCAPE,KEY_RSHIFT,KEY_RALT,']','['};
    reset(0);
    for (unsigned n=0;n<12;n++) {
        push(physical[n],1);expect(engine[n],1);
        push(physical[n],1);empty();
        push(physical[n],0);expect(engine[n],0);
        push(physical[n],0);empty();
    }
}
static void launch_and_overflow(void)
{
    reset(NLR_KEY_OK);
    error=-NLR_API_EOVERFLOW;empty();
    push(NLR_KEY_OK,0);empty();
    push(NLR_KEY_OK,1);expect(KEY_ENTER,1);
    push(NLR_KEY_OK,0);expect(KEY_ENTER,0);
    push(NLR_KEY_UP,1);expect(KEY_UPARROW,1);
    push(NLR_KEY_AF_ON,1);expect(KEY_FIRE,1);
    raw=NLR_KEY_RIGHT|NLR_KEY_FN1|NLR_KEY_FN2;error=-NLR_API_EOVERFLOW;
    expect(KEY_UPARROW,0);expect(KEY_FIRE,0);
    expect(KEY_RIGHTARROW,1);expect(KEY_RSHIFT,1);expect(KEY_RALT,1);empty();
    error=-NLR_API_EOVERFLOW;empty(); /* unchanged raw must never toggle */
    raw=0;error=-NLR_API_EOVERFLOW;
    expect(KEY_RIGHTARROW,0);expect(KEY_RSHIFT,0);expect(KEY_RALT,0);empty();
    raw=(1u<<12)-1;error=-NLR_API_EOVERFLOW;
    unsigned seen=0;int pressed;unsigned char key;
    while (nlr_doom_input_get(&pressed,&key)) { assert(pressed==1);seen++;assert(seen<=12); }
    assert(seen==12);
    error=-NLR_API_EBUSY;seen=0;
    while (nlr_doom_input_get(&pressed,&key)) { assert(pressed==0);seen++;assert(seen<=12); }
    assert(seen==12);
}
static void bounds_and_errors(void)
{
    reset(0);
    push(1u<<29,1);push(1u<<30,1);empty();
    push(NLR_KEY_DOWN,1);expect(KEY_DOWNARROW,1);
    unsigned before=polls;repeat=1;empty();assert(polls-before==32);repeat=0;
    int pressed;unsigned char key;
    push(NLR_KEY_UP|NLR_KEY_DOWN,1);
    assert(nlr_doom_input_get(&pressed,&key)==-NLR_API_EINVAL);
    push(NLR_KEY_UP,2);
    assert(nlr_doom_input_get(&pressed,&key)==-NLR_API_EINVAL);
    error=-NLR_API_EIO;assert(nlr_doom_input_get(&pressed,&key)==-NLR_API_EIO);
    assert(nlr_doom_input_get(0,&key)==-NLR_API_EINVAL);
    error=-NLR_API_ENOSYS;assert(nlr_doom_input_start()==-NLR_API_ENOSYS);
}
int main(void)
{
    mappings_and_edges();launch_and_overflow();bounds_and_errors();
    puts("DOOM_INPUT edges, seed, overflow recovery, focus loss and bounds passed");
}
