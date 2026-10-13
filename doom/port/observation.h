#ifndef NLR_DOOM_OBSERVATION_H
#define NLR_DOOM_OBSERVATION_H
#include <stdint.h>

#define NLR_DOOM_OBSERVATION_MAGIC UINT64_C(0x4e4c444f4f4d3031)

struct nlr_doom_input_state {
    uint32_t events, presses, releases, overflows, focus_losses;
    uint32_t held, delivered, last_key, last_pressed;
};

/* A read-only observer accepts an even, unchanged sequence and the exact
 * magic/version/bytes. Values come from the same worker after a drawn frame;
 * they are evidence about real engine state, never simulated game actions. */
struct nlr_doom_observation {
    uint64_t magic;
    uint32_t version, bytes, sequence, frames;
    struct nlr_doom_input_state input;
    int32_t gametic, game_state;
    uint32_t player_valid;
    int32_t x, y;
    uint32_t angle;
    int32_t health, weapon, ammo_clip, ammo_shell, ammo_cell, ammo_missile;
};

extern volatile struct nlr_doom_observation nlr_doom_observation;
void nlr_doom_observe(uint32_t frames);
void nlr_doom_input_snapshot(struct nlr_doom_input_state *out);
#endif
