/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 npi3pak */
#include "fm.h"

#define PRESETS 16u
#define BANK_MAGIC 0x4F505254u
#define BANK_VERSION 1u
#define KEY_VELOCITY 100u
#define SCOPE_N 240u
#define ROW_H 28u
#define DRAWS_PER_FRAME 2u
#define CPU_GUARD_Q8 218u

enum { PAGE_MAIN, PAGE_OSC, PAGE_ENV, PAGE_GLOBAL, PAGE_SAVE, PAGES };
enum { FMT_100, FMT_BIP, FMT_INT, FMT_SEMI, FMT_RATIO, FMT_CHANNEL, FMT_TIME, FMT_PCT };
enum { D_HEADER = 0, D_ROW0 = 1, D_ENC = 12, D_SCOPE = 13, D_STATUS = 14, D_ALL = 0x7FFFu };

typedef struct {
    const char *label;
    int16_t *value;
    int16_t lo, hi, step;
    uint8_t fmt;
} param_t;

typedef struct {
    uint32_t magic;
    uint16_t version, current;
    fm_global_t global;
    fm_patch_t preset[PRESETS];
} bank_t;

static const char *const OP_NAME[FM_OPS] = {"A", "B", "C", "D"};
static const uint16_t OP_COLOR[FM_OPS] = {RGB(255, 98, 26), RGB(255, 196, 40), RGB(40, 200, 220), RGB(150, 110, 255)};
static const uint16_t ACCENT = RGB(255, 98, 26);

static fm_patch_t patch;
static fm_global_t global;
static fm_monitor_t mon;
static bank_t bank;
static uint32_t page = PAGE_MAIN, op_sel, save_slot, dirty = D_ALL, frame_count;
static int32_t octave;
static uint32_t key_held;
static int32_t knob_fast[FW_KNOB_COUNT];
static uint32_t knob_ms[FW_KNOB_COUNT];
static uint8_t key_note[FW_KEYS];
static int16_t scope[SCOPE_N];
static volatile uint32_t scope_ready;
static volatile uint32_t overloads;
static uint32_t overloads_seen, shown_voices;
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

static void clamp16(int16_t *v, int32_t lo, int32_t hi) { *v = (int16_t)clampi(*v, lo, hi); }

static param_t param(const char *label, int16_t *value, int32_t lo, int32_t hi, int32_t step, uint32_t fmt)
{
    const param_t p = {label, value, (int16_t)lo, (int16_t)hi, (int16_t)step, (uint8_t)fmt};
    return p;
}

static param_t page_param(uint32_t pg, uint32_t row)
{
    fm_op_t *o = &patch.op[op_sel];
    static const param_t none = {0, 0, 0, 0, 0, 0};
    if (pg == PAGE_MAIN)
        return param(OP_NAME[row], &patch.op[row].level, 0, 100, 1, FMT_INT);
    if (pg == PAGE_OSC && row == 0u)
        return param("COARSE", &o->coarse, 0, 48, 1, FMT_RATIO);
    if (pg == PAGE_OSC && row == 1u)
        return param("FINE", &o->fine, 0, 1000, 5, FMT_INT);
    if (pg == PAGE_OSC && row == 2u)
        return param("LEVEL", &o->level, 0, 100, 1, FMT_INT);
    if (pg == PAGE_ENV && row == 0u)
        return param("ATTACK", &o->env_a, 0, 1000, 10, FMT_TIME);
    if (pg == PAGE_ENV && row == 1u)
        return param("DECAY", &o->env_d, 0, 1000, 10, FMT_TIME);
    if (pg == PAGE_ENV && row == 2u)
        return param("SUSTAIN", &o->env_s, 0, 1000, 10, FMT_100);
    if (pg == PAGE_ENV && row == 3u)
        return param("RELEASE", &o->env_r, 0, 1000, 10, FMT_TIME);
    if (pg == PAGE_GLOBAL && row == 0u)
        return param("TRANSPOSE", &patch.transpose, -24, 24, 1, FMT_SEMI);
    if (pg == PAGE_GLOBAL && row == 1u)
        return param("BEND RANGE", &global.bend_range, 0, 12, 1, FMT_SEMI);
    if (pg == PAGE_GLOBAL && row == 2u)
        return param("FINE TUNE", &global.fine, -100, 100, 1, FMT_BIP);
    if (pg == PAGE_GLOBAL && row == 3u)
        return param("MIDI CH", &global.channel, 0, 16, 1, FMT_CHANNEL);
    return none;
}

