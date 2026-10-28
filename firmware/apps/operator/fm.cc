// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 npi3pak
#include <algorithm>
#include <math.h>

#include "fw_rate.h"
#include "fm.h"

namespace {

const uint32_t kBlock = 16;
const uint32_t kEvents = 32;
const uint32_t kSineBits = 10;
const uint32_t kSineSize = 1u << kSineBits;
const float kDt = static_cast<float>(kBlock) / FW_FS_HZ;
const float kModDepth = 1.0f;

enum { EV_OFF = 0, EV_ON = 1, EV_ALL_OFF = 2 };
enum { ENV_IDLE, ENV_ATTACK, ENV_DECAY, ENV_RELEASE };

// bit j of MODS[algo][i]: operator j modulates operator i; a modulator always has a higher index than its target
const uint8_t MODS[FM_ALGOS][FM_OPS] = {
    {0x2, 0x4, 0x8, 0x0},
    {0x2, 0xC, 0x0, 0x0},
    {0x6, 0x0, 0x8, 0x0},
    {0xE, 0x0, 0x0, 0x0},
    {0x2, 0x0, 0x8, 0x0},
    {0x0, 0x4, 0x8, 0x0},
    {0x8, 0x8, 0x8, 0x0},
    {0x0, 0x0, 0x8, 0x0},
    {0x6, 0x8, 0x8, 0x0},
    {0x0, 0xC, 0x0, 0x0},
    {0x0, 0x0, 0x0, 0x0},
};
const uint8_t CARRIERS[FM_ALGOS] = {0x1, 0x1, 0x1, 0x1, 0x5, 0x3, 0x7, 0x7, 0x1, 0x3, 0xF};

struct Op {
    uint32_t phase, inc;
    float env, out;
    int stage;
};

struct Voice {
    Op op[FM_OPS];
    uint8_t note, gate;
    float velocity;
    uint32_t age;
};

struct Rates {
    float attack, decay, sustain, release;
};

float sine[kSineSize + 1];
Voice voices[FM_VOICES];
uint32_t voice_clock;
float out_block[kBlock];
uint32_t block_pos = kBlock;

volatile uint32_t events[kEvents];
volatile uint32_t events_w, events_r;
volatile int16_t ctl_bend;

inline float clampf(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }

float sine_at(uint32_t phase) {
    const uint32_t i = phase >> (32 - kSineBits);
    const float f = static_cast<float>((phase >> (32 - kSineBits - 16)) & 0xFFFFu) * (1.0f / 65536.0f);
    return sine[i] + (sine[i + 1] - sine[i]) * f;
}

// 1 ms * 10^(4 key / 1000), as app.c's env_time_us
float env_seconds(int16_t key) { return 0.001f * expf(9.2103f * static_cast<float>(key) * 0.001f); }

Rates op_rates(const fm_op_t& o) {
    Rates r;
    r.attack = std::min(1.0f, kDt / env_seconds(o.env_a));
    r.decay = 1.0f - expf(-6.9078f * kDt / env_seconds(o.env_d));
    r.sustain = clampf(static_cast<float>(o.env_s) * 0.001f, 0.0f, 1.0f);
    r.release = 1.0f - expf(-6.9078f * kDt / env_seconds(o.env_r));
    return r;
}

void env_step(Op* o, const Rates& r, bool gate) {
    if (!gate && o->stage != ENV_IDLE)
        o->stage = ENV_RELEASE;
    if (o->stage == ENV_ATTACK) {
        o->env += r.attack;
        if (o->env >= 1.0f) {
            o->env = 1.0f;
            o->stage = ENV_DECAY;
        }
        return;
    }
    if (o->stage == ENV_DECAY) {
        o->env += (r.sustain - o->env) * r.decay;
        return;
    }
    if (o->stage == ENV_RELEASE) {
        o->env -= o->env * r.release;
        if (o->env < 0.0005f) {
            o->env = 0.0f;
            o->stage = ENV_IDLE;
        }
    }
}

float op_ratio(const fm_op_t& o) {
    const float coarse = o.coarse <= 0 ? 0.5f : static_cast<float>(o.coarse);
    return coarse + static_cast<float>(o.fine) * 0.001f;
}

float bit_count(uint8_t m) {
    return static_cast<float>((m & 1u) + (m >> 1 & 1u) + (m >> 2 & 1u) + (m >> 3 & 1u));
}

uint32_t phase_inc(float hz) {
    return static_cast<uint32_t>(clampf(hz, 0.0f, FW_FS_HZ * 0.45f) * (4294967296.0f / FW_FS_HZ));
}

void voice_start(uint8_t note, uint8_t velocity) {
    Voice* pick = &voices[0];
    for (uint32_t i = 0; i < FM_VOICES; i++) {
        Voice* v = &voices[i];
        const bool free = v->op[0].stage == ENV_IDLE && v->op[1].stage == ENV_IDLE && v->op[2].stage == ENV_IDLE &&
                          v->op[3].stage == ENV_IDLE;
        if (free) {
            pick = v;
            break;
        }
        if (v->age < pick->age)
            pick = v;
    }
    pick->note = note;
    pick->gate = 1;
    pick->velocity = static_cast<float>(velocity) * (1.0f / 127.0f);
    pick->age = ++voice_clock;
    for (uint32_t k = 0; k < FM_OPS; k++) {
        pick->op[k].stage = ENV_ATTACK;
        pick->op[k].phase = 0;
        pick->op[k].out = 0.0f;
    }
}

void voice_stop(uint8_t note) {
    for (uint32_t i = 0; i < FM_VOICES; i++) {
        if (voices[i].gate && voices[i].note == note)
            voices[i].gate = 0;
    }
}

void drain_events() {
    while (events_r != events_w) {
        const uint32_t e = events[events_r % kEvents];
        events_r++;
        const uint8_t kind = static_cast<uint8_t>(e >> 16), note = static_cast<uint8_t>(e >> 8);
        if (kind == EV_ON)
            voice_start(note, static_cast<uint8_t>(e));
        if (kind == EV_OFF)
            voice_stop(note);
        if (kind == EV_ALL_OFF) {
            for (uint32_t i = 0; i < FM_VOICES; i++)
                voices[i].gate = 0;
        }
    }
}

void push_event(uint32_t kind, uint32_t note, uint32_t velocity) {
    if (events_w - events_r >= kEvents)
        return;
    events[events_w % kEvents] = kind << 16 | (note & 127u) << 8 | (velocity & 127u);
    events_w++;
}

void render_voice(Voice* v, const fm_patch_t* p, const Rates* rates, float pitch, float* out) {
    const int algo = std::max(0, std::min(FM_ALGOS - 1, static_cast<int>(p->algo)));
    const uint8_t carriers = CARRIERS[algo];
    const float hz = 440.0f * expf((pitch + static_cast<float>(v->note) - 69.0f) * 0.05776227f);
    const float feedback = clampf(static_cast<float>(p->feedback) * 0.01f, 0.0f, 1.0f) * 0.5f;
    float level[FM_OPS];
    for (uint32_t k = 0; k < FM_OPS; k++) {
        env_step(&v->op[k], rates[k], v->gate != 0);
        v->op[k].inc = phase_inc(hz * op_ratio(p->op[k]));
        level[k] = clampf(static_cast<float>(p->op[k].level) * 0.01f, 0.0f, 1.0f) * v->op[k].env;
    }
    const float gain = v->velocity / bit_count(carriers);
    for (uint32_t i = 0; i < kBlock; i++) {
        float mix = 0.0f;
        for (int k = FM_OPS - 1; k >= 0; k--) {
            Op* o = &v->op[k];
            const uint8_t mods = MODS[algo][k];
            float m = k == OP_D ? o->out * feedback : 0.0f;
            for (int j = k + 1; j < FM_OPS; j++)
                m += (mods >> j) & 1u ? v->op[j].out : 0.0f;
            o->phase += o->inc;
            const uint32_t pm = static_cast<uint32_t>(static_cast<int32_t>(m * kModDepth * 16777216.0f)) << 8;
            o->out = sine_at(o->phase + pm) * level[k];
            mix += (carriers >> k) & 1u ? o->out : 0.0f;
        }
        out[i] += mix * gain;
    }
}

bool voice_active(const Voice& v) {
    return v.op[0].stage != ENV_IDLE || v.op[1].stage != ENV_IDLE || v.op[2].stage != ENV_IDLE ||
           v.op[3].stage != ENV_IDLE;
}

void render_block(const fm_patch_t* p, const fm_global_t* g) {
    Rates rates[FM_OPS];
    for (uint32_t k = 0; k < FM_OPS; k++)
        rates[k] = op_rates(p->op[k]);
    const float bend = static_cast<float>(ctl_bend) * (1.0f / 8192.0f) * static_cast<float>(g->bend_range);
    const float pitch = static_cast<float>(p->transpose) + static_cast<float>(g->fine) * 0.01f + bend;
    std::fill(out_block, out_block + kBlock, 0.0f);
    drain_events();
    for (uint32_t i = 0; i < FM_VOICES; i++) {
        if (voice_active(voices[i]))
            render_voice(&voices[i], p, rates, pitch, out_block);
    }
}

}  // namespace

