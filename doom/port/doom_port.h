#ifndef NLR_DOOM_PORT_H
#define NLR_DOOM_PORT_H
#include "nlr.h"
#include <stddef.h>
#include <stdint.h>
#include "observation.h"

#pragma GCC visibility push(hidden)
#define NLR_DOOM_WIDTH 320u
#define NLR_DOOM_HEIGHT 200u
#define NLR_DOOM_ALLOCATIONS 16u
#define NLR_DOOM_FILES 16u
/* The normal program is interactive. The separately named render build
 * selects a fixed frame count and deliberately never consumes game input. */
#ifndef NLR_DOOM_DIAGNOSTIC_FRAMES
#define NLR_DOOM_DIAGNOSTIC_FRAMES 0u
#endif

extern const struct nlr_api *nlr_doom_api;
void nlr_doom_close_files(void);
void nlr_doom_free_allocations(void);
void nlr_doom_log_bytes(const char *bytes, size_t length);
_Noreturn void nlr_doom_fail(int code, const char *reason);
int nlr_doom_platform_ready(void);
void nlr_doom_platform_shutdown(void);
uint32_t nlr_doom_completed_frames(void);
int nlr_doom_input_start(void);
int nlr_doom_input_get(int *pressed, unsigned char *key);
#endif