static void patch_init(fm_patch_t *p, int32_t algo)
{
    uint32_t k;
    memset(p, 0, sizeof *p);
    p->algo = (int16_t)algo;
    for (k = 0; k < FM_OPS; k++) {
        p->op[k].coarse = (int16_t)(k + 1u);
        p->op[k].level = (fm_algo_carriers(algo) >> k) & 1u ? 80 : 40;
        p->op[k].env_a = 0;
        p->op[k].env_d = 600;
        p->op[k].env_s = 700;
        p->op[k].env_r = 500;
    }
    p->op[OP_A].coarse = 1;
}

static void patch_sanitize(fm_patch_t *p)
{
    uint32_t k;
    clamp16(&p->algo, 0, FM_ALGOS - 1);
    clamp16(&p->feedback, 0, 100);
    clamp16(&p->transpose, -24, 24);
    for (k = 0; k < FM_OPS; k++) {
        clamp16(&p->op[k].coarse, 0, 48);
        clamp16(&p->op[k].fine, 0, 1000);
        clamp16(&p->op[k].level, 0, 100);
        clamp16(&p->op[k].env_a, 0, 1000);
        clamp16(&p->op[k].env_d, 0, 1000);
        clamp16(&p->op[k].env_s, 0, 1000);
        clamp16(&p->op[k].env_r, 0, 1000);
    }
}

static void factory_bank(void)
{
    uint32_t i;
    memset(&bank, 0, sizeof bank);
    bank.magic = BANK_MAGIC;
    bank.version = BANK_VERSION;
    bank.global.bend_range = 2;
    for (i = 0; i < PRESETS; i++)
        patch_init(&bank.preset[i], (int32_t)(i % FM_ALGOS));
}

static void bank_load(void)
{
    const int n = fw_store_load(0, &bank, sizeof bank);
    uint32_t i;
    if (n != (int)sizeof bank || bank.magic != BANK_MAGIC || bank.version != BANK_VERSION || bank.current >= PRESETS)
        factory_bank();
    for (i = 0; i < PRESETS; i++)
        patch_sanitize(&bank.preset[i]);
    clamp16(&bank.global.bend_range, 0, 12);
    clamp16(&bank.global.fine, -100, 100);
    clamp16(&bank.global.channel, 0, 16);
}

static void preset_select(uint32_t i)
{
    bank.current = (uint16_t)(i % PRESETS);
    fm1_irq_off();
    patch = bank.preset[bank.current];
    fm1_irq_on();
    dirty = D_ALL;
}

static void message(const char *s, uint32_t now)
{
    flash_msg = s;
    flash_msg_until = now + 1500u;
    dirty |= FW_BIT(D_STATUS);
}

static int edited(void) { return memcmp(&patch, &bank.preset[bank.current], sizeof patch) != 0; }

static void bank_save(uint32_t slot, uint32_t now)
{
    const uint16_t keep = bank.current;
    bank.preset[slot] = patch;
    bank.current = (uint16_t)slot;
    bank.global = global;
    if (fw_store_save(0, &bank, sizeof bank) == 0) {
        message("SAVED", now);
        return;
    }
    bank.current = keep;
    message("SAVE FAILED", now);
}

static int adjust(int16_t *v, int32_t d, int32_t step, int32_t lo, int32_t hi)
{
    const int16_t next = (int16_t)clampi(*v + d * step, lo, hi);
    if (next == *v)
        return 0;
    *v = next;
    return 1;
}

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

static int adjust_param(const param_t *p, const fw_input_t *in, uint32_t k)
{
    if (!p->value || !in->knob[k])
        return 0;
    if (p->hi - p->lo < 40 * p->step)
        return adjust(p->value, in->knob[k], p->step, p->lo, p->hi);
    return adjust(p->value, knob_fast[k], p->step, p->lo, p->hi);
}

