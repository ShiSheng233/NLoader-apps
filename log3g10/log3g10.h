#ifndef LOG3G10_H
#define LOG3G10_H
/*
 * Log3G10 in the Z 6III's N-Log slot: what the settings page (page.c) and
 * the start-up extension (start.c) share. Z 6III 2.00 only.
 *
 * Video N-Log is not a circuit of its own. The R2Y block of the image
 * pipeline turns scene-linear RGB into the recorded code through one
 * 229-point table, Lut1, and the job builder copies that table -- once per
 * job -- from templates in initialised data (sub_41121010 ->
 * sub_41126C78): tone mode N-Log copies template rows 3, 4 and 5, one per
 * colour lane. The templates are RAM (the firmware copies them out of its
 * flash image at start), so writing them changes every N-Log job built
 * afterwards, until the camera is switched off.
 *
 * Nikon's ZR is the same pipeline with a Log3G10 tone mode, and in that
 * mode it does exactly this: it fills Lut1 from the Log3G10 formula and
 * keeps N-Log's NlgGamma and gamma-gain rows (ZR 1.11 0x4131b3a0 and
 * 0x41315f90). So these three rows are all that changes; the curve itself
 * comes from the card (APPS/LOG3G10.CRV, made by mkcurve.py).
 */
#include <stdint.h>

#include "nlr.h"

#define L3G_POINTS    229u
#define L3G_LANES     3u
#define L3G_VALUE_MAX 32767u        /* Lut1 outputs are 15 bits */

/* Lut1 rows 3..5 in RAM, each 229 x u16, contiguous. */
/* With a loader that has the firmware by name (ABI 8), the rows are the
 * profile's table "isp.lut1.nlog" (the three rows as one span) and this
 * program holds no address: the loader checks the stock CRC before the
 * first write. With an older loader -- or api null, as the host test
 * passes -- the rows are read and written at L3G_RAM_ROWS. */
#define L3G_TABLE "isp.lut1.nlog"
#ifndef L3G_RAM_ROWS /* a host test points this at its own array */
#define L3G_RAM_ROWS 0xa029e473cUL
#endif
/* CRC-32 of those 1374 bytes as the firmware itself loads them at start
 * (rows 3, 4 and 5 are identical). It is how the programs know the rows
 * are the stock N-Log of the firmware they were built for: the flash
 * original they are copied from is not mapped once the camera runs
 * (0x451f9000.. faults; measured), so there is nothing else to compare
 * with, and nothing to restore from -- the firmware does that itself at
 * the next power-on. */
#define L3G_STOCK_CRC 0xf4b99215u

/* The program's files, beside it in APPS: the switch and the chosen
 * curve, the curves themselves (any NAME.CRV there; LOG3G10.CRV unless
 * another was chosen), and the start-up record. */
#define L3G_DIR          "APPS"
#define L3G_CONFIG_PATH  "APPS/LOG3G10.CFG"
#define L3G_LOG_PATH     "APPS/LOG3G10.LOG"
#define L3G_CURVE_NAME   "LOG3G10.CRV"
#define L3G_NAME_MAX     31u
#define L3G_CURVE_BYTES_MAX 16384u

/* What the three rows hold right now. */
enum l3g_state {
    L3G_STATE_STOCK, /* the firmware's own N-Log, as it loads it */
    L3G_STATE_CURVE, /* the curve given */
    L3G_STATE_OTHER, /* neither: another curve, or another firmware */
};

/* A line of text, built in place and always terminated. */
#define L3G_TEXT_MAX 128u
struct l3g_text {
    char s[L3G_TEXT_MAX];
    uint32_t n;
};
void l3g_clear(struct l3g_text *t);
void l3g_put(struct l3g_text *t, const char *s);
void l3g_put_int(struct l3g_text *t, int32_t v);

/* What the three rows hold now; `curve` may be null. */
enum l3g_state l3g_state(const struct nlr_api *api, const uint16_t *curve);
/* Write the curve into all three rows and read it back: 0 or
 * -NLR_API_EIO. Only ever called on stock rows. */
int l3g_write(const struct nlr_api *api, const uint16_t *curve);
/* The curve at 18% grey (grid level 400), as a 10-bit code. */
uint32_t l3g_grey(const uint16_t *curve);

/* A curve file's name as the program accepts it: a plain name in APPS of
 * at most L3G_NAME_MAX characters ending in .CRV (any case), nothing that
 * leads elsewhere. */
int l3g_curve_name_ok(const char *name);
/* Read APPS/<name>: 0, or a negative error with the reason in `why` (the
 * file's line number where there is one). */
int l3g_load_curve(const struct nlr_api *api, const char *name,
                   uint16_t *curve, struct l3g_text *why);

/* What the page saves: the switch and the chosen curve. */
struct l3g_config {
    int enabled;
    char curve[L3G_NAME_MAX + 1];
};
/* The saved settings. A missing or unreadable file, or a line that does
 * not parse, leaves its default: off, LOG3G10.CRV. */
void l3g_config_read(const struct nlr_api *api, struct l3g_config *c);
/* Save them and read them back: 0 or a negative error. */
int l3g_config_save(const struct nlr_api *api, const struct l3g_config *c);
/* Create or replace a file with `len` bytes: 0 or a negative error. */
int l3g_write_file(const struct nlr_api *api, const char *path,
                   const char *text, uint32_t len);

#endif
