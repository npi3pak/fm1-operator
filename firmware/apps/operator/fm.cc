// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 npi3pak
#include <algorithm>
#include <math.h>

#include "fw_rate.h"
#include "fm.h"

#ifdef FW_HOST
#define FM_POOL
#else
#define FM_POOL __attribute__((section(".pool")))
#endif

namespace {

const uint32_t kBlock = 16;
const uint32_t kEvents = 64;
const uint32_t kTableBits = 10;
const uint32_t kTable = 1u << kTableBits;
const uint32_t kFixedWaves = 22;
const uint32_t kHeld = 16;
const float kFs = FW_FS_HZ;
const float kBlockSec = static_cast<float>(kBlock) / FW_FS_HZ;

// Calibrated by ear against Live later: the manual gives no number for these.
const float kModCycles = 1.0f;          // a modulator at 0 dB shifts the carrier's phase by this many cycles
const float kFeedbackCycles = 0.5f;     // feedback 100 %
const float kLfoPitchSemis = 12.0f;     // LFO Dest. A 100 % at amount 100 %, oscillator pitch
const float kLfoFilterSemis = 36.0f;    // the same, filter cutoff
const float kFilterEnvSemis = 108.0f;   // envelope 100 %: about 9 octaves (manual)
const float kFilterVelSemis = 48.0f;
const float kSpreadSemis = 0.25f;       // spread 100 %: each side detuned by this
const float kTimeOctaves = 3.3219f;     // Time 100 %: envelopes 10x longer
const float kKeyScaleDb = 6.0f;         // level < key 100 %: dB per octave from C3
const float kRouteOctaves = 4.0f;       // a routing 100 % to a frequency or a rate
const float kLfoSpan = 10.550747f;      // log2(1500): Low 0.02 .. 30 Hz, High 8 Hz .. 12 kHz
const float kSilent = 0.001f;           // a voice below -60 dBFS that cannot rise again is freed

const float FIXED_MULTI[6] = {0.0001f, 0.001f, 0.01f, 0.1f, 1.0f, 10.0f};
// beat lengths in quarter notes for Repeat (envelopes) and the synced LFO rate
const float BEATS[15] = {0.125f, 0.1667f, 0.25f, 0.3333f, 0.5f, 0.6667f, 0.75f, 1.0f,
                         1.3333f, 1.5f, 2.0f, 3.0f, 4.0f, 8.0f, 16.0f};
const uint8_t VOICE_COUNT[14] = {1, 2, 3, 4, 5, 6, 8, 10, 12, 14, 16, 20, 24, 32};
const uint32_t GAIN_DESTS = 1u << DST_VOL_A | 1u << DST_VOL_B | 1u << DST_VOL_C | 1u << DST_VOL_D |
                            1u << DST_FM_DRIVE | 1u << DST_FILTER_ENV | 1u << DST_LFO_AMOUNT |
                            1u << DST_PITCH_ENV | 1u << DST_VOLUME;

// Provisional: Operator's eleven routings have to be copied from Live's algorithm icons.
// bit j of MODS[algo][i]: oscillator j modulates oscillator i; a modulator always has a higher index (D renders first)
const uint8_t MODS[FM_ALGOS][FM_OPS] = {
    {0x2, 0x4, 0x8, 0x0},  // D > C > B > A
    {0x2, 0xC, 0x0, 0x0},  // C + D > B > A
    {0xA, 0x4, 0x0, 0x0},  // C > B > A, D > A
    {0x6, 0x0, 0x8, 0x0},  // B > A, D > C > A
    {0x6, 0x8, 0x8, 0x0},  // D > B + C > A
    {0xE, 0x0, 0x0, 0x0},  // B + C + D > A
    {0x2, 0x0, 0x8, 0x0},  // B > A, D > C
    {0x0, 0x4, 0x8, 0x0},  // D > C > B, A
    {0x8, 0x8, 0x8, 0x0},  // D > A, B, C
    {0x0, 0x0, 0x8, 0x0},  // D > C, A, B
    {0x0, 0x0, 0x0, 0x0},  // four carriers
};
const uint8_t CARRIERS[FM_ALGOS] = {0x1, 0x1, 0x1, 0x1, 0x1, 0x1, 0x5, 0x3, 0x7, 0x7, 0xF};

enum { EV_OFF, EV_ON, EV_ALL_OFF, EV_KILL };
enum { ST_IDLE, ST_ATTACK, ST_DECAY, ST_SUSTAIN, ST_LOOP, ST_RELEASE, ST_DONE };

struct Env {
    float value, from, to, pos, beat;
    uint8_t stage;
};

struct Core {
    uint32_t phase[FM_OPS];
    float out[FM_OPS], prev[FM_OPS], tone[FM_OPS], gain[FM_OPS];
    float s1[2], s2[2];
    uint32_t rng;
};

struct Voice {
    Env env[FM_ENVS];
    Core core[2];
    float pitch, target, glide_rate;
    float velocity, pan, lfo_phase, lfo_hold, lfo_noise, lfo_last;
    uint32_t age, rng;
    uint8_t note, gate, active, spread;
};

struct Shared {
    float secs[FM_ENVS][4];   // attack, decay, release, loop
    float slope[FM_ENVS][3];
    float level[FM_OPS], user_gain[FM_OPS], feedback[FM_OPS];
    const int16_t* table[FM_OPS];
    float tone_coef, bend, wheel, aftertouch, beat_secs, tempo;
    uint8_t mods[FM_OPS], carriers, unmodulated, interp;
};

struct Filter {
    float g, k, a1, a2, a3, drive, morph;
    int type, stages, saturate;
};

int16_t wave_table[kFixedWaves][kTable + 1] FM_POOL;
int16_t user_table[FM_OPS][2][kTable + 1] FM_POOL;
float user_gain[FM_OPS][2] FM_POOL;
float scratch[kTable] FM_POOL;
Voice voices[FM_VOICES_MAX] FM_POOL;
volatile uint8_t user_live[FM_OPS];
float out_l[kBlock], out_r[kBlock];
uint32_t block_pos = kBlock;
uint32_t voice_clock;
float last_pitch = 60.0f;
uint32_t global_rng = 0x1234567u;
float free_lfo_phase;
uint8_t held[kHeld];
uint32_t held_n;

volatile uint32_t events[kEvents];
volatile uint32_t events_w, events_r;
volatile int32_t ctl_bend, ctl_wheel, ctl_aftertouch;
volatile uint32_t voice_cap = FM_VOICES_MAX;
volatile uint8_t mon_active;

inline float clampf(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }
inline int clampi(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }

inline uint32_t xorshift(uint32_t* s) {
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

inline float rand_bipolar(uint32_t* s) { return static_cast<float>(static_cast<int32_t>(xorshift(s))) * 4.656613e-10f; }

inline float bits_float(int32_t i) {
    float f;
    __builtin_memcpy(&f, &i, sizeof f);
    return f;
}

inline int32_t float_bits(float f) {
    int32_t i;
    __builtin_memcpy(&i, &f, sizeof i);
    return i;
}

inline int32_t floor_int(float x) {
    const int32_t i = static_cast<int32_t>(x);
    return static_cast<float>(i) > x ? i - 1 : i;
}

float fast_exp2(float x) {
    x = clampf(x, -60.0f, 60.0f);
    const int32_t i = floor_int(x);
    const float f = x - static_cast<float>(i);
    const float p = 1.0f + f * (0.6951786f + f * (0.2261487f + f * 0.0790184f));
    return bits_float(float_bits(p) + i * (1 << 23));
}

float fast_log2(float x) {
    const int32_t b = float_bits(std::max(x, 1e-12f));
    const float e = static_cast<float>(((b >> 23) & 255) - 127);
    const float m = bits_float((b & 0x007FFFFF) | 0x3F800000) - 1.0f;
    return e + m * (1.4425449f + m * (-0.7181452f + m * 0.2729931f));
}

inline float db_gain(float db) { return db <= -69.99f ? 0.0f : fast_exp2(db * 0.16609640f); }
inline float gain_db(float g) { return g <= 0.000316f ? -70.0f : 6.0205999f * fast_log2(g); }
inline float semis_ratio(float s) { return fast_exp2(s * (1.0f / 12.0f)); }
inline float time_secs(int16_t code) { return 0.0001f * fast_exp2(static_cast<float>(code) * 0.0033219281f); }
inline float unit(int16_t v) { return static_cast<float>(v) * 0.001f; }

inline float table_at(const int16_t* t, uint32_t phase, bool interp) {
    const uint32_t i = phase >> (32 - kTableBits);
    if (!interp)
        return static_cast<float>(t[i]) * (1.0f / 32768.0f);
    const float f = static_cast<float>((phase >> (32 - kTableBits - 16)) & 0xFFFFu) * (1.0f / 65536.0f);
    const float a = static_cast<float>(t[i]);
    return (a + (static_cast<float>(t[i + 1]) - a) * f) * (1.0f / 32768.0f);
}

inline uint32_t cycles_phase(float c) {
    const float frac = c - static_cast<float>(floor_int(c));
    return static_cast<uint32_t>(frac * 16777215.0f) << 8;
}

// ---- waveforms (built in the main loop: fm_init runs before the audio starts)

void fill_table(int16_t* t, const float* v) {
    float peak = 1e-9f;
    for (uint32_t i = 0; i < kTable; i++)
        peak = std::max(peak, fabsf(v[i]));
    for (uint32_t i = 0; i < kTable; i++)
        t[i] = static_cast<int16_t>(lrintf(clampf(v[i] / peak, -1.0f, 1.0f) * 32767.0f));
    t[kTable] = t[0];
}

void additive(float* v, uint32_t partials, bool odd_only, bool triangle) {
    for (uint32_t i = 0; i < kTable; i++)
        v[i] = 0.0f;
    uint32_t h = 1;
    for (uint32_t n = 0; n < partials; n++, h += odd_only ? 2u : 1u) {
        const float amp = triangle ? ((n & 1u) ? -1.0f : 1.0f) / static_cast<float>(h * h) : 1.0f / static_cast<float>(h);
        for (uint32_t i = 0; i < kTable; i++)
            v[i] += amp * static_cast<float>(wave_table[0][(i * h) & (kTable - 1)]) * (1.0f / 32767.0f);
    }
}

void build_waves() {
    static const uint8_t COUNTS[7] = {3, 4, 6, 8, 16, 32, 64};
    float* v = scratch;
    uint32_t rng = 0x9E3779B9u;
    for (uint32_t i = 0; i < kTable; i++)
        v[i] = sinf(6.2831853f * static_cast<float>(i) / static_cast<float>(kTable));
    fill_table(wave_table[0], v);
    for (uint32_t bits = 0; bits < 2; bits++) {
        const float steps = bits ? 127.0f : 7.0f;
        for (uint32_t i = 0; i < kTable; i++)
            v[i] = roundf(static_cast<float>(wave_table[0][i]) * (1.0f / 32767.0f) * steps) / steps;
        fill_table(wave_table[1 + bits], v);
    }
    for (uint32_t n = 0; n < 7; n++) {
        additive(v, COUNTS[n], false, false);
        fill_table(wave_table[3 + n], v);
        additive(v, COUNTS[n], true, false);
        fill_table(wave_table[11 + n], v);
    }
    for (uint32_t i = 0; i < kTable; i++)
        v[i] = 1.0f - 2.0f * static_cast<float>(i) / static_cast<float>(kTable);
    fill_table(wave_table[10], v);
    for (uint32_t i = 0; i < kTable; i++)
        v[i] = i < kTable / 2 ? 1.0f : -1.0f;
    fill_table(wave_table[18], v);
    additive(v, 16, true, true);
    fill_table(wave_table[19], v);
    for (uint32_t i = 0; i < kTable; i++)
        v[i] = rand_bipolar(&rng);
    fill_table(wave_table[20], v);
    fill_table(wave_table[21], v);
}

void build_user(const fm_osc_t& o, int16_t* t, float* gain) {
    static const uint8_t COUNT[3] = {16, 32, 64};
    const uint32_t n = COUNT[clampi(o.harm_count, 0, 2)];
    float* v = scratch;
    float sum = 0.0f;
    for (uint32_t i = 0; i < kTable; i++)
        v[i] = 0.0f;
    for (uint32_t h = 0; h < n; h++) {
        const float amp = static_cast<float>(o.harm[h]) * (1.0f / 255.0f);
        if (amp <= 0.0f)
            continue;
        sum += amp;
        for (uint32_t i = 0; i < kTable; i++)
            v[i] += amp * static_cast<float>(wave_table[0][(i * (h + 1)) & (kTable - 1)]) * (1.0f / 32767.0f);
    }
    float peak = 0.0f;
    for (uint32_t i = 0; i < kTable; i++)
        peak = std::max(peak, fabsf(v[i]));
    fill_table(t, v);
    *gain = o.harm_normalize || peak <= 0.0f ? (sum > 0.0f ? 1.0f : 0.0f) : std::min(8.0f, peak);
}

// ---- envelopes: levels in dB (oscillators, LFO: the attack is linear in gain), unit (filter), semitones (pitch)

inline bool db_env(uint32_t e) { return e < ENV_FILTER || e == ENV_LFO; }

float env_level(uint32_t e, int16_t raw) { return static_cast<float>(raw) * (e == ENV_FILTER ? 0.001f : 0.01f); }

float env_out(uint32_t e, float level) { return db_env(e) ? db_gain(level) : level; }

void env_segment(Env* s, uint32_t e, uint8_t stage, int16_t to) {
    s->stage = stage;
    s->pos = 0.0f;
    s->from = db_env(e) ? gain_db(s->value) : s->value;
    s->to = env_level(e, to);
}

void env_attack(Env* s, uint32_t e, const fm_env_t& p) {
    s->stage = ST_ATTACK;
    s->pos = 0.0f;
    s->from = s->value;
    s->to = env_out(e, env_level(e, p.peak));
}

void env_note_on(Env* s, uint32_t e, const fm_env_t& p, bool fresh) {
    if (fresh || s->stage == ST_IDLE || s->stage == ST_DONE)
        s->value = env_out(e, env_level(e, p.initial));
    s->beat = 0.0f;
    env_attack(s, e, p);
}

inline float curve(float t, float k) { return t * (1.0f + k) / (1.0f + k * t); }

void env_step(Env* s, uint32_t e, const fm_env_t& p, const float* secs, const float* slope, bool gate, float beat) {
    if (s->stage == ST_IDLE || s->stage == ST_DONE)
        return;
    const int mode = p.loop_mode;
    if (!gate && mode != LOOP_TRIGGER && s->stage < ST_RELEASE)
        env_segment(s, e, ST_RELEASE, p.end);
    if (gate && (mode == LOOP_BEAT || mode == LOOP_SYNC) && s->stage < ST_RELEASE) {
        s->beat += kBlockSec;
        if (s->beat >= beat) {
            s->beat -= beat;
            env_attack(s, e, p);
        }
    }
    if (s->stage == ST_SUSTAIN)
        return;
    const int seg = s->stage == ST_ATTACK ? 0 : s->stage == ST_RELEASE ? 2 : 1;
    const float time = s->stage == ST_LOOP ? secs[3] : secs[seg];
    s->pos += kBlockSec / std::max(time, 1e-5f);
    const float t = std::min(s->pos, 1.0f);
    if (db_env(e))
        s->value = s->stage == ST_ATTACK ? s->from + (s->to - s->from) * t : db_gain(s->from + (s->to - s->from) * t);
    if (!db_env(e))
        s->value = s->from + (s->to - s->from) * curve(t, slope[seg]);
    if (s->pos < 1.0f)
        return;
    if (s->stage == ST_ATTACK) {
        env_segment(s, e, ST_DECAY, p.sustain);
        return;
    }
    if (s->stage == ST_LOOP) {
        env_attack(s, e, p);
        return;
    }
    if (s->stage == ST_RELEASE) {
        s->stage = ST_DONE;
        return;
    }
    if (mode == LOOP_LOOP && gate) {
        env_segment(s, e, ST_LOOP, p.initial);
        return;
    }
    if (mode == LOOP_TRIGGER) {
        env_segment(s, e, ST_RELEASE, p.end);
        return;
    }
    s->stage = ST_SUSTAIN;
}

const fm_env_t& env_params(const fm_patch_t* p, uint32_t e) {
    if (e < ENV_FILTER)
        return p->osc[e].env;
    if (e == ENV_FILTER)
        return p->filter.env;
    if (e == ENV_PITCH)
        return p->pitch.env;
    return p->lfo.env;
}

// ---- voices

uint32_t patch_voices(const fm_patch_t* p) { return VOICE_COUNT[clampi(p->main.voices, 0, 13)]; }

bool voice_sounding(const Voice& v, uint8_t carriers, const fm_patch_t* p) {
    for (uint32_t k = 0; k < FM_OPS; k++) {
        if (((carriers >> k) & 1u) && p->osc[k].on && v.env[k].stage != ST_DONE && v.env[k].stage != ST_IDLE)
            return true;
    }
    return false;
}

// Every carrier is below kSilent at the output and none can come back: no attack or loop under way, no loop, beat
// or sync mode while the key is held, and a sustain that is silent too. Long releases end there, not at -70 dB.
bool voice_silent(const Voice& v, const fm_patch_t* p, const Shared& s, const float* target, float volume) {
    for (uint32_t k = 0; k < FM_OPS; k++) {
        const fm_osc_t& o = p->osc[k];
        const Env& e = v.env[k];
        const int mode = o.env.loop_mode;
        if (!((s.carriers >> k) & 1u) || !o.on)
            continue;
        if (target[k] * volume > kSilent || e.stage == ST_ATTACK || e.stage == ST_LOOP)
            return false;
        if (v.gate && (mode == LOOP_LOOP || mode == LOOP_BEAT || mode == LOOP_SYNC))
            return false;
        if (e.stage == ST_DECAY && s.level[k] * db_gain(static_cast<float>(o.env.sustain) * 0.01f) * volume > kSilent)
            return false;
    }
    return true;
}

void voice_start(Voice* v, uint8_t note, uint8_t velocity, const fm_patch_t* p) {
    const bool fresh = !v->active;
    const float glide = time_secs(p->main.glide_time);
    v->note = note;
    v->gate = 1;
    v->active = 1;
    v->velocity = static_cast<float>(velocity) * (1.0f / 127.0f);
    v->age = ++voice_clock;
    v->spread = p->main.spread > 0;
    v->target = static_cast<float>(note);
    v->pitch = p->main.glide ? last_pitch : v->target;
    v->glide_rate = fabsf(v->target - v->pitch) * kBlockSec / std::max(glide, 1e-4f);
    last_pitch = v->target;
    v->pan = unit(p->main.pan_rnd) * rand_bipolar(&global_rng);
    for (uint32_t e = 0; e < FM_ENVS; e++)
        env_note_on(&v->env[e], e, env_params(p, e), fresh);
    for (uint32_t c = 0; c < 2; c++) {
        Core* core = &v->core[c];
        for (uint32_t k = 0; k < FM_OPS; k++) {
            const uint32_t start = static_cast<uint32_t>(clampi(p->osc[k].phase, 0, 999)) * 4294967u;
            if (p->osc[k].retrigger)
                core->phase[k] = start;
            if (fresh) {
                core->out[k] = core->prev[k] = core->tone[k] = core->gain[k] = 0.0f;
                core->phase[k] = p->osc[k].retrigger ? core->phase[k] : xorshift(&global_rng);
            }
        }
        if (fresh)
            core->s1[0] = core->s1[1] = core->s2[0] = core->s2[1] = 0.0f;
    }
    v->lfo_phase = p->lfo.retrigger ? 0.0f : free_lfo_phase;
    v->lfo_hold = rand_bipolar(&v->rng);
}

Voice* steal(uint32_t cap) {
    Voice* pick = 0;
    for (uint32_t i = 0; i < cap; i++) {
        Voice* v = &voices[i];
        if (!v->active)
            return v;
        if (!pick || (!v->gate && pick->gate) || (v->gate == pick->gate && v->age < pick->age))
            pick = v;
    }
    return pick;
}

void held_remove(uint8_t note) {
    uint32_t w = 0;
    for (uint32_t i = 0; i < held_n; i++) {
        if (held[i] != note)
            held[w++] = held[i];
    }
    held_n = w;
}

void note_on(uint8_t note, uint8_t velocity, const fm_patch_t* p) {
    const uint32_t count = patch_voices(p);
    const uint32_t cap = std::max(1u, std::min(count, static_cast<uint32_t>(voice_cap)));
    held_remove(note);
    if (held_n == kHeld)
        held_remove(held[0]);
    held[held_n++] = note;
    if (count == 1 && voices[0].active && voices[0].gate) {
        Voice* v = &voices[0];
        v->note = note;
        v->target = static_cast<float>(note);
        v->glide_rate = fabsf(v->target - v->pitch) * kBlockSec / std::max(time_secs(p->main.glide_time), 1e-4f);
        v->pitch = p->main.glide ? v->pitch : v->target;
        last_pitch = v->target;
        return;
    }
    if (p->main.retrigger) {
        for (uint32_t i = 0; i < cap; i++) {
            if (voices[i].active && voices[i].note == note) {
                voice_start(&voices[i], note, velocity, p);
                return;
            }
        }
    }
    voice_start(count == 1 ? &voices[0] : steal(cap), note, velocity, p);
}

void note_off(uint8_t note, const fm_patch_t* p) {
    held_remove(note);
    if (patch_voices(p) == 1 && voices[0].gate && voices[0].note == note && held_n) {
        Voice* v = &voices[0];
        v->note = held[held_n - 1];
        v->target = static_cast<float>(v->note);
        v->glide_rate = fabsf(v->target - v->pitch) * kBlockSec / std::max(time_secs(p->main.glide_time), 1e-4f);
        v->pitch = p->main.glide ? v->pitch : v->target;
        return;
    }
    for (uint32_t i = 0; i < FM_VOICES_MAX; i++) {
        if (voices[i].gate && voices[i].note == note)
            voices[i].gate = 0;
    }
}

void drain_events(const fm_patch_t* p) {
    while (events_r != events_w) {
        const uint32_t e = events[events_r % kEvents];
        events_r++;
        const uint32_t kind = e >> 16;
        const uint8_t note = static_cast<uint8_t>(e >> 8);
        if (kind == EV_ON)
            note_on(note, static_cast<uint8_t>(e), p);
        if (kind == EV_OFF)
            note_off(note, p);
        if (kind == EV_ALL_OFF) {
            held_n = 0;
            for (uint32_t i = 0; i < FM_VOICES_MAX; i++)
                voices[i].gate = 0;
        }
        if (kind == EV_KILL) {
            held_n = 0;
            for (uint32_t i = 0; i < FM_VOICES_MAX; i++) {
                voices[i].gate = 0;
                voices[i].active = 0;
                for (uint32_t e = 0; e < FM_ENVS; e++)
                    voices[i].env[e].stage = ST_IDLE;
            }
        }
    }
}

void push_event(uint32_t kind, uint32_t note, uint32_t velocity) {
    if (events_w - events_r >= kEvents)
        return;
    events[events_w % kEvents] = kind << 16 | (note & 127u) << 8 | (velocity & 127u);
    events_w++;
}

// ---- the block

void shared_params(const fm_patch_t* p, const fm_global_t* g, Shared* s) {
    const int algo = clampi(p->main.algorithm, 0, FM_ALGOS - 1);
    s->carriers = CARRIERS[algo];
    s->unmodulated = 0;
    for (uint32_t k = 0; k < FM_OPS; k++) {
        const fm_osc_t& o = p->osc[k];
        s->mods[k] = MODS[algo][k];
        s->unmodulated |= MODS[algo][k] ? 0 : 1u << k;
        s->level[k] = o.on ? db_gain(static_cast<float>(o.level) * 0.01f) : 0.0f;
        s->feedback[k] = unit(o.feedback) * kFeedbackCycles;
        s->user_gain[k] = 1.0f;
        s->table[k] = wave_table[clampi(o.wave, 0, WAVE_NOISE_WHITE)];
        if (o.wave == WAVE_USER) {
            const uint32_t live = user_live[k] & 1u;
            s->table[k] = user_table[k][live];
            s->user_gain[k] = user_gain[k][live];
        }
    }
    for (uint32_t e = 0; e < FM_ENVS; e++) {
        const fm_env_t& n = env_params(p, e);
        s->secs[e][0] = time_secs(n.attack);
        s->secs[e][1] = time_secs(n.decay);
        s->secs[e][2] = time_secs(n.release);
        s->secs[e][3] = time_secs(n.loop_time);
        s->slope[e][0] = fast_exp2(unit(n.a_slope) * 4.0f) - 1.0f;
        s->slope[e][1] = fast_exp2(unit(n.d_slope) * 4.0f) - 1.0f;
        s->slope[e][2] = fast_exp2(unit(n.r_slope) * 4.0f) - 1.0f;
    }
    const float tone_hz = 200.0f * fast_exp2(unit(p->main.tone) * 7.2f);
    s->tone_coef = p->main.tone >= 1000 ? 1.0f : 1.0f - fast_exp2(-9.0647f * tone_hz / kFs);
    s->tempo = static_cast<float>(clampi(g->tempo, 20, 300));
    s->bend = static_cast<float>(ctl_bend) * (1.0f / 8192.0f);
    s->wheel = static_cast<float>(ctl_wheel) * (1.0f / 127.0f);
    s->aftertouch = static_cast<float>(ctl_aftertouch) * (1.0f / 127.0f);
    s->interp = p->main.interpolation != 0;
}

void mod_bus(const Voice& v, const fm_patch_t* p, const Shared& s, float* bus) {
    const float src[FM_SOURCES] = {v.velocity, (static_cast<float>(v.note) - 60.0f) * (1.0f / 64.0f), s.aftertouch,
                                   s.bend, s.wheel};
    const bool unipolar[FM_SOURCES] = {true, false, true, false, true};
    for (uint32_t d = 0; d < FM_DESTS; d++)
        bus[d] = 0.0f;
    for (uint32_t i = 0; i < FM_SOURCES; i++) {
        for (uint32_t j = 0; j < 2; j++) {
            const int d = clampi(p->route[i][j].dst, 0, FM_DESTS - 1);
            const bool gain = (GAIN_DESTS >> d) & 1u;
            bus[d] += d ? unit(p->route[i][j].amount) * (gain && unipolar[i] ? src[i] - 1.0f : src[i]) : 0.0f;
        }
    }
    const int lfo_dst = clampi(p->lfo.dst_b, 0, FM_DESTS - 1);
    const int pitch_dst = clampi(p->pitch.dst_b, 0, FM_DESTS - 1);
    bus[lfo_dst] += lfo_dst && p->lfo.on ? unit(p->lfo.amount_b) * v.lfo_last : 0.0f;
    bus[pitch_dst] += pitch_dst && p->pitch.on ? unit(p->pitch.amount_b) * v.env[ENV_PITCH].value * (1.0f / 48.0f) : 0.0f;
}

inline float gain_mod(const float* bus, int d) { return std::max(0.0f, 1.0f + bus[d]); }

float lfo_shape(Voice* v, int type, float rate_hz) {
    const float ph = v->lfo_phase;
    if (type == LFO_SINE)
        return table_at(wave_table[0], static_cast<uint32_t>(ph * 16777215.0f) << 8, true);
    if (type == LFO_SQUARE)
        return ph < 0.5f ? 1.0f : -1.0f;
    if (type == LFO_TRIANGLE)
        return 4.0f * fabsf(ph - 0.5f) - 1.0f;
    if (type == LFO_SAW_UP)
        return 2.0f * ph - 1.0f;
    if (type == LFO_SAW_DOWN)
        return 1.0f - 2.0f * ph;
    if (type == LFO_SH)
        return v->lfo_hold;
    v->lfo_noise += (rand_bipolar(&v->rng) - v->lfo_noise) * clampf(rate_hz * 6.2831853f / kFs, 0.0f, 1.0f);
    return clampf(v->lfo_noise * 3.0f, -1.0f, 1.0f);
}

float lfo_rate(const fm_patch_t* p, const Shared& s) {
    const fm_lfo_t& l = p->lfo;
    if (l.range == LFO_SYNC)
        return s.tempo * (1.0f / 60.0f) / BEATS[clampi(l.rate_synced, 0, 14)];
    const float base = l.range == LFO_HIGH ? 8.0f : 0.02f;
    return base * fast_exp2(static_cast<float>(l.rate) * (1.0f / 1270.0f) * kLfoSpan);
}

void filter_setup(Filter* f, const fm_patch_t* p, const Voice& v, const float* bus, float lfo) {
    const fm_filter_t& fl = p->filter;
    const float note = static_cast<float>(fl.freq) * 0.01f + unit(fl.freq_key) * (static_cast<float>(v.note) - 60.0f) +
                       unit(fl.freq_vel) * v.velocity * kFilterVelSemis +
                       unit(fl.env_amount) * gain_mod(bus, DST_FILTER_ENV) * v.env[ENV_FILTER].value * kFilterEnvSemis +
                       (fl.lfo_on ? lfo * unit(p->lfo.amount_a) * kLfoFilterSemis : 0.0f) +
                       bus[DST_FILTER_FREQ] * kRouteOctaves * 12.0f;
    const float hz = clampf(440.0f * semis_ratio(note - 69.0f), 20.0f, kFs * 0.45f);
    const float w = 3.14159265f * hz / kFs;
    const float w2 = w * w;
    const float res = clampf(unit(fl.res) + (bus[DST_FILTER_RES] + bus[DST_FILTER_Q]) * 1.25f, 0.0f, 1.25f);
    f->g = w * (15.0f - w2) / (15.0f - 6.0f * w2);
    f->k = 2.0f - 1.97f * res * 0.8f;
    f->a1 = 1.0f / (1.0f + f->g * (f->g + f->k));
    f->a2 = f->g * f->a1;
    f->a3 = f->g * f->a2;
    f->type = clampi(fl.type, 0, FILTER_TYPES - 1);
    f->stages = fl.slope24 ? 2 : 1;
    f->morph = clampf(unit(fl.morph) + bus[DST_FILTER_MORPH], 0.0f, 1.0f) * 4.0f;
    const int circuit = f->type == FILTER_BP || f->type == FILTER_NOTCH ? fl.circuit_bp : fl.circuit;
    f->saturate = circuit != 0 && f->type != FILTER_MORPH;
    f->drive = db_gain(static_cast<float>(fl.drive) * 0.01f + bus[DST_FILTER_DRIVE] * 24.0f);
}

inline float filter_run(const Filter& f, float* s1, float* s2, float x) {
    if (f.saturate)
        x = x * f.drive / (1.0f + fabsf(x * f.drive));
    float y = x;
    for (int st = 0; st < f.stages; st++) {
        const float v3 = y - s2[st];
        const float v1 = f.a1 * s1[st] + f.a2 * v3;
        const float v2 = s2[st] + f.a2 * s1[st] + f.a3 * v3;
        s1[st] = 2.0f * v1 - s1[st];
        s2[st] = 2.0f * v2 - s2[st];
        const float lp = v2, bp = v1, hp = y - f.k * v1 - v2;
        if (f.type == FILTER_LP)
            y = lp;
        if (f.type == FILTER_HP)
            y = hp;
        if (f.type == FILTER_BP)
            y = bp;
        if (f.type == FILTER_NOTCH)
            y = lp + hp;
        if (f.type == FILTER_MORPH) {
            const float m = f.morph;
            const float a = m < 1.0f ? lp : m < 2.0f ? bp : m < 3.0f ? hp : lp + hp;
            const float b = m < 1.0f ? bp : m < 2.0f ? hp : m < 3.0f ? lp + hp : lp;
            const float t = m - static_cast<float>(floor_int(std::min(m, 3.999f)));
            y = a + (b - a) * t;
        }
    }
    return y;
}

inline float shape(int type, float x) {
    if (type == SHAPER_SOFT)
        return x / (1.0f + fabsf(x));
    if (type == SHAPER_HARD)
        return clampf(x, -1.0f, 1.0f);
    if (type == SHAPER_SINE)
        return table_at(wave_table[0], static_cast<uint32_t>(static_cast<int32_t>(clampf(x, -7.99f, 7.99f) * 268435456.0f)) * 4u, true);
    if (type == SHAPER_4BIT)
        return roundf(clampf(x, -1.0f, 1.0f) * 8.0f) * 0.125f;
    return x;
}

void render_voice(Voice* v, const fm_patch_t* p, const fm_global_t* g, const Shared& s) {
    float bus[FM_DESTS];
    mod_bus(*v, p, s, bus);
    const float note_oct = (static_cast<float>(v->note) - 60.0f) * (1.0f / 12.0f);
    const float time_all = fast_exp2(clampf(unit(p->main.time) + bus[DST_TIME], -1.0f, 1.0f) * kTimeOctaves -
                                     note_oct * unit(p->main.time_key));
    for (uint32_t e = 0; e < FM_ENVS; e++) {
        const fm_env_t& n = env_params(p, e);
        const float vel = fast_exp2(-unit(n.time_vel) * v->velocity * 2.0f);
        float secs[4];
        for (uint32_t i = 0; i < 4; i++)
            secs[i] = s.secs[e][i] * vel * (i == 0 && !p->main.time_attack ? 1.0f : time_all);
        const float beat = BEATS[clampi(n.repeat, 0, 14)] * 60.0f / s.tempo;
        env_step(&v->env[e], e, n, secs, s.slope[e], v->gate != 0, beat);
    }
    if (v->pitch != v->target) {
        const float d = v->target - v->pitch;
        v->pitch = fabsf(d) <= v->glide_rate ? v->target : v->pitch + (d > 0.0f ? v->glide_rate : -v->glide_rate);
    }

    // LFO, one value per sample: it reaches audio rates
    const fm_lfo_t& l = p->lfo;
    float lfo[kBlock];
    const float penv = p->pitch.on ? v->env[ENV_PITCH].value * unit(p->pitch.env_amount) * gain_mod(bus, DST_PITCH_ENV) : 0.0f;
    const float lfo_hz = lfo_rate(p, s) * fast_exp2(note_oct * unit(l.rate_key) + bus[DST_LFO_RATE] * kRouteOctaves +
                                                     (l.pitch_on ? penv * unit(p->pitch.amount_a) * (1.0f / 12.0f) : 0.0f));
    const float lfo_inc = std::min(lfo_hz / kFs, 0.5f);
    const float av = unit(l.amount_vel);
    const float lfo_amt = l.on ? unit(l.amount) * v->env[ENV_LFO].value * gain_mod(bus, DST_LFO_AMOUNT) *
                                     std::max(0.0f, av >= 0.0f ? 1.0f - av * (1.0f - v->velocity) : 1.0f + av * v->velocity)
                               : 0.0f;
    for (uint32_t i = 0; i < kBlock; i++) {
        v->lfo_phase += lfo_inc;
        if (v->lfo_phase >= 1.0f) {
            v->lfo_phase -= 1.0f;
            v->lfo_hold = rand_bipolar(&v->rng);
        }
        lfo[i] = l.on ? lfo_shape(v, l.type, lfo_hz) * lfo_amt : 0.0f;
    }
    v->lfo_last = lfo[kBlock - 1];

    float pm[kBlock];
    const float lfo_pitch = unit(l.amount_a) * kLfoPitchSemis * (1.0f / 12.0f);
    for (uint32_t i = 0; i < kBlock; i++)
        pm[i] = l.on ? fast_exp2(lfo[i] * lfo_pitch) : 1.0f;

    Filter f;
    if (p->filter.on)
        filter_setup(&f, p, *v, bus, v->lfo_last);
    const int shaper = clampi(p->filter.shaper, 0, SHAPER_TYPES - 1);
    const float shaper_drive = db_gain(static_cast<float>(p->filter.shaper_drive) * 0.01f + bus[DST_SHAPER_DRIVE] * 12.0f);
    const float wet = shaper ? unit(p->filter.shaper_wet) : 0.0f;

    float target[FM_OPS];
    float ratio[FM_OPS];
    const float fm_drive = gain_mod(bus, DST_FM_DRIVE);
    const float xac = bus[DST_XFADE_AC], xbd = bus[DST_XFADE_BD];
    const float xfade[FM_OPS] = {clampf(1.0f - xac, 0.0f, 1.0f), clampf(1.0f - xbd, 0.0f, 1.0f),
                                 clampf(1.0f + xac, 0.0f, 1.0f), clampf(1.0f + xbd, 0.0f, 1.0f)};
    for (uint32_t k = 0; k < FM_OPS; k++) {
        const fm_osc_t& o = p->osc[k];
        const float vs = unit(o.vel_scale);
        const float vel = std::max(0.0f, vs >= 0.0f ? 1.0f - vs * (1.0f - v->velocity) : 1.0f + vs * v->velocity);
        const float key = db_gain(unit(o.key_scale) * note_oct * kKeyScaleDb);
        const bool carrier = (s.carriers >> k) & 1u;
        target[k] = s.level[k] * s.user_gain[k] * v->env[k].value * vel * key * gain_mod(bus, DST_VOL_A + static_cast<int>(k)) *
                    xfade[k] * (carrier ? 1.0f : fm_drive);
        const float coarse = o.coarse <= 0 ? 0.5f : static_cast<float>(o.coarse);
        float r = o.coarse <= 0 ? 0.5f * (1.0f + unit(o.fine)) : coarse + unit(o.fine);
        r *= 1.0f + unit(o.vel_coarse) * v->velocity;
        ratio[k] = o.vel_coarse && o.vel_coarse_q ? std::max(0.5f, roundf(r)) : r;
    }
    const float feedback_mod = bus[DST_FEEDBACK];
    const float fixed_mod = fast_exp2(bus[DST_FIXED] * kRouteOctaves);
    const float tone = clampf(s.tone_coef + bus[DST_TONE], 0.001f, 1.0f);
    const float volume = db_gain(static_cast<float>(p->main.volume) * 0.01f) * gain_mod(bus, DST_VOLUME);
    const float spread = v->spread ? unit(p->main.spread) * kSpreadSemis : 0.0f;
    const float pan = clampf(unit(p->main.pan) + unit(p->main.pan_key) * note_oct * 0.25f + v->pan + bus[DST_PAN], -1.0f, 1.0f);
    const float base = v->pitch + static_cast<float>(p->main.transpose) + s.bend * static_cast<float>(p->main.bend_range) +
                       static_cast<float>(g->fine) * 0.01f;
    const float penv_osc = penv * unit(p->pitch.amount_a);
    const uint32_t cores = v->spread ? 2u : 1u;

    for (uint32_t c = 0; c < cores; c++) {
        Core* core = &v->core[c];
        const float detune = c ? -spread : spread;
        uint32_t inc[FM_OPS];
        float inc_f[FM_OPS], dg[FM_OPS], fb[FM_OPS];
        for (uint32_t k = 0; k < FM_OPS; k++) {
            const fm_osc_t& o = p->osc[k];
            const float bend = (o.pitch_on ? penv_osc : 0.0f) + detune;
            const float hz = o.fixed ? static_cast<float>(o.fixed_freq) * 0.1f * FIXED_MULTI[clampi(o.fixed_multi, 0, 5)] *
                                           semis_ratio(bend) * fixed_mod
                                     : 440.0f * semis_ratio(base + bend - 69.0f) * ratio[k];
            inc_f[k] = clampf(hz, 0.0f, kFs * 0.499f) * (4294967296.0f / kFs);
            inc[k] = static_cast<uint32_t>(inc_f[k]);
            dg[k] = (target[k] - core->gain[k]) * (1.0f / static_cast<float>(kBlock));
            fb[k] = ((s.unmodulated >> k) & 1u) ? clampf(s.feedback[k] + feedback_mod * kFeedbackCycles, 0.0f, 2.0f) : 0.0f;
        }
        const float l_gain = v->spread && c == 1 ? 0.0f : volume * clampf(1.0f - pan, 0.0f, 1.0f);
        const float r_gain = v->spread && c == 0 ? 0.0f : volume * clampf(1.0f + pan, 0.0f, 1.0f);
        for (uint32_t i = 0; i < kBlock; i++) {
            float mix = 0.0f;
            for (int k = FM_OPS - 1; k >= 0; k--) {
                const uint8_t mods = s.mods[k];
                float m = fb[k] * (core->out[k] + core->prev[k]) * 0.5f;
                for (int j = k + 1; j < FM_OPS; j++)
                    m += (mods >> j) & 1u ? core->out[j] * kModCycles : 0.0f;
                core->tone[k] += (m - core->tone[k]) * tone;
                const bool lfo_on = p->osc[k].lfo_on && l.on;
                core->phase[k] += lfo_on ? static_cast<uint32_t>(clampf(inc_f[k] * pm[i], 0.0f, 2147483647.0f)) : inc[k];
                core->gain[k] += dg[k];
                const float w = p->osc[k].wave == WAVE_NOISE_WHITE
                                    ? rand_bipolar(&core->rng)
                                    : table_at(s.table[k], core->phase[k] + cycles_phase(core->tone[k]), s.interp);
                core->prev[k] = core->out[k];
                core->out[k] = w * core->gain[k];
                mix += (s.carriers >> k) & 1u ? core->out[k] : 0.0f;
            }
            float y = p->filter.on ? filter_run(f, core->s1, core->s2, mix) : mix;
            y += wet * (shape(shaper, y * shaper_drive) - y);
            out_l[i] += y * l_gain;
            out_r[i] += y * r_gain;
        }
    }
    for (uint32_t k = 0; k < FM_OPS; k++)
        v->core[1].gain[k] = v->spread ? v->core[1].gain[k] : v->core[0].gain[k];
    v->active = voice_sounding(*v, s.carriers, p) && !voice_silent(*v, p, s, target, volume) ? 1 : 0;
}

void render_block(const fm_patch_t* p, const fm_global_t* g) {
    Shared s;
    shared_params(p, g, &s);
    drain_events(p);
    std::fill(out_l, out_l + kBlock, 0.0f);
    std::fill(out_r, out_r + kBlock, 0.0f);
    free_lfo_phase += lfo_rate(p, s) * kBlockSec;
    free_lfo_phase -= static_cast<float>(floor_int(free_lfo_phase));
    uint8_t n = 0;
    for (uint32_t i = 0; i < FM_VOICES_MAX; i++) {
        if (!voices[i].active)
            continue;
        render_voice(&voices[i], p, g, s);
        n++;
    }
    mon_active = n;
}

}  // namespace

extern "C" void fm_init(void) {
    build_waves();
    for (uint32_t k = 0; k < FM_OPS; k++) {
        for (uint32_t b = 0; b < 2; b++) {
            for (uint32_t i = 0; i <= kTable; i++)
                user_table[k][b][i] = wave_table[0][i];
            user_gain[k][b] = 1.0f;
        }
        user_live[k] = 0;
    }
    for (uint32_t i = 0; i < FM_VOICES_MAX; i++) {
        voices[i] = Voice();
        voices[i].rng = 0x2545F491u * (i + 1u);
        voices[i].core[0].rng = 0x9E3779B9u * (i + 1u);
        voices[i].core[1].rng = 0x7F4A7C15u * (i + 1u);
    }
    for (uint32_t i = 0; i < kTable; i++)
        scratch[i] = 0.0f;
}

extern "C" void fm_patch_loaded(const fm_patch_t* p) {
    for (uint32_t k = 0; k < FM_OPS; k++) {
        const uint32_t next = (user_live[k] & 1u) ^ 1u;
        build_user(p->osc[k], user_table[k][next], &user_gain[k][next]);
        user_live[k] = static_cast<uint8_t>(next);
    }
}

extern "C" void fm_render(const fm_patch_t* p, const fm_global_t* g, int32_t* lr, uint32_t frames) {
    const float volume = static_cast<float>(g->volume) * (1.0f / 256.0f) * 32767.0f;
    for (uint32_t i = 0; i < frames; i++) {
        if (block_pos >= kBlock) {
            render_block(p, g);
            block_pos = 0;
        }
        lr[2u * i] = static_cast<int32_t>(clampf(out_l[block_pos], -1.0f, 1.0f) * volume);
        lr[2u * i + 1u] = static_cast<int32_t>(clampf(out_r[block_pos], -1.0f, 1.0f) * volume);
        block_pos++;
    }
}

extern "C" void fm_note_on(uint8_t note, uint8_t velocity) { push_event(EV_ON, note, velocity ? velocity : 1u); }

extern "C" void fm_note_off(uint8_t note) { push_event(EV_OFF, note, 0); }

extern "C" void fm_all_notes_off(void) { push_event(EV_ALL_OFF, 0, 0); }

extern "C" void fm_kill_all(void) { push_event(EV_KILL, 0, 0); }

extern "C" void fm_controller(uint32_t source, int32_t value) {
    if (source == SRC_BEND)
        ctl_bend = value;
    if (source == SRC_WHEEL)
        ctl_wheel = value;
    if (source == SRC_AFTERTOUCH)
        ctl_aftertouch = value;
}

extern "C" void fm_set_voice_cap(uint32_t n) { voice_cap = std::max(1u, std::min(n, static_cast<uint32_t>(FM_VOICES_MAX))); }

extern "C" void fm_monitor(fm_monitor_t* m) {
    m->active = mon_active;
    m->cap = static_cast<uint8_t>(voice_cap);
}

extern "C" uint8_t fm_algo_mods(int32_t algo, int32_t op) { return MODS[clampi(algo, 0, FM_ALGOS - 1)][op & 3]; }

extern "C" uint8_t fm_algo_carriers(int32_t algo) { return CARRIERS[clampi(algo, 0, FM_ALGOS - 1)]; }

extern "C" uint32_t fm_voice_count(int32_t index) { return VOICE_COUNT[clampi(index, 0, 13)]; }

extern "C" int16_t fm_wave_sample(const fm_patch_t* p, uint32_t op, uint32_t i) {
    const fm_osc_t& o = p->osc[op & 3u];
    const uint32_t at = (i & 255u) * (kTable / 256u);
    if (o.wave == WAVE_USER)
        return user_table[op & 3u][user_live[op & 3u] & 1u][at];
    return wave_table[clampi(o.wave, 0, WAVE_NOISE_WHITE)][at];
}