static void fmt_signed(char *b, int32_t v)
{
    if (v > 0) {
        b[0] = '+';
        fmt_int(b + 1, v);
        return;
    }
    fmt_int(b, v);
}

/* the envelope's times, as fm.cc: 1 ms * 10^(4 v / 1000), 250 steps a decade */
static uint32_t env_time_us(int32_t v)
{
    static const uint16_t DECADE[26] = {1000, 1096, 1202, 1318, 1445, 1585, 1738, 1905, 2089, 2291, 2512, 2754, 3020,
                                        3311, 3631, 3981, 4365, 4786, 5248, 5754, 6310, 6918, 7586, 8318, 9120, 10000};
    static const uint32_t SCALE[5] = {1u, 10u, 100u, 1000u, 10000u};
    const int32_t c = clampi(v, 0, 1000);
    const int32_t s = c / 10, f = c % 10;
    const int32_t j = s % 25;
    const uint32_t m = (uint32_t)(DECADE[j] * (10 - f) + DECADE[j + 1] * f) / 10u;
    return m * SCALE[s / 25];
}

static void fmt_time(char *b, uint32_t size, uint32_t us)
{
    if (us < 9950u) {
        fmt_fix(b, (int32_t)((us + 50u) / 100u), 1);
        str_cpy(b + str_len(b), " ms", size - str_len(b));
        return;
    }
    if (us < 999500u) {
        fmt_int(b, (int32_t)((us + 500u) / 1000u));
        str_cpy(b + str_len(b), " ms", size - str_len(b));
        return;
    }
    if (us < 9995000u) {
        fmt_fix(b, (int32_t)((us + 5000u) / 10000u), 2);
        str_cpy(b + str_len(b), " s", size - str_len(b));
        return;
    }
    fmt_fix(b, (int32_t)((us + 50000u) / 100000u), 1);
    str_cpy(b + str_len(b), " s", size - str_len(b));
}

static void fmt_ratio(char *b, const fm_op_t *o)
{
    const int32_t coarse = o->coarse <= 0 ? 500 : o->coarse * 1000;
    fmt_fix(b, (coarse + o->fine + 5) / 10, 2);
}

static void format_param(char *b, uint32_t size, const param_t *p)
{
    const int32_t v = *p->value;
    if (p->fmt == FMT_TIME) {
        fmt_time(b, size, env_time_us(v));
        return;
    }
    if (p->fmt == FMT_100) {
        fmt_int(b, v / 10);
        return;
    }
    if (p->fmt == FMT_RATIO) {
        fmt_ratio(b, &patch.op[op_sel]);
        return;
    }
    if (p->fmt == FMT_BIP || p->fmt == FMT_SEMI) {
        fmt_signed(b, v);
        if (p->fmt == FMT_SEMI)
            str_cpy(b + str_len(b), " ST", size - str_len(b));
        return;
    }
    if (p->fmt == FMT_CHANNEL && v == 0) {
        str_cpy(b, "OMNI", size);
        return;
    }
    fmt_int(b, v);
}

static void draw_bar(int32_t x, int32_t y, int32_t w, int32_t value, int32_t lo, int32_t hi, uint16_t c)
{
    const int32_t span = hi - lo;
    const int32_t bx = x + (clampi(value, lo, hi) - lo) * w / span;
    const int32_t zero = lo < 0 ? x + (0 - lo) * w / span : x;
    cv_rect(x, y, w, 4, C_DIM);
    cv_rect(zero < bx ? zero : bx, y, zero < bx ? bx - zero : zero - bx, 4, c);
}

static void draw_row(uint32_t y, uint16_t c, const char *label, const char *sub, const char *value,
                     int32_t v, int32_t lo, int32_t hi)
{
    int32_t x;
    cv_begin(240, ROW_H, C_BLACK);
    x = cv_text(8, 1, &FONT_S, label, c);
    if (sub)
        cv_text(x + 8, 1, &FONT_S, sub, C_GRAY);
    cv_text(232 - text_w(&FONT_S, value), 1, &FONT_S, value, C_HI);
    draw_bar(8, 20, 224, v, lo, hi, c);
    cv_blit(0, y);
}