extern "C" void fm_init(void) {
    for (uint32_t i = 0; i <= kSineSize; i++)
        sine[i] = sinf(6.2831853f * static_cast<float>(i) / static_cast<float>(kSineSize));
    for (uint32_t i = 0; i < FM_VOICES; i++)
        voices[i] = Voice();
}

extern "C" void fm_render(const fm_patch_t* p, const fm_global_t* g, int32_t* lr, uint32_t frames) {
    const float volume = static_cast<float>(g->volume) * (1.0f / 256.0f) * 0.5f * 32767.0f;
    for (uint32_t i = 0; i < frames; i++) {
        if (block_pos >= kBlock) {
            render_block(p, g);
            block_pos = 0;
        }
        const int32_t s = static_cast<int32_t>(clampf(out_block[block_pos++], -1.0f, 1.0f) * volume);
        lr[2u * i] = s;
        lr[2u * i + 1u] = s;
    }
}

extern "C" void fm_note_on(uint8_t note, uint8_t velocity) { push_event(EV_ON, note, velocity); }

extern "C" void fm_note_off(uint8_t note) { push_event(EV_OFF, note, 0); }

extern "C" void fm_all_notes_off(void) { push_event(EV_ALL_OFF, 0, 0); }

extern "C" void fm_bend(int16_t bend) { ctl_bend = bend; }

extern "C" void fm_monitor(const fm_patch_t* p, fm_monitor_t* m) {
    uint8_t n = 0;
    for (uint32_t i = 0; i < FM_VOICES; i++)
        n += voice_active(voices[i]) ? 1 : 0;
    m->active = n;
    m->carriers = fm_algo_carriers(p->algo);
}

extern "C" uint8_t fm_algo_mods(int32_t algo, int32_t op) {
    return MODS[std::max(0, std::min(FM_ALGOS - 1, static_cast<int>(algo)))][op & 3];
}

extern "C" uint8_t fm_algo_carriers(int32_t algo) {
    return CARRIERS[std::max(0, std::min(FM_ALGOS - 1, static_cast<int>(algo)))];
}
