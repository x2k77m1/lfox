/* SPDX-License-Identifier: GPL-2.0-or-later */
/* LFOX for the Digitone mk1 (elekloader / core-dn1 4.0).
 *
 * The LFO key cycles LFO1, LFO2 and LFOX: the third press opens core-dn1's
 * page 27.  LFOX has the stock LFO page's eight controls, with the stock
 * ranges, the stock values and the stock text:
 *
 *   A SPD   B MULT   C FADE   D DEST
 *   E WAVE  F SPH    G MODE   H DEP
 *
 * and it is computed by the firmware's own LFO engine: the same routine, the
 * same rate table, the same waveforms, the same fade and trigger modes, once
 * per voice per audio block, with its modulation written into the same
 * per-voice parameter words LFO1 and LFO2 use.  Nothing of the mod runs in
 * the audio path but one short stub.
 *
 * HOW THE STOCK ENGINE IS REUSED (the Model:Cycles pattern, measured on the
 * 1.44 image):
 *
 *   The 1.44 image calls its LFO routine at 0x400FED62 from exactly one
 *   place, a `jsr` at 0x4009DD02, once per audio block.  mod.json's "sites"
 *   redirects that one call to `lfox_audio` below, which
 *
 *     1. calls the stock routine unchanged, so LFO1 and LFO2 stay stock;
 *     2. when any track's LFOX has both a depth and a destination, saves the
 *        words the second call borrows and both LFOs' state for the block,
 *        then installs LFOX's eight settings in the values 9-16 block and
 *        LFOX's own state in the LFO2 state records, and zeroes LFO1's DEP
 *        (value 8) so the second call adds nothing but LFOX;
 *     3. calls the stock routine again, which now computes LFOX -- per voice,
 *        with the stock rate, waveform, fade and mode -- and writes its
 *        modulation into the destination value;
 *     4. keeps the advanced state and gives everything back, so LFO1 and LFO2
 *        end the block exactly as stock left them.
 *
 *   The routine's first argument is the block's parameter words: value k of
 *   voice v at words[9 + 79*v + k] (core-dn1's FW_VOICE_PARAM).  LFO1's
 *   settings are values 1-8 (SPD MULT FADE DEST WAVE SPH MODE DEP), LFO2's
 *   9-16, and each takes its settings from there every block, so borrowing a
 *   block of eight values for one call is all it takes.  Each LFO's state is
 *   40 bytes, LFO1 of voice v at FW_LFO(v, 0) and LFO2 at FW_LFO(v, 1); LFOX
 *   keeps its own 40 bytes a voice in the mod.  fw_voice_track[v] says which
 *   track the voice belongs to, and LFOX's settings are per track.
 *
 *   LFO2 is the one borrowed: its eight values hold LFOX's for the second
 *   call.  A destination LFOX aims at inside the borrowed block or at LFO1's
 *   DEP is left as the pass wrote it, so LFOX can modulate LFO1's and LFO2's
 *   own settings -- the stock UI's destination list does not offer those, but
 *   the engine's destination is just a value index 0-78.
 *
 * THE PAGE is the stock LFO page: core-dn1's page 27 carries LFO1's parameter
 * ids, so the stock view itself draws the eight boxes, the knobs, the icons,
 * the labels and their value text.  While page 27 is up LFO1's eight sound
 * slots hold LFOX's values (lfox_lend) and LFO1's own words wait in the mod, so
 * LFO1's own audio is untouched.
 *
 * The KNOBS are the stock UI's: all eight ids are LFO1's, so a turn on this
 * page goes through the firmware's own knob path -- its step, its acceleration,
 * its push for the default, its value flash and its redraw, the same as on
 * LFO1's and LFO2's pages -- and writes LFOX's value into the lent slot.  The
 * tick takes it back out (lfox_adopt): the engine gets the word the knob wrote,
 * the packed copy the whole number.  DEST's turn opens the stock destination
 * list, the same modal LFO1's and LFO2's pages show, and the slot that list
 * confirms is turned back into this mod's list entry by lfox_adopt.
 *
 * SETTINGS are kept with the project: one core-dn1 project data block
 * ("Project data"), eight whole-number bytes a track, saved in the gap of the
 * project's storage block and loaded again with it, and held in RAM (lfox_ram)
 * for the engine, which reads them per voice at interrupt level.  They are not
 * the sound's any more, and that is on purpose: the only sound slots no stock
 * parameter uses are 0, 26 and 65, and machines keeps slot 0 while digitables
 * keeps 26 and 65 (the linker's `soundslot` claims), so a mod that stored in
 * them could not be built beside either.  LFOX stores in none.
 *
 * A project with no block reads the defaults, which are LFO1's own (fw_params
 * ids 49-56: SPD 0x6000, MULT 5, FADE 0x4000, DEST 0, WAVE 0, SPH 0, MODE 0,
 * DEP 0x4000).  Every field keeps the value the stock UI shows as a whole
 * number, so what the page shows and what the engine runs agree.
 */
#include "digitone-mk1/core3.h"

#define LFOX_VOICES     8
#define LFOX_DEP_ZERO   0x4000          /* the depth word's zero (and SPD's) */
#define LFOX_BLOCK_L2   9               /* the second LFO's value block: 9-16 */
#define LFOX_L1_DEP     8               /* LFO1's own depth: value 8 */

/* The stock LFO routine's address in 1.44 (one `jsr` reaches it, at
 * 0x4009DD02).  Another release is another port in mod.json. */
#ifndef LFOX_TICK_ADDR
#define LFOX_TICK_ADDR  0x400FED62UL
#endif

enum {
    C_SPD, C_MULT, C_FADE, C_DEST, C_WAVE, C_SPH, C_MODE, C_DEP, C_COUNT
};

/* Eight values, one byte each: the high byte of the sound word (the stock
 * parameter's value on screen), with MULT 0-23 and SPD/FADE/DEP bipolar
 * about 64 -- exactly what a value word is: value[k] = v[k] << 8. */
struct lfox_state {
    uint8_t v[C_COUNT];
};

struct lfox_target {
    int16_t slot;                       /* -1: none */
    const char *group;                  /* the DEST box's top line */
    const char *name;                   /* and its bottom line */
};

/* LFO1 and LFO2 first, then the filter, amp and synth values a stock LFO can
 * reach (the destination is a value index 0-78; these are the stock list's
 * entries, each mapped to the value it modulates).  Values 0, 26 and 65 hold no
 * stock parameter and are absent: nothing reads them. */