static void draw_title(const char *title, const char *right, uint16_t c)
{
    cv_begin(240, 46, C_BLACK);
    cv_text(8, 8, &FONT_L, title, c);
    if (right)
        cv_text(232 - text_w(&FONT_S, right), 2, &FONT_S, right, C_GRAY);
    cv_blit(0, 0);
}

static void algo_outputs(char *b, uint32_t size)
{
    const uint32_t carriers = fm_algo_carriers(patch.algo);
    uint32_t k;
    str_cpy(b, "OUT", size);
    for (k = 0; k < FM_OPS; k++) {
        if ((carriers >> k) & 1u) {
            str_cpy(b + str_len(b), " ", size - str_len(b));
            str_cpy(b + str_len(b), OP_NAME[k], size - str_len(b));
        }
    }
}

static void draw_header(void)
{
    char b[16], n[8], r[16];
    if (page == PAGE_MAIN) {
        str_cpy(b, "ALGO ", sizeof b);
        fmt_int(n, patch.algo + 1);
        str_cpy(b + str_len(b), n, sizeof b - str_len(b));
        algo_outputs(r, sizeof r);
        draw_title(b, r, C_WHITE);
        return;
    }
    if (page == PAGE_OSC || page == PAGE_ENV) {
        str_cpy(b, page == PAGE_OSC ? "OSC " : "ENV ", sizeof b);
        str_cpy(b + 4, OP_NAME[op_sel], sizeof b - 4);
        draw_title(b, (fm_algo_carriers(patch.algo) >> op_sel) & 1u ? "CARRIER" : "MODULATOR", OP_COLOR[op_sel]);
        return;
    }
    if (page == PAGE_SAVE) {
        draw_title("SAVE", "PRESET", C_WHITE);
        return;
    }
    draw_title("GLOBAL", 0, C_WHITE);
}

static void draw_param_row(uint32_t row)
{
    const param_t p = page_param(page, row);
    const uint32_t y = 48u + row * 30u;
    const uint16_t c = page == PAGE_MAIN ? OP_COLOR[row] : (page == PAGE_GLOBAL ? C_HI : OP_COLOR[op_sel]);
    char b[16];
    if (!p.label) {
        cv_begin(240, ROW_H, C_BLACK);
        cv_blit(0, y);
        return;
    }
    format_param(b, sizeof b, &p);
    if (page == PAGE_MAIN) {
        draw_row(y, c, p.label, (fm_algo_carriers(patch.algo) >> row) & 1u ? "CARRIER" : "MOD", b, *p.value, p.lo, p.hi);
        return;
    }
    draw_row(y, c, p.label, 0, b, *p.value, p.lo, p.hi);
}

static void draw_save_row(uint32_t row)
{
    char b[32], n[8];
    const uint32_t slot = (save_slot + PRESETS + row - 1u) % PRESETS;
    str_cpy(b, row == 1 ? "> P" : "  P", sizeof b);
    fmt_int(n, (int32_t)slot + 1);
    str_cpy(b + 3, n, sizeof b - 3);
    str_cpy(b + str_len(b), "  ALGO ", sizeof b - str_len(b));
    fmt_int(n, bank.preset[slot].algo + 1);
    str_cpy(b + str_len(b), n, sizeof b - str_len(b));
    cv_begin(240, ROW_H, C_BLACK);
    cv_text(8, 6, &FONT_S, b, row == 1 ? C_WHITE : C_DIM);
    cv_blit(0, 48u + row * 30u);
}

static void draw_enc_line(void)
{
    char b[40], n[8];
    b[0] = 0;
    if (page == PAGE_MAIN) {
        str_cpy(b, "SEL ALGO  ALGO FB ", sizeof b);
        fmt_int(n, patch.feedback);
        str_cpy(b + str_len(b), n, sizeof b - str_len(b));
    }
    if (page == PAGE_OSC || page == PAGE_ENV)
        str_cpy(b, "SEL OPERATOR A-D", sizeof b);
    if (page == PAGE_SAVE)
        str_cpy(b, "SEL SLOT  SAVE = WRITE", sizeof b);
    cv_begin(240, 18, C_BLACK);
    cv_text(8, 1, &FONT_S, b, C_GRAY);
    cv_blit(0, 168);
}

