#include "doom_port.h"
#include "doomstat.h"

volatile struct nlr_doom_observation nlr_doom_observation = {
    .magic = NLR_DOOM_OBSERVATION_MAGIC,
    .version = 1,
    .bytes = sizeof(struct nlr_doom_observation)
};

void nlr_doom_observe(uint32_t frames)
{
    volatile struct nlr_doom_observation *out = &nlr_doom_observation;
    out->sequence++;
    __asm__ volatile("" ::: "memory");
    struct nlr_doom_input_state input;
    nlr_doom_input_snapshot(&input);
    out->input = input;
    out->frames = frames;
    out->gametic = gametic;
    out->game_state = gamestate;
    const player_t *player = &players[consoleplayer];
    out->player_valid = player->mo != 0;
    out->x = player->mo ? player->mo->x : 0;
    out->y = player->mo ? player->mo->y : 0;
    out->angle = player->mo ? player->mo->angle : 0;
    out->health = player->health;
    out->weapon = player->readyweapon;
    out->ammo_clip = player->ammo[am_clip];
    out->ammo_shell = player->ammo[am_shell];
    out->ammo_cell = player->ammo[am_cell];
    out->ammo_missile = player->ammo[am_misl];
    __asm__ volatile("" ::: "memory");
    out->sequence++;
}
