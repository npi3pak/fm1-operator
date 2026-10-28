/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 npi3pak */
#include "fm.h"
#include "op_bank.h"

#define STORE_MAGIC 0x4F505235u
#define STORE_VERSION 1u
#define USER_PER_SLOT ((FW_STORE_MAX - 32u) / sizeof(fm_patch_t))
#define USER_N (USER_PER_SLOT * FW_STORE_SLOTS)
#define PRESETS_N (OP_BANK_N + USER_N)
#define KEY_VELOCITY 100u
#define SCOPE_N 228u
#define DRAWS_PER_FRAME 2u
#define CPU_MUTE_Q8 240u
#define CPU_HIGH_Q8 205u
#define CPU_LOW_Q8 150u
#define ROWS_MAX 4u
#define TITLE_H 22
#define HOME_Y 24
#define HOME_ROW_H 48
#define LCD_X 5
#define LCD_Y 25
#define LCD_W 230
#define LCD_H 76
#define LCD_TOP 15
#define GRAPH_H (LCD_H - LCD_TOP - 2)
#define ROW_Y 103
#define ROW_H 39
#define STATUS_Y 222
#define ALGO_SHOW_MS 1500u
#define OFFSET(t, f) ((uint16_t)__builtin_offsetof(t, f))

/* Live 12, Default Dark Neutral Medium: the device, its controls, the display; Operator1..4 are the same in every theme */
#define C_BODY RGB(0x3e, 0x3e, 0x3e)
#define C_BODY_FOCUS RGB(0x4a, 0x4a, 0x4a)
#define C_BAR RGB(0x2e, 0x2e, 0x2e)
#define C_BAR_EDGE RGB(0x24, 0x24, 0x24)
#define C_SEPARATOR RGB(0x34, 0x34, 0x34)
#define C_CONTROL RGB(0x1e, 0x1e, 0x1e)
#define C_LCD RGB(0x18, 0x18, 0x18)
#define C_LINE RGB(0x3e, 0x3e, 0x3e)
#define C_ORANGE RGB(0xff, 0xad, 0x56)
#define C_CYAN RGB(0x03, 0xc3, 0xd5)
#define C_PINK RGB(0xff, 0x69, 0x7f)
#define C_SCALE RGB(0x86, 0x86, 0x86)
#define C_LABEL RGB(0x9a, 0x9a, 0x9a)
#define C_TEXT RGB(0xb5, 0xb5, 0xb5)
#define C_BRIGHT RGB(0xe6, 0xe6, 0xe6)
#define C_OFF RGB(0x75, 0x75, 0x75)
#define C_ALERT RGB(0xe7, 0x69, 0x42)
#define C_INK RGB(0x07, 0x07, 0x07)

enum { PAGE_HOME, PAGE_OSC, PAGE_ENV, PAGE_LFO, PAGE_FILTER, PAGE_PITCH, PAGE_GLOBAL, PAGE_SAVE, PAGES };
enum { B_NONE, B_OSC, B_OSCN, B_ENV, B_FILTER, B_LFO, B_PITCH, B_MAIN, B_ROUTE, B_DEV, B_HARM_SEL, B_HARM };
enum {
    F_INT, F_ONOFF, F_DB, F_TIME, F_RATIO, F_WAVE, F_HZ10, F_MULTI, F_PCT, F_PCTU, F_INDEX, F_HARM, F_PARTIALS,
    F_ENVLVL, F_LOOP, F_BEAT, F_SLOPE, F_LFOTYPE, F_RANGE, F_LFORATE, F_CUTOFF, F_RES, F_FTYPE, F_CIRCUIT, F_SLOPE24,
    F_DRIVE, F_SHAPER, F_DST, F_SEMI, F_VOICES, F_PAN, F_CHANNEL, F_CENT, F_BPCIRC
};
enum { W_ALL, W_GLOBAL, W_SOURCE, W_SLOPED };
enum { D_TITLE, D_LCD, D_ROW0, D_STATUS = D_ROW0 + ROWS_MAX, D_FRAME, D_ALL = 0xFFu };
enum { K_KNOB, K_TOGGLE, K_CHOOSER };
#define D_ROWS (FW_BIT(D_ROW0) | FW_BIT(D_ROW0 + 1) | FW_BIT(D_ROW0 + 2) | FW_BIT(D_ROW0 + 3))

typedef struct {
    const char *label;
    uint8_t base, fmt;
    uint16_t off;
    int16_t lo, hi, step;
} pdef_t;

typedef struct {
    const char *name;
    uint8_t when;
    pdef_t p[4];
} group_t;

typedef struct {
    uint32_t magic;
    uint16_t version, current;
    fm_global_t dev;
    fm_patch_t p[USER_PER_SLOT];
} store_t;

typedef char store_fits[sizeof(store_t) <= FW_STORE_MAX ? 1 : -1];

#define OSC(l, f, fmt, lo, hi, st) {l, B_OSC, fmt, OFFSET(fm_osc_t, f), lo, hi, st}
#define OSCN(l, f, fmt, lo, hi, st) {l, B_OSCN, fmt, OFFSET(fm_osc_t, f), lo, hi, st}
#define ENV(l, f, fmt, lo, hi, st) {l, B_ENV, fmt, OFFSET(fm_env_t, f), lo, hi, st}
#define FIL(l, f, fmt, lo, hi, st) {l, B_FILTER, fmt, OFFSET(fm_filter_t, f), lo, hi, st}
#define LFO(l, f, fmt, lo, hi, st) {l, B_LFO, fmt, OFFSET(fm_lfo_t, f), lo, hi, st}
#define PIT(l, f, fmt, lo, hi, st) {l, B_PITCH, fmt, OFFSET(fm_pitch_t, f), lo, hi, st}
#define MAIN(l, f, fmt, lo, hi, st) {l, B_MAIN, fmt, OFFSET(fm_main_t, f), lo, hi, st}
#define DEV(l, f, fmt, lo, hi, st) {l, B_DEV, fmt, OFFSET(fm_global_t, f), lo, hi, st}
#define ROUTE(l, j, f, fmt, lo, hi, st) {l, B_ROUTE, fmt, (uint16_t)((j) * sizeof(fm_route_t) + OFFSET(fm_route_t, f)), lo, hi, st}
#define NONE {0, B_NONE, 0, 0, 0, 0, 0}
#define TIME_MAX 5778
#define HOME_LEVEL 4u

/* HOME: one column per group, one row per oscillator (K1..K4 = A..D) */
static const group_t G_HOME[] = {
    {"Osc On", W_ALL, {OSCN("A", on, F_ONOFF, 0, 1, 1), OSCN("B", on, F_ONOFF, 0, 1, 1),
                       OSCN("C", on, F_ONOFF, 0, 1, 1), OSCN("D", on, F_ONOFF, 0, 1, 1)}},
    {"Coarse", W_ALL, {OSCN("Coarse", coarse, F_RATIO, 0, 48, 1), OSCN("Coarse", coarse, F_RATIO, 0, 48, 1),
                       OSCN("Coarse", coarse, F_RATIO, 0, 48, 1), OSCN("Coarse", coarse, F_RATIO, 0, 48, 1)}},
    {"Fine", W_ALL, {OSCN("Fine", fine, F_INT, 0, 1000, 1), OSCN("Fine", fine, F_INT, 0, 1000, 1),
                     OSCN("Fine", fine, F_INT, 0, 1000, 1), OSCN("Fine", fine, F_INT, 0, 1000, 1)}},
    {"Fixed", W_ALL, {OSCN("Fixed", fixed, F_ONOFF, 0, 1, 1), OSCN("Fixed", fixed, F_ONOFF, 0, 1, 1),
                      OSCN("Fixed", fixed, F_ONOFF, 0, 1, 1), OSCN("Fixed", fixed, F_ONOFF, 0, 1, 1)}},
    {"Level", W_ALL, {OSCN("Level", level, F_DB, -7000, 0, 50), OSCN("Level", level, F_DB, -7000, 0, 50),
                      OSCN("Level", level, F_DB, -7000, 0, 50), OSCN("Level", level, F_DB, -7000, 0, 50)}},
};

static const group_t G_OSC[] = {
    {"Tune", W_ALL, {OSC("Coarse", coarse, F_RATIO, 0, 48, 1), OSC("Fine", fine, F_INT, 0, 1000, 1),
                     OSC("Level", level, F_DB, -7000, 0, 50), OSC("Wave", wave, F_WAVE, 0, 22, 1)}},
    {"Fixed", W_ALL, {OSC("Fixed", fixed, F_ONOFF, 0, 1, 1), OSC("Freq", fixed_freq, F_HZ10, 100, 20000, 10),
                      OSC("Multi", fixed_multi, F_MULTI, 0, 5, 1), OSC("Feedback", feedback, F_PCTU, 0, 1000, 10)}},
    {"Phase", W_ALL, {OSC("Phase", phase, F_PCTU, 0, 1000, 10), OSC("Retrig", retrigger, F_ONOFF, 0, 1, 1),
                      OSC("Osc<Vel", vel_coarse, F_PCT, -1000, 1000, 10), OSC("Quantize", vel_coarse_q, F_ONOFF, 0, 1, 1)}},
    {"Scale", W_ALL, {OSC("Vel", vel_scale, F_PCT, -1000, 1000, 10), OSC("Key", key_scale, F_PCT, -1000, 1000, 10),
                      OSC("LFO", lfo_on, F_ONOFF, 0, 1, 1), OSC("Pitch Env", pitch_on, F_ONOFF, 0, 1, 1)}},
    {"Partials", W_ALL, {{"Partial", B_HARM_SEL, F_INDEX, 0, 0, 63, 1}, {"Amp", B_HARM, F_HARM, 0, 0, 255, 1},
                         OSC("Count", harm_count, F_PARTIALS, 0, 2, 1), OSC("Repeat", harm_repeat, F_INT, 0, 10, 1)}},
    {"Partials+", W_ALL, {OSC("Normalize", harm_normalize, F_ONOFF, 0, 1, 1), NONE, NONE, NONE}},
};