static void draw_status(void)
{
    char b[24], n[8];
    cv_begin(240, 18, C_BLACK);
    cv_rect(0, 0, 240, 1, C_DIM);
    if (flash_msg) {
        cv_text(8, 2, &FONT_S, flash_msg, ACCENT);
        cv_blit(0, 222);
        return;
    }
    str_cpy(b, "VOX ", sizeof b);
    fmt_int(n, mon.active);
    str_cpy(b + 4, n, sizeof b - 4);
    cv_text(8, 2, &FONT_S, b, mon.active ? ACCENT : C_GRAY);
    str_cpy(b, "P", sizeof b);
    fmt_int(n, bank.current + 1);
    str_cpy(b + 1, n, sizeof b - 1);
    str_cpy(b + str_len(b), edited() ? "*" : "", sizeof b - str_len(b));
    cv_text(80, 2, &FONT_S, b, C_HI);
    str_cpy(b, "OCT", sizeof b);
    fmt_signed(n, octave);
    str_cpy(b + 3, n, sizeof b - 3);
    cv_text(128, 2, &FONT_S, b, C_GRAY);
    fmt_int(n, (int32_t)(fw_stats.cpu_q8 * 100u >> 8));
    str_cpy(n + str_len(n), overloads != overloads_seen ? "%!" : "%", sizeof n - str_len(n));
    cv_text(232 - text_w(&FONT_S, n), 2, &FONT_S, n, fw_stats.cpu_q8 > 192u ? ACCENT : C_GRAY);
    overloads_seen = overloads;
    cv_blit(0, 222);
}

static void draw_scope(void)
{
    uint32_t x;
    int32_t prev = 16;
    cv_begin(240, 32, C_BLACK);
    cv_rect(0, 16, 240, 1, C_DIM);
    for (x = 0; x < SCOPE_N; x++) {
        const int32_t y = clampi(16 - scope[x] / 900, 0, 31);
        cv_line((int32_t)x - 1, prev, (int32_t)x, y, ACCENT);
        prev = y;
    }
    cv_blit(0, 188);
    scope_ready = 0;
}

static void draw_strip(uint32_t bit)
{
    if (bit == D_HEADER)
        draw_header();
    if (bit >= D_ROW0 && bit < D_ROW0 + 3u && page == PAGE_SAVE)
        draw_save_row(bit - D_ROW0);
    if (bit >= D_ROW0 && bit < D_ROW0 + 4u && page != PAGE_SAVE)
        draw_param_row(bit - D_ROW0);
    if (bit == D_ENC)
        draw_enc_line();
    if (bit == D_SCOPE && page != PAGE_SAVE)
        draw_scope();
    if (bit == D_STATUS)
        draw_status();
}

static void draw(void)
{
    uint32_t n = 0, bit;
    for (bit = 0; bit < 15u && n < DRAWS_PER_FRAME; bit++) {
        if (!(dirty & FW_BIT(bit)) || bit == D_SCOPE)
            continue;
        dirty &= ~FW_BIT(bit);
        draw_strip(bit);
        n++;
    }
    if (n == 0 && (dirty & FW_BIT(D_SCOPE)) && scope_ready) {
        dirty &= ~FW_BIT(D_SCOPE);
        draw_strip(D_SCOPE);
    }
}

static void set_page(uint32_t p)
{
    if (p == page)
        return;
    page = p;
    dirty = D_ALL;
    lcd_fill(0, 0, 240, 222, C_BLACK);
}

static void handle_buttons(const fw_input_t *in)
{
    static const uint8_t MAP[][2] = {
        {FW_BTN_HOME, PAGE_MAIN}, {FW_BTN_EDIT, PAGE_OSC}, {FW_BTN_ENV, PAGE_ENV}, {FW_BTN_GLO, PAGE_GLOBAL}
    };
    const uint32_t was = page;
    uint32_t i;
    for (i = 0; i < FW_NELEM(MAP); i++) {
        if (in->pressed & FW_BIT(MAP[i][0]))
            set_page(MAP[i][1]);
    }
    if ((in->pressed & FW_BIT(FW_BTN_SAVE)) && was == PAGE_SAVE) {
        bank_save(save_slot, in->ms);
        set_page(PAGE_MAIN);
    }
    if ((in->pressed & FW_BIT(FW_BTN_SAVE)) && was != PAGE_SAVE) {
        save_slot = bank.current;
        set_page(PAGE_SAVE);
    }
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
        if (kind == 0xB0u && (m.d1 == 120u || m.d1 == 123u))
            fm_all_notes_off();
        if (kind == 0xE0u)
            fm_bend((int16_t)(((int32_t)m.d2 << 7 | m.d1) - 8192));
    }
}

