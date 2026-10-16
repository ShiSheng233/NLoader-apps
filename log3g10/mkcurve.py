#!/usr/bin/env python3
"""mkcurve.py: the curve file the Log3G10 program loads (LOG3G10.CRV).

The Z 6III's image pipeline turns scene-linear RGB into the recorded code
through one 229-point table (Lut1). Its input is a fixed grid of 229 linear
levels on which grid value 400 is 18% grey; its output is 15 bits. The
curve file holds the 229 outputs, one per line, for the three Lut1 rows
that tone mode N-Log uses.

Nikon's ZR has the same pipeline and a Log3G10 tone mode. It does not
carry a Log3G10 table: in that mode its job builder computes Lut1 from the
grid (ZR 1.11 function 0x4131b3a0):

    lut[i] = min(0x7fff, trunc(32768 * 0.224282 *
                 log10f(155.975327 * (0.18 * grid[i] / (400 * 2^e) + 0.01)
                        + 1) + 0.5))

e = 0.5 - (two exposure fields of the job). It leaves N-Log's other two
tables (NlgGamma, gamma gain) in place. This tool reproduces that.

  mkcurve.py zr <eg2210_011100.b> [-e E] -o LOG3G10.CRV
      Read the grid, the constants and the generator's instructions out of
      the decrypted ZR 1.11 EG module (checked by SHA-256) and compute the
      curve the way the ZR does.
  mkcurve.py formula [-e E] -o LOG3G10.CRV
      The same arithmetic with the published Log3G10 constants and the grid
      rebuilt from its spacing rule; equal to `zr` (the tool checks this
      when both are available).
  mkcurve.py cube <file.cube> -o NAME.CRV
      A curve of your own from a .cube whose INPUT is N-Log (full range):
      a 1D LUT (the three columns averaged) or a 3D LUT (its grey axis
      only -- the table is 1D, it cannot hold colour changes). Each grid
      level is encoded to N-Log first, so everything above N-Log's own
      clip (code 1023, 14.78 x linear) reaches the cube as 1.0.

-e shifts exposure: grey lands on grid level 400 * 2^e. 0 (the default)
puts it where the camera's N-Log puts it, which is also what Nikon's own
Log3G10 <-> N-Log conversion LUT assumes.
"""
import argparse
import hashlib
import math
import struct
import sys

POINTS = 229
VALUE_MAX = 0x7FFF

ZR_EG_SHA256 = "82b7c33646a24fd084293bce61d0f934e15639ab806525a09553aaa081f27d2f"
# ZR 1.11 EG layout (SOLID descriptor): file offset of each segment.
ZR_SEGMENTS = [  # (virtual address, size, file offset)
    (0x40168000, 23982028, 32768),       # text
    (0x41847000, 59178176, 24014848),    # rodata
    (0xA02808000, 4661248, 83193856),    # initialised data
]
ZR_GRID = 0xA02A1B996        # 229 x u16, read by the loop at 0x4131b4f0
ZR_CONST = {                 # doubles the generator loads
    "grey_level": 0x444DABB8,   # 400.0
    "grey": 0x444DABC0,         # 0.18
    "c": 0x444DABC8,            # 0.01 (as float)
    "b": 0x444DABD0,            # 155.975327 (as float)
    "a": 0x444DABD8,            # 0.224282 (as float)
    "scale": 0x42BA6F70,        # 32768.0
    "e_base": 0xA02A1BB60,      # -2.0  } e = e_base - ratio - e_off + 15 - 12
    "e_off": 0xA02A1BB68,       #  0.5  }   = 0.5 - ratio
}
# The generator's inner loop, 0x4131b4f0..0x4131b53f, the twenty
# instructions this tool's arithmetic follows: ldrh grid; ucvtf; fdiv by
# 400*2^e; fmul 0.18; fadd 0.01; fmul b; fadd 1; fcvt to float; bl log10f;
# fcvt back; fmul a; fmul 32768; fadd 0.5; fcvtzs; clamp to 0x7fff; strh;
# next of 229.
ZR_LOOP = (0x4131B4F0, bytes.fromhex(
    "a86a73780001631e00186c1e0008691e00286a1e"
    "01086b1e2028681e0040621ee80e069400c0221e"
    "00086e1e01086f1e20286d1e0800781e1f01176b"
    "0831971a886a3378730a00917f2a07f1a1fdff54"))


