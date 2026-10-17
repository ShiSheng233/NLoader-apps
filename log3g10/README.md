# Log3G10 for the Z 6III (NLoader programs)

Records RED Log3G10 in place of N-Log: the camera's video tone mode
**N-Log** uses a Log3G10 curve instead. Z 6III firmware 2.00 only.

**Not for the Z 8 (checked 2026-10-17).** The programs write the N-Log rows
of Lut1 through a RAM copy the Z 6III keeps, and check them against that
firmware's CRC. The Z 8 3.11 has no RAM copy: its Lut1 rows stay in the
flash image (`isp.lut1.nlog` is declared there as class `repack`, and
`table_write` answers -EACCES, as `abi8test` shows in Z 8 QEMU). On the Z 8
the curve can only come with a repacked firmware, which these programs do
not do; their declarations keep `models` at `Z6III/2.00`.

## What it changes, and where the curve comes from

The image pipeline turns scene-linear RGB into the recorded code through
one 229-point table (Lut1). For each video job the camera copies that table
from templates in RAM; tone mode N-Log copies rows 3, 4 and 5 (one per
colour lane). These programs overwrite those three rows -- 1,374 bytes of
RAM -- and nothing else. Nothing is flashed: switching the camera off
brings back the firmware's own N-Log, and the next start applies the curve
again only if the switch is on.

Nikon's **ZR** has the same pipeline and its own Log3G10 tone mode. It does
not store a Log3G10 table; in that mode it computes Lut1 at run time (ZR
1.11, function `0x4131b3a0`) from the table's 229-node input grid and the
published Log3G10 constants, and keeps N-Log's other two tables (NlgGamma,
gamma gain). `mkcurve.py zr` reads the grid, the constants and the
generator's instructions out of the ZR firmware and computes the same
curve; `LOG3G10.CRV` here is its output (18% grey -> 341, black -> 94 in
10-bit codes).

Colour stays N-Log's (BT.2020 primaries): the result is Log3G10 /
Rec.2020, not REDWideGamutRGB. In post, decode it as Log3G10 with Rec.2020
primaries. The file's own tags still say N-Log.

## Files on the card (under the loader's root, `NLOADER\`)

| file | what |
|---|---|
| `APPS/LOG3G10.NLB` | the settings page (foreground program) |
| `APPS/LOG3G10X.NLB` | loads the curve when NLoader starts (resource extension) |
| `APPS/LOG3G10.CRV` | the curve: 229 numbers, one per line |
| `APPS/LOG3G10.CFG` | the switch, written by the page |
| `APPS/LOG3G10.LOG` | what the extension did at the last start |

The two programs describe themselves: a loader with ABI 7 or later finds
them in `APPS` with no `MANIFEST` line. Put `log3g10_start` in `ENABLED`
(one id per line), or switch it on in NLoader -> Plugin settings.

An older loader refuses a file that describes itself: build with `PLAIN=1`
and add the two lines `build.sh` writes into `MANIFEST.page` /
`MANIFEST.start` to the card's `MANIFEST` (on the ABI 4 loader the NLoader
page did not save ENABLED itself either).

## Using it

Setup -> NLoader -> Log3G10 -> Open application:

    Log3G10 at start: Off      OK switches it; saved on the card at once
    Now: stock N-Log           what tone mode N-Log uses right now
    Curve: grey 341, top 841   LOG3G10.CRV as read just now (or why not)

Switching on also applies the curve straight away (the camera uses it
from the next video job it builds; leaving the menu is enough). Switching
off is saved at once and takes effect at the next power-on: the firmware
reloads its own rows itself when it starts, and these programs never write
anything except the curve over the firmware's own rows -- they cannot put
N-Log back by themselves, because the flash copy of those rows is not
mapped while the camera runs (reading it stops the camera; found in QEMU).

**When "start" is:** a loader with the Z 6III's boot checkpoint starts its
extensions at power-on. An older one starts them the first time the
NLoader page is opened after power-on; with it, open Setup -> NLoader once
before recording.

The extension never forces anything: it only writes when the rows match
the stock N-Log of this firmware (by CRC-32), and with the switch off, no
usable curve file or rows it does not recognise, it leaves N-Log alone and
says why in `LOG3G10.LOG`.

