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
#define SCOPE_N 230u
#define DRAWS_PER_FRAME 2u
#define CPU_MUTE_Q8 240u
#define CPU_HIGH_Q8 205u
#define CPU_LOW_Q8 150u
#define ROWS 3u
#define ROW_Y(r) (44u + (r) * 34u)
#define GRAPH_Y 148u
#define GRAPH_H 70
#define OFFSET(t, f) ((uint16_t)__builtin_offsetof(t, f))

/* Live's dark theme (Default Dark Neutral Medium) and the oscillator colours Operator1..4 of every theme */
#define C_BODY RGB(0x36, 0x36, 0x36)
#define C_BAR RGB(0x46, 0x46, 0x46)
#define C_BAR_EDGE RGB(0x24, 0x24, 0x24)
#define C_LCD RGB(0x18, 0x18, 0x18)
#define C_LCD_FOCUS RGB(0x26, 0x26, 0x26)
#define C_LINE RGB(0x3e, 0x3e, 0x3e)
#define C_ORANGE RGB(0xff, 0xad, 0x56)
#define C_CYAN RGB(0x03, 0xc3, 0xd5)
#define C_PINK RGB(0xff, 0x69, 0x7f)
#define C_SCALE RGB(0x86, 0x86, 0x86)
#define C_TEXT RGB(0xb5, 0xb5, 0xb5)
#define C_BRIGHT RGB(0xe0, 0xe0, 0xe0)
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
enum { D_TITLE, D_HEADER, D_ROW0, D_GRAPH = D_ROW0 + ROWS, D_STATUS, D_FRAME, D_ALL = 0xFFu };

typedef struct {
    const char *label;
    uint8_t base, fmt;
    uint16_t off;
    int16_t lo, hi, step;
} pdef_t;