static const group_t G_ENV[] = {
    {"Times", W_ALL, {ENV("Attack", attack, F_TIME, 0, TIME_MAX, 10), ENV("Decay", decay, F_TIME, 0, TIME_MAX, 10),
                      ENV("Sustain", sustain, F_ENVLVL, 0, 0, 0), ENV("Release", release, F_TIME, 0, TIME_MAX, 10)}},
    {"Levels", W_ALL, {ENV("Initial", initial, F_ENVLVL, 0, 0, 0), ENV("Peak", peak, F_ENVLVL, 0, 0, 0),
                       ENV("End", end, F_ENVLVL, 0, 0, 0), ENV("Time<Vel", time_vel, F_PCT, -1000, 1000, 10)}},
    {"Mode", W_ALL, {ENV("Mode", loop_mode, F_LOOP, 0, LOOP_MODES - 1, 1), ENV("Loop Time", loop_time, F_TIME, 301, 5301, 10),
                     ENV("Repeat", repeat, F_BEAT, 0, 14, 1), NONE}},
    {"Slopes", W_SLOPED, {ENV("A Slope", a_slope, F_SLOPE, -1000, 1000, 10), ENV("D Slope", d_slope, F_SLOPE, -1000, 1000, 10),
                          ENV("R Slope", r_slope, F_SLOPE, -1000, 1000, 10), NONE}},
};

static const group_t G_LFO[] = {
    {"Main", W_ALL, {LFO("LFO", on, F_ONOFF, 0, 1, 1), LFO("Type", type, F_LFOTYPE, 0, LFO_TYPES - 1, 1),
                     LFO("Rate", rate, F_LFORATE, 0, 1270, 5), LFO("Amount", amount, F_PCTU, 0, 1000, 10)}},
    {"Rate", W_ALL, {LFO("Range", range, F_RANGE, 0, 2, 1), LFO("Synced", rate_synced, F_BEAT, 0, 14, 1),
                     LFO("Rate<Key", rate_key, F_PCTU, 0, 1000, 10), LFO("Retrig", retrigger, F_ONOFF, 0, 1, 1)}},
    {"Dest A", W_ALL, {OSCN("Osc A", lfo_on, F_ONOFF, 0, 1, 1), OSCN("Osc B", lfo_on, F_ONOFF, 0, 1, 1),
                       OSCN("Osc C", lfo_on, F_ONOFF, 0, 1, 1), OSCN("Osc D", lfo_on, F_ONOFF, 0, 1, 1)}},
    {"Dest A+", W_ALL, {FIL("Filter", lfo_on, F_ONOFF, 0, 1, 1), LFO("Amount A", amount_a, F_PCT, -1000, 1000, 10),
                        LFO("Amt<Vel", amount_vel, F_PCT, -1000, 1000, 10), NONE}},
    {"Dest B", W_ALL, {LFO("Dest B", dst_b, F_DST, 0, FM_DESTS - 1, 1), LFO("Amount B", amount_b, F_PCT, -1000, 1000, 10),
                       NONE, NONE}},
};

static const group_t G_FILTER[] = {
    {"Filter", W_ALL, {FIL("Freq", freq, F_CUTOFF, 2251, 13372, 10), FIL("Res", res, F_RES, 0, 1250, 10),
                       FIL("Type", type, F_FTYPE, 0, FILTER_TYPES - 1, 1), FIL("Envelope", env_amount, F_PCT, -1000, 1000, 10)}},
    {"Circuit", W_ALL, {FIL("Filter", on, F_ONOFF, 0, 1, 1), FIL("Circuit", circuit, F_CIRCUIT, 0, 4, 1),
                        FIL("Slope", slope24, F_SLOPE24, 0, 1, 1), FIL("Drive", drive, F_DRIVE, 0, 2400, 25)}},
    {"Mod", W_ALL, {FIL("Morph", morph, F_PCTU, 0, 1000, 10), FIL("Freq<Vel", freq_vel, F_PCT, -1000, 1000, 10),
                    FIL("Freq<Key", freq_key, F_PCT, -2000, 2000, 10), FIL("LFO", lfo_on, F_ONOFF, 0, 1, 1)}},
    {"Shaper", W_ALL, {FIL("Shaper", shaper, F_SHAPER, 0, SHAPER_TYPES - 1, 1), FIL("Shp Drive", shaper_drive, F_DRIVE, -1200, 1200, 25),
                       FIL("Dry/Wet", shaper_wet, F_PCTU, 0, 1000, 10), FIL("BP Circuit", circuit_bp, F_BPCIRC, 0, 1, 1)}},
};

static const group_t G_PITCH[] = {
    {"Pitch Env", W_ALL, {PIT("Pitch Env", on, F_ONOFF, 0, 1, 1), PIT("Amount", env_amount, F_PCT, -1000, 1000, 10),
                          PIT("Amount A", amount_a, F_PCT, -1000, 1000, 10), NONE}},
    {"Dest A", W_ALL, {OSCN("Osc A", pitch_on, F_ONOFF, 0, 1, 1), OSCN("Osc B", pitch_on, F_ONOFF, 0, 1, 1),
                       OSCN("Osc C", pitch_on, F_ONOFF, 0, 1, 1), OSCN("Osc D", pitch_on, F_ONOFF, 0, 1, 1)}},
    {"Dest", W_ALL, {LFO("To LFO", pitch_on, F_ONOFF, 0, 1, 1), PIT("Dest B", dst_b, F_DST, 0, FM_DESTS - 1, 1),
                     PIT("Amount B", amount_b, F_PCT, -1000, 1000, 10), MAIN("PB Range", bend_range, F_SEMI, 0, 24, 1)}},
    {"Pitch", W_ALL, {MAIN("Spread", spread, F_PCTU, 0, 1000, 10), MAIN("Transpose", transpose, F_SEMI, -48, 48, 1),
                      MAIN("Glide", glide, F_ONOFF, 0, 1, 1), MAIN("Glide Time", glide_time, F_TIME, 0, 5000, 10)}},
};

static const group_t G_GLOBAL[] = {
    {"Voice", W_GLOBAL, {MAIN("Voices", voices, F_VOICES, 0, 13, 1), MAIN("Retrig", retrigger, F_ONOFF, 0, 1, 1),
                         MAIN("Volume", volume, F_DB, -7000, 600, 50), MAIN("Pan", pan, F_PAN, -1000, 1000, 20)}},
    {"Time", W_GLOBAL, {MAIN("Time", time, F_PCT, -1000, 1000, 10), MAIN("Time<Key", time_key, F_PCT, -1000, 1000, 10),
                        MAIN("Time Atk", time_attack, F_ONOFF, 0, 1, 1), MAIN("Tone", tone, F_PCTU, 0, 1000, 10)}},
    {"Pan", W_GLOBAL, {MAIN("Pan<Key", pan_key, F_PCTU, 0, 1000, 10), MAIN("Pan<Rnd", pan_rnd, F_PCTU, 0, 1000, 10),
                       MAIN("Interpol", interpolation, F_ONOFF, 0, 1, 1), MAIN("Antialias", antialias, F_ONOFF, 0, 1, 1)}},
    {"Device", W_GLOBAL, {DEV("MIDI Ch", channel, F_CHANNEL, 0, 16, 1), DEV("Tune", fine, F_CENT, -100, 100, 1),
                          DEV("Tempo", tempo, F_INT, 40, 240, 1), NONE}},
    {"Routing", W_SOURCE, {ROUTE("Dest 1", 0, dst, F_DST, 0, FM_DESTS - 1, 1), ROUTE("Amount 1", 0, amount, F_PCT, -1000, 1000, 10),
                           ROUTE("Dest 2", 1, dst, F_DST, 0, FM_DESTS - 1, 1), ROUTE("Amount 2", 1, amount, F_PCT, -1000, 1000, 10)}},
};

static const struct {
    const group_t *g;
    uint8_t n, objects;
} PAGE_DEF[PAGES] = {
    {G_HOME, FW_NELEM(G_HOME), 0}, {G_OSC, FW_NELEM(G_OSC), FM_OPS}, {G_ENV, FW_NELEM(G_ENV), FM_ENVS},
    {G_LFO, FW_NELEM(G_LFO), 0}, {G_FILTER, FW_NELEM(G_FILTER), 0}, {G_PITCH, FW_NELEM(G_PITCH), 0},
    {G_GLOBAL, FW_NELEM(G_GLOBAL), 1 + FM_SOURCES}, {0, 0, 0},
};

static const char *const PAGE_NAME[PAGES] = {"Operator", "Oscillator", "Envelope", "LFO", "Filter", "Pitch", "Global", "Save"};
static const char *const ENV_NAME[FM_ENVS] = {"Envelope A", "Envelope B", "Envelope C", "Envelope D", "Filter Envelope",
                                              "Pitch Envelope", "LFO Envelope"};
static const char *const SOURCE_NAME[1 + FM_SOURCES] = {"Global", "Velocity", "Key", "Aftertouch", "Pitch Bend", "Mod Wheel"};
static const char *const OP_NAME[FM_OPS] = {"A", "B", "C", "D"};
static const char *const OSC_NAME[FM_OPS] = {"Oscillator A", "Oscillator B", "Oscillator C", "Oscillator D"};
static const uint16_t OP_COLOR[FM_OPS] = {RGB(0xe0, 0xd8, 0x25), RGB(0x29, 0xd6, 0xcd), RGB(0x65, 0x71, 0xf6), RGB(0xf3, 0x75, 0x1b)};
static const char *const WAVE_NAME[FM_WAVES] = {"Sine", "Sine 4 Bit", "Sine 8 Bit", "Saw 3", "Saw 4", "Saw 6", "Saw 8",
                                                "Saw 16", "Saw 32", "Saw 64", "Saw D", "Square 3", "Square 4", "Square 6",
                                                "Square 8", "Square 16", "Square 32", "Square 64", "Square D", "Triangle",
                                                "Noise Loop", "Noise White", "User"};