static const struct lfox_target lfox_targets[] = {
    { -1, "",    "---" },
    {  1, "L1",  "SPD" }, {  2, "L1",  "MULT" }, {  3, "L1",  "FADE" }, {  4, "L1",  "DEST" },
    {  5, "L1",  "WAVE" }, {  6, "L1",  "SPH" }, {  7, "L1",  "MODE" }, {  8, "L1",  "DEP" },
    {  9, "L2",  "SPD" }, { 10, "L2",  "MULT" }, { 11, "L2",  "FADE" }, { 12, "L2",  "DEST" },
    { 13, "L2",  "WAVE" }, { 14, "L2",  "SPH" }, { 15, "L2",  "MODE" }, { 16, "L2",  "DEP" },
    { 55, "FLT", "FRQ" }, { 56, "FLT", "RES" }, { 54, "FLT", "TYP" }, { 58, "FLT", "ATK" },
    { 59, "FLT", "DEC" }, { 60, "FLT", "SUS" }, { 61, "FLT", "REL" }, { 57, "FLT", "ENV" },
    { 64, "FLT", "DEL" }, { 62, "FLT", "BAS" }, { 63, "FLT", "WID" },
    { 66, "AMP", "ATK" }, { 67, "AMP", "DEC" }, { 68, "AMP", "SUS" }, { 69, "AMP", "REL" },
    { 70, "AMP", "DRV" }, { 71, "AMP", "PAN" }, { 72, "AMP", "VOL" }, { 73, "AMP", "REV" },
    { 74, "AMP", "DEL" }, { 75, "AMP", "CHO" },
    { 17, "SYN", "ALG" }, { 18, "SYN", "RTC" }, { 19, "SYN", "RTA" }, { 20, "SYN", "RTB" },
    { 21, "SYN", "HRM" }, { 22, "SYN", "DTN" }, { 23, "SYN", "FBK" }, { 24, "SYN", "MIX" },
    { 25, "SYN", "PHR" }, { 27, "SYN", "ATA" }, { 28, "SYN", "DEA" }, { 29, "SYN", "ENA" },
    { 30, "SYN", "LVA" }, { 31, "SYN", "ATB" }, { 32, "SYN", "DEB" }, { 33, "SYN", "ENB" },
    { 34, "SYN", "LVB" }, { 35, "SYN", "ADL" }, { 36, "SYN", "ATR" }, { 37, "SYN", "ARS" },
    { 38, "SYN", "BDL" }, { 39, "SYN", "BTR" }, { 40, "SYN", "BRS" }, { 42, "SYN", "PIT" },
    { 43, "SYN", "P2A" }, { 44, "SYN", "RAL" }, { 45, "SYN", "LAB" }, { 46, "SYN", "AAT" },
    { 47, "SYN", "ADT" }, { 48, "SYN", "AED" }, { 49, "SYN", "AD2" }, { 50, "SYN", "COF" },
    { 51, "SYN", "AOF" }, { 52, "SYN", "B1O" }, { 53, "SYN", "B2O" },
};

#define LFOX_TARGETS ((int32_t)(sizeof lfox_targets / sizeof lfox_targets[0]))

/* The stock parameter records' bounds (fw_params ids 49-56, high bytes). */
static const uint8_t lfox_max[C_COUNT] = {
    127, 23, 127, LFOX_TARGETS - 1, 6, 127, 4, 127
};

/* And their defaults: a new LFOX is a new LFO1. */
static const uint8_t lfox_default[C_COUNT] = {
    96, 5, 64, 0, 0, 0, 0, 64
};

static void lfox_defaults(struct lfox_state *s)
{
    int32_t i;

    for (i = 0; i < C_COUNT; i++)
        s->v[i] = lfox_default[i];
}

/* ---- the settings the engine reads, and where they come from ------------------------------ */

/* The settings of each track, as the sound words' high bytes, for the audio
 * stub; and the same eight values as the words themselves (value << 8). */
/* Both start at the defaults, so a track nothing has read yet is silent
 * (LFO1's own defaults have no depth and no destination) rather than at
 * whatever zero happens to mean in a value word. */
#define LFOX_DEFAULTS { 96, 5, 64, 0, 0, 0, 0, 64 }
#define LFOX_ENG_DEFAULTS { 0x6000, 0x0500, 0x4000, 0, 0, 0, 0, 0x4000 }

volatile uint8_t lfox_ram[4][C_COUNT] = {
    LFOX_DEFAULTS, LFOX_DEFAULTS, LFOX_DEFAULTS, LFOX_DEFAULTS
};                                              /* the host test reads it */
static volatile int16_t lfox_eng[4][C_COUNT] = {
    LFOX_ENG_DEFAULTS, LFOX_ENG_DEFAULTS, LFOX_ENG_DEFAULTS, LFOX_ENG_DEFAULTS
};
static volatile int32_t lfox_live;      /* some track has a depth and a destination */
static volatile int32_t lfox_dirty;     /* the state moved: the page lends again */

/* The value index the tick needs for a track's DEST.  The mod's own list is
 * in page order (LFO1 and LFO2 first, then filter, amp and synth); the tick's
 * destination is the value it adds to, an index 0-78.  A missing
 * destination is written as 0xFF, which the tick reads as -1 and skips. */
static int32_t lfox_dest_value(int32_t track)
{
    return lfox_targets[lfox_ram[track][C_DEST]].slot;
}

/* -> the entry whose slot is `slot`, or entry 0 (none) for a slot the list does
 * not offer.  The page lends DEST as a slot, the engine and the sound hold the
 * entry, and this is the way back. */
static int32_t lfox_dest_entry(int32_t slot)
{
    int32_t i;

    for (i = 0; i < LFOX_TARGETS; i++)
        if (lfox_targets[i].slot == slot)
            return i;
    return 0;
}

static void lfox_eng_update(int32_t track)
{
    int32_t i, slot;

    for (i = 0; i < C_COUNT; i++) {
        if (i == C_DEST) {
            slot = lfox_dest_value(track);
            lfox_eng[track][i] = (int16_t)((slot < 0) ? 0xFF00 : (slot << 8));
            continue;
        }
        lfox_eng[track][i] = (int16_t)((uint16_t)lfox_ram[track][i] << 8);
    }
}

static void lfox_live_update(void)
{
    int32_t t, any = 0;

    for (t = 0; t < 4; t++)
        if (lfox_ram[t][C_DEP] != 64 && lfox_ram[t][C_DEST] != 0)
            any = 1;
    lfox_live = any;
}

/* Clamped on the way in: the engine reads these, so a value the knob cannot
 * reach (a loaded sound, a bad save) never reaches a value word. */
static void lfox_ram_set(int32_t track, const struct lfox_state *s)
{
    int32_t i;

    for (i = 0; i < C_COUNT; i++)
        lfox_ram[track][i] = s->v[i] > lfox_max[i] ? lfox_max[i] : s->v[i];
    lfox_eng_update(track);
    lfox_live_update();
}

static void lfox_ram_get(int32_t track, struct lfox_state *s)
{
    int32_t i;

    for (i = 0; i < C_COUNT; i++)
        s->v[i] = (uint8_t)lfox_ram[track][i];
}

/* Defined with the page's state, below. */
static void lfox_forget_lend(int32_t track);

/* ---- the project data block: where the settings are saved --------------------------------- */

