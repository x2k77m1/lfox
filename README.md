# LFOX — a third LFO for the Digitone mk1

The **LFO** key on a stock Digitone cycles LFO1 → LFO2 → LFO1. With this mod it
cycles **LFO1 → LFO2 → LFOX**: the third press opens an LFO page of its own,
with all eight of the stock LFO's controls, its own settings and its own
per-voice state.

LFOX is computed by **the firmware's own LFO engine** — the same routine, rate
table, waveforms, fade and trigger modes, per voice, once an audio block —
writing into the same per-voice parameter words LFO1 and LFO2 use. Nothing of
the mod runs in the audio path but one short stub, and LFO1 and LFO2 behave
exactly as stock.

Unofficial, not affiliated with Elektron. Ships no firmware.

## Install

You need the stock **Digitone mk1 OS 1.44** `.syx`, **core-dn1 4.0** and
**lfox-1.0a.elemod**.

In elekloader, add `core-4.0-os1.44.elemod` and `lfox-1.0a.elemod` to the
build, patch the stock OS file and send the result with Elektron Transfer.
With the command line:

```sh
python -m elekloader.patch \
    --stock Digitone_and_Digitone_Keys_OS1.44.syx \
    --mod core-4.0-os1.44.elemod \
    --mod lfox-1.0a.elemod \
    --out Digitone_OS1.44_LFOX.syx --version 1.0a
```

The version must be exactly four characters. The mod targets **OS 1.44 only**:
do not send it to a Digitone II or to another OS release. A stock OS file
always recovers the device.

## The page

Press **LFO** three times: LFO1, then LFO2, then **LFOX**, which the view heads
**LFO (3/3)**. Another page key leaves the LFO view; the cycle starts again on
the next visit.

The page is the stock LFO page — the same grid, fonts, frames, labels, knobs and
value text, since all eight boxes carry LFO1's parameter ids:

| Knob | Control | Range | What it does |
|---|---|---|---|
| A | **SPD** | 0…127 | rate |
| B | **MULT** | 0…23 | multiplier, BPM values included |
| C | **FADE** | 0…127 | fade, bipolar about 64 (64 is none) |
| D | **DEST** | the stock list | modulation destination |
| E | **WAVE** | 0…6 | TRI SIN SQR SAW EXP RMP RND |
| F | **SPH** | 0…127 | start phase |
| G | **MODE** | 0…4 | FRE TRG HLD ONE HLF |
| H | **DEP** | 0…127 | depth, bipolar about 64 (64 is none) |

The knobs are the firmware's own, so they step, accelerate, flash and push to
the default exactly as on LFO1's and LFO2's pages. **DEST** opens the stock
destination list (filter, amp and synth parameters), and its name is always the
parameter that is actually modulated.

The waveform display between the **WAVE** and **SPH** boxes is drawn by the mod
— the stock view binds that one to LFO1's and LFO2's records — with the stock
page's own artwork, and `SPH` rotates the curve the way stock does.

## Settings

All eight settings live in the track's sound, in three sound slots no stock
parameter uses (0, 26 and 65), packed into 48 bits. They are saved and loaded
with the sound, the kit and the project, with **no format change**: a sound
without the marker reads the stock LFO1 defaults. The per-voice LFO state is
RAM and is not saved.

## Build

```sh
ELEKLOADER=/path/to/elekloader \
STOCK=/path/to/Digitone_and_Digitone_Keys_OS1.44.syx sh build.sh
```

Needs the m68k-elf cross toolchain (`ELEKLOADER_CROSS`, default
`m68k-elf-`) and an elekloader checkout with core-dn1. The `.elemod` is written
to `release/`.

## Licence

GPL-2.0-or-later. Stock firmware is Elektron's copyright; this repository ships
no firmware bytes.
