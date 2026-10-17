/*
 * Pre-release capture, step one: write one frame's HE RAW payload to the
 * card and let the camera go on as if nothing had happened.
 *
 * The splice is a single branch over the first instruction of the ring
 * slot's release (`LDR X0,[X23,#0x6C0]`, with X23 the frame's request
 * block R). It goes to a trampoline in the code window's second gap --
 * a branch reaches that, and not a program block in DRAM -- which saves
 * every register a caller may hold, calls the handler below with R, puts
 * them back, runs the instruction it replaced and returns. So the camera's
 * own code is unchanged in effect: the frame is released as always.
 *
 * The handler writes at most one file per power-on, and only while the
 * d3 switch is on.
 */
#include "pre.h"
#include "block.h"

#define DUMP_PATH "A:\\NLOADER\\APPS\\PREDUMP.BIN"
#define DUMP_MAGIC 0x45525036u /* "6PRE" */
#define R_BYTES    0x800u
#define M_BYTES    0x2600u
#define E_BYTES    0x100u
#define CHUNK      0x40000u /* 256 KiB per write request */

/* What the splice keeps, in the plugin's block: one word is enough, but
 * the file's own header is built from the frame, so the rest records what
 * happened for the log. */
struct pre_state {
    uint32_t done;      /* a file was written (or refused) this power-on */
    uint32_t result;    /* the firmware's error, or 0 */
    uint32_t sequence;  /* the frame's number in the burst */
    uint32_t bytes;     /* the payload length written */
};

/* The file's 64-byte header: everything needed to make sense of the rest
 * without this source at hand. */
struct dump_header {
    uint32_t magic;
    uint32_t version;
    uint64_t r;
    uint64_t e;
    uint64_t m;
    uint64_t payload;
    uint32_t payload_bytes;
    uint32_t width;
    uint32_t height;
    uint32_t sequence;
    uint32_t mode;      /* 3 C30, 4 C120, 5 C60, 6 C15 */
    uint32_t drained;
    uint32_t r_bytes;
    uint32_t m_bytes;
    uint32_t e_bytes;
    uint32_t reserved;
};

static int write_all(int32_t fd, uint64_t from, uint32_t bytes)
{
    while (bytes) {
        uint32_t chunk = bytes < CHUNK ? bytes : CHUNK;
        uint32_t written = 0;
        /* The card layer may read the buffer by DMA. */
        fw_dcache_clean((void *)(uintptr_t)from, chunk);
        int32_t result = fw_file_write(fd, (const void *)(uintptr_t)from,
                                       chunk, &written);
        if (result != 0 || !written)
            return result ? result : -1;
        from += written;
        bytes -= written;
    }
    return 0;
}

/* Runs on the camera's own task, inside one frame's release. */
void pre_frame(uint64_t r);

void pre_frame(uint64_t r)
{
    uint64_t block = plugin_block();
    volatile struct pre_state *state =
        (volatile struct pre_state *)(uintptr_t)(block + OFF_PRE_STATE);
    if (!block || state->done || !pre_raw_on() || !r)
        return;

    uint64_t e = rd64(r + R_SLOT_E), m = rd64(r + R_SLOT_M);
    if (!e || !m)
        return;
    uint64_t payload = rd64(e + E_PAYLOAD);
    uint32_t width = rd16(e + E_SHAPE), height = rd16(e + E_SHAPE + 2u);
    /* Only the full-frame high-speed modes (C30, C60, C15), whose payload
     * this build can size: C120 reads out DX, and an ordinary frame that
     * reached this stage some other way is not what the export is about. */
    uint32_t mode = rd32(HSFC_MODE);
    if (mode != 3u && mode != 5u && mode != 6u)
        return;
    if (!payload || width != FX_WIDTH || height != FX_HEIGHT)
        return;
    uint32_t bytes = (uint32_t)(((uint64_t)HE_RATE * width * height / 80u) &
                                ~511ull);

    /* Whatever happens next, it happens once: a failure that repeated on
     * every frame would stall the whole burst. */
    state->done = 1;
    state->sequence = rd32(m + M_SEQUENCE);
    state->bytes = bytes;

    struct dump_header header;
    for (uint32_t n = 0; n < sizeof header; n++)
        ((uint8_t *)&header)[n] = 0;
    header.magic = DUMP_MAGIC;
    header.version = 1;
    header.r = r;
    header.e = e;
    header.m = m;
    header.payload = payload;
    header.payload_bytes = bytes;
    header.width = width;
    header.height = height;
    header.sequence = state->sequence;
    header.mode = rd32(HSFC_MODE);
    header.drained = rd32(PRE_DRAINED);
    header.r_bytes = R_BYTES;
    header.m_bytes = M_BYTES;
    header.e_bytes = E_BYTES;

    int32_t fd = fw_file_open(DUMP_PATH, FILE_WRITE_CREATE, FILE_ATTRIBUTES);
    if (fd < 0) {
        state->result = (uint32_t)fd;
        return;
    }
    /* TENC wrote the payload; nothing of it has been read through the data
     * cache, so the lines are invalidated before it is handed over. */
    fw_dcache_inv((void *)(uintptr_t)payload, bytes);

    int result = write_all(fd, (uint64_t)(uintptr_t)&header, sizeof header);
    if (!result)
        result = write_all(fd, r, R_BYTES);
    if (!result)
        result = write_all(fd, m, M_BYTES);
    if (!result)
        result = write_all(fd, e, E_BYTES);
    if (!result)
        result = write_all(fd, payload, bytes);
    fw_file_close(fd);
    state->result = (uint32_t)result;
}