def f32(x):
    """Round a double to the nearest float, as fcvt does."""
    return struct.unpack("<f", struct.pack("<f", x))[0]


def grid_rule():
    """The grid's spacing: steps of 1, 2, 4, ... between powers of two,
    256 from 4096 to 32768, then 1024 and 2048, ending at 65535."""
    g = list(range(0, 9))
    for lo, hi, step in ((8, 16, 2), (16, 32, 4), (32, 128, 8), (128, 256, 16),
                         (256, 1024, 32), (1024, 2048, 64), (2048, 4096, 128),
                         (4096, 32768, 256), (32768, 49152, 1024),
                         (49152, 63488, 2048)):
        g += list(range(lo + step, hi + 1, step))
    g.append(65535)
    assert len(g) == POINTS, len(g)
    return g


def zr_read(path):
    data = open(path, "rb").read()
    digest = hashlib.sha256(data).hexdigest()
    if digest != ZR_EG_SHA256:
        raise SystemExit("%s: SHA-256 %s is not the ZR 1.11 EG module (%s)"
                         % (path, digest, ZR_EG_SHA256))

    def offset(va):
        for base, size, off in ZR_SEGMENTS:
            if base <= va < base + size:
                return va - base + off
        raise ValueError(hex(va))

    loop_at, loop = ZR_LOOP
    got = data[offset(loop_at):offset(loop_at) + len(loop)]
    if got != loop:
        raise SystemExit("the generator loop at %#x is not the expected code"
                         % loop_at)
    grid = list(struct.unpack_from("<229H", data, offset(ZR_GRID)))
    const = {k: struct.unpack_from("<d", data, offset(v))[0]
             for k, v in ZR_CONST.items()}
    return grid, const


def published():
    return grid_rule(), {
        "grey_level": 400.0, "grey": 0.18, "c": f32(0.01),
        "b": f32(155.975327), "a": f32(0.224282), "scale": 32768.0,
        "e_base": -2.0, "e_off": 0.5}


def log3g10_curve(grid, k, e):
    """The ZR's loop, step by step, in its own precision (doubles, with the
    logarithm taken in float)."""
    level = k["grey_level"] * 2.0 ** e
    out = []
    for g in grid:
        x = float(g) / level * k["grey"] + k["c"]
        v = f32(x * k["b"] + 1.0)
        y = f32(math.log10(v)) * k["a"] * k["scale"] + 0.5
        out.append(min(int(y), VALUE_MAX))
    return out


# ---- .cube ------------------------------------------------------------------

def nlog_encode(x):
    """Nikon N-Log, linear reflectance -> normalised full-range code."""
    c = 650.0 * (x + 0.0075) ** (1.0 / 3.0) if x < 0.328 else 150.0 * math.log(x) + 619.0
    return min(c, 1023.0) / 1023.0


def read_cube(path):
    size, dims, lo, hi, rows = None, 0, [0.0] * 3, [1.0] * 3, []
    for number, line in enumerate(open(path, encoding="ascii", errors="replace"), 1):
        parts = line.split()
        if not parts or parts[0].startswith("#"):
            continue
        key = parts[0]
        if key in ("LUT_1D_SIZE", "LUT_3D_SIZE"):
            size, dims = int(parts[1]), 1 if key == "LUT_1D_SIZE" else 3
        elif key == "DOMAIN_MIN":
            lo = [float(v) for v in parts[1:4]]
        elif key == "DOMAIN_MAX":
            hi = [float(v) for v in parts[1:4]]
        elif key in ("LUT_1D_INPUT_RANGE", "LUT_3D_INPUT_RANGE"):
            lo, hi = [float(parts[1])] * 3, [float(parts[2])] * 3
        elif key[0] in "+-.0123456789":
            rows.append([float(v) for v in parts[:3]])
        # TITLE and anything else: not part of the numbers
    if not size:
        raise SystemExit("%s: no LUT_1D_SIZE / LUT_3D_SIZE" % path)
    want = size if dims == 1 else size ** 3
    if len(rows) != want:
        raise SystemExit("%s: %d rows, %d expected" % (path, len(rows), want))
    if len(set(lo)) != 1 or len(set(hi)) != 1:
        raise SystemExit("%s: per-channel domains are not supported" % path)
    if dims == 1:
        axis = [sum(r) / 3.0 for r in rows]
    else:  # the grey axis: r = g = b = i, row i + i*size + i*size*size
        axis = [sum(rows[i * (1 + size + size * size)]) / 3.0 for i in range(size)]
    return axis, lo[0], hi[0], dims, size