/* Eight whole numbers a track, one byte each.  They go in one core-dn1 project
 * data block (a struct core_projdata, its own 32 bytes in the 480-byte gap of
 * the project's storage block), which core fills and reads at every project
 * load and save.  See the top of this file for why they are not sound slots. */
#define LFOX_TAG        0x4C464F33u     /* 'LFOX' */
#define LFOX_STORE_SIZE (4 * C_COUNT)

static uint8_t lfox_saved[LFOX_STORE_SIZE] __attribute__((aligned(4))) = {
    96, 5, 64, 0, 0, 0, 0, 64,     /* the defaults, a track each: a project */
    96, 5, 64, 0, 0, 0, 0, 64,     /* that saved no block reads these in  */
    96, 5, 64, 0, 0, 0, 0, 64,     /* lfox_loaded's place                  */
    96, 5, 64, 0, 0, 0, 0, 64
};

/* The settings RAM holds into the block (what a save copies). */
static void lfox_block_put(int32_t track)
{
    struct lfox_state c;
    int32_t i;

    if (track < 0 || track > 3)
        return;
    lfox_ram_get(track, &c);
    for (i = 0; i < C_COUNT; i++)
        lfox_saved[track * C_COUNT + i] = c.v[i];
}

/* The block -> the settings the engine reads, every track. */
static void lfox_block_load(void)
{
    struct lfox_state s;
    int32_t t, i;

    for (t = 0; t < 4; t++) {
        for (i = 0; i < C_COUNT; i++)
            s.v[i] = lfox_saved[t * C_COUNT + i];
        lfox_ram_set(t, &s);
    }
    lfox_dirty = 1;                     /* the page re-lends from the new values */
}

/* core calls this after a project load, and for a new project, with the block's
 * bytes already in lfox_saved; `found` is 0 when the project has none (a new
 * project, one saved without LFOX, or a block of another size) and the bytes are
 * zeros, which would be a silent LFOX -- so the defaults go in their place. */
static void lfox_loaded(int32_t found)
{
    struct lfox_state s;
    int32_t t, i;

    if (!found) {
        lfox_defaults(&s);
        for (t = 0; t < 4; t++)
            for (i = 0; i < C_COUNT; i++)
                lfox_saved[t * C_COUNT + i] = s.v[i];
    }
    lfox_block_load();
    /* The kit the page lent LFO1's slots from may be gone with the project. */
    for (t = 0; t < 4; t++)
        lfox_forget_lend(t);
}

const struct core_projdata lfox_store = {
    LFOX_TAG, LFOX_STORE_SIZE, lfox_saved, lfox_loaded
};

/* A track's settings changed: RAM and the block, without touching the engine
 * (which may hold a value the stock knob has stepped more finely than a whole
 * number).  The block is stamped into the project the next time it is saved. */
static void lfox_save(int32_t track)
{
    lfox_block_put(track);
    lfox_dirty = 1;
}

/* ---- the engine: the stock LFO routine, a second time -------------------------------- */

/* The page's lend leaves LFOX's settings in the active track's sound slots,
 * which the render has read into each voice's words by the time the LFOs run.
 * Those words are what the stock pass reads, so this gives that track's voices
 * LFO1's own settings back for it.  Defined below, with the page's state. */
static void lfox_lent_words(int16_t *words);

/* The stock routine, as the firmware calls it: six words on the stack, the
 * first the block's parameter words (which the device's C ABI passes as a
 * pointer, as core-dn1's own render hooks do). */