/* ---- the trampoline ----------------------------------------------------- */

/* AArch64 encodings, written out rather than assembled, because the words
 * go into the firmware's own text through the loader's code channel. */
static uint32_t stp_sp(uint32_t t1, uint32_t t2, uint32_t off)
{
    return 0xa9000000u | ((off / 8u) << 15) | (t2 << 10) | (31u << 5) | t1;
}

static uint32_t ldp_sp(uint32_t t1, uint32_t t2, uint32_t off)
{
    return 0xa9400000u | ((off / 8u) << 15) | (t2 << 10) | (31u << 5) | t1;
}

static uint32_t str_sp(uint32_t t, uint32_t off)
{
    return 0xf9000000u | ((off / 8u) << 10) | (31u << 5) | t;
}

static uint32_t ldr_sp(uint32_t t, uint32_t off)
{
    return 0xf9400000u | ((off / 8u) << 10) | (31u << 5) | t;
}

#define FRAME 0xb0u /* x0..x17, x18 and x30, and the flags */

static uint32_t trampoline(uint32_t *out, uint64_t handler, uint32_t replaced,
                           uint64_t resume, uint64_t at)
{
    uint32_t n = 0;
    out[n++] = 0xd1000000u | (FRAME << 10) | (31u << 5) | 31u; /* SUB SP,SP,#FRAME */
    for (uint32_t pair = 0; pair < 9u; pair++)
        out[n++] = stp_sp(pair * 2u, pair * 2u + 1u, pair * 16u);
    out[n++] = stp_sp(18u, 30u, 0x90u);
    out[n++] = 0xd53b4200u;            /* MRS X0, NZCV */
    out[n++] = str_sp(0u, 0xa0u);
    out[n++] = 0xaa1703e0u;            /* MOV X0, X23 -- the request block */
    uint32_t ldr_at = n;
    out[n++] = 0;                      /* LDR X16, <literal>, filled below */
    out[n++] = 0xd63f0200u;            /* BLR X16 */
    out[n++] = ldr_sp(0u, 0xa0u);
    out[n++] = 0xd51b4200u;            /* MSR NZCV, X0 */
    for (uint32_t pair = 0; pair < 9u; pair++)
        out[n++] = ldp_sp(pair * 2u, pair * 2u + 1u, pair * 16u);
    out[n++] = ldp_sp(18u, 30u, 0x90u);
    out[n++] = 0x91000000u | (FRAME << 10) | (31u << 5) | 31u; /* ADD SP,SP,#FRAME */
    out[n++] = replaced;               /* the instruction the branch took */
    int64_t back = (int64_t)resume - (int64_t)(at + n * 4u);
    out[n++] = 0x14000000u | (uint32_t)((back >> 2) & 0x3ffffff);
    if (n & 1u)
        out[n++] = 0xd503201fu;        /* NOP: the literal is 8-aligned */
    out[ldr_at] = 0x58000000u | (((n - ldr_at) & 0x7ffffu) << 5) | 16u;
    out[n++] = (uint32_t)handler;
    out[n++] = (uint32_t)(handler >> 32);
    return n;
}

/* ---- install ------------------------------------------------------------ */

int pre_fingerprint(void)
{
    if (rd32(PRE_FREE_SITE) != PRE_FREE_STOCK_0 ||
        rd32(PRE_FREE_SITE + 4) != PRE_FREE_STOCK_1 ||
        rd32(PRE_FREE_SITE + 8) != PRE_FREE_STOCK_2)
        return 0;
    for (uint64_t at = GAP_TRAMP; at < GAP_TRAMP + GAP_TRAMP_BYTES; at += 4)
        if (rd32(at))
            return 0;
    return 1;
}

int pre_install(const struct nlr_api *api, uint64_t block)
{
    volatile struct pre_state *state =
        (volatile struct pre_state *)(uintptr_t)(block + OFF_PRE_STATE);
    state->done = 0;
    state->result = 0;
    state->sequence = 0;
    state->bytes = 0;

    uint32_t words[GAP_TRAMP_BYTES / 4u];
    uint32_t count = trampoline(words, (uint64_t)(uintptr_t)&pre_frame,
                                PRE_FREE_STOCK_0, PRE_FREE_RESUME, GAP_TRAMP);
    int result = code_write(api, GAP_TRAMP, words, count * 4u);
    if (result != 0)
        return result;
    /* Read the gap back before anything branches into it: the one place
     * this plugin writes code rather than a constant, and a trampoline
     * that is not what it should be would take the camera with it. */
    for (uint32_t n = 0; n < count; n++)
        if (rd32(GAP_TRAMP + n * 4u) != words[n])
            return -NLR_API_EIO;

    /* The branch goes in last: until it does, the trampoline is unreachable
     * code in a gap nothing runs. */
    int64_t delta = (int64_t)(uint64_t)GAP_TRAMP - (int64_t)(uint64_t)PRE_FREE_SITE;
    uint32_t branch = 0x14000000u | (uint32_t)((delta >> 2) & 0x3ffffff);
    return api->code_patch((void *)(uintptr_t)PRE_FREE_SITE, &branch, 4);
}