def cube_curve(path, grid, e):
    axis, lo, hi, dims, size = read_cube(path)
    out = []
    for g in grid:
        x = 0.18 * g / (400.0 * 2.0 ** e)
        t = (nlog_encode(x) - lo) / (hi - lo) * (size - 1)
        t = min(max(t, 0.0), size - 1.0)
        i = min(int(t), size - 2)
        v = axis[i] + (axis[i + 1] - axis[i]) * (t - i)
        # .cube values are normalised codes (code / 1023): 1023 * 32 is the
        # full scale the stock N-Log rows use.
        out.append(min(max(int(round(v * 32736.0)), 0), VALUE_MAX))
    return out, "%dD %d" % (dims, size)


# ---- output -----------------------------------------------------------------

def write(path, curve, source):
    for i in range(1, POINTS):
        if curve[i] < curve[i - 1]:
            raise SystemExit("the curve falls at point %d (%d -> %d): refused"
                             % (i, curve[i - 1], curve[i]))
    grey = (curve[40] + curve[41]) / 2.0 / 32.0  # grid 384 / 416
    lines = [
        "# Log3G10 program curve: 229 Lut1 outputs (0..32767), one per node",
        "# of the R2Y input grid (grid level 400 = 18% grey).",
        "# " + source,
        "# 18%% grey -> 10-bit %.1f, black -> %.1f, top -> %.1f"
        % (grey, curve[0] / 32.0, curve[-1] / 32.0),
    ] + ["%d" % v for v in curve]
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print("%s: grey %.1f, black %.1f, top %.1f (10-bit codes)"
          % (path, grey, curve[0] / 32.0, curve[-1] / 32.0))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="how", required=True)
    z = sub.add_parser("zr")
    z.add_argument("eg")
    f = sub.add_parser("formula")
    c = sub.add_parser("cube")
    c.add_argument("cube")
    for p in (z, f, c):
        p.add_argument("-e", type=float, default=0.0)
        p.add_argument("-o", required=True)
    a = ap.parse_args()

    grid, k = published()
    if a.how == "zr":
        zgrid, zk = zr_read(a.eg)
        if zgrid != grid:
            raise SystemExit("the ZR grid differs from the spacing rule")
        if zk["e_base"] - zk["e_off"] + 15 - 12 != 0.5:
            raise SystemExit("the ZR exposure constants changed: %r" % zk)
        curve = log3g10_curve(zgrid, zk, a.e)
        if curve != log3g10_curve(grid, k, a.e):
            raise SystemExit("ZR constants and published constants disagree")
        write(a.o, curve, "ZR 1.11 Log3G10 generator (EG sha256 %s..., 0x4131b3a0), e=%g"
              % (ZR_EG_SHA256[:16], a.e))
    elif a.how == "formula":
        write(a.o, log3g10_curve(grid, k, a.e),
              "Log3G10 formula on the R2Y grid, as the ZR computes it, e=%g" % a.e)
    else:
        curve, what = cube_curve(a.cube, grid, a.e)
        write(a.o, curve, "from %s (%s, N-Log input), e=%g" % (a.cube.split("/")[-1], what, a.e))
    return 0


if __name__ == "__main__":
    sys.exit(main())
