/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 npi3pak */
#ifndef FM_H
#define FM_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The preset holds every parameter of Live's Operator in its own units (tools/oppreset.py converts .adv files):
 *   time  code: 0.1 ms * 10^(v / 1000), 0 .. 5778 (60 s)
 *   level cB:   dB * 100, -7000 (-inf) .. 0 (or +600 for the global volume)
 *   pct:        percent * 10, -1000 .. 1000
 *   unit:       0 .. 1 * 1000
 *   pitch:      cents (MIDI note * 100) */

#define FM_OPS 4
#define FM_ALGOS 11
#define FM_WAVES 23
#define FM_HARMONICS 64
#define FM_NAME_LEN 32
#define FM_VOICES_MAX 32
#define FM_SOURCES 5
#define FM_DESTS 24
#define FM_ENVS 7

enum { OP_A, OP_B, OP_C, OP_D };
enum { ENV_A, ENV_B, ENV_C, ENV_D, ENV_FILTER, ENV_PITCH, ENV_LFO };
enum { LOOP_NONE, LOOP_LOOP, LOOP_BEAT, LOOP_SYNC, LOOP_TRIGGER, LOOP_MODES };
enum { WAVE_SINE, WAVE_NOISE_LOOP = 20, WAVE_NOISE_WHITE = 21, WAVE_USER = 22 };
enum { LFO_SINE, LFO_SQUARE, LFO_TRIANGLE, LFO_SAW_UP, LFO_SAW_DOWN, LFO_SH, LFO_NOISE, LFO_TYPES };
enum { LFO_LOW, LFO_HIGH, LFO_SYNC };
enum { FILTER_LP, FILTER_HP, FILTER_BP, FILTER_NOTCH, FILTER_MORPH, FILTER_TYPES };
enum { SHAPER_OFF, SHAPER_SOFT, SHAPER_HARD, SHAPER_SINE, SHAPER_4BIT, SHAPER_TYPES };
enum { SRC_VEL, SRC_KEY, SRC_AFTERTOUCH, SRC_BEND, SRC_WHEEL };
enum {
    DST_OFF, DST_VOL_A, DST_VOL_B, DST_VOL_C, DST_VOL_D, DST_XFADE_AC, DST_XFADE_BD, DST_FEEDBACK, DST_FIXED,
    DST_FM_DRIVE, DST_FILTER_FREQ, DST_FILTER_Q, DST_FILTER_RES, DST_FILTER_MORPH, DST_FILTER_DRIVE,
    DST_FILTER_ENV, DST_SHAPER_DRIVE, DST_LFO_RATE, DST_LFO_AMOUNT, DST_PITCH_ENV, DST_VOLUME, DST_PAN, DST_TONE,
    DST_TIME
};

typedef struct {
    int16_t attack, decay, release, loop_time;  /* time */
    int16_t initial, peak, sustain, end;        /* cB (oscillators, LFO), unit (filter), cents (pitch) */
    int16_t a_slope, d_slope, r_slope;          /* -1000 .. 1000 */
    int16_t loop_mode, repeat, time_vel;        /* LOOP_*, 0 .. 14, pct */
} fm_env_t;

typedef struct {
    int16_t on, wave, coarse, fine;             /* fine 0 .. 1000 */
    int16_t fixed, fixed_freq, fixed_multi;     /* Hz * 10 (100 .. 20000), 0 .. 5 */
    int16_t vel_coarse, vel_coarse_q;           /* pct, bool */
    int16_t level, feedback, phase, retrigger;  /* cB, unit, unit, bool */
    int16_t vel_scale, key_scale;               /* pct */
    int16_t lfo_on, pitch_on;
    int16_t harm_count, harm_repeat, harm_normalize;
    uint8_t harm[FM_HARMONICS];                 /* 0 .. 255 */
    fm_env_t env;
} fm_osc_t;

typedef struct {
    int16_t on, type, circuit, circuit_bp, slope24, legacy_type, legacy_q;
    int16_t freq, res, morph, drive;            /* cents, unit (0 .. 1250), unit, dB * 100 */
    int16_t freq_vel, freq_key, env_amount;     /* pct (freq_key -2000 .. 2000) */
    int16_t lfo_on;
    int16_t shaper, shaper_drive, shaper_wet;   /* SHAPER_*, dB * 100, unit */
    fm_env_t env;
} fm_filter_t;

typedef struct {
    int16_t on, type, range, rate, rate_synced; /* rate 0 .. 1270 */
    int16_t rate_key, retrigger;                /* unit, bool */
    int16_t amount, amount_a, amount_vel;       /* unit, pct, unit -1000 .. 1000 */
    int16_t pitch_on, dst_b, amount_b;
    fm_env_t env;
} fm_lfo_t;

typedef struct {
    int16_t on, amount_a, dst_b, amount_b, env_amount;
    fm_env_t env;
} fm_pitch_t;

typedef struct {
    int16_t algorithm, voices, retrigger, interpolation, antialias;  /* voices: index, fm_voice_count() */
    int16_t transpose, bend_range, volume, pan, pan_key, pan_rnd;   /* semitones, semitones, cB, unit -1000.., pct, pct */
    int16_t tone, spread, glide, glide_time;                        /* unit, unit, bool, time */
    int16_t time, time_key, time_attack;                            /* pct, pct, bool */
} fm_main_t;

typedef struct {
    int16_t dst, amount;
} fm_route_t;

typedef struct {
    char name[FM_NAME_LEN];
    fm_main_t main;
    fm_osc_t osc[FM_OPS];
    fm_filter_t filter;
    fm_lfo_t lfo;
    fm_pitch_t pitch;
    fm_route_t route[FM_SOURCES][2];
} fm_patch_t;

/* the device's settings, outside the presets */
typedef struct {
    int16_t channel, fine, volume, tempo;      /* 0 = omni, cents, 0 .. 256 (MASTER), BPM for Beat/Sync */
} fm_global_t;

typedef struct {
    uint8_t active, cap;
} fm_monitor_t;

void fm_init(void);
void fm_render(const fm_patch_t *p, const fm_global_t *g, int32_t *lr, uint32_t frames);
void fm_note_on(uint8_t note, uint8_t velocity);
void fm_note_off(uint8_t note);
void fm_all_notes_off(void);
void fm_controller(uint32_t source, int32_t value);  /* SRC_AFTERTOUCH / SRC_WHEEL 0 .. 127, SRC_BEND -8192 .. 8191 */
void fm_set_voice_cap(uint32_t n);
void fm_patch_loaded(const fm_patch_t *p);           /* rebuilds the user waveforms; main loop only */
void fm_monitor(fm_monitor_t *m);
uint8_t fm_algo_mods(int32_t algo, int32_t op);
uint8_t fm_algo_carriers(int32_t algo);
int16_t fm_wave_sample(const fm_patch_t *p, uint32_t op, uint32_t i);  /* the waveform of op, 256 points */
uint32_t fm_voice_count(int32_t index);

#ifdef __cplusplus
}
#endif

#endif
