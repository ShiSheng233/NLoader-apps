# ProRes grades (Z 6III 2.00 only)

`PRORES.NLB` adds ProRes 422 / LT / Proxy rows to the Z 6III's own video
menu (a resident extension; see `start.c`). Build with `build.sh`.

**Not for the Z 8 (checked 2026-10-17).** The program patches the Z 6III
2.00 firmware's record service and menu pages by address (`z63fw.h`: the
addresses, stock bytes and encoder layout of that firmware). None of them
has been studied on the Z 8 3.11, whose encoder and menus differ, and the
Z 8 profile declares no named hook or table this program could use
instead. `prores.json` therefore keeps `models` at `z6iii/2.00`; the loader
lists it greyed ("other body") on a Z 8 and never loads it.
