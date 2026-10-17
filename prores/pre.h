#ifndef PRE_H
#define PRE_H

#include "z63fw.h"

/* Pre-release capture, step one: the read-only export.
 *
 * The HSFC ring holds every pre-release frame as HE-compressed 14-bit RAW,
 * and the camera throws that payload away once the frame's JPEG is made.
 * Writing a NEF from it in the camera needs two things this build does not
 * have yet: proof that the payload is byte-compatible with the Z 6III's own
 * HE NEF, and the offsets of the black level, the CFA pattern and the shot
 * number inside the frame's metadata block.
 *
 * Both come from one file. With the switch on, the next pre-release frame
 * the camera is about to release is written to the card as it is -- the
 * metadata block, the ring descriptor and the payload -- and then released
 * exactly as it always was. Nothing is retained, so the ring cannot starve;
 * the only cost is one card write inside one frame's release, once per
 * power-on.
 *
 * Pair the file with the first JPEG of the same burst: the NEF is built
 * offline from the JPEG's Exif and MakerNote plus this payload, and if
 * that decodes, the in-camera writer can be built with confidence.
 */

/* The three instructions of the release are the ones this build knows, and
 * the trampoline's gap is free: 1, or 0 with nothing written. */
int pre_fingerprint(void);

/* Put the trampoline in the gap and branch the release's first
 * instruction at it. 0, or the loader's error. */
int pre_install(const struct nlr_api *api, uint64_t block);

#endif /* PRE_H */
