#include "doom_port.h"
#include "doomkeys.h"

/* One engine key per physical key keeps held-state recovery unambiguous. */
static const unsigned char engine_keys[12] = {
    KEY_UPARROW, KEY_DOWNARROW, KEY_LEFTARROW, KEY_RIGHTARROW,
    KEY_ENTER, KEY_PAUSE, KEY_FIRE, KEY_ESCAPE, KEY_RSHIFT, KEY_RALT, ']', '['
};

struct queued_key { uint32_t bit; unsigned char key, pressed; };
static struct queued_key pending[12];
static uint32_t held, delivered, startup_held, pending_count, pending_next;
static struct nlr_doom_input_state observed;

#define DOOM_INPUT_MASK ((UINT32_C(1) << 12) - 1u)
#define DOOM_POLL_BUDGET 32u

static void reconcile(void)
{
    uint32_t wanted = held & ~startup_held;
    pending_count = pending_next = 0;
    /* Release first, including modifiers, before delivering new presses. */
    for (uint32_t pressed = 0; pressed <= 1; ++pressed)
        for (uint32_t n = 0; n < 12; ++n)
        {
            uint32_t bit = UINT32_C(1) << n;
            if (((wanted ^ delivered) & bit) && !!(wanted & bit) == pressed)
                pending[pending_count++] = (struct queued_key){bit, engine_keys[n],
                                                               (unsigned char)pressed};
        }
}

static int pump(void)
{
    for (uint32_t n = 0; n < DOOM_POLL_BUDGET; ++n)
    {
        struct nlr_key_event event = {0};
        int result = nlr_doom_api->input_poll(&event);
        if (result == -NLR_API_EOVERFLOW)
        {
            observed.overflows++;
            held = nlr_doom_api->input_raw() & DOOM_INPUT_MASK;
            startup_held &= held;
            reconcile();
            return 0;
        }
        if (result == -NLR_API_EBUSY)
        {
            observed.focus_losses++;
            held = startup_held = 0;
            reconcile();
            return 0;
        }
        if (result <= 0)
            return result;
        if (result != 1 || !event.key || (event.key & (event.key - 1u)) ||
            event.pressed > 1u)
            return -NLR_API_EINVAL;
        /* Reserved/unknown buttons can never become engine keys. */
        if (!(event.key & DOOM_INPUT_MASK))
            continue;
        observed.events++;
        if (event.pressed) observed.presses++; else observed.releases++;
        observed.last_key = event.key;
        observed.last_pressed = event.pressed;
        startup_held &= ~event.key;
        if (event.pressed)
            held |= event.key;
        else
            held &= ~event.key;
        reconcile();
        if (pending_count)
            return 0;
    }
    return 0;
}

int nlr_doom_input_start(void)
{
    if (!nlr_doom_api || !nlr_doom_api->input_poll || !nlr_doom_api->input_raw)
        return -NLR_API_ENOSYS;
    held = nlr_doom_api->input_raw() & DOOM_INPUT_MASK;
    observed = (struct nlr_doom_input_state){0};
    /* The loader seeds raw state but suppresses the launch key's down edge.
     * Do not resurrect that edge during a later overflow recovery. */
    startup_held = held;
    delivered = pending_count = pending_next = 0;
    return pump();
}

int nlr_doom_input_get(int *pressed, unsigned char *key)
{
    if (!pressed || !key)
        return -NLR_API_EINVAL;
    if (pending_next == pending_count)
    {
        int result = pump();
        if (result)
            return result;
    }
    if (pending_next == pending_count)
        return 0;
    struct queued_key event = pending[pending_next++];
    if (event.pressed)
        delivered |= event.bit;
    else
        delivered &= ~event.bit;
    *pressed = event.pressed;
    *key = event.key;
    return 1;
}

void nlr_doom_input_snapshot(struct nlr_doom_input_state *out)
{
    *out = observed;
    out->held = held;
    out->delivered = delivered;
}