static const char *const LOOP_NAME[LOOP_MODES] = {"None", "Loop", "Beat", "Sync", "Trigger"};
static const char *const BEAT_NAME[15] = {"1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4T", "3/16", "1/4",
                                          "1/2T", "3/8", "1/2", "3/4", "1 Bar", "2 Bars", "4 Bars"};
static const char *const LFO_NAME[LFO_TYPES] = {"Sine", "Square", "Triangle", "Saw Up", "Saw Down", "S&H", "Noise"};
static const char *const RANGE_NAME[3] = {"Low", "High", "Sync"};
static const char *const FTYPE_NAME[FILTER_TYPES] = {"Lowpass", "Highpass", "Bandpass", "Notch", "Morph"};
static const char *const CIRCUIT_NAME[5] = {"Clean", "OSR", "MS2", "SMP", "PRD"};
static const char *const SHAPER_NAME[SHAPER_TYPES] = {"Off", "Soft", "Hard", "Sine", "4 Bit"};
static const char *const MULTI_NAME[6] = {"x0.0001", "x0.001", "x0.01", "x0.1", "x1", "x10"};
static const char *const PARTIALS_NAME[3] = {"16", "32", "64"};
static const char *const DST_NAME[FM_DESTS] = {"Off", "Osc Vol A", "Osc Vol B", "Osc Vol C", "Osc Vol D", "X-Fade A/C",
                                               "X-Fade B/D", "Feedback", "Fixed Freq", "FM Drive", "Filter Freq",
                                               "Filter Q", "Filter Res", "Morph", "Flt Drive", "Filter Env",
                                               "Shp Drive", "LFO Rate", "LFO Amt", "Pitch Env", "Volume", "Pan",
                                               "Tone", "Time"};
static const uint32_t MULTI_DIV[6] = {10000u, 1000u, 100u, 10u, 1u, 1u};
static const uint32_t POW2[101] = {
    65536, 65992, 66451, 66913, 67378, 67847, 68319, 68794, 69273, 69755, 70240, 70728, 71220, 71716, 72214, 72717,
    73223, 73732, 74245, 74761, 75281, 75805, 76332, 76863, 77398, 77936, 78478, 79024, 79573, 80127, 80684, 81245,
    81811, 82380, 82953, 83530, 84111, 84696, 85285, 85878, 86475, 87077, 87682, 88292, 88906, 89525, 90148, 90775,
    91406, 92042, 92682, 93327, 93976, 94629, 95288, 95950, 96618, 97290, 97966, 98648, 99334, 100025, 100721, 101421,
    102127, 102837, 103552, 104273, 104998, 105728, 106464, 107204, 107950, 108701, 109457, 110218, 110985, 111757,
    112534, 113317, 114105, 114898, 115698, 116502, 117313, 118129, 118950, 119778, 120611, 121450, 122295, 123145,
    124002, 124864, 125733, 126607, 127488, 128375, 129267, 130167, 131072};

static fm_patch_t patch;
static fm_global_t global;
static fm_monitor_t mon;
static store_t store[FW_STORE_SLOTS] FW_POOL;
static uint32_t page = PAGE_HOME, preset, save_slot, save_cursor, dirty = D_ALL, frame_count, harm_sel, now_ms;
static uint32_t algo_until;
static uint8_t focus[PAGES], object[PAGES];
static char save_name[FM_NAME_LEN];
static int32_t octave;
static uint32_t key_held;
static int32_t knob_fast[FW_KNOB_COUNT];
static uint32_t knob_ms[FW_KNOB_COUNT];
static uint8_t key_note[FW_KEYS];
static int16_t scope[SCOPE_N];
static volatile uint32_t scope_ready;
static volatile uint32_t overloads;
static uint32_t overloads_seen, voice_cap = FM_VOICES_MAX, shown_voices;
static int shown_edited;
static const char *flash_msg;
static uint32_t flash_msg_until;