typedef struct {
    const char *name, *tag;
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

static const group_t G_HOME[] = {
    {"LEVEL", "LVL", W_ALL, {OSCN("A", level, F_DB, -7000, 0, 50), OSCN("B", level, F_DB, -7000, 0, 50),
                      OSCN("C", level, F_DB, -7000, 0, 50), OSCN("D", level, F_DB, -7000, 0, 50)}},
    {"COARSE", "CRS", W_ALL, {OSCN("A", coarse, F_RATIO, 0, 48, 1), OSCN("B", coarse, F_RATIO, 0, 48, 1),
                       OSCN("C", coarse, F_RATIO, 0, 48, 1), OSCN("D", coarse, F_RATIO, 0, 48, 1)}},
    {"FINE", "FIN", W_ALL, {OSCN("A", fine, F_INT, 0, 1000, 1), OSCN("B", fine, F_INT, 0, 1000, 1),
                     OSCN("C", fine, F_INT, 0, 1000, 1), OSCN("D", fine, F_INT, 0, 1000, 1)}},
    {"WAVE", "WAV", W_ALL, {OSCN("A", wave, F_WAVE, 0, 22, 1), OSCN("B", wave, F_WAVE, 0, 22, 1),
                     OSCN("C", wave, F_WAVE, 0, 22, 1), OSCN("D", wave, F_WAVE, 0, 22, 1)}},
    {"FEEDBACK", "FB", W_ALL, {OSCN("A", feedback, F_PCTU, 0, 1000, 10), OSCN("B", feedback, F_PCTU, 0, 1000, 10),
                         OSCN("C", feedback, F_PCTU, 0, 1000, 10), OSCN("D", feedback, F_PCTU, 0, 1000, 10)}},
    {"ON", "ON", W_ALL, {OSCN("A", on, F_ONOFF, 0, 1, 1), OSCN("B", on, F_ONOFF, 0, 1, 1),
                   OSCN("C", on, F_ONOFF, 0, 1, 1), OSCN("D", on, F_ONOFF, 0, 1, 1)}},
};

static const group_t G_OSC[] = {
    {"TUNE", "", W_ALL, {OSC("COARSE", coarse, F_RATIO, 0, 48, 1), OSC("FINE", fine, F_INT, 0, 1000, 1),
                     OSC("LEVEL", level, F_DB, -7000, 0, 50), OSC("WAVE", wave, F_WAVE, 0, 22, 1)}},
    {"FIXED", "", W_ALL, {OSC("FIXED", fixed, F_ONOFF, 0, 1, 1), OSC("FREQ", fixed_freq, F_HZ10, 100, 20000, 10),
                      OSC("MULTI", fixed_multi, F_MULTI, 0, 5, 1), OSC("FEEDBK", feedback, F_PCTU, 0, 1000, 10)}},
    {"PHASE", "", W_ALL, {OSC("PHASE", phase, F_PCTU, 0, 1000, 10), OSC("RETRIG", retrigger, F_ONOFF, 0, 1, 1),
                      OSC("OSC<V", vel_coarse, F_PCT, -1000, 1000, 10), OSC("Q", vel_coarse_q, F_ONOFF, 0, 1, 1)}},
    {"SCALE", "", W_ALL, {OSC("LVL<V", vel_scale, F_PCT, -1000, 1000, 10), OSC("LVL<K", key_scale, F_PCT, -1000, 1000, 10),
                      OSC("LFO", lfo_on, F_ONOFF, 0, 1, 1), OSC("P.ENV", pitch_on, F_ONOFF, 0, 1, 1)}},
    {"PARTIALS", "", W_ALL, {{"HARM #", B_HARM_SEL, F_INDEX, 0, 0, 63, 1}, {"AMP", B_HARM, F_HARM, 0, 0, 255, 1},
                         OSC("COUNT", harm_count, F_PARTIALS, 0, 2, 1), OSC("REPEAT", harm_repeat, F_INT, 0, 10, 1)}},
    {"PARTIALS+", "", W_ALL, {OSC("NORMAL", harm_normalize, F_ONOFF, 0, 1, 1), NONE, NONE, NONE}},
};

static const group_t G_ENV[] = {
    {"TIMES", "", W_ALL, {ENV("ATTACK", attack, F_TIME, 0, TIME_MAX, 10), ENV("DECAY", decay, F_TIME, 0, TIME_MAX, 10),
                      ENV("SUST", sustain, F_ENVLVL, 0, 0, 0), ENV("RELEASE", release, F_TIME, 0, TIME_MAX, 10)}},
    {"LEVELS", "", W_ALL, {ENV("INIT", initial, F_ENVLVL, 0, 0, 0), ENV("PEAK", peak, F_ENVLVL, 0, 0, 0),
                       ENV("END", end, F_ENVLVL, 0, 0, 0), ENV("T<VEL", time_vel, F_PCT, -1000, 1000, 10)}},
    {"MODE", "", W_ALL, {ENV("MODE", loop_mode, F_LOOP, 0, LOOP_MODES - 1, 1), ENV("LOOP T", loop_time, F_TIME, 301, 5301, 10),
                     ENV("REPEAT", repeat, F_BEAT, 0, 14, 1), NONE}},
    {"SLOPES", "", W_SLOPED, {ENV("A.SLP", a_slope, F_SLOPE, -1000, 1000, 10), ENV("D.SLP", d_slope, F_SLOPE, -1000, 1000, 10),
                          ENV("R.SLP", r_slope, F_SLOPE, -1000, 1000, 10), NONE}},
};

static const group_t G_LFO[] = {
    {"MAIN", "", W_ALL, {LFO("ON", on, F_ONOFF, 0, 1, 1), LFO("TYPE", type, F_LFOTYPE, 0, LFO_TYPES - 1, 1),
                     LFO("RATE", rate, F_LFORATE, 0, 1270, 5), LFO("AMOUNT", amount, F_PCTU, 0, 1000, 10)}},
    {"RATE", "", W_ALL, {LFO("RANGE", range, F_RANGE, 0, 2, 1), LFO("SYNCED", rate_synced, F_BEAT, 0, 14, 1),
                     LFO("R<KEY", rate_key, F_PCTU, 0, 1000, 10), LFO("RETRIG", retrigger, F_ONOFF, 0, 1, 1)}},
    {"DEST A", "DST", W_ALL, {OSCN("A", lfo_on, F_ONOFF, 0, 1, 1), OSCN("B", lfo_on, F_ONOFF, 0, 1, 1),
                       OSCN("C", lfo_on, F_ONOFF, 0, 1, 1), OSCN("D", lfo_on, F_ONOFF, 0, 1, 1)}},
    {"DEST A+", "", W_ALL, {FIL("FILTER", lfo_on, F_ONOFF, 0, 1, 1), LFO("AMT A", amount_a, F_PCT, -1000, 1000, 10),
                        LFO("AMT<V", amount_vel, F_PCT, -1000, 1000, 10), NONE}},
    {"DEST B", "", W_ALL, {LFO("DEST B", dst_b, F_DST, 0, FM_DESTS - 1, 1), LFO("AMT B", amount_b, F_PCT, -1000, 1000, 10),
                       NONE, NONE}},
};

static const group_t G_FILTER[] = {
    {"FILTER", "", W_ALL, {FIL("FREQ", freq, F_CUTOFF, 2251, 13372, 10), FIL("RES", res, F_RES, 0, 1250, 10),
                       FIL("TYPE", type, F_FTYPE, 0, FILTER_TYPES - 1, 1), FIL("ENV", env_amount, F_PCT, -1000, 1000, 10)}},
    {"CIRCUIT", "", W_ALL, {FIL("ON", on, F_ONOFF, 0, 1, 1), FIL("CIRC", circuit, F_CIRCUIT, 0, 4, 1),
                        FIL("SLOPE", slope24, F_SLOPE24, 0, 1, 1), FIL("DRIVE", drive, F_DRIVE, 0, 2400, 25)}},
    {"MOD", "", W_ALL, {FIL("MORPH", morph, F_PCTU, 0, 1000, 10), FIL("F<VEL", freq_vel, F_PCT, -1000, 1000, 10),
                    FIL("F<KEY", freq_key, F_PCT, -2000, 2000, 10), FIL("LFO", lfo_on, F_ONOFF, 0, 1, 1)}},
    {"SHAPER", "", W_ALL, {FIL("SHAPER", shaper, F_SHAPER, 0, SHAPER_TYPES - 1, 1), FIL("DRIVE", shaper_drive, F_DRIVE, -1200, 1200, 25),
                       FIL("DRY/WT", shaper_wet, F_PCTU, 0, 1000, 10), FIL("BP/N", circuit_bp, F_BPCIRC, 0, 1, 1)}},
};

static const group_t G_PITCH[] = {
    {"PITCH ENV", "", W_ALL, {PIT("ON", on, F_ONOFF, 0, 1, 1), PIT("AMOUNT", env_amount, F_PCT, -1000, 1000, 10),
                          PIT("AMT A", amount_a, F_PCT, -1000, 1000, 10), NONE}},
    {"DEST A", "DST", W_ALL, {OSCN("A", pitch_on, F_ONOFF, 0, 1, 1), OSCN("B", pitch_on, F_ONOFF, 0, 1, 1),
                       OSCN("C", pitch_on, F_ONOFF, 0, 1, 1), OSCN("D", pitch_on, F_ONOFF, 0, 1, 1)}},
    {"DEST", "", W_ALL, {LFO("LFO", pitch_on, F_ONOFF, 0, 1, 1), PIT("DEST B", dst_b, F_DST, 0, FM_DESTS - 1, 1),
                     PIT("AMT B", amount_b, F_PCT, -1000, 1000, 10), MAIN("PB RNG", bend_range, F_SEMI, 0, 24, 1)}},
    {"PITCH", "", W_ALL, {MAIN("SPREAD", spread, F_PCTU, 0, 1000, 10), MAIN("TRANSP", transpose, F_SEMI, -48, 48, 1),
                      MAIN("GLIDE", glide, F_ONOFF, 0, 1, 1), MAIN("G.TIME", glide_time, F_TIME, 0, 5000, 10)}},
};

static const group_t G_GLOBAL[] = {
    {"VOICE", "", W_GLOBAL, {MAIN("VOICES", voices, F_VOICES, 0, 13, 1), MAIN("RETRIG", retrigger, F_ONOFF, 0, 1, 1),
                         MAIN("VOLUME", volume, F_DB, -7000, 600, 50), MAIN("PAN", pan, F_PAN, -1000, 1000, 20)}},
    {"TIME", "", W_GLOBAL, {MAIN("TIME", time, F_PCT, -1000, 1000, 10), MAIN("T<KEY", time_key, F_PCT, -1000, 1000, 10),
                        MAIN("T.ATK", time_attack, F_ONOFF, 0, 1, 1), MAIN("TONE", tone, F_PCTU, 0, 1000, 10)}},
    {"PAN", "", W_GLOBAL, {MAIN("P<KEY", pan_key, F_PCTU, 0, 1000, 10), MAIN("P<RND", pan_rnd, F_PCTU, 0, 1000, 10),
                       MAIN("INTERP", interpolation, F_ONOFF, 0, 1, 1), MAIN("AA", antialias, F_ONOFF, 0, 1, 1)}},
    {"DEVICE", "", W_GLOBAL, {DEV("CHAN", channel, F_CHANNEL, 0, 16, 1), DEV("TUNE", fine, F_CENT, -100, 100, 1),
                          DEV("TEMPO", tempo, F_INT, 40, 240, 1), NONE}},
    {"ROUTING", "", W_SOURCE, {ROUTE("DEST 1", 0, dst, F_DST, 0, FM_DESTS - 1, 1), ROUTE("AMT 1", 0, amount, F_PCT, -1000, 1000, 10),
                           ROUTE("DEST 2", 1, dst, F_DST, 0, FM_DESTS - 1, 1), ROUTE("AMT 2", 1, amount, F_PCT, -1000, 1000, 10)}},
};

static const struct {
    const group_t *g;
    uint8_t n, objects;
} PAGE_DEF[PAGES] = {
    {G_HOME, FW_NELEM(G_HOME), 0}, {G_OSC, FW_NELEM(G_OSC), FM_OPS}, {G_ENV, FW_NELEM(G_ENV), FM_ENVS},
    {G_LFO, FW_NELEM(G_LFO), 0}, {G_FILTER, FW_NELEM(G_FILTER), 0}, {G_PITCH, FW_NELEM(G_PITCH), 0},
    {G_GLOBAL, FW_NELEM(G_GLOBAL), 1 + FM_SOURCES}, {0, 0, 0},
};

static const char *const PAGE_NAME[PAGES] = {"ALG", "OSC", "ENV", "LFO", "FILTER", "PITCH", "GLOBAL", "SAVE"};
static const char *const ENV_NAME[FM_ENVS] = {"ENV A", "ENV B", "ENV C", "ENV D", "FILTER ENV", "PITCH ENV", "LFO ENV"};
static const char *const SOURCE_NAME[1 + FM_SOURCES] = {"GLOBAL", "VELOCITY", "KEY", "AFTERTOUCH", "PITCH BEND", "MOD WHEEL"};
static const char *const OP_NAME[FM_OPS] = {"A", "B", "C", "D"};
static const uint16_t OP_COLOR[FM_OPS] = {RGB(0xe0, 0xd8, 0x25), RGB(0x29, 0xd6, 0xcd), RGB(0x65, 0x71, 0xf6), RGB(0xf3, 0x75, 0x1b)};
static const char *const WAVE_NAME[FM_WAVES] = {"Sine", "Sin 4b", "Sin 8b", "Saw 3", "Saw 4", "Saw 6", "Saw 8", "Saw 16",
                                                "Saw 32", "Saw 64", "Saw D", "Sqr 3", "Sqr 4", "Sqr 6", "Sqr 8", "Sqr 16",
                                                "Sqr 32", "Sqr 64", "Sqr D", "Tri", "NoiseL", "NoiseW", "User"};
static const char *const LOOP_NAME[LOOP_MODES] = {"None", "Loop", "Beat", "Sync", "Trig"};
static const char *const BEAT_NAME[15] = {"1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4T", "3/16", "1/4",
                                          "1/2T", "3/8", "1/2", "3/4", "1 bar", "2 bars", "4 bars"};
static const char *const LFO_NAME[LFO_TYPES] = {"Sine", "Square", "Tri", "Saw Up", "Saw Dn", "S&H", "Noise"};
static const char *const RANGE_NAME[3] = {"Low", "High", "Sync"};
static const char *const FTYPE_NAME[FILTER_TYPES] = {"LP", "HP", "BP", "Notch", "Morph"};
static const char *const CIRCUIT_NAME[5] = {"Clean", "OSR", "MS2", "SMP", "PRD"};
static const char *const SHAPER_NAME[SHAPER_TYPES] = {"Off", "Soft", "Hard", "Sine", "4-Bit"};
static const char *const MULTI_NAME[6] = {"x.0001", "x.001", "x.01", "x0.1", "x1", "x10"};
static const char *const PARTIALS_NAME[3] = {"16", "32", "64"};
static const char *const DST_NAME[FM_DESTS] = {"Off", "Vol A", "Vol B", "Vol C", "Vol D", "X A/C", "X B/D", "Feedbk",
                                               "Fixed", "FM Drv", "Freq", "Q (L)", "Res", "Morph", "Drive", "F.Env",
                                               "Shaper", "LFO R", "LFO A", "P.Env", "Volume", "Pan", "Tone", "Time"};
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
static uint32_t page = PAGE_HOME, preset, save_slot, save_cursor, dirty = D_ALL, frame_count, harm_sel;
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

static const pdef_t P_SYNCED = LFO("RATE", rate_synced, F_BEAT, 0, 14, 1);

static const pdef_t *cell(uint32_t pg, uint32_t g, uint32_t col)
{
    const pdef_t *d = &PAGE_DEF[pg].g[g].p[col];
    if (!d->label)
        return 0;
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

/* ---- value text, at most 7 characters */

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

/* ---- drawing */

static uint16_t object_color(void)
{
    const uint32_t obj = object[page];
    if (page == PAGE_OSC || (page == PAGE_ENV && obj < ENV_FILTER))
        return OP_COLOR[obj];
    if (page == PAGE_ENV && obj == ENV_PITCH)
        return C_CYAN;
    if ((page == PAGE_ENV && obj == ENV_LFO) || page == PAGE_LFO)
        return C_PINK;
    if (page == PAGE_PITCH)
        return C_CYAN;
    return C_ORANGE;
}

static void draw_frame(void)
{
    lcd_fill(0, 22, 240, 20, C_BODY);
    lcd_fill(0, 42, 4, 180, C_BODY);
    lcd_fill(236, 42, 4, 180, C_BODY);
    lcd_fill(4, 42, 232, 1, C_LINE);
    lcd_fill(4, 219, 232, 1, C_LINE);
    lcd_fill(4, 43, 1, 176, C_LINE);
    lcd_fill(235, 43, 1, 176, C_LINE);
    lcd_fill(5, 43, 230, 176, C_LCD);
    lcd_fill(0, 220, 240, 2, C_BODY);
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

/* the device title bar: activator, preset name, slot */
static void draw_title(void)
{
    char r[12], n[8];
    cv_begin(240, 22, C_BAR);
    cv_rect(0, 21, 240, 1, C_BAR_EDGE);
    fill_circle(11, 10, 5, C_ORANGE);
    cv_text_on(24, 3, &FONT_S, patch.name, C_BRIGHT, C_BAR);
    r[0] = 0;
    if (preset >= OP_BANK_N)
        str_cpy(r, "U", sizeof r);
    fmt_int(n, (int32_t)(preset < OP_BANK_N ? preset + 1u : preset - OP_BANK_N + 1u));
    str_add(r, sizeof r, n);
    if (edited())
        cv_text_on(232 - text_w(&FONT_S, r) - 10, 3, &FONT_S, "*", C_ORANGE, C_BAR);
    cv_text_on(232 - text_w(&FONT_S, r), 3, &FONT_S, r, C_SCALE, C_BAR);
    cv_blit(0, 0);
}

static void draw_chip(int32_t x, const char *s, uint16_t c)
{
    const int32_t w = text_w(&FONT_S, s) + 10;
    cv_rect(x, 2, w, 16, c);
    cv_text_on(x + 5, 2, &FONT_S, s, C_INK, c);
}

static void draw_header(void)
{
    char b[24], n[8];
    const uint32_t obj = object[page];
    const uint32_t vis = visible_count(page);
    int32_t x = 4;
    cv_begin(240, 20, C_BODY);
    b[0] = 0;
    if (page == PAGE_HOME) {
        str_cpy(b, "ALG ", sizeof b);
        fmt_int(n, patch.main.algorithm + 1);
        str_add(b, sizeof b, n);
    }
    if (page == PAGE_OSC) {
        str_cpy(b, "OSC ", sizeof b);
        str_add(b, sizeof b, OP_NAME[obj]);
    }
    if (page == PAGE_ENV)
        str_cpy(b, ENV_NAME[obj], sizeof b);
    if (page == PAGE_GLOBAL)
        str_cpy(b, SOURCE_NAME[obj], sizeof b);
    if (!b[0])
        str_cpy(b, PAGE_NAME[page], sizeof b);
    draw_chip(x, b, object_color());
    x += text_w(&FONT_S, b) + 16;
    if (page == PAGE_OSC)
        cv_text_on(x, 2, &FONT_S, (fm_algo_carriers(patch.main.algorithm) >> obj) & 1u ? "CARRIER" : "MOD", C_SCALE, C_BODY);
    if (page != PAGE_SAVE && vis) {
        const uint32_t f = focus[page];
        str_cpy(b, PAGE_DEF[page].g[visible_group(page, f)].name, sizeof b);
        str_add(b, sizeof b, " ");
        fmt_int(n, (int32_t)f + 1);
        str_add(b, sizeof b, n);
        str_add(b, sizeof b, "/");
        fmt_int(n, (int32_t)vis);
        str_add(b, sizeof b, n);
        cv_text_on(236 - text_w(&FONT_S, b), 2, &FONT_S, b, C_TEXT, C_BODY);
    }
    if (page == PAGE_SAVE)
        cv_text_on(236 - text_w(&FONT_S, "USER SLOT"), 2, &FONT_S, "USER SLOT", C_TEXT, C_BODY);
    cv_blit(0, 22);
}

static uint16_t cell_label_color(const pdef_t *d, uint32_t col, int focused)
{
    if (d->base == B_OSCN)
        return OP_COLOR[col];
    return focused ? C_TEXT : C_SCALE;
}

static void draw_group_row(uint32_t row)
{
    const uint32_t vis = visible_count(page);
    const uint32_t last_first = vis > ROWS ? vis - ROWS : 0u;
    const uint32_t first = (uint32_t)clampi((int32_t)focus[page] - 1, 0, (int32_t)last_first);
    const uint32_t n = first + row;
    const int focused = n == focus[page];
    const uint16_t bg = focused ? C_LCD_FOCUS : C_LCD;
    uint32_t col;
    cv_begin(230, 32, bg);
    if (n < vis) {
        const uint32_t g = visible_group(page, n);
        for (col = 0; col < 4u; col++) {
            const pdef_t *d = cell(page, g, col);
            const int32_t x = 4 + (int32_t)col * 57;
            char v[16], label[12];
            if (!d)
                continue;
            format_value(v, sizeof v, d, col);
            str_cpy(label, d->label, sizeof label);
            if (d->base == B_OSCN) {
                str_add(label, sizeof label, " ");
                str_add(label, sizeof label, PAGE_DEF[page].g[g].tag);
            }
            cv_text_on(x, 0, &FONT_S, label, cell_label_color(d, col, focused), bg);
            cv_text_on(x, 15, &FONT_S, v, focused ? C_ORANGE : C_TEXT, bg);
        }
        if (focused)
            cv_rect(0, 0, 2, 32, C_ORANGE);
    }
    cv_blit(5, ROW_Y(row));
}

static int32_t lerp_y(int32_t v, int32_t lo, int32_t hi, int32_t h) { return h - 1 - (clampi(v, lo, hi) - lo) * (h - 1) / (hi - lo); }

static void draw_algorithm(void)
{
    const uint32_t algo = (uint32_t)patch.main.algorithm, carriers = fm_algo_carriers((int32_t)algo);
    int32_t depth[FM_OPS], x[FM_OPS], y[FM_OPS], row_n[FM_OPS] = {0, 0, 0, 0}, row_i[FM_OPS] = {0, 0, 0, 0};
    int32_t k, j;
    for (k = 0; k < FM_OPS; k++)
        depth[k] = 0;
    for (k = 0; k < FM_OPS; k++) {
        for (j = 0; j < k; j++) {
            if ((fm_algo_mods((int32_t)algo, j) >> k) & 1u)
                depth[k] = depth[k] > depth[j] + 1 ? depth[k] : depth[j] + 1;
        }
    }
    for (k = 0; k < FM_OPS; k++)
        row_n[depth[k]]++;
    for (k = 0; k < FM_OPS; k++) {
        const int32_t d = depth[k];
        x[k] = 115 - (row_n[d] - 1) * 20 + row_i[d]++ * 40;
        y[k] = 54 - d * 16;
    }
    for (k = 0; k < FM_OPS; k++) {
        for (j = k + 1; j < FM_OPS; j++) {
            if ((fm_algo_mods((int32_t)algo, k) >> j) & 1u)
                cv_line(x[j], y[j] + 6, x[k], y[k] - 6, C_SCALE);
        }
        if ((carriers >> k) & 1u)
            cv_line(x[k], y[k] + 6, x[k], 64, C_SCALE);
        if (!fm_algo_mods((int32_t)algo, k) && patch.osc[k].feedback > 0) {
            cv_line(x[k] + 11, y[k], x[k] + 15, y[k], C_SCALE);
            cv_line(x[k] + 15, y[k], x[k] + 15, y[k] - 9, C_SCALE);
            cv_line(x[k] + 15, y[k] - 9, x[k], y[k] - 9, C_SCALE);
        }
    }
    cv_rect(20, 64, 190, 2, C_SCALE);
    for (k = 0; k < FM_OPS; k++) {
        const uint16_t c = patch.osc[k].on ? OP_COLOR[k] : C_OFF;
        cv_rect(x[k] - 10, y[k] - 7, 21, 15, c);
        cv_text_on(x[k] - 3, y[k] - 8, &FONT_S, OP_NAME[k], C_INK, c);
    }
}

static void draw_wave(void)
{
    const uint32_t op = object[PAGE_OSC];
    int32_t prev = 0, i;
    cv_rect(0, GRAPH_H / 2, 230, 1, C_LINE);
    for (i = 0; i < 228; i++) {
        const int32_t y = GRAPH_H / 2 - fm_wave_sample(&patch, op, (uint32_t)(i * 256 / 228)) * (GRAPH_H / 2 - 4) / 32768;
        if (i)
            cv_line(i, prev, i + 1, y, OP_COLOR[op]);
        prev = y;
    }
}

static int32_t env_graph_y(uint32_t e, int32_t v)
{
    if (e == ENV_FILTER)
        return 4 + lerp_y(v, 0, 1000, GRAPH_H - 8);
    if (e == ENV_PITCH)
        return 4 + lerp_y(v, -4800, 4800, GRAPH_H - 8);
    return 4 + lerp_y(v, -7000, 0, GRAPH_H - 8);
}

static void draw_envelope(uint32_t e, uint16_t c)
{
    const fm_env_t *n = env_of(e);
    const int32_t wa = 4 + n->attack * 46 / TIME_MAX, wd = 4 + n->decay * 46 / TIME_MAX, wr = 4 + n->release * 46 / TIME_MAX;
    const int32_t end = e < ENV_FILTER ? -7000 : n->end;
    const int32_t x1 = 6 + wa, x2 = x1 + wd, x3 = 225 - wr;
    const int32_t y0 = env_graph_y(e, n->initial), y1 = env_graph_y(e, n->peak), y2 = env_graph_y(e, n->sustain);
    if (e == ENV_PITCH)
        cv_rect(0, env_graph_y(e, 0), 230, 1, C_LINE);
    cv_line(6, y0, x1, y1, c);
    cv_line(x1, y1, x2, y2, c);
    cv_line(x2, y2, x3, y2, c);
    cv_line(x3, y2, 225, env_graph_y(e, end), c);
    if (n->loop_mode == LOOP_LOOP)
        cv_line(x2, y2 + 3, x3, y2 + 3, C_LINE);
    cv_text(8, GRAPH_H - 16, &FONT_S, LOOP_NAME[clampi(n->loop_mode, 0, LOOP_MODES - 1)], C_SCALE);
}

static void draw_lfo_shape(void)
{
    const int32_t type = patch.lfo.type;
    const uint16_t c = patch.lfo.on ? C_PINK : C_OFF;
    int32_t prev = 0, i;
    uint32_t rng = 12345u;
    int32_t hold = 0;
    cv_rect(0, GRAPH_H / 2, 230, 1, C_LINE);
    for (i = 0; i < 228; i++) {
        const int32_t ph = (i * 2048 / 228) & 1023;
        int32_t v = 0;
        if (ph < 1024 * 2 / 228 + 1) {
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
        v = GRAPH_H / 2 - v * (GRAPH_H / 2 - 4) / 32768;
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
        const int32_t oct10 = (x - xc) * 10 * 10 / 228;   /* tenths of an octave from the cutoff */
        const int32_t near = 10 - (oct10 < 0 ? -oct10 : oct10) * 3;
        const int32_t peak = near > 0 ? bump * near / 10 : 0;
        int32_t db = 0;
        if (f->type == FILTER_LP || f->type == FILTER_MORPH)
            db = (oct10 > 0 ? -slope * oct10 / 10 : 0) + peak;
        if (f->type == FILTER_HP)
            db = (oct10 < 0 ? slope * oct10 / 10 : 0) + peak;
        if (f->type == FILTER_BP)
            db = -(slope / 2) * (oct10 < 0 ? -oct10 : oct10) / 10 + peak;
        if (f->type == FILTER_NOTCH)
            db = near > 0 ? -30 * near / 10 : 0;
        {
            const int32_t y = clampi(20 - db * 2, 0, GRAPH_H - 1);
            if (x)
                cv_line(x, prev, x + 1, y, c);
            prev = y;
        }
    }
    cv_rect(xc < 0 ? 0 : xc, 0, 1, 4, C_SCALE);
    cv_text(8, GRAPH_H - 16, &FONT_S, f->type == FILTER_MORPH ? "Morph" : FTYPE_NAME[clampi(f->type, 0, FILTER_TYPES - 1)], C_SCALE);
}

static void draw_scope(void)
{
    uint32_t x;
    int32_t prev = GRAPH_H / 2;
    cv_rect(0, GRAPH_H / 2, 230, 1, C_LINE);
    for (x = 0; x < SCOPE_N; x++) {
        const int32_t y = clampi(GRAPH_H / 2 - scope[x] / 700, 0, GRAPH_H - 1);
        if (x)
            cv_line((int32_t)x - 1, prev, (int32_t)x, y, C_ORANGE);
        prev = y;
    }
    scope_ready = 0;
}

static void draw_save_row(uint32_t row)
{
    const uint32_t slot = (save_slot + USER_N + row - 1u) % USER_N;
    const int focused = row == 1u;
    const uint16_t bg = focused ? C_LCD_FOCUS : C_LCD;
    char b[8];
    cv_begin(230, 32, bg);
    str_cpy(b, "U", sizeof b);
    fmt_int(b + 1, (int32_t)slot + 1);
    cv_text_on(4, 8, &FONT_S, b, C_SCALE, bg);
    cv_text_on(40, 8, &FONT_S, focused ? save_name : preset_source(OP_BANK_N + slot)->name, focused ? C_ORANGE : C_TEXT, bg);
    if (focused) {
        cv_rect(0, 0, 2, 32, C_ORANGE);
        cv_rect(40 + (int32_t)save_cursor * 8, 25, 8, 2, C_BRIGHT);
    }
    cv_blit(5, ROW_Y(row));
}

static void draw_save_help(void)
{
    cv_text_on(8, 4, &FONT_S, "SELECT  slot", C_SCALE, C_LCD);
    cv_text_on(8, 20, &FONT_S, "K1 cursor  K2 letter", C_SCALE, C_LCD);
    cv_text_on(8, 36, &FONT_S, "SAVE    write", C_ORANGE, C_LCD);
}

static void draw_graph(void)
{
    cv_begin(230, GRAPH_H, C_LCD);
    if (page == PAGE_HOME)
        draw_algorithm();
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
    if (page == PAGE_GLOBAL)
        draw_scope();
    if (page == PAGE_SAVE)
        draw_save_help();
    cv_blit(5, GRAPH_Y);
}

static void draw_status(void)
{
    char b[24], n[8];
    cv_begin(240, 18, C_BODY);
    if (flash_msg) {
        cv_text_on(8, 1, &FONT_S, flash_msg, C_ORANGE, C_BODY);
        cv_blit(0, 222);
        return;
    }
    str_cpy(b, "VOX ", sizeof b);
    fmt_int(n, mon.active);
    str_add(b, sizeof b, n);
    str_add(b, sizeof b, "/");
    fmt_int(n, (int32_t)(fm_voice_count(patch.main.voices) < mon.cap ? fm_voice_count(patch.main.voices) : mon.cap));
    str_add(b, sizeof b, n);
    cv_text_on(8, 1, &FONT_S, b, mon.active ? C_ORANGE : C_SCALE, C_BODY);
    str_cpy(b, "OCT", sizeof b);
    fmt_signed(n, octave, 0, 1);
    str_add(b, sizeof b, octave ? n : "0");
    cv_text_on(104, 1, &FONT_S, b, C_SCALE, C_BODY);
    fmt_int(n, (int32_t)(fw_stats.cpu_q8 * 100u >> 8));
    str_add(n, sizeof n, overloads != overloads_seen ? "%!" : "%");
    cv_text_on(232 - text_w(&FONT_S, n), 1, &FONT_S, n, fw_stats.cpu_q8 > CPU_HIGH_Q8 ? C_ALERT : C_SCALE, C_BODY);
    overloads_seen = overloads;
    cv_blit(0, 222);
}

static void draw_strip(uint32_t bit)
{
    if (bit == D_FRAME)
        draw_frame();
    if (bit == D_TITLE)
        draw_title();
    if (bit == D_HEADER)
        draw_header();
    if (bit >= D_ROW0 && bit < D_ROW0 + ROWS && page != PAGE_SAVE)
        draw_group_row(bit - D_ROW0);
    if (bit >= D_ROW0 && bit < D_ROW0 + ROWS && page == PAGE_SAVE)
        draw_save_row(bit - D_ROW0);
    if (bit == D_GRAPH)
        draw_graph();
    if (bit == D_STATUS)
        draw_status();
}

static void draw(void)
{
    uint32_t n = 0, bit;
    if (dirty & FW_BIT(D_FRAME)) {
        dirty &= ~FW_BIT(D_FRAME);
        draw_frame();
        n++;
    }
    for (bit = 0; bit < D_FRAME && n < DRAWS_PER_FRAME; bit++) {
        if (!(dirty & FW_BIT(bit)) || (bit == D_GRAPH && page == PAGE_GLOBAL && !scope_ready))
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
        dirty |= FW_BIT(D_ROW0) | FW_BIT(D_ROW0 + 1) | FW_BIT(D_ROW0 + 2);
}

static void handle_page_knobs(const fw_input_t *in)
{
    const uint32_t vis = visible_count(page);
    const uint32_t objects = page_objects(page);
    uint32_t col;
    if (page == PAGE_HOME && in->knob[FW_KNOB_ALGO]) {
        patch.main.algorithm = (int16_t)clampi(patch.main.algorithm + in->knob[FW_KNOB_ALGO], 0, FM_ALGOS - 1);
        dirty = D_ALL & ~FW_BIT(D_FRAME);
    }
    if (objects && in->knob[FW_KNOB_ALGO]) {
        object[page] = (uint8_t)clampi((int32_t)object[page] + in->knob[FW_KNOB_ALGO], 0, (int32_t)objects - 1);
        focus[page] = (uint8_t)clampi(focus[page], 0, (int32_t)visible_count(page) - 1);
        dirty = D_ALL & ~FW_BIT(D_FRAME);
    }
    if (in->knob[FW_KNOB_SELECT] && vis) {
        focus[page] = (uint8_t)clampi((int32_t)focus[page] + in->knob[FW_KNOB_SELECT], 0, (int32_t)vis - 1);
        dirty |= FW_BIT(D_HEADER) | FW_BIT(D_ROW0) | FW_BIT(D_ROW0 + 1) | FW_BIT(D_ROW0 + 2);
    }
    if (!vis)
        return;
    for (col = 0; col < 4u; col++) {
        const pdef_t *d = cell(page, visible_group(page, focus[page]), col);
        if (knob_param(d, col, in, FW_KNOB_1 + col))
            dirty |= FW_BIT(D_ROW0) | FW_BIT(D_ROW0 + 1) | FW_BIT(D_ROW0 + 2) | FW_BIT(D_GRAPH) | FW_BIT(D_HEADER);
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
    store_load();
    fm_init();
    preset_select(store[0].current < PRESETS_N ? store[0].current : 0u);
}

static void app_frame(const fw_input_t *in)
{
    frame_count++;
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
    if (page == PAGE_GLOBAL)
        dirty |= FW_BIT(D_GRAPH);
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
