#ifndef NEF_H
#define NEF_H

#include "z63fw.h"

/* The NEF this plugin writes for a pre-release frame.
 *
 * The pixels are the frame's own TicoRAW payload, left where the ring put
 * it: byte for byte what the camera's own high-efficiency NEF carries as
 * its strip. The metadata is the camera's too: the Exif block of the same
 * frame's JPEG, MakerNote and thumbnail included, copied whole to the
 * start of the file so that no offset inside it changes.
 *
 * What a JPEG's block lacks and a NEF needs is added (../nef-writer-notes.md,
 * each found on the body):
 *
 *   - 0xC7D5 in the raw SubIFD, Nikon's per-shot raw parameters; without
 *     it the strip does not decode at all;
 *   - the black level (0x003D) and the crop (0x0045) in the MakerNote;
 *     without the black level the picture comes out grey and pink;
 *   - the MakerNote's PreviewIFD (0x0011), naming the JPEG's own 640 x 424
 *     MPF image; the camera's playback shows a NEF from it, and from
 *     nothing else we tried (not a 1620 x 1080 SubIFD).
 *
 * The MakerNote gets a new directory of ours, and the copy's one word that
 * names it is changed. The file:
 *
 *   head     the copied block, our directories, padding    (ours)
 *   preview  the 640 x 424 JPEG                             (the caller's)
 *   payload  the frame                                      (the ring)
 *
 * The strip starts on 512.
 */
struct nef_source {
    uint64_t payload;      /* the TicoRAW stream, as the ring holds it */
    uint32_t payload_bytes;
    uint32_t width;        /* the sensor readout the stream describes */
    uint32_t height;
    uint16_t black[4];     /* M+0x2b4 */
    uint32_t cfa;          /* 0 = RGGB, the only pattern written */
    uint64_t jpeg;         /* the same frame's JPEG, in the camera's memory */
    uint32_t jpeg_bytes;
    uint32_t preview_bytes; /* the preview the caller writes, 0 for none */
};

/* The file, as pieces to be written in order; an empty piece is skipped. */
struct nef_plan {
    uint64_t head;
    uint32_t head_bytes;
    uint32_t preview_at;    /* file offset; the caller supplies the bytes */
    uint32_t preview_bytes; /* 0: no preview in this file */
    uint64_t payload;
    uint32_t payload_bytes;
};

/* Build the head into `out` (at most `cap` bytes) and describe the file
 * in `plan`. 0: with the camera's Exif and the MakerNote extended (and the
 * preview, if one was offered); 2: with the Exif but a MakerNote this could
 * not extend; 1: a plain header, the JPEG having no Exif block; negative:
 * no payload, or `cap` too small. */
int nef_build(uint64_t out, uint32_t cap, const struct nef_source *src,
              struct nef_plan *plan);

#endif /* NEF_H */