typedef void (*lfox_tick_fn)(int16_t *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

/* The stock routine; a host test replaces this with its own. */
lfox_tick_fn lfox_tick_impl = (lfox_tick_fn)LFOX_TICK_ADDR;

/* Where the tick's iterations look, in words of fw_lfo_state (which the core
 * exports 40 bytes below the array the routine itself walks).  The routine
 * loads 0x419EB33C (one record above fw_lfo_state) as a voice's base and runs
 * slots 9-16 FIRST, at that base, then slots 1-8 one record lower.  So the
 * record the borrowed (slots 9-16) pass keeps is the base itself, its phase at
 * base + 40, i.e. 20 words above fw_lfo_state -- and the voice's other record
 * (slots 1-8) starts 10 words above.  LFOX's state goes in the slots 9-16
 * record, 10 words higher than where 1.0 put it: 1.0 wrote the slots 1-8
 * record instead, so its pass advanced LFOX's phase with LFO1's own SPD and
 * MULT, which is why SPD and MULT did nothing on the unit. */
#define LFOX_ST_SAVE    10              /* both of the voice's records, from here */
#define LFOX_ST_PASS    20              /* the slots 9-16 record: LFOX's */

static int32_t lfox_st[LFOX_VOICES][10];        /* LFOX's own 40 bytes a voice */
static int16_t lfox_save_cfg[LFOX_VOICES][8];   /* what the second call borrows */
static int16_t lfox_save_dep[LFOX_VOICES];
static int32_t lfox_save_st[LFOX_VOICES][20];   /* both LFOs' state, this block */
static const int16_t lfox_silent[C_COUNT] = LFOX_ENG_DEFAULTS;   /* no depth: nothing */

/* LFOX's modulation in the voice's own parameter word.  The pass writes it into
 * the block's parameter words (`words`), which is where LFO1's and LFO2's
 * modulation goes too, and that is what the render reads for a stock parameter.
 * A machine mod's knob is not read from there: machines-dn1's dnm_knob returns
 * FW_VOICE_PARAM(voice, slot), the word the render starts the block from, and a
 * machine reads its knobs itself (the two stock LFOs reach a machine parameter
 * only because the machine adds their records -- `FW_LFO_DEST`/`FW_LFO_MOD` --
 * to its knobs, and there is no third record it would look for).  So LFOX puts
 * its modulation in the word as well, and takes the pass's own write back out
 * of the block, so a parameter is modulated once whichever reads it. */
static int16_t lfox_pre[LFOX_VOICES];    /* the destination word before the pass */
static int16_t lfox_add[LFOX_VOICES];    /* what LFOX has added to the word */
static int16_t lfox_after[LFOX_VOICES];  /* the word as LFOX left it */
static int16_t lfox_slot[LFOX_VOICES];   /* the slot it was added to */

static void lfox_word_release(int32_t v)
{
    volatile int16_t *p;

    if (!lfox_add[v])
        return;
    p = &FW_VOICE_PARAM(v, lfox_slot[v]);
    /* Only what is still there is taken out: a knob, a p-lock or a sound load
     * since the last block has written the word, and that value is the base. */
    if (*p == lfox_after[v])
        *p = (int16_t)(*p - lfox_add[v]);
    lfox_add[v] = 0;
}

/* slot < 0: no destination, so nothing of LFOX stays in any word. */
static void lfox_word(int32_t v, int32_t slot, int32_t mod)
{
    volatile int16_t *p;
    int32_t cur, base, out;

    if (slot < 0 || slot > 78) {
        lfox_word_release(v);
        return;
    }
    if (lfox_add[v] && lfox_slot[v] != slot)
        lfox_word_release(v);
    p = &FW_VOICE_PARAM(v, slot);
    cur = *p;
    base = (lfox_add[v] && cur == lfox_after[v]) ? cur - lfox_add[v] : cur;
    out = base + mod;
    if (out > 32767)
        out = 32767;
    else if (out < -32768)
        out = -32768;
    *p = (int16_t)out;
    lfox_add[v] = (int16_t)(out - base);
    lfox_after[v] = (int16_t)out;
    lfox_slot[v] = (int16_t)slot;
}

/* The `jsr 0x400FED62` at 0x4009DD02 now reaches this: the same six arguments,
 * the same registers (d2-d7/a2-a6 are the compiler's to keep), and the stock
 * routine's own call, so nothing about its contract changes. */
void lfox_audio(int16_t *words, uint32_t rate, uint32_t mask, uint32_t mask2,
                uint32_t arg4, uint32_t arg5)
{
    volatile int16_t *val = words;
    volatile int32_t *st = fw_lfo_state;
    const volatile int16_t *set;
    int32_t v, i, track, dest;

    lfox_lent_words(words);         /* LFO1's own settings, while the page lends */
    lfox_tick_impl(words, rate, mask, mask2, arg4, arg5);   /* LFO1 and LFO2, stock */

    if (!lfox_live) {
        for (v = 0; v < LFOX_VOICES; v++)
            lfox_word_release(v);
        return;
    }

    if (arg4 != 0xFFFFFFFFu || arg5 != 0xFFFFFFFFu) {
        /* No LFOX this block: the words keep nothing of the last one. */
        for (v = 0; v < LFOX_VOICES; v++)
            lfox_word_release(v);
        /* The reset call: the routine puts its pristine state back over the
         * live records, so this block computes no LFOX -- the next one will,
         * from the carried state.  LFOX's own state is left alone here: the
         * firmware issues this call when a voice is initialised, not only in a
         * retriggering mode, and zeroing LFOX's record for it restarted a
         * free-running LFOX on every note and threw away its fade state (the
         * unit's "it retriggers in FRE" and "the depth fades in by itself"
         * reports, both from this line).  Whatever `MODE` says is honoured by
         * the routine itself, on the same gate and the same per-voice start
         * flag it uses for LFO1 and LFO2, so the mod has nothing to add. */
        return;
    }

    /* 1. What the second call borrows: LFO2's eight values, LFO1's depth, and
     *    both LFOs' state for this block.  Taken after the stock call, so the
     *    stock modulation of those words rides along. */
    for (v = 0; v < LFOX_VOICES; v++) {
        volatile int16_t *w = val + 79 * v;

        for (i = 0; i < 8; i++)
            lfox_save_cfg[v][i] = w[9 + LFOX_BLOCK_L2 + i];
        lfox_save_dep[v] = w[9 + LFOX_L1_DEP];
        for (i = 0; i < 20; i++)
            lfox_save_st[v][i] = st[20 * v + LFOX_ST_SAVE + i];
    }

    /* 2. LFOX's settings and state in their place, LFO1 quiet for the pass.
     *    Value 8 is LFO1's depth: with it at its zero the stock routine adds
     *    nothing for LFO1 (it still writes value[DEST] = value[DEST] + 0). */
    for (v = 0; v < LFOX_VOICES; v++) {
        volatile int16_t *w = val + 79 * v;

        track = fw_voice_track[v];
        dest = (track >= 0 && track <= 3) ? lfox_dest_value(track) : -1;
        lfox_pre[v] = (dest >= 0 && dest <= 78) ? w[9 + dest] : 0;
        set = (track >= 0 && track <= 3) ? lfox_eng[track] : lfox_silent;
        for (i = 0; i < 8; i++)
            w[9 + LFOX_BLOCK_L2 + i] = set[i];
        w[9 + LFOX_L1_DEP] = LFOX_DEP_ZERO;
        for (i = 0; i < 10; i++)
            st[20 * v + LFOX_ST_PASS + i] = lfox_st[v][i];
    }

    /* 3. The pass that computes LFOX and writes its modulation. */
    lfox_tick_impl(words, rate, mask, mask2, arg4, arg5);

    /* 4. LFOX's advanced state is kept and everything else handed back.  The
     *    destination's word goes back to what the first pass left, and the
     *    modulation it gained goes into the voice's own parameter word, where a
     *    machine mod's dnm_knob and the render's next block both read it.  A
     *    destination inside the borrowed block or at LFO1's depth means LFOX
     *    modulating that LFO's own settings for this block: the word is the
     *    same one either way. */
    for (v = 0; v < LFOX_VOICES; v++) {
        volatile int16_t *w = val + 79 * v;
        int32_t mod = 0;

        track = fw_voice_track[v];
        dest = (track >= 0 && track <= 3) ? lfox_dest_value(track) : -1;
        /* What the pass added for LFOX, and the block's word as it was before
         * it: the modulation is carried to the voice's own word instead, so the
         * parameter is not modulated twice and a machine knob sees it. */
        if (dest >= 0 && dest <= 78) {
            mod = w[9 + dest] - lfox_pre[v];
            w[9 + dest] = lfox_pre[v];
        }
        for (i = 0; i < 10; i++)
            lfox_st[v][i] = st[20 * v + LFOX_ST_PASS + i];
        for (i = 0; i < 20; i++)
            st[20 * v + LFOX_ST_SAVE + i] = lfox_save_st[v][i];
        for (i = 0; i < 8; i++)
            if (LFOX_BLOCK_L2 + i != dest)
                w[9 + LFOX_BLOCK_L2 + i] = lfox_save_cfg[v][i];
        if (LFOX_L1_DEP != dest)
            w[9 + LFOX_L1_DEP] = lfox_save_dep[v];
        lfox_word(v, dest, mod);
    }
}/* A note-on does NOT reset LFOX from here.  It used to, for every mode but the
 * one this file guessed was free-run, and that guess is the bug the unit
 * reported: with the page showing `FRE`, every note zeroed the voice's whole
 * 40-byte record -- the phase (so a free-running LFOX restarted on each note)
 * and the fade state (so the first few cycles after a note swept at a depth the
 * fade then walked back from, which sounds exactly like `FADE` being on).  The
 * engine already does whatever its mode says with LFOX's settings and the
 * voice's gate, the same way it does for LFO1 and LFO2, and it resets a record
 * by copying a pristine one rather than by zeroing the live state.  So the mod
 * keeps its hands off and the mode on the page is the mode that runs. */




/* ---- the page ----------------------------------------------------------------------------- */

/* core-dn1 writes view and pos into this record.  The ids are LFO1's eight,
 * all of them, so this is the stock LFO page as the firmware's own view builds
 * it: the knobs, the fade and mode graphics, the labels, the value text and the
 * DEST box's frame and destination name, exactly as for page 10.  While the
 * page is up LFO1's own slots hold LFOX's values: lfox_lend keeps them there,
 * lfox_audio gives the voices LFO1's own words for the block, and a key or
 * leaving the page gives the sound them back. */
struct core_page lfox_page = {
    /* "LFO", not "LFOX": the stock view heads a view's pages with the page's
     * title and a count of the view's pages -- "LFO (3/3)" -- so the 3 belongs
     * to that count, not to the name. */
    27, 11, "LFO", "LFO",
    { 49, 50, 51, 52, 53, 54, 55, 56 },
    9, 0, 0
};

#define LFOX_BORROW     8               /* LFO1's eight slots, SPD first */
#define LFOX_SLOT_SPD   1
#define LFOX_WORD_L1    9               /* LFO1's eigth words in a block: 9 + slot */

static void *lfox_brain;
static int32_t lfox_shown;
static int32_t lfox_press, lfox_opening;
static int32_t lfox_ptn;                /* PTN is held */
static int32_t lfox_func;               /* FUNC is held: its hints are the
                                         * page's own, not the strip's */
static volatile int32_t lfox_lend_track = -1;   /* the track whose slots the page lends */
static volatile int32_t lfox_lent;              /* 1 while the slots hold LFOX's values */
static volatile int32_t lfox_patch_track = -1;  /* whose voices still need LFO1's words */
static int16_t lfox_keep[LFOX_BORROW];  /* LFO1's words, while the slots are lent */
static int16_t lfox_shadow[LFOX_BORROW];/* LFOX's values, as the slots hold them */
static int32_t lfox_unlend_wait;        /* ticks left to put LFO1's words back */
static uint8_t *lfox_kit_seen;          /* the kit the settings were read from */
static int32_t lfox_track_seen = -1;

/* What the sound's slots hold while the page lends: LFOX's eight words as the
 * *engine* has them, not the whole-number copy the three free slots can pack.
 * That matters for the stock knobs, which step in finer units than a whole
 * number -- SPD moves 0x20 of an 8.8 word, so its whole number changes once in
 * eight -- and their value has to stay where the knob left it, or the box would
 * jump back under the knob.  DEST is the one that is not the setting itself:
 * the stock view names the parameter its value points at, so the slot gets that
 * parameter's index (the mod's own list entry through lfox_targets), or 0 for
 * no destination, which is what the stock page shows for one. */
static void lfox_shadow_make(int32_t track)
{
    int32_t i, slot;

    for (i = 0; i < LFOX_BORROW; i++) {
        if (i == C_DEST) {
            slot = lfox_dest_value(track);
            lfox_shadow[i] = (int16_t)(((slot < 0) ? 0 : slot) << 8);
            continue;
        }
        lfox_shadow[i] = lfox_eng[track][i];
    }
}

/* The UI task, while the page is on screen: LFO1's slots hold LFOX's values.  The
 * first lend keeps LFO1's own words; a later one only refreshes LFOX's. */
static void lfox_give_back(void);

static void lfox_lend(int32_t track)
{
    int32_t i;

    if (!fw_kit || track < 0 || track > 3)
        return;
    if (lfox_lend_track != track) {
        lfox_give_back();
        for (i = 0; i < LFOX_BORROW; i++)
            lfox_keep[i] = (int16_t)FW_SOUND_SLOT(fw_kit, track, LFOX_SLOT_SPD + i);
        lfox_lend_track = track;
    }
    lfox_shadow_make(track);
    for (i = 0; i < LFOX_BORROW; i++)
        FW_SOUND_SLOT(fw_kit, track, LFOX_SLOT_SPD + i) = lfox_shadow[i];
    lfox_lent = 1;
}

/* The block's words, for the block the stock pass is about to read: with the
 * page lending, the active track's voices hold LFOX's settings there, and the
 * stock LFOs must run on LFO1's own.  The sound is left alone -- it keeps
 * LFOX's values for the page -- so there is nothing to put back afterwards.
 *
 * Gated on the page's track, not on `lfox_lent`: a key event gives the sound its
 * words back (a save must copy LFO1's own), and lfox_lend sets that flag again
 * on the next tick -- but the audio blocks in between would then run LFO1 on
 * LFOX's settings, which is a track's pitch following LFOX's destination.
 *
 * AND after the page has gone.  Measured:
 * the sound is LFO1's own again the moment the page is left, while a voice that
 * played during the visit still carries LFOX's SPD and DEP with LFO1's own DEST
 * -- and the render puts those values back every block, so the voice keeps
 * modulating its destination at LFOX's rate and depth until LFOX's page is
 * shown again, which is exactly how the unit reports it.  lfox_put_back now
 * goes through core_sound_set (the stock edit's own path to a voice), and this
 * stays as the guard for anything that still holds a lent value: a word an LFO
 * has since written is left alone, and a word that only happens to equal the
 * lent value is written with the same value it already has. */
static void lfox_lent_words(int16_t *words)
{
    volatile int16_t *val = words;
    int32_t v, i, track;

    track = lfox_lent ? lfox_lend_track : lfox_patch_track;
    if (track < 0)
        return;
    for (v = 0; v < LFOX_VOICES; v++) {
        volatile int16_t *w;

        if (fw_voice_track[v] != track)
            continue;
        w = val + 79 * v + LFOX_WORD_L1 + LFOX_SLOT_SPD;
        if (lfox_lent) {
            for (i = 0; i < LFOX_BORROW; i++)
                w[i] = lfox_keep[i];
        } else {
            for (i = 0; i < LFOX_BORROW; i++)
                if (w[i] == lfox_shadow[i])
                    w[i] = lfox_keep[i];
        }
    }
}

/* How many ticks after a hand-back LFO1's words are written again.  The stock
 * view keeps the eight parameters' values itself while the page is up, and a
 * page close (or the next key) can copy them back to the sound after this
 * module has written LFO1's own into it -- which is the unit's "leave LFOX's
 * page and LFO1 starts running LFOX's settings: its waveform, its rate, its
 * depth".  So the hand-back is repeated for a few ticks, after the firmware's
 * own write, and only while the page is not shown (so a visit is never
 * disturbed). */
#define LFOX_UNLEND_TICKS 6

/* LFO1 has its own words in the sound again: on a key event, so that a save, a
 * pattern change or a menu key copies LFO1's own settings, and when the page is
 * left.  The page's track stays known, so the next tick lends them back. */
static void lfox_put_back(int32_t track)
{
    int32_t i;

    if (!fw_kit || track < 0 || track > 3)
        return;
    /* core_sound_set, not a write to the kit: measured,
     * the render puts the sound's values back into a voice every block from the
     * copy a stock edit leaves with it, so a kit write alone is overwritten in
     * the voices on the next block -- which is the unit's "leave LFOX's page and
     * LFO1 modulates at LFOX's rate and depth".  core_sound_set is the stock
     * edit's own path to a voice. */
    for (i = 0; i < LFOX_BORROW; i++)
        core_sound_set(track, LFOX_SLOT_SPD + i, lfox_keep[i]);
}

/* The lent record is gone with the sound it came from: forget the lend without
 * writing anything back, so the next visit keeps LFO1's words from the sound
 * that is there now. */
static void lfox_forget_lend(int32_t track)
{
    if (track != lfox_lend_track)
        return;
    lfox_lent = 0;
    lfox_lend_track = -1;
    lfox_patch_track = -1;
}

static void lfox_give_back(void)
{
    if (!lfox_lent)
        return;
    lfox_lent = 0;
    lfox_put_back(lfox_lend_track);
    lfox_unlend_wait = LFOX_UNLEND_TICKS;
    lfox_patch_track = lfox_lend_track;
}

/* ---- the preview: the waveform strip over the WAVE and SPH cells -------------------------- */

/* The stock LFO page draws one display across its WAVE and SPH cells: a bracket
 * at each end, one period of the chosen waveform between them, and the start
 * phase as a hollow square on the curve.  That drawing is the stock view's own
 * and is bound to LFO1's and LFO2's records, so a mod page's two cells stay
 * empty -- the labels under two blank cells, which is what the page looked like
 * until now.  The mod draws the same display itself, to the stock page's own
 * pixels (measured off it):
 *
 *   `[` at x 25, one period over columns 32-60, `]` at x 67; rows 41-55 with
 *   the mid line at 48 and dots a quarter period apart; the phase square three
 *   pixels wide, its middle on the curve.
 *
 * Stock draws the curve as a blit of prepared art, one bitmap a waveform, which
 * is why a plotted approximation never matched it; the shapes below are that
 * art, read back from the page's own pixels.  y is the firmware's: 0 is the
 * screen's bottom row (row 55 is 8), and fw_fillrect's colour 1 sets. */
#define LFOX_PV_L       25      /* the strip's opening bracket */
#define LFOX_PV_R       67      /* and its closing one */
#define LFOX_PV_X       32      /* the curve's first column */
#define LFOX_PV_BOT      8      /* the curve's lowest row (row 55) */
#define LFOX_PV_MID     15      /* its mid line (row 48) */
#define LFOX_PV_TOP     22      /* and its highest (row 41) */

/* The seven shapes, as the stock page plots them: one period over columns 0-28
 * (x 32-60), each column holding the rows between the sample before it and its
 * own, so a slope of one is a pixel and a square wave's jump is a vertical line.
 * Two int8 a column, the first and last row, offset from the mid line.
 *
 * Measured, not modelled: the stock page's own
 * pixels for every waveform at SPH 0, read back as runs, re-rendered and diffed
 * -- zero differing pixels on all seven.  An earlier version plotted one pixel
 * a column, which is right for TRI and left the sine, the square, the saw, the
 * exponential, the ramp and RND with gaps where they jump. */
#define LFOX_PV_COLS    29      /* a period: columns 0-28, the last repeat of
                                   the first, as stock wraps it */

static const int8_t lfox_lo[7][LFOX_PV_COLS] = {
    /* TRI */ { 0, 1, 2, 3, 4, 5, 6, 7, 6, 5, 4, 3, 2, 1, 0, -1, -2, -3, -4, -5, -6, -7, -6, -5, -4, -3, -2, -1, 0 },
    /* SIN */ { 0, 2, 3, 5, 6, 6, 7, 7, 7, 6, 6, 5, 3, 1, 0, -2, -4, -5, -6, -6, -7, -7, -7, -6, -6, -5, -4, -2, 0 },
    /* SQR */ { -7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, -7, -7, -7, -7, -7, -7, -7, -7, -7, -7, -7, -7, -7, -7, -7 },
    /* SAW */ { -7, 6, 6, 5, 5, 4, 4, 3, 3, 2, 2, 1, 1, 0, 0, -1, -1, -2, -2, -3, -3, -4, -4, -5, -5, -6, -6, -7, -7 },
    /* EXP */ { 0, 5, 4, 4, 3, 3, 3, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    /* RMP */ { 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    /* RND */ { 0, 0, 0, -4, -4, -4, -4, -4, 3, 3, 3, 7, 7, 7, -2, -2, -2, -7, -7, -7, -7, -7, 3, 3, 3, 0, 0, 0, 0 },
};

static const int8_t lfox_hi[7][LFOX_PV_COLS] = {
    /* TRI */ { 0, 1, 2, 3, 4, 5, 6, 7, 6, 5, 4, 3, 2, 1, 0, -1, -2, -3, -4, -5, -6, -7, -6, -5, -4, -3, -2, -1, 0 },
    /* SIN */ { 0, 2, 4, 5, 6, 6, 7, 7, 7, 6, 6, 5, 4, 2, 0, -1, -3, -5, -6, -6, -7, -7, -7, -6, -6, -5, -3, -1, 0 },
    /* SQR */ { 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, -7, -7, -7, -7, -7, -7, -7, -7, -7, -7, -7, -7, -7, 7 },
    /* SAW */ { 7, 6, 6, 5, 5, 4, 4, 3, 3, 2, 2, 1, 1, 0, 0, -1, -1, -2, -2, -3, -3, -4, -4, -5, -5, -6, -6, -7, 7 },
    /* EXP */ { 7, 5, 4, 4, 3, 3, 3, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    /* RMP */ { 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    /* RND */ { 0, 0, 0, 0, -4, -4, -4, 3, 3, 3, 7, 7, 7, 7, 7, -2, -2, -2, -7, -7, -7, 3, 3, 3, 3, 3, 0, 0, 0 },
};

/* A strip bracket at x: a vertical line with a gap at the mid line, a pixel
 * outside it above and below that gap, and two pixels of serif at the top and
 * the bottom, pointing into the strip.  `open`: the left one. */
static void lfox_bracket(void *bmp, int32_t x, int32_t open)
{
    int32_t y, out = open ? x - 1 : x + 1;

    for (y = LFOX_PV_BOT; y <= LFOX_PV_TOP; y++)
        if (y != LFOX_PV_MID)
            fw_fillrect(bmp, x, y, x, y, 1);
    fw_fillrect(bmp, open ? x + 1 : x - 2, LFOX_PV_TOP + 1,
                open ? x + 2 : x - 1, LFOX_PV_TOP + 1, 1);
    fw_fillrect(bmp, open ? x + 1 : x - 2, LFOX_PV_BOT - 1,
                open ? x + 2 : x - 1, LFOX_PV_BOT - 1, 1);
    fw_fillrect(bmp, out, LFOX_PV_MID + 1, out, LFOX_PV_MID + 1, 1);
    fw_fillrect(bmp, out, LFOX_PV_MID - 1, out, LFOX_PV_MID - 1, 1);
}

/* The display, cleared and drawn: the brackets, the curve, its quarter marks
 * and the phase square.  Nothing else is drawn between the brackets, so the
 * clear takes the whole interior.
 *
 * Two things here are stock's and were measured off it
 * ("Stock LFO1's and LFO2's
 * strips"): the curve is the period that starts at `SPH` -- the shape is
 * rotated, not merely marked -- and the hollow square is drawn only where that
 * start sample sits on the mid line, which is why stock shows it for TRI, SIN,
 * EXP and RMP at phase 0 and not for SQR, SAW or RND, nor at half a period. */
static void lfox_strip(void *bmp, int32_t wave, int32_t phase, int32_t right)
{
    int32_t c, col, idx, lo, hi, x, y;

    if (wave < 0 || wave > 6)
        wave = 0;
    if (phase < 0)
        phase = 0;
    if (phase > 127)
        phase = 127;
    fw_fillrect(bmp, LFOX_PV_L + 1, LFOX_PV_BOT - 1,
                right < LFOX_PV_R - 1 ? right : LFOX_PV_R - 1,
                LFOX_PV_TOP + 1, 0);
    lfox_bracket(bmp, LFOX_PV_L, 1);
    if (right >= LFOX_PV_R)
        lfox_bracket(bmp, LFOX_PV_R, 0);
    /* the mid line's dots, a quarter period apart, before the curve so the
     * curve covers them where it passes: stock's own layering */
    for (c = 0; c < LFOX_PV_COLS; c += LFOX_PV_COLS / 4)
        if (LFOX_PV_X + c <= right)
            fw_fillrect(bmp, LFOX_PV_X + c, LFOX_PV_MID, LFOX_PV_X + c, LFOX_PV_MID, 1);
    /* a column at a time, from the row the previous sample sat on to its own,
     * which is what makes a square wave's jump a vertical line.  The table is
     * one period, so the phase is an offset into it: column 0 shows the sample
     * `SPH` points at, and the period runs from there, as stock's does. */
    col = (phase * (LFOX_PV_COLS - 1) + 64) / 127;
    for (c = 0; c < LFOX_PV_COLS; c++) {
        if (LFOX_PV_X + c > right)
            break;
        idx = (c + col) % (LFOX_PV_COLS - 1);
        lo = lfox_lo[wave][idx];
        hi = lfox_hi[wave][idx];
        if (lo > hi) {
            y = lo;
            lo = hi;
            hi = y;
        }
        fw_fillrect(bmp, LFOX_PV_X + c, LFOX_PV_MID - hi,
                    LFOX_PV_X + c, LFOX_PV_MID - lo, 1);
    }
    /* The period's start, as stock marks it: a hollow square at the curve's
     * first column, on the mid line, drawn only where the sample the period
     * starts on is the mid line's own row.  Stock draws it for TRI, SIN, EXP
     * and RMP at phase 0, not for SQR, SAW or RND (their first column is a
     * jump), and not at half a period; measured for every waveform at phase 0,
     * 32, 64 and 96. */
    if (wave == 6 || (lfox_lo[wave][col] != 0 && lfox_hi[wave][col] != 0))
        return;
    x = LFOX_PV_X;
    y = LFOX_PV_MID;
    fw_fillrect(bmp, x, y, x, y, 0);             /* the hole, over the curve */
    fw_fillrect(bmp, x - 1, y - 1, x + 1, y - 1, 1);
    fw_fillrect(bmp, x - 1, y + 1, x + 1, y + 1, 1);
    fw_fillrect(bmp, x - 1, y, x - 1, y, 1);
    fw_fillrect(bmp, x + 1, y, x + 1, y, 1);
}

/* Is a stock layer over the page -- the destination list, a menu, a dialog?
 *
 * core_page_shown says no for the destination list, because the list is not
 * another screen in the dispatcher's sense: it is a layer pushed on this
 * screen's stack.  The brain's +0x58 is the top layer, and the page's own is
 * the bottom of that stack -- the one whose +4 points at itself minus 0x10
 * (the stack's header sits there).  Measured on the unit's build for the
 * destination list on both stock's LFO page and the mod's (00x41c185a0 plain,
 * below 0x41c18590; a list open, a new top whose below is 0x41c185a0), see
 * Anything that cannot be read as that stack -- a
 * field this OS does not keep, memory that is not RAM -- is taken as "nothing
 * over the page": the strip is then drawn as before, and a field this code
 * reads wrong can only cost the fix, never the page.
 *
 * This is *not* the same question as "may the strip be drawn": stock draws
 * the strip as part of the page and the overlays land on it, so both the
 * destination list and FUNC's hints leave it visible (measured on stock's
 * page: with FUNC held and with the list open the strip's own pixels are
 * unchanged).  The mod draws after all of them, so it
 * takes the columns that are free instead: the list's own text starts at
 * column 52, and FUNC's hints stay out of the strip's box altogether. */
static int32_t lfox_covered(void)
{
    uintptr_t brain = (uintptr_t)lfox_brain;
    uintptr_t layer, below;

    if (!brain)
        return 0;
    layer = *(volatile uint32_t *)(brain + 0x58);
    if (layer < 0x40000000UL || layer >= 0x49000000UL)
        return 0;
    below = *(volatile uint32_t *)(layer + 4);
    if (below < 0x40000000UL || below >= 0x49000000UL)
        return 0;
    return below != layer - 0x10;
}

/* The last column the strip may paint in this frame.  With nothing over the
 * page the whole strip; under the destination list only up to the column the
 * list's text begins at -- the left bracket, the mid line's dots and the left
 * half of the curve stay, where 1.12 dropped the lot.  FUNC's hints are drawn
 * outside the strip's box, so they clip nothing: with FUNC held the strip is
 * whole, as stock's is. */
#define LFOX_PV_FREE   LFOX_PV_R
#define LFOX_PV_LIST   51

/* After each frame the stock view has drawn: the page is ours, so its display
 * is drawn again.  Gated on firmware state (the page being the one shown, and
 * what is over it), never on pixels. */
void lfox_draw(void *bmp, void *ctrl)
{
    int32_t t, right;

    (void)ctrl;
    if (!lfox_brain || !core_page_shown(lfox_brain, &lfox_page))
        return;
    t = fw_active_track;
    if (t < 0 || t > 3)
        return;
    right = (lfox_covered() && !lfox_func) ? LFOX_PV_LIST : LFOX_PV_FREE;
    lfox_strip(bmp, lfox_ram[t][C_WAVE], lfox_ram[t][C_SPH], right);
}

/* ---- the UI task -------------------------------------------------------------------------- */

/* Is the page up, with a stock menu or list over it or without?
 *
 * core_page_shown answers the stricter question -- its own comment says "with
 * no overlay over it" -- and that is what the drawing wants: over a stock list
 * the mod must not paint.  The settings want the looser one.  DEST's turn now
 * belongs to the stock view (below) and the destination list it opens is a
 * modal: while that list is up the page is still the bottom screen's, and the
 * choice it confirms goes into the sound's DEST slot, where lfox_adopt reads
 * it.  Read as "the page has gone", the modal would hand LFO1 its own words
 * back mid-edit and then, on the way out, lend LFOX's old destination over the
 * choice just made -- losing it, and giving LFO1 LFOX's destination for as
 * long.  So the lend and the adopt follow core_page_shown's own checks -- the
 * page manager's view, and the page's place in that view's list -- without the
 * one for a screen on top (core-dn1's mods/core-dn1/pages.s). */
static int32_t lfox_page_up(void *brain)
{
    void *screen, *mgr, *view;

    if (!brain || !lfox_page.view)
        return 0;
    screen = *(void **)((char *)brain + 0x54);   /* the pages' screen */
    if (!screen)
        return 0;
    mgr = *(void **)((char *)screen + 8);        /* its page manager */
    if (!mgr)
        return 0;
    view = *(void **)((char *)mgr + 0xc0);       /* the view it shows */
    if (view != lfox_page.view)
        return 0;
    return *(int32_t *)((char *)view + 144) == lfox_page.pos;
}

/* All eight knobs are the stock UI's, DEST included.  Their ids are LFO1's and
 * their slots are the ones the page lends, so the stock knob path edits, steps,
 * accelerates, draws them and flashes their value exactly as on LFO1's and
 * LFO2's pages, and the tick takes the value back out of the sound
 * (lfox_adopt).  DEST was the exception -- the mod answered its encoder itself,
 * one step per two detents -- because its own list holds LFO1's and LFO2's
 * settings, which the stock list does not offer.  On the unit that one knob was
 * the only one that jumped and lagged: the stock knob path gives a knob the
 * stock pace, a bounce-free dead zone and a frame a step, and a hand-rolled
 * one has to match all three.  Handing it back gives LFOX the stock destination
 * list -- the same modal LFO1's and LFO2's DEST opens, with the same names and
 * the same confirm -- and the mod keeps the slots' meaning: lfox_adopt turns
 * whichever slot the list confirms into this list's entry, so what the box
 * names and what the engine modulates stay one number.  A sound that carries
 * one of the 16 LFO destinations (written before this change, or saved from
 * one) still modulates it: the list is the only thing that does not offer
 * them. */

/* The stock knob path, on this page: a turn writes the parameter's slot in the
 * track sound -- the slot the page lends it -- and the stock view redraws from
 * there itself, flash and all.  This takes the value back out of the sound, once
 * a tick: the engine gets the knob's own word (the fine steps too), the packed
 * copy the whole number a save keeps, and a knob push (the stock default)
 * arrives the same way. */
static int32_t lfox_adopt(int32_t t)
{
    struct lfox_state s;
    int32_t i, v, w, slot, changed = 0;

    lfox_ram_get(t, &s);
    for (i = 0; i < LFOX_BORROW; i++) {
        w = (int32_t)(uint16_t)FW_SOUND_SLOT(fw_kit, t, LFOX_SLOT_SPD + i);
        if (w == (int32_t)(uint16_t)lfox_shadow[i])
            continue;
        lfox_shadow[i] = (int16_t)w;
        v = ((uint16_t)w >> 8);
        if (i == C_DEST) {
            /* The lent slot holds the *slot* the engine adds to -- that is the
             * value the stock view names -- while the setting and the packed
             * state are this list's entry.  Reading the slot as an entry aimed
             * LFOX at whatever that number happens to be (entry 55 is SYN ADL,
             * not the filter the box reads), so it is converted back here. */
            s.v[i] = (uint8_t)lfox_dest_entry(v);
        } else {
            if (v > lfox_max[i])
                v = lfox_max[i];
            s.v[i] = (uint8_t)v;
            lfox_eng[t][i] = (int16_t)w;
        }
        changed = 1;
    }
    if (!changed)
        return 0;
    for (i = 0; i < C_COUNT; i++)
        lfox_ram[t][i] = s.v[i];
    slot = lfox_dest_value(t);
    lfox_eng[t][C_DEST] = (int16_t)((slot < 0) ? 0xFF00 : (slot << 8));
    lfox_live_update();
    lfox_save(t);
    return 1;
}

void lfox_tick(void *ctrl)
{
    int32_t shown, t;

    (void)ctrl;
    if (!lfox_brain)
        return;
    /* The settings follow the project, not the sound: only the lend needs the
     * kit, and a kit, sound or project load takes that sound's LFO1 words with
     * it, so the lend is forgotten for the sound that is there now. */
    if (fw_kit != lfox_kit_seen || fw_active_track != lfox_track_seen) {
        for (t = 0; t < 4; t++)
            lfox_forget_lend(t);
        lfox_dirty = 1;
        lfox_kit_seen = fw_kit;
        lfox_track_seen = fw_active_track;
    }

    shown = lfox_page_up(lfox_brain);
    lfox_shown = shown;
    if (!shown) {
        lfox_give_back();
        /* The hand-back again, a few ticks on: the firmware writes the page's
         * own eight values to the sound as it closes, and that copy has to be
         * overwritten before LFO1 runs on it.  Once lend_track is -1 the sound
         * is not ours any more, so this waits its ticks first. */
        if (lfox_unlend_wait > 0 && lfox_lend_track >= 0) {
            lfox_put_back(lfox_lend_track);
            lfox_unlend_wait--;
            return;
        }
        lfox_lend_track = -1;           /* the next visit keeps LFO1's words afresh */
        return;
    }
    t = fw_active_track;
    if (!fw_kit || t < 0 || t > 3)
        return;
    if (lfox_lend_track != t || !lfox_lent) {
        lfox_lend(t);
        return;
    }
    if (lfox_dirty) {                   /* a load or a set: the boxes follow */
        lfox_dirty = 0;
        lfox_lend(t);
        return;
    }
    lfox_adopt(t);
}

int32_t lfox_key(void *brain, void *event)
{
    int32_t *ev = (int32_t *)event;
    int32_t code = ev[3];
    int32_t flags = ev[4];

    lfox_brain = brain;
    if (code == 1)                      /* FUNC: its hints leave the strip slot
                                         * alone, so the strip stays whole */
        lfox_func = (flags & 1) != 0;
    if (code == 4)                      /* PTN */
        lfox_ptn = (flags & 1) != 0;
    /* A save, a pattern change or a menu key can copy the kit while it runs:
     * LFO1's own words are in the slots for those (flags bit 1 is FUNC held).
     * The keys that only play or page along leave the lent values alone. */
    if (!((code >= 26 && code <= 45) && !(flags & 2) && !lfox_ptn))
        lfox_give_back();

    /* A knob push on this page is the stock UI's too: it writes the stock
     * default to the lent slot, and the tick takes it back out of the sound. */
    if (code != 25) {                   /* LFO */
        if (flags & 1)
            lfox_press = 0;
        return 0;
    }
    if (lfox_opening)
        return 1;                       /* a press core_page_open sent */
    if (!(flags & 1) || (flags & 8))
        return 0;

    if (++lfox_press < 3)
        return 0;                       /* let stock show LFO1, then LFO2 */

    lfox_opening = 1;
    (void)core_page_open(brain, event, 25, &lfox_page);
    lfox_opening = 0;
    lfox_press = 0;
    lfox_shown = core_page_shown(brain, &lfox_page);
    return 1;                           /* the third press is ours */
}