static void handle_knobs_save(const fw_input_t *in)
{
    const int32_t d = in->knob[FW_KNOB_SELECT] + in->knob[FW_KNOB_ALGO] + in->knob[FW_KNOB_1];
    if (!d)
        return;
    save_slot = (uint32_t)(((int32_t)save_slot + d) % (int32_t)PRESETS + (int32_t)PRESETS) % PRESETS;
    dirty |= 0x0Eu;
}

static void handle_knobs_page(const fw_input_t *in)
{
    uint32_t i;
    if ((page == PAGE_OSC || page == PAGE_ENV) && in->knob[FW_KNOB_SELECT]) {
        op_sel = (uint32_t)clampi((int32_t)op_sel + in->knob[FW_KNOB_SELECT], 0, FM_OPS - 1);
        dirty |= FW_BIT(D_HEADER) | 0x1Eu;
    }
    for (i = 0; i < 4u; i++) {
        const param_t p = page_param(page, i);
        if (adjust_param(&p, in, FW_KNOB_1 + i))
            dirty |= FW_BIT(D_ROW0 + i);
    }
    if (page == PAGE_MAIN && adjust(&patch.algo, in->knob[FW_KNOB_SELECT], 1, 0, FM_ALGOS - 1))
        dirty |= FW_BIT(D_HEADER) | 0x1Eu;
    if (page == PAGE_MAIN && adjust(&patch.feedback, knob_fast[FW_KNOB_ALGO], 1, 0, 100))
        dirty |= FW_BIT(D_ENC);
}

static void handle_knobs(const fw_input_t *in)
{
    const int32_t p = in->knob[FW_KNOB_PRESET];
    knob_speed(in);
    if (p && page != PAGE_SAVE) {
        preset_select((uint32_t)(((int32_t)bank.current + p) % (int32_t)PRESETS + (int32_t)PRESETS));
        lcd_fill(0, 0, 240, 222, C_BLACK);
        return;
    }
    if (page == PAGE_SAVE) {
        handle_knobs_save(in);
        return;
    }
    handle_knobs_page(in);
}

static void update_leds(void)
{
    static const uint8_t PAGE_LED[PAGES] = {FW_BTN_HOME, FW_BTN_EDIT, FW_BTN_ENV, FW_BTN_GLO, FW_BTN_SAVE};
    uint32_t i;
    for (i = 0; i < PAGES; i++)
        fw_led_button(PAGE_LED[i], i == page);
    fw_leds_keys(key_held);
}

static void app_init(void)
{
    bank_load();
    global = bank.global;
    preset_select(bank.current);
    fm_init();
}

static void app_frame(const fw_input_t *in)
{
    frame_count++;
    global.volume = (int16_t)(in->master * in->master >> 12);
    handle_buttons(in);
    handle_knobs(in);
    handle_keys(in);
    handle_midi();
    fm_monitor(&patch, &mon);
    if (flash_msg && (int32_t)(in->ms - flash_msg_until) > 0) {
        flash_msg = 0;
        dirty |= FW_BIT(D_STATUS);
    }
    if ((frame_count & 31u) == 0u || edited() != shown_edited || mon.active != shown_voices) {
        shown_edited = edited();
        shown_voices = mon.active;
        dirty |= FW_BIT(D_STATUS);
    }
    if (page != PAGE_SAVE)
        dirty |= FW_BIT(D_SCOPE);
    update_leds();
    draw();
}

static void app_audio(int32_t *lr, uint32_t frames)
{
    uint32_t i;
    if (fw_stats.cpu_q8 > CPU_GUARD_Q8) {
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