static int32_t clampi(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static void str_add(char *b, uint32_t size, const char *s) { str_cpy(b + str_len(b), s, size - str_len(b)); }

/* base * 2^(mo / 1000), base below 8 000 000 */
static uint32_t pow2m(uint32_t base, int32_t mo)
{
    const int32_t q = mo >= 0 ? mo / 1000 : -((999 - mo) / 1000);
    const uint32_t r = (uint32_t)(mo - q * 1000), i = r / 10u, f = r % 10u;
    const uint32_t m = (POW2[i] * (10u - f) + POW2[i + 1u] * f) / 10u;
    const uint32_t v = base * (m >> 8) >> 8;
    if (q >= 0)
        return q > 20 ? 0xFFFFFFFFu : v << q;
    return q < -31 ? 0u : v >> -q;
}

/* ---- presets */

static const fm_patch_t *preset_source(uint32_t i)
{
    const uint32_t u = i - OP_BANK_N;
    if (i < OP_BANK_N)
        return &OP_BANK[i];
    return &store[u / USER_PER_SLOT].p[u % USER_PER_SLOT];
}

static void store_init(uint32_t s)
{
    uint32_t i;
    char n[8];
    memset(&store[s], 0, sizeof store[s]);
    store[s].magic = STORE_MAGIC;
    store[s].version = STORE_VERSION;
    store[s].dev.tempo = 120;
    for (i = 0; i < USER_PER_SLOT; i++) {
        store[s].p[i] = OP_INIT;
        str_cpy(store[s].p[i].name, "User ", FM_NAME_LEN);
        fmt_int(n, (int32_t)(s * USER_PER_SLOT + i + 1u));
        str_add(store[s].p[i].name, FM_NAME_LEN, n);
    }
}

static void store_load(void)
{
    uint32_t s, i;
    for (s = 0; s < FW_STORE_SLOTS; s++) {
        const int n = fw_store_load(s, &store[s], sizeof store[s]);
        if (n != (int)sizeof store[s] || store[s].magic != STORE_MAGIC || store[s].version != STORE_VERSION)
            store_init(s);
        for (i = 0; i < USER_PER_SLOT; i++)
            store[s].p[i].name[FM_NAME_LEN - 1] = 0;
    }
    global = store[0].dev;
    global.channel = (int16_t)clampi(global.channel, 0, 16);
    global.fine = (int16_t)clampi(global.fine, -100, 100);
    global.tempo = (int16_t)clampi(global.tempo ? global.tempo : 120, 40, 240);
}

static void preset_select(uint32_t i)
{
    preset = i % PRESETS_N;
    fm1_irq_off();
    patch = *preset_source(preset);
    fm1_irq_on();
    fm_patch_loaded(&patch);
    dirty = D_ALL;
}

static int edited(void) { return memcmp(&patch, preset_source(preset), sizeof patch) != 0; }

static void message(const char *s, uint32_t now)
{
    flash_msg = s;
    flash_msg_until = now + 1500u;
    dirty |= FW_BIT(D_STATUS);
}

static int store_write(uint32_t s)
{
    store[s].dev = global;
    store[s].current = (uint16_t)preset;
    return fw_store_save(s, &store[s], sizeof store[s]);
}

static void preset_save(uint32_t slot, uint32_t now)
{
    const uint32_t s = slot / USER_PER_SLOT;
    fm_patch_t *dst = &store[s].p[slot % USER_PER_SLOT];
    const fm_patch_t keep = *dst;
    const uint32_t was = preset;
    *dst = patch;
    str_cpy(dst->name, save_name, FM_NAME_LEN);
    str_cpy(patch.name, save_name, FM_NAME_LEN);
    preset = OP_BANK_N + slot;
    if (store_write(s) == 0 && (s == 0 || store_write(0) == 0)) {
        message("SAVED", now);
        return;
    }
    *dst = keep;
    preset = was;
    message("SAVE FAILED", now);
}

/* ---- parameters */

static uint32_t page_objects(uint32_t pg) { return PAGE_DEF[pg].objects; }

static int group_visible(uint32_t pg, uint32_t g)
{
    const uint8_t w = PAGE_DEF[pg].g[g].when;
    const uint32_t obj = object[pg];
    if (w == W_GLOBAL)
        return obj == 0;
    if (w == W_SOURCE)
        return obj != 0;
    if (w == W_SLOPED)
        return obj == ENV_FILTER || obj == ENV_PITCH;
    return 1;
}

static const pdef_t P_SYNCED = LFO("Rate", rate_synced, F_BEAT, 0, 14, 1);
static const pdef_t P_FREQ = OSCN("Freq", fixed_freq, F_HZ10, 100, 20000, 10);
static const pdef_t P_MULTI = OSCN("Multi", fixed_multi, F_MULTI, 0, 5, 1);

/* as in Live, a fixed oscillator's Coarse and Fine become Freq and Multi */
static const pdef_t *cell(uint32_t pg, uint32_t g, uint32_t col)
{
    const pdef_t *d = &PAGE_DEF[pg].g[g].p[col];
    if (!d->label)
        return 0;
    if (d->base == B_OSCN && patch.osc[col].fixed && d->off == OFFSET(fm_osc_t, coarse))
        return &P_FREQ;
    if (d->base == B_OSCN && patch.osc[col].fixed && d->off == OFFSET(fm_osc_t, fine))
        return &P_MULTI;
    if (d->fmt == F_LFORATE && patch.lfo.range == LFO_SYNC)
        return &P_SYNCED;
    if (pg == PAGE_ENV && d->off == OFFSET(fm_env_t, end) && object[pg] < ENV_FILTER)
        return 0;
    return d;
}

static fm_env_t *env_of(uint32_t e)
{
    if (e < ENV_FILTER)
        return &patch.osc[e].env;
    if (e == ENV_FILTER)
        return &patch.filter.env;
    if (e == ENV_PITCH)
        return &patch.pitch.env;
    return &patch.lfo.env;
}

static uint32_t osc_of(const pdef_t *d, uint32_t col) { return d->base == B_OSCN ? col : object[PAGE_OSC]; }

static int16_t *field(const pdef_t *d, uint32_t col)
{
    char *b = 0;
    if (d->base == B_OSC || d->base == B_OSCN)
        b = (char *)&patch.osc[osc_of(d, col)];
    if (d->base == B_ENV)
        b = (char *)env_of(object[PAGE_ENV]);
    if (d->base == B_FILTER)
        b = (char *)&patch.filter;
    if (d->base == B_LFO)
        b = (char *)&patch.lfo;
    if (d->base == B_PITCH)
        b = (char *)&patch.pitch;
    if (d->base == B_MAIN)
        b = (char *)&patch.main;
    if (d->base == B_ROUTE)
        b = (char *)patch.route[clampi(object[PAGE_GLOBAL], 1, FM_SOURCES) - 1];
    if (d->base == B_DEV)
        b = (char *)&global;
    return b ? (int16_t *)(b + d->off) : 0;
}

static int32_t pget(const pdef_t *d, uint32_t col)
{
    const int16_t *f = field(d, col);
    if (d->base == B_HARM_SEL)
        return (int32_t)harm_sel;
    if (d->base == B_HARM)
        return patch.osc[object[PAGE_OSC]].harm[harm_sel];
    return f ? *f : 0;
}

static void env_range(uint32_t e, int32_t *lo, int32_t *hi, int32_t *step)
{
    *lo = -7000;
    *hi = 0;
    *step = 50;
    if (e == ENV_FILTER) {
        *lo = 0;
        *hi = 1000;
        *step = 10;
    }
    if (e == ENV_PITCH) {
        *lo = -4800;
        *hi = 4800;
        *step = 10;
    }
}

static void prange(const pdef_t *d, int32_t *lo, int32_t *hi, int32_t *step)
{
    *lo = d->lo;
    *hi = d->hi;
    *step = d->step;
    if (d->fmt == F_ENVLVL)
        env_range(object[PAGE_ENV], lo, hi, step);
}

static int pset(const pdef_t *d, uint32_t col, int32_t v)
{
    int16_t *f = field(d, col);
    int32_t lo, hi, step;
    prange(d, &lo, &hi, &step);
    v = clampi(v, lo, hi);
    if (v == pget(d, col))
        return 0;
    if (d->base == B_HARM_SEL) {
        harm_sel = (uint32_t)v;
        return 1;
    }
    if (d->base == B_HARM) {
        fm_osc_t *o = &patch.osc[object[PAGE_OSC]];
        o->harm[harm_sel] = (uint8_t)v;
        o->wave = WAVE_USER;
        fm_patch_loaded(&patch);
        return 1;
    }
    *f = (int16_t)v;
    if (d->base == B_OSC && (d->off == OFFSET(fm_osc_t, harm_count) || d->off == OFFSET(fm_osc_t, harm_normalize)))
        fm_patch_loaded(&patch);
    return 1;
}

/* ---- value text */

static void fmt_signed(char *b, int32_t v, int decimals, int sign)
{
    if (v > 0 && sign) {
        b[0] = '+';
        fmt_fix(b + 1, v, decimals);
        return;
    }
    fmt_fix(b, v, decimals);
}

static void fmt_unit(char *b, uint32_t size, int32_t v, int decimals, const char *unit)
{
    fmt_fix(b, v, decimals);
    str_add(b, size, unit);
}

static void fmt_time(char *b, uint32_t size, int32_t code)
{
    const uint32_t us = pow2m(1600u, code * 3322 / 1000) >> 4;
    if (us < 9950u) {
        fmt_unit(b, size, (int32_t)((us + 50u) / 100u), 1, "ms");
        return;
    }
    if (us < 999500u) {
        fmt_unit(b, size, (int32_t)((us + 500u) / 1000u), 0, "ms");
        return;
    }
    if (us < 9995000u) {
        fmt_unit(b, size, (int32_t)((us + 5000u) / 10000u), 2, "s");
        return;
    }
    fmt_unit(b, size, (int32_t)((us + 50000u) / 100000u), 1, "s");
}

static void fmt_mhz(char *b, uint32_t size, uint32_t mhz)
{
    if (mhz >= 10000000u) {
        fmt_unit(b, size, (int32_t)((mhz + 50000u) / 100000u), 1, "k");
        return;
    }
    if (mhz >= 1000000u) {
        fmt_unit(b, size, (int32_t)((mhz + 5000u) / 10000u), 2, "k");
        return;
    }
    if (mhz >= 100000u) {
        fmt_unit(b, size, (int32_t)((mhz + 500u) / 1000u), 0, "Hz");
        return;
    }
    if (mhz >= 10000u) {
        fmt_unit(b, size, (int32_t)((mhz + 50u) / 100u), 1, "Hz");
        return;
    }
    if (mhz >= 1000u) {
        fmt_unit(b, size, (int32_t)((mhz + 5u) / 10u), 2, "Hz");
        return;
    }
    fmt_unit(b, size, (int32_t)mhz, 0, "mHz");
}

/* at most 6 characters: -inf, -12dB, -5.5dB, +6.0dB */
static void fmt_db(char *b, uint32_t size, int32_t cb, int sign)
{
    const int32_t r = (cb - (cb < 0 ? 5 : -5)) / 10;
    if (cb <= -7000) {
        str_cpy(b, "-inf", size);
        return;
    }
    if (r <= -100 || r >= 100) {
        fmt_signed(b, (r - (r < 0 ? 5 : -5)) / 10, 0, sign);
        str_add(b, size, "dB");
        return;
    }
    fmt_signed(b, r, 1, sign);
    str_add(b, size, "dB");
}

static uint32_t ratio_milli(const fm_osc_t *o)
{
    if (o->coarse <= 0)
        return 500u + (uint32_t)o->fine / 2u;
    return (uint32_t)o->coarse * 1000u + (uint32_t)o->fine;
}

static uint32_t cutoff_mhz(int32_t cents) { return pow2m(440u * 1000u / 16u, (cents - 6900) * 10 / 12) * 16u; }

static uint32_t lfo_mhz(const fm_lfo_t *l)
{
    const uint32_t base = l->range == LFO_HIGH ? 8000u * 64u : 20u * 64u;
    return pow2m(base, (int32_t)l->rate * 10551 / 1270) / 64u;
}

static const char *list_name(uint32_t fmt, int32_t v)
{
    if (fmt == F_ONOFF)
        return v ? "On" : "Off";
    if (fmt == F_WAVE)
        return WAVE_NAME[clampi(v, 0, FM_WAVES - 1)];
    if (fmt == F_MULTI)
        return MULTI_NAME[clampi(v, 0, 5)];
    if (fmt == F_PARTIALS)
        return PARTIALS_NAME[clampi(v, 0, 2)];
    if (fmt == F_LOOP)
        return LOOP_NAME[clampi(v, 0, LOOP_MODES - 1)];
    if (fmt == F_BEAT)
        return BEAT_NAME[clampi(v, 0, 14)];
    if (fmt == F_LFOTYPE)
        return LFO_NAME[clampi(v, 0, LFO_TYPES - 1)];
    if (fmt == F_RANGE)
        return RANGE_NAME[clampi(v, 0, 2)];
    if (fmt == F_FTYPE)
        return FTYPE_NAME[clampi(v, 0, FILTER_TYPES - 1)];
    if (fmt == F_CIRCUIT)
        return CIRCUIT_NAME[clampi(v, 0, 4)];
    if (fmt == F_BPCIRC)
        return CIRCUIT_NAME[clampi(v, 0, 1)];
    if (fmt == F_SLOPE24)
        return v ? "24 dB" : "12 dB";
    if (fmt == F_SHAPER)
        return SHAPER_NAME[clampi(v, 0, SHAPER_TYPES - 1)];
    if (fmt == F_DST)
        return DST_NAME[clampi(v, 0, FM_DESTS - 1)];
    if (fmt == F_CHANNEL && v == 0)
        return "Omni";
    return 0;
}

static void format_value(char *b, uint32_t size, const pdef_t *d, uint32_t col)
{
    const int32_t v = pget(d, col);
    const char *name = list_name(d->fmt, v);
    const uint32_t env = object[PAGE_ENV];
    b[0] = 0;
    if (name) {
        str_cpy(b, name, size);
        return;
    }
    if (d->fmt == F_DB || (d->fmt == F_ENVLVL && (env < ENV_FILTER || env == ENV_LFO))) {
        fmt_db(b, size, v, d->hi > 0);
        return;
    }
    if (d->fmt == F_ENVLVL && env == ENV_FILTER) {
        fmt_fix(b, v / 10, 2);
        return;
    }
    if (d->fmt == F_ENVLVL) {
        fmt_signed(b, v / 10, 1, 1);
        return;
    }
    if (d->fmt == F_TIME) {
        fmt_time(b, size, v);
        return;
    }
    if (d->fmt == F_RATIO) {
        fmt_fix(b, (int32_t)ratio_milli(&patch.osc[osc_of(d, col)]), 3);
        return;
    }
    if (d->fmt == F_HZ10) {
        const fm_osc_t *o = &patch.osc[osc_of(d, col)];
        const uint32_t m = (uint32_t)clampi(o->fixed_multi, 0, 5);
        fmt_mhz(b, size, (uint32_t)v * 100u * (m == 5 ? 10u : 1u) / MULTI_DIV[m]);
        return;
    }
    if (d->fmt == F_PCT || d->fmt == F_SLOPE) {
        if (d->fmt == F_SLOPE) {
            fmt_signed(b, v / 10, 2, 1);
            return;
        }
        fmt_signed(b, v / 10, 0, 1);
        str_add(b, size, "%");
        return;
    }
    if (d->fmt == F_PCTU || d->fmt == F_HARM) {
        fmt_unit(b, size, d->fmt == F_HARM ? v * 100 / 255 : v / 10, 0, "%");
        return;
    }
    if (d->fmt == F_INDEX) {
        fmt_int(b, v + 1);
        return;
    }
    if (d->fmt == F_LFORATE && patch.lfo.range == LFO_SYNC) {
        str_cpy(b, BEAT_NAME[clampi(patch.lfo.rate_synced, 0, 14)], size);
        return;
    }
    if (d->fmt == F_LFORATE) {
        fmt_mhz(b, size, lfo_mhz(&patch.lfo));
        return;
    }
    if (d->fmt == F_CUTOFF) {
        fmt_mhz(b, size, cutoff_mhz(v));
        return;
    }
    if (d->fmt == F_RES) {
        fmt_fix(b, v / 10, 2);
        return;
    }
    if (d->fmt == F_DRIVE) {
        fmt_db(b, size, v, 1);
        return;
    }
    if (d->fmt == F_SEMI) {
        fmt_signed(b, v, 0, 1);
        str_add(b, size, "st");
        return;
    }
    if (d->fmt == F_CENT) {
        fmt_signed(b, v, 0, 1);
        str_add(b, size, "ct");
        return;
    }
    if (d->fmt == F_VOICES) {
        fmt_int(b, (int32_t)fm_voice_count(v));
        return;
    }
    if (d->fmt == F_PAN) {
        str_cpy(b, v < 0 ? "L" : v > 0 ? "R" : "C", size);
        if (v)
            fmt_int(b + 1, (v < 0 ? -v : v) / 10);
        return;
    }
    fmt_int(b, v);
}

/* ---- knobs */

static int32_t knob_accel(int32_t d, uint32_t gap)
{
    const uint32_t each = gap / (uint32_t)(d < 0 ? -d : d);
    if (each <= 20u)
        return d * 8;
    if (each <= 40u)
        return d * 4;
    if (each <= 80u)
        return d * 2;
    return d;
}

static void knob_speed(const fw_input_t *in)
{
    uint32_t k;
    for (k = 0; k < FW_KNOB_COUNT; k++) {
        knob_fast[k] = in->knob[k] ? knob_accel(in->knob[k], in->ms - knob_ms[k]) : 0;
        if (in->knob[k])
            knob_ms[k] = in->ms;
    }
}

static int knob_param(const pdef_t *d, uint32_t col, const fw_input_t *in, uint32_t k)
{
    int32_t lo, hi, step;
    if (!d || !in->knob[k])
        return 0;
    prange(d, &lo, &hi, &step);
    return pset(d, col, pget(d, col) + (hi - lo < 40 * step ? in->knob[k] : knob_fast[k]) * step);
}

static uint32_t visible_count(uint32_t pg)
{
    uint32_t g, n = 0;
    for (g = 0; g < PAGE_DEF[pg].n; g++)
        n += (uint32_t)group_visible(pg, g);
    return n;
}

/* the index of the n-th visible group */
static uint32_t visible_group(uint32_t pg, uint32_t n)
{
    uint32_t g;
    for (g = 0; g < PAGE_DEF[pg].n; g++) {
        if (group_visible(pg, g) && n-- == 0)
            return g;
    }
    return 0;
}

/* ---- drawing: the look of a Live device */

static uint16_t object_color(void)
{
    const uint32_t obj = object[page];
    if (page == PAGE_OSC || (page == PAGE_ENV && obj < ENV_FILTER))
        return OP_COLOR[obj];
    if (page == PAGE_PITCH || (page == PAGE_ENV && obj == ENV_PITCH))
        return C_CYAN;
    if (page == PAGE_LFO || (page == PAGE_ENV && obj == ENV_LFO))
        return C_PINK;
    return C_ORANGE;
}

static int32_t sin_at(int32_t a) { return FW_SINE[(uint32_t)a & (FW_SINE_N - 1u)]; }

static void text_center(int32_t cx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c, uint16_t bg)
{
    cv_text_on(cx - text_w(f, s) / 2, y, f, s, c, bg);
}

static void fill_circle(int32_t cx, int32_t cy, int32_t r, uint16_t c)
{
    int32_t y;
    for (y = -r; y <= r; y++) {
        int32_t w = 0;
        while ((w + 1) * (w + 1) + y * y <= r * r)
            w++;
        cv_rect(cx - w, cy + y, 2 * w + 1, 1, c);
    }
}

static void outline(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    cv_rect(x, y, w, 1, c);
    cv_rect(x, y + h - 1, w, 1, c);
    cv_rect(x, y, 1, h, c);
    cv_rect(x + w - 1, y, 1, h, c);
}

/* angles: FW_SINE units (1024 a turn), 0 at the top, clockwise */
static void arc(int32_t cx, int32_t cy, int32_t r, int32_t a0, int32_t a1, uint16_t c)
{
    int32_t a;
    for (a = a0; a <= a1; a += 3) {
        cv_pset(cx + sin_at(a) * r / 32767, cy - sin_at(a + 256) * r / 32767, c);
        cv_pset(cx + sin_at(a) * (r - 1) / 32767, cy - sin_at(a + 256) * (r - 1) / 32767, c);
    }
}

/* Live's dial: a 270 degree track, the value arc (from the middle when bipolar) and a needle */
static void draw_knob(int32_t cx, int32_t cy, int32_t r, int32_t v, int32_t lo, int32_t hi, uint16_t c)
{
    const int32_t a0 = -384, span = 768;
    const int32_t av = a0 + (clampi(v, lo, hi) - lo) * span / (hi - lo);
    const int32_t az = lo < 0 && hi > 0 ? a0 + (0 - lo) * span / (hi - lo) : a0;
    arc(cx, cy, r, a0, a0 + span, C_CONTROL);
    arc(cx, cy, r, az < av ? az : av, az < av ? av : az, c);
    cv_line(cx, cy, cx + sin_at(av) * (r - 2) / 32767, cy - sin_at(av + 256) * (r - 2) / 32767, c);
}

static void draw_toggle(int32_t x, int32_t y, int32_t w, int32_t h, const char *s, int on, uint16_t on_color)
{
    const uint16_t fill = on ? on_color : C_CONTROL;
    cv_rect(x, y, w, h, fill);
    text_center(x + w / 2, y + (h - (int32_t)FONT_T.h) / 2, &FONT_T, s, on ? C_INK : C_TEXT, fill);
}

static void draw_chooser(int32_t x, int32_t y, int32_t w, const char *s, uint16_t c)
{
    cv_rect(x, y, w, 15, C_CONTROL);
    cv_text_on(x + 3, y + 1, &FONT_T, s, c, C_CONTROL);
    cv_rect(x + w - 8, y + 6, 5, 1, C_SCALE);
    cv_rect(x + w - 7, y + 7, 3, 1, C_SCALE);
    cv_rect(x + w - 6, y + 8, 1, 1, C_SCALE);
}

static uint32_t control_kind(const pdef_t *d)
{
    if (d->fmt == F_ONOFF)
        return K_TOGGLE;
    if (list_name(d->fmt, d->lo) || d->fmt == F_VOICES)
        return K_CHOOSER;
    return K_KNOB;
}

static void draw_control(const pdef_t *d, uint32_t col, int32_t x, int focused, uint16_t bg)
{
    const int32_t w = 56, cx = x + w / 2;
    const uint32_t kind = control_kind(d);
    char v[24];
    int32_t lo, hi, step;
    format_value(v, sizeof v, d, col);
    prange(d, &lo, &hi, &step);
    if (kind == K_TOGGLE) {
        draw_toggle(x + 1, 11, w - 2, 17, d->label, pget(d, col) != 0, d->base == B_OSCN ? OP_COLOR[col] : C_ORANGE);
        return;
    }
    text_center(cx, 0, &FONT_T, d->label, focused ? C_TEXT : C_LABEL, bg);
    if (kind == K_CHOOSER) {
        draw_chooser(x + 1, 17, w - 2, v, focused ? C_BRIGHT : C_TEXT);
        return;
    }
    draw_knob(cx, 19, 8, pget(d, col), lo, hi, focused ? C_ORANGE : C_TEXT);
    text_center(cx, 27, &FONT_T, v, focused ? C_BRIGHT : C_TEXT, bg);
}

static void draw_title(void)
{
    char r[12], n[8];
    cv_begin(240, TITLE_H, C_BAR);
    cv_rect(0, TITLE_H - 1, 240, 1, C_BAR_EDGE);
    fill_circle(11, 10, 5, C_ORANGE);
    cv_text_on(23, 3, &FONT_M, patch.name, C_BRIGHT, C_BAR);
    r[0] = 0;
    if (preset >= OP_BANK_N)
        str_cpy(r, "U", sizeof r);
    fmt_int(n, (int32_t)(preset < OP_BANK_N ? preset + 1u : preset - OP_BANK_N + 1u));
    str_add(r, sizeof r, n);
    if (edited())
        cv_text_on(232 - text_w(&FONT_T, r) - 9, 4, &FONT_T, "*", C_ORANGE, C_BAR);
    cv_text_on(232 - text_w(&FONT_T, r), 4, &FONT_T, r, C_SCALE, C_BAR);
    cv_blit(0, 0);
}

/* modulators above the oscillators they modulate, carriers on the output line */
static void draw_algo(int32_t cx, int32_t base, int32_t bw, int32_t bh, int32_t sy, int32_t sx, const felucca_font_t *f)
{
    const int32_t algo = patch.main.algorithm;
    const uint32_t carriers = fm_algo_carriers(algo);
    int32_t depth[FM_OPS] = {0, 0, 0, 0}, x[FM_OPS], y[FM_OPS], row_n[FM_OPS] = {0, 0, 0, 0}, row_i[FM_OPS] = {0, 0, 0, 0};
    int32_t k, j;
    const int32_t out_y = base + bh / 2 + 5;
    for (k = 0; k < FM_OPS; k++) {
        for (j = 0; j < k; j++) {
            if ((fm_algo_mods(algo, j) >> k) & 1u)
                depth[k] = depth[k] > depth[j] + 1 ? depth[k] : depth[j] + 1;
        }
    }
    for (k = 0; k < FM_OPS; k++)
        row_n[depth[k]]++;
    for (k = 0; k < FM_OPS; k++) {
        const int32_t d = depth[k];
        x[k] = cx - (row_n[d] - 1) * sx / 2 + row_i[d]++ * sx;
        y[k] = base - d * sy;
    }
    for (k = 0; k < FM_OPS; k++) {
        for (j = k + 1; j < FM_OPS; j++) {
            if ((fm_algo_mods(algo, k) >> j) & 1u)
                cv_line(x[j], y[j] + bh / 2, x[k], y[k] - bh / 2, C_SCALE);
        }
        if ((carriers >> k) & 1u)
            cv_line(x[k], y[k] + bh / 2, x[k], out_y, C_SCALE);
        if (!fm_algo_mods(algo, k) && patch.osc[k].feedback > 0) {
            cv_line(x[k] + bw / 2, y[k], x[k] + bw / 2 + 4, y[k], C_SCALE);
            cv_line(x[k] + bw / 2 + 4, y[k], x[k] + bw / 2 + 4, y[k] - bh / 2 - 3, C_SCALE);
            cv_line(x[k] + bw / 2 + 4, y[k] - bh / 2 - 3, x[k], y[k] - bh / 2 - 3, C_SCALE);
        }
    }
    cv_rect(cx - 2 * sx, out_y, 4 * sx, 2, C_SCALE);
    for (k = 0; k < FM_OPS; k++) {
        const uint16_t c = patch.osc[k].on ? OP_COLOR[k] : C_OFF;
        cv_rect(x[k] - bw / 2, y[k] - bh / 2, bw, bh, c);
        text_center(x[k], y[k] - (int32_t)f->h / 2, f, OP_NAME[k], C_INK, c);
    }
}

/* HOME: one oscillator per row, as the left half of Operator's shell */
static void draw_home_row(uint32_t k)
{
    const fm_osc_t *o = &patch.osc[k];
    const uint32_t f = focus[PAGE_HOME];
    const int carrier = (fm_algo_carriers(patch.main.algorithm) >> k) & 1u;
    const uint16_t oc = o->on ? OP_COLOR[k] : C_OFF;
    const int32_t bar_w = 70, bar_x = 164;
    char v[24];
    uint32_t c;
    cv_begin(240, HOME_ROW_H, C_BODY);
    cv_rect(0, HOME_ROW_H - 1, 240, 1, C_SEPARATOR);
    cv_rect(6, 13, 22, 22, o->on ? OP_COLOR[k] : C_CONTROL);
    text_center(17, 16, &FONT_M, OP_NAME[k], o->on ? C_INK : C_OFF, o->on ? OP_COLOR[k] : C_CONTROL);
    if (f == 0u)
        outline(4, 11, 26, 26, C_ORANGE);
    for (c = 1; c <= 2u; c++) {
        const pdef_t *d = cell(PAGE_HOME, c, k);
        const int32_t cx = c == 1u ? 54 : 96;
        int32_t lo, hi, step;
        prange(d, &lo, &hi, &step);
        format_value(v, sizeof v, d, k);
        text_center(cx, 1, &FONT_T, d->label, f == c ? C_ORANGE : C_LABEL, C_BODY);
        draw_knob(cx, 23, 10, pget(d, k), lo, hi, f == c ? C_ORANGE : C_TEXT);
        text_center(cx, 34, &FONT_T, v, C_BRIGHT, C_BODY);
    }
    draw_toggle(119, 16, 38, 17, "Fixed", o->fixed != 0, C_ORANGE);
    if (f == 3u)
        outline(117, 14, 42, 21, C_ORANGE);
    fmt_db(v, sizeof v, o->level, 0);
    cv_text_on(bar_x, 3, &FONT_T, "Level", f == HOME_LEVEL ? C_ORANGE : C_LABEL, C_BODY);
    cv_text_on(bar_x + bar_w - text_w(&FONT_T, v), 3, &FONT_T, v, C_BRIGHT, C_BODY);
    cv_rect(bar_x, 20, bar_w, 7, C_CONTROL);
    cv_rect(bar_x, 20, (clampi(o->level, -7000, 0) + 7000) * bar_w / 7000, 7, oc);
    cv_text_on(bar_x, 30, &FONT_T, carrier ? "Carrier" : "Modulator", C_SCALE, C_BODY);
    cv_blit(0, (uint32_t)(HOME_Y + (int32_t)k * HOME_ROW_H));
}

/* turning ALGO on HOME shows the algorithm over the rows for a moment */
static void draw_algo_overlay(uint32_t row)
{
    char b[24], n[8];
    cv_begin(240, HOME_ROW_H, C_BODY);
    cv_oy = -(int32_t)row * HOME_ROW_H;
    cv_rect(8, 4, 224, 184, C_LCD);
    str_cpy(b, "Algorithm ", sizeof b);
    fmt_int(n, patch.main.algorithm + 1);
    str_add(b, sizeof b, n);
    cv_text_on(16, 10, &FONT_M, b, C_TEXT, C_LCD);
    draw_algo(120, 140, 34, 22, 34, 50, &FONT_M);
    cv_oy = 0;
    cv_blit(0, (uint32_t)(HOME_Y + (int32_t)row * HOME_ROW_H));
}

static void draw_group_row(uint32_t row)
{
    const uint32_t vis = visible_count(page);
    const uint32_t last_first = vis > 3u ? vis - 3u : 0u;
    const uint32_t first = (uint32_t)clampi((int32_t)focus[page] - 1, 0, (int32_t)last_first);
    const uint32_t n = first + row;
    const int focused = n == focus[page];
    const uint16_t bg = focused ? C_BODY_FOCUS : C_BODY;
    uint32_t col;
    cv_begin(240, ROW_H, bg);
    cv_rect(0, ROW_H - 1, 240, 1, C_SEPARATOR);
    if (n < vis) {
        const uint32_t g = visible_group(page, n);
        for (col = 0; col < 4u; col++) {
            const pdef_t *d = cell(page, g, col);
            if (d)
                draw_control(d, col, 2 + (int32_t)col * 59, focused, bg);
        }
        if (focused)
            cv_rect(0, 0, 2, ROW_H - 1, C_ORANGE);
    }
    cv_blit(0, (uint32_t)(ROW_Y + (int32_t)row * ROW_H));
}

static int32_t lerp_y(int32_t v, int32_t lo, int32_t hi, int32_t h) { return h - 1 - (clampi(v, lo, hi) - lo) * (h - 1) / (hi - lo); }

static void draw_wave(void)
{
    const uint32_t op = object[PAGE_OSC];
    int32_t prev = 0, i;
    cv_rect(0, GRAPH_H / 2, LCD_W, 1, C_LINE);
    for (i = 0; i < 228; i++) {
        const int32_t y = GRAPH_H / 2 - fm_wave_sample(&patch, op, (uint32_t)(i * 256 / 228)) * (GRAPH_H / 2 - 3) / 32768;
        if (i)
            cv_line(i, prev, i + 1, y, OP_COLOR[op]);
        prev = y;
    }
}

static int32_t env_graph_y(uint32_t e, int32_t v)
{
    if (e == ENV_FILTER)
        return 2 + lerp_y(v, 0, 1000, GRAPH_H - 4);
    if (e == ENV_PITCH)
        return 2 + lerp_y(v, -4800, 4800, GRAPH_H - 4);
    return 2 + lerp_y(v, -7000, 0, GRAPH_H - 4);
}

static void draw_envelope(uint32_t e, uint16_t c)
{
    const fm_env_t *n = env_of(e);
    const int32_t wa = 4 + n->attack * 50 / TIME_MAX, wd = 4 + n->decay * 50 / TIME_MAX, wr = 4 + n->release * 50 / TIME_MAX;
    const int32_t end = e < ENV_FILTER ? -7000 : n->end;
    const int32_t x1 = 4 + wa, x2 = x1 + wd, x3 = 224 - wr;
    const int32_t y0 = env_graph_y(e, n->initial), y1 = env_graph_y(e, n->peak), y2 = env_graph_y(e, n->sustain);
    if (e == ENV_PITCH)
        cv_rect(0, env_graph_y(e, 0), LCD_W, 1, C_LINE);
    cv_line(4, y0, x1, y1, c);
    cv_line(x1, y1, x2, y2, c);
    cv_line(x2, y2, x3, y2, c);
    cv_line(x3, y2, 224, env_graph_y(e, end), c);
    fill_circle(x1, y1, 2, C_PINK);
    fill_circle(x2, y2, 2, C_PINK);
    fill_circle(x3, y2, 2, C_PINK);
    if (n->loop_mode)
        cv_text_on(LCD_W - 4 - text_w(&FONT_T, LOOP_NAME[clampi(n->loop_mode, 0, LOOP_MODES - 1)]), GRAPH_H - 13, &FONT_T,
                   LOOP_NAME[clampi(n->loop_mode, 0, LOOP_MODES - 1)], C_SCALE, C_LCD);
}

static void draw_lfo_shape(void)
{
    const int32_t type = patch.lfo.type;
    const uint16_t c = patch.lfo.on ? C_PINK : C_OFF;
    int32_t prev = 0, i, hold = 0;
    uint32_t rng = 12345u;
    cv_rect(0, GRAPH_H / 2, LCD_W, 1, C_LINE);
    for (i = 0; i < 228; i++) {
        const int32_t ph = (i * 2048 / 228) & 1023;
        int32_t v = 0;
        if (ph < 10) {
            rng = rng * 1103515245u + 12345u;
            hold = (int32_t)(rng >> 17 & 0x7FFF) * 2 - 32767;
        }
        if (type == LFO_SINE)
            v = FW_SINE[ph];
        if (type == LFO_SQUARE)
            v = ph < 512 ? 30000 : -30000;
        if (type == LFO_TRIANGLE)
            v = (ph < 512 ? ph : 1023 - ph) * 120 - 30700;
        if (type == LFO_SAW_UP)
            v = ph * 60 - 30700;
        if (type == LFO_SAW_DOWN)
            v = 30700 - ph * 60;
        if (type == LFO_SH)
            v = hold;
        if (type == LFO_NOISE) {
            rng = rng * 1103515245u + 12345u;
            v = (int32_t)(rng >> 17 & 0x7FFF) * 2 - 32767;
        }
        v = GRAPH_H / 2 - v * (GRAPH_H / 2 - 3) / 32768;
        if (i)
            cv_line(i, prev, i + 1, v, c);
        prev = v;
    }
}

/* a sketch of the response: x is log frequency (20 Hz .. 20 kHz), y dB */
static void draw_filter(void)
{
    const fm_filter_t *f = &patch.filter;
    const uint16_t c = f->on ? C_ORANGE : C_OFF;
    const int32_t xc = (f->freq - 1551) * 228 / 11960, slope = f->slope24 ? 24 : 12, bump = f->res * 14 / 1250;
    int32_t prev = 0, x;
    for (x = 0; x < 228; x++) {
        const int32_t oct10 = (x - xc) * 100 / 228;
        const int32_t near = 10 - (oct10 < 0 ? -oct10 : oct10) * 3;
        const int32_t peak = near > 0 ? bump * near / 10 : 0;
        int32_t db = 0, y;
        if (f->type == FILTER_LP || f->type == FILTER_MORPH)
            db = (oct10 > 0 ? -slope * oct10 / 10 : 0) + peak;
        if (f->type == FILTER_HP)
            db = (oct10 < 0 ? slope * oct10 / 10 : 0) + peak;
        if (f->type == FILTER_BP)
            db = -(slope / 2) * (oct10 < 0 ? -oct10 : oct10) / 10 + peak;
        if (f->type == FILTER_NOTCH)
            db = near > 0 ? -30 * near / 10 : 0;
        y = clampi(18 - db * 2, 0, GRAPH_H - 1);
        if (x)
            cv_line(x, prev, x + 1, y, c);
        prev = y;
    }
    cv_rect(clampi(xc, 0, 227), 0, 1, 4, C_SCALE);
}

static void draw_scope(void)
{
    uint32_t x;
    int32_t prev = GRAPH_H / 2;
    cv_rect(0, GRAPH_H / 2, LCD_W, 1, C_LINE);
    for (x = 0; x < SCOPE_N; x++) {
        const int32_t y = clampi(GRAPH_H / 2 - scope[x] / 900, 0, GRAPH_H - 1);
        if (x)
            cv_line((int32_t)x - 1, prev, (int32_t)x, y, C_ORANGE);
        prev = y;
    }
    scope_ready = 0;
}

static void draw_save_lcd(void)
{
    char b[24], n[8];
    str_cpy(b, "Save to U", sizeof b);
    fmt_int(n, (int32_t)save_slot + 1);
    str_add(b, sizeof b, n);
    cv_text_on(4, 1, &FONT_T, b, C_ORANGE, C_LCD);
    cv_text_on(8, 22, &FONT_M, save_name, C_BRIGHT, C_LCD);
    {
        char head[FM_NAME_LEN];
        str_cpy(head, save_name, sizeof head);
        head[save_cursor < FM_NAME_LEN ? save_cursor : FM_NAME_LEN - 1u] = 0;
        cv_rect(8 + text_w(&FONT_M, head), 39, 7, 2, C_ORANGE);
    }
    cv_text_on(4, LCD_H - 15, &FONT_T, "K1 cursor   K2 letter   SAVE write", C_SCALE, C_LCD);
}

static void lcd_name(char *b, uint32_t size)
{
    const uint32_t obj = object[page];
    str_cpy(b, PAGE_NAME[page], size);
    if (page == PAGE_OSC)
        str_cpy(b, OSC_NAME[obj], size);
    if (page == PAGE_ENV)
        str_cpy(b, ENV_NAME[obj], size);
    if (page == PAGE_GLOBAL)
        str_cpy(b, SOURCE_NAME[obj], size);
}

/* the display above the controls: what the page edits, the focused group, the graph */
static void draw_lcd(void)
{
    char b[32], n[8];
    const uint32_t vis = visible_count(page);
    cv_begin(LCD_W, LCD_H, C_LCD);
    if (page == PAGE_SAVE) {
        draw_save_lcd();
        cv_blit(LCD_X, LCD_Y);
        return;
    }
    lcd_name(b, sizeof b);
    cv_text_on(4, 1, &FONT_T, b, object_color(), C_LCD);
    if (page == PAGE_OSC)
        cv_text_on(8 + text_w(&FONT_T, b), 1, &FONT_T,
                   (fm_algo_carriers(patch.main.algorithm) >> object[page]) & 1u ? "Carrier" : "Modulator", C_SCALE, C_LCD);
    if (vis) {
        str_cpy(b, PAGE_DEF[page].g[visible_group(page, focus[page])].name, sizeof b);
        str_add(b, sizeof b, "  ");
        fmt_int(n, (int32_t)focus[page] + 1);
        str_add(b, sizeof b, n);
        str_add(b, sizeof b, "/");
        fmt_int(n, (int32_t)vis);
        str_add(b, sizeof b, n);
        cv_text_on(LCD_W - 4 - text_w(&FONT_T, b), 1, &FONT_T, b, C_SCALE, C_LCD);
    }
    cv_oy = LCD_TOP;
    if (page == PAGE_OSC)
        draw_wave();
    if (page == PAGE_ENV)
        draw_envelope(object[PAGE_ENV], object_color());
    if (page == PAGE_LFO)
        draw_lfo_shape();
    if (page == PAGE_FILTER)
        draw_filter();
    if (page == PAGE_PITCH)
        draw_envelope(ENV_PITCH, patch.pitch.on ? C_CYAN : C_OFF);
    if (page == PAGE_GLOBAL && object[page] == 0)
        draw_algo(LCD_W / 2, GRAPH_H - 14, 18, 11, 13, 26, &FONT_T);
    if (page == PAGE_GLOBAL && object[page] != 0)
        draw_scope();
    cv_oy = 0;
    cv_blit(LCD_X, LCD_Y);
}

static void draw_save_row(uint32_t row)
{
    const uint32_t slot = (save_slot + USER_N + row - 1u) % USER_N;
    const int focused = row == 1u;
    const uint16_t bg = focused ? C_BODY_FOCUS : C_BODY;
    char b[8];
    cv_begin(240, ROW_H, bg);
    cv_rect(0, ROW_H - 1, 240, 1, C_SEPARATOR);
    str_cpy(b, "U", sizeof b);
    fmt_int(b + 1, (int32_t)slot + 1);
    cv_text_on(10, 11, &FONT_T, b, C_LABEL, bg);
    cv_text_on(40, 10, &FONT_M, preset_source(OP_BANK_N + slot)->name, focused ? C_BRIGHT : C_TEXT, bg);
    if (focused)
        cv_rect(0, 0, 2, ROW_H - 1, C_ORANGE);
    cv_blit(0, (uint32_t)(ROW_Y + (int32_t)row * ROW_H));
}

static void draw_status(void)
{
    char b[24], n[8];
    cv_begin(240, 18, C_BODY);
    cv_rect(0, 0, 240, 1, C_SEPARATOR);
    if (flash_msg) {
        cv_text_on(8, 3, &FONT_T, flash_msg, C_ORANGE, C_BODY);
        cv_blit(0, STATUS_Y);
        return;
    }
    str_cpy(b, "Alg ", sizeof b);
    fmt_int(n, patch.main.algorithm + 1);
    str_add(b, sizeof b, n);
    cv_text_on(8, 3, &FONT_T, b, C_TEXT, C_BODY);
    str_cpy(b, "Voices ", sizeof b);
    fmt_int(n, mon.active);
    str_add(b, sizeof b, n);
    str_add(b, sizeof b, "/");
    fmt_int(n, (int32_t)(fm_voice_count(patch.main.voices) < mon.cap ? fm_voice_count(patch.main.voices) : mon.cap));
    str_add(b, sizeof b, n);
    cv_text_on(56, 3, &FONT_T, b, mon.active ? C_ORANGE : C_SCALE, C_BODY);
    str_cpy(b, "Oct ", sizeof b);
    fmt_signed(n, octave, 0, 1);
    str_add(b, sizeof b, n);
    cv_text_on(140, 3, &FONT_T, b, C_SCALE, C_BODY);
    str_cpy(b, "CPU ", sizeof b);
    fmt_int(n, (int32_t)(fw_stats.cpu_q8 * 100u >> 8));
    str_add(b, sizeof b, n);
    str_add(b, sizeof b, overloads != overloads_seen ? "%!" : "%");
    cv_text_on(232 - text_w(&FONT_T, b), 3, &FONT_T, b, fw_stats.cpu_q8 > CPU_HIGH_Q8 ? C_ALERT : C_SCALE, C_BODY);
    overloads_seen = overloads;
    cv_blit(0, STATUS_Y);
}

static void draw_frame(void)
{
    lcd_fill(0, TITLE_H, 240, STATUS_Y - TITLE_H, C_BODY);
    if (page != PAGE_HOME)
        lcd_fill(LCD_X - 1, LCD_Y - 1, LCD_W + 2, LCD_H + 2, C_BAR_EDGE);
}

static void draw_strip(uint32_t bit)
{
    const uint32_t row = bit - D_ROW0;
    if (bit == D_TITLE)
        draw_title();
    if (bit == D_LCD && page != PAGE_HOME)
        draw_lcd();
    if (bit >= D_ROW0 && row < ROWS_MAX && page == PAGE_HOME && algo_until)
        draw_algo_overlay(row);
    if (bit >= D_ROW0 && row < ROWS_MAX && page == PAGE_HOME && !algo_until)
        draw_home_row(row);
    if (bit >= D_ROW0 && row < 3u && page == PAGE_SAVE)
        draw_save_row(row);
    if (bit >= D_ROW0 && row < 3u && page != PAGE_HOME && page != PAGE_SAVE)
        draw_group_row(row);
    if (bit == D_STATUS)
        draw_status();
}

static int scope_page(void) { return page == PAGE_GLOBAL && object[PAGE_GLOBAL] != 0; }

static void draw(void)
{
    uint32_t n = 0, bit;
    if (dirty & FW_BIT(D_FRAME)) {
        dirty &= ~FW_BIT(D_FRAME);
        draw_frame();
        n++;
    }
    for (bit = 0; bit < D_FRAME && n < DRAWS_PER_FRAME; bit++) {
        if (!(dirty & FW_BIT(bit)) || (bit == D_LCD && scope_page() && !scope_ready))
            continue;
        dirty &= ~FW_BIT(bit);
        draw_strip(bit);
        n++;
    }
}

/* ---- input */

static void set_page(uint32_t p)
{
    if (p == page)
        return;
    page = p;
    dirty = D_ALL;
}

static void open_save(void)
{
    save_slot = preset >= OP_BANK_N ? preset - OP_BANK_N : 0u;
    str_cpy(save_name, patch.name, sizeof save_name);
    save_cursor = 0;
    set_page(PAGE_SAVE);
}

static void handle_buttons(const fw_input_t *in)
{
    static const uint8_t MAP[][2] = {{FW_BTN_HOME, PAGE_HOME}, {FW_BTN_EDIT, PAGE_OSC}, {FW_BTN_ENV, PAGE_ENV},
                                     {FW_BTN_LFO, PAGE_LFO}, {FW_BTN_FX, PAGE_FILTER}, {FW_BTN_SCL, PAGE_PITCH},
                                     {FW_BTN_GLO, PAGE_GLOBAL}};
    const uint32_t was = page;
    uint32_t i;
    for (i = 0; i < FW_NELEM(MAP); i++) {
        if (in->pressed & FW_BIT(MAP[i][0]))
            set_page(MAP[i][1]);
    }
    if ((in->pressed & FW_BIT(FW_BTN_SAVE)) && was == PAGE_SAVE) {
        preset_save(save_slot, in->ms);
        set_page(PAGE_HOME);
    }
    if ((in->pressed & FW_BIT(FW_BTN_SAVE)) && was != PAGE_SAVE)
        open_save();
    if (in->pressed & FW_BIT(FW_BTN_OCT_DOWN))
        octave = clampi(octave - 1, -3, 3);
    if (in->pressed & FW_BIT(FW_BTN_OCT_UP))
        octave = clampi(octave + 1, -3, 3);
    if (in->pressed & (FW_BIT(FW_BTN_OCT_DOWN) | FW_BIT(FW_BTN_OCT_UP)))
        dirty |= FW_BIT(D_STATUS);
}

static void handle_keys(const fw_input_t *in)
{
    uint32_t k;
    for (k = 0; k < FW_KEYS; k++) {
        if ((in->keys_released & FW_BIT(k)) && (key_held & FW_BIT(k))) {
            key_held &= ~FW_BIT(k);
            fm_note_off(key_note[k]);
        }
        if (!(in->keys_pressed & FW_BIT(k)))
            continue;
        key_note[k] = (uint8_t)clampi((int32_t)(FW_KEY_BASE_NOTE + k) + octave * 12, 0, 127);
        key_held |= FW_BIT(k);
        fm_note_on(key_note[k], KEY_VELOCITY);
    }
}

static void handle_midi(void)
{
    fw_midi_t m;
    while (fw_midi_read(&m)) {
        const uint32_t kind = m.status & 0xF0u, ch = (m.status & 15u) + 1u;
        if (kind < 0x80u || kind == 0xF0u)
            continue;
        if (global.channel && (uint32_t)global.channel != ch)
            continue;
        if (kind == 0x90u && m.d2)
            fm_note_on(m.d1, m.d2);
        if (kind == 0x80u || (kind == 0x90u && !m.d2))
            fm_note_off(m.d1);
        if (kind == 0xB0u && m.d1 == 1u)
            fm_controller(SRC_WHEEL, m.d2);
        if (kind == 0xB0u && (m.d1 == 120u || m.d1 == 123u))
            fm_all_notes_off();
        if (kind == 0xD0u)
            fm_controller(SRC_AFTERTOUCH, m.d1);
        if (kind == 0xE0u)
            fm_controller(SRC_BEND, ((int32_t)m.d2 << 7 | m.d1) - 8192);
    }
}

static void handle_save_knobs(const fw_input_t *in)
{
    const int32_t d = in->knob[FW_KNOB_SELECT] + in->knob[FW_KNOB_PRESET];
    if (d)
        save_slot = (uint32_t)(((int32_t)save_slot + d) % (int32_t)USER_N + (int32_t)USER_N) % USER_N;
    if (in->knob[FW_KNOB_1])
        save_cursor = (uint32_t)clampi((int32_t)save_cursor + in->knob[FW_KNOB_1], 0, FM_NAME_LEN - 2);
    while (str_len(save_name) < save_cursor + 1u && in->knob[FW_KNOB_2])
        str_add(save_name, sizeof save_name, " ");
    if (in->knob[FW_KNOB_2])
        save_name[save_cursor] = (char)clampi(save_name[save_cursor] + in->knob[FW_KNOB_2], 32, 126);
    if (d || in->knob[FW_KNOB_1] || in->knob[FW_KNOB_2])
        dirty |= D_ROWS | FW_BIT(D_LCD);
}

static void handle_page_knobs(const fw_input_t *in)
{
    const uint32_t vis = visible_count(page);
    const uint32_t objects = page_objects(page);
    uint32_t col;
    if (page == PAGE_HOME && in->knob[FW_KNOB_ALGO]) {
        patch.main.algorithm = (int16_t)clampi(patch.main.algorithm + in->knob[FW_KNOB_ALGO], 0, FM_ALGOS - 1);
        algo_until = now_ms + ALGO_SHOW_MS;
        dirty |= D_ROWS | FW_BIT(D_STATUS) | FW_BIT(D_TITLE);
    }
    if (objects && in->knob[FW_KNOB_ALGO]) {
        object[page] = (uint8_t)clampi((int32_t)object[page] + in->knob[FW_KNOB_ALGO], 0, (int32_t)objects - 1);
        focus[page] = (uint8_t)clampi(focus[page], 0, (int32_t)visible_count(page) - 1);
        dirty = D_ALL & ~FW_BIT(D_FRAME);
    }
    if (in->knob[FW_KNOB_SELECT] && vis) {
        focus[page] = (uint8_t)clampi((int32_t)focus[page] + in->knob[FW_KNOB_SELECT], 0, (int32_t)vis - 1);
        dirty |= D_ROWS | FW_BIT(D_LCD);
    }
    if (!vis)
        return;
    for (col = 0; col < 4u; col++) {
        const pdef_t *d = cell(page, visible_group(page, focus[page]), col);
        if (knob_param(d, col, in, FW_KNOB_1 + col))
            dirty |= D_ROWS | FW_BIT(D_LCD) | FW_BIT(D_STATUS);
    }
}

static void handle_knobs(const fw_input_t *in)
{
    const int32_t p = in->knob[FW_KNOB_PRESET];
    knob_speed(in);
    if (page == PAGE_SAVE) {
        handle_save_knobs(in);
        return;
    }
    if (p) {
        preset_select((uint32_t)(((int32_t)preset + p) % (int32_t)PRESETS_N + (int32_t)PRESETS_N));
        return;
    }
    handle_page_knobs(in);
}

/* fewer voices while the audio interrupt is near its budget, more again when it is not */
static void adapt_voices(void)
{
    if ((frame_count & 7u) != 0u)
        return;
    if (fw_stats.cpu_q8 > CPU_HIGH_Q8 && voice_cap > 1u)
        voice_cap--;
    if (fw_stats.cpu_q8 < CPU_LOW_Q8 && voice_cap < FM_VOICES_MAX)
        voice_cap++;
    fm_set_voice_cap(voice_cap);
}

static void update_leds(void)
{
    static const uint8_t PAGE_LED[PAGES] = {FW_BTN_HOME, FW_BTN_EDIT, FW_BTN_ENV, FW_BTN_LFO,
                                            FW_BTN_FX, FW_BTN_SCL, FW_BTN_GLO, FW_BTN_SAVE};
    uint32_t i;
    for (i = 0; i < PAGES; i++)
        fw_led_button(PAGE_LED[i], i == page);
    fw_leds_keys(key_held);
}

static void app_init(void)
{
    focus[PAGE_HOME] = HOME_LEVEL;
    store_load();
    fm_init();
    preset_select(store[0].current < PRESETS_N ? store[0].current : 0u);
}

static void app_frame(const fw_input_t *in)
{
    frame_count++;
    now_ms = in->ms;
    global.volume = (int16_t)(in->master * in->master >> 12);
    handle_buttons(in);
    handle_knobs(in);
    handle_keys(in);
    handle_midi();
    adapt_voices();
    fm_monitor(&mon);
    if (flash_msg && (int32_t)(in->ms - flash_msg_until) > 0) {
        flash_msg = 0;
        dirty |= FW_BIT(D_STATUS);
    }
    if ((frame_count & 31u) == 0u || mon.active != shown_voices)
        dirty |= FW_BIT(D_STATUS);
    if (edited() != shown_edited)
        dirty |= FW_BIT(D_TITLE);
    shown_edited = edited();
    shown_voices = mon.active;
    if (algo_until && (int32_t)(now_ms - algo_until) > 0) {
        algo_until = 0;
        dirty |= D_ROWS;
    }
    if (scope_page())
        dirty |= FW_BIT(D_LCD);
    update_leds();
    draw();
}

static void app_audio(int32_t *lr, uint32_t frames)
{
    uint32_t i;
    if (fw_stats.cpu_q8 > CPU_MUTE_Q8) {
        memset(lr, 0, frames * 2u * sizeof *lr);
        overloads++;
        return;
    }
    fm_render(&patch, &global, lr, frames);
    if (scope_ready)
        return;
    for (i = 0; i < SCOPE_N && i < frames; i++)
        scope[i] = (int16_t)clampi(lr[2u * i], -32768, 32767);
    scope_ready = 1;
}
