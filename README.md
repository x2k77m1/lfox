# LFOX - a third LFO for the Digitone mk1

LFOX adds a third LFO to the Digitone mk1.

The **LFO** key on a stock Digitone cycles LFO1 → LFO2 → LFO1. With this mod it
cycles **LFO1 → LFO2 → LFOX**: the third press opens an LFO page of its own,
with all eight of the stock LFO's controls, its own settings and its own
per-voice state.

LFOX is computed by **the firmware's own LFO engine** - the same routine, rate
table, waveforms, fade and trigger modes, per voice, once an audio block -
writing into the same per-voice parameter words LFO1 and LFO2 use. Nothing of
the mod runs in the audio path but one short stub, and LFO1 and LFO2 behave
exactly as stock.

Unofficial, not affiliated with Elektron. Ships no firmware.

## Install

You need the stock **Digitone mk1 OS 1.44** `.syx`, **core-dn1 4.0** and
**lfox-1.0c.elemod**.

In elekloader, add `core-4.0-os1.44.elemod` and `lfox-1.0c.elemod` to the
build, patch the stock OS file and send the result with Elektron Transfer.
With the command line:

```sh
python -m elekloader.patch \
    --stock Digitone_and_Digitone_Keys_OS1.44.syx \
    --mod core-4.0-os1.44.elemod \
    --mod lfox-1.0c.elemod \
    --out Digitone_OS1.44_LFOX.syx --version 1.0c
```

The version must be exactly four characters. The mod targets **OS 1.44 only**:
do not send it to a Digitone II or to another OS release. A stock OS file
always recovers the device.

## The page

Press **LFO** three times: LFO1, then LFO2, then **LFOX**, which the view heads
**LFO (3/3)** - the 3 is the view's page count, so the title stays `LFO`. The
gesture boxes name the page, as they name LFO1 and LFO2: **"COPY PAGE LFOX"**,
"PASTE PAGE LFOX", "CLEAR PAGE LFOX", "Randomize Page LFOX" and "Reload Page
LFOX". Another page key leaves the LFO view; the cycle starts again on the next
visit.

The page is the stock LFO page - the same grid, fonts, frames, labels, knobs and
value text, since all eight boxes carry LFO1's parameter ids:

| Knob | Control  | Range          | What it does                         |
| ---- | -------- | -------------- | ------------------------------------ |
| A    | **SPD**  | 0…127          | rate                                 |
| B    | **MULT** | 0…23           | multiplier, BPM values included      |
| C    | **FADE** | 0…127          | fade, bipolar about 64 (64 is none)  |
| D    | **DEST** | the stock list | modulation destination               |
| E    | **WAVE** | 0…6            | TRI SIN SQR SAW EXP RMP RND          |
| F    | **SPH**  | 0…127          | start phase                          |
| G    | **MODE** | 0…4            | FRE TRG HLD ONE HLF                  |
| H    | **DEP**  | 0…127          | depth, bipolar about 64 (64 is none) |

The knobs are the firmware's own, so they step, accelerate, flash and push to
the default exactly as on LFO1's and LFO2's pages. **DEST** opens the stock
destination list (filter, amp and synth parameters), and its name is always the
parameter that is actually modulated.

The page's gestures are the stock page's: **[LFO] + [YES]** randomises,
**[LFO] + [NO]** reloads, **[LFO] + [PLAY]** clears (a second press undoes),
**[LFO] + [RECORD]** copies and **[LFO] + [STOP]** pastes, each under stock's
own popup. A copy made on LFO1 or LFO2 pastes onto LFOX, and LFOX's copy pastes
onto either of them.

LFOX also modulates the parameters a machine mod reads itself - machines-dn1's
knobs, SAY IT's PIT, SPD, THRT and MOUT - as well as the stock ones.

The waveform display between the **WAVE** and **SPH** boxes is drawn by the mod
(the stock view binds that one to LFO1's and LFO2's records), with the stock
page's own artwork, and `SPH` rotates the curve the way stock does.

## Settings

The eight settings are saved with your **project**. A pattern carries them as
part of the project it belongs to.

LFOX is not part of a sound. Its settings follow the track, so loading a
different sound onto that track leaves LFOX as it was.

## Load

LFOX runs the firmware's LFO routine a second time over the eight voices, once
an audio block. Armed with a destination and a depth away from 64, it costs
about six points of CPU and DSP. At its defaults it costs nothing.

| Scenario                      | CPU      | DSP      |
| ----------------------------- | -------- | -------- |
| LFOX armed, sequencer stopped | **+6.3** | **+6.1** |
| LFOX armed, one track playing | **+6.3** | **+6.1** |
| LFOX at its defaults          | **+0**   | **+0**   |

Points of the unit's CPU and DSP readouts, against the same pattern without
LFOX. With the track playing, the render's peak reached 92.5 %, 7.5 points
below its limit. At the defaults (DEST none, DEP 64) the second pass is skipped
and only stock's runs.

Measured over USB on a Digitone mk1, OS 1.44, tempo 120, one track of eighth
notes at the default sound, three one-minute runs per scenario.

## What is not stock

- **A paste that crosses pages lands silently.** Copy on LFO1 or LFO2 and paste
  on LFOX (or copy on LFOX and paste on LFO1 or LFO2) and the eight values
  arrive, but no "PASTE PAGE" box appears: stock refuses a clip taken on another
  page before it draws one, so the mod writes the values itself. The copy is
  announced, as stock's is, and every paste within a page - LFOX's own copy on
  LFOX, LFO1's on LFO1, LFO1's on LFO2 - keeps stock's box.


## Build

```sh
ELEKLOADER=/path/to/elekloader \
STOCK=/path/to/Digitone_and_Digitone_Keys_OS1.44.syx sh build.sh
```

Needs the m68k-elf cross toolchain (`ELEKLOADER_CROSS`, default
`m68k-elf-`) and an elekloader checkout with core-dn1. The `.elemod` is written
to `release/`.

## Licence

GPL-2.0-or-later.