## Your own curve

    python3 mkcurve.py zr eg2210_011100.b -o LOG3G10.CRV   # from the ZR firmware
    python3 mkcurve.py formula -o LOG3G10.CRV              # same numbers, no ZR file
    python3 mkcurve.py cube my.cube -o LOG3G10.CRV         # any N-Log-input .cube

`-e 0.5` moves grey half a stop (grid level 400 * 2^e). A `.cube` must take
N-Log in; a 1D LUT's columns are averaged, a 3D LUT contributes only its
grey axis (the table is 1D). Because the cube sees N-Log first, nothing
above N-Log's clip reaches it. The Log3G10 curves from `zr`/`formula` do
not go through N-Log, so they keep the top grid nodes N-Log clips.

Nikon's `RWG_Log3G10_to_REC2020_N-LOG` LUT is the opposite direction and
compresses highlights to fit N-Log; inverting it is not Log3G10 above
middle grey.

## Build and test

    ./build.sh <new dir>                  # LOG3G10.NLB, LOG3G10X.NLB, MANIFEST lines
    cc -I <sdk> -I . -DL3G_HOSTED ... test_log3g10.c log3g10.c page.c start.c
                                          # host test (59 checks), see its header

Build against the SDK and `nloader` tool of the loader on the camera.
`NLOADER=<NLoader tree>` names the tree whose tool is used, `SDK=<dir>`
the `nlr.h`/`crt0.s`. The programs are built for ABI 5 (see "By name"
below). Their first version, ABI 4 and by address only, was tested on the
ABI 4 loader `fw-v3` (`exec-fix8`, sha256 `458e39c6…`):

| file | sha256 |
|---|---|
| `LOG3G10.NLB` | `1bd37afaca3f59dceceb338f63eb605bf9b63c873547936403dcdee63046a145` |
| `LOG3G10X.NLB` | `45f16a6aaec8ba03cc9452a59e37c1683a307e7643ea060110e2d40154c34321` |

A loader newer than the SDK the programs were built with: the page refuses
to run (it exits with code 1), and the extension leaves N-Log alone and says
so in `LOG3G10.LOG` -- it does not fail, because a failed extension pauses
every later start. Rebuild them with that loader's SDK and tool.

The camera code uses no floating point (`-mgeneral-regs-only`), no C
library, no writable static data and no heap:

- No static data: NLoader then gives each program one 2 MiB region instead
  of two, and it keeps an extension's region for the whole boot.
- No heap: on this body a small `mem_alloc` block (up to one page) goes
  back to the front of the allocator's free list as an 8 KiB node, and
  while that node is there the loader's grant probe answers 0 and no
  program can start for the rest of the boot ("Program refused at stage
  A"; seen in QEMU with an earlier build of this extension). Files are
  read through a 256-byte buffer on the stack instead.

## Not verified

- On a body, only in part (2026-10-15, the ABI 4 build on `exec-fix8`): a
  clip recorded in N-Log with the switch on has its neutral floor at code
  93 (darkest blocks 98-103, full range), below stock N-Log's floor of 128
  and at Log3G10's 93.75, so video N-Log jobs do take these rows (the job
  builder copies them, `sub_41126C78`; the emulator renders no image).
  Not checked yet: grey (a grey card should record near 341 full range /
  356 limited), the clip level, and an A/B clip against stock N-Log.
- The ZR's own grey placement `e` (from two job fields, not traced to the
  menu); 0 matches N-Log's and Nikon's own conversion LUT.
- On this grid the stock N-Log row runs above the published N-Log formula
  in the top three stops (up to ~20 codes); the Log3G10 curve follows the
  ZR's generator, not that row.

## By name (ABI 8)

With a loader that has the firmware by name, both programs use the profile's
table `isp.lut1.nlog` (the three N-Log rows of Lut1 as one span) through
table_read / table_write, and hold no address: the loader checks the stock
CRC before the first write and keeps the stock rows. The code asks for ABI 5
only (abi_version 5): built with `PLAIN=1`, the same programs run on older
loaders, where they fall back to the RAM address in log3g10.h.
