/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 npi3pak */
#ifndef FM_H
#define FM_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FM_OPS 4
#define FM_ALGOS 11
#define FM_VOICES 4

enum { OP_A, OP_B, OP_C, OP_D };

typedef struct {
    int16_t coarse, fine, level;
    int16_t env_a, env_d, env_s, env_r;
    int16_t reserved;
} fm_op_t;

typedef struct {
    int16_t algo;
    int16_t feedback;
    int16_t transpose;
    int16_t reserved[5];
    fm_op_t op[FM_OPS];
} fm_patch_t;

typedef struct {
    int16_t bend_range;
    int16_t fine;
    int16_t channel;
    int16_t volume;
    int16_t reserved[4];
} fm_global_t;

typedef struct {
    uint8_t active;
    uint8_t carriers;
} fm_monitor_t;

void fm_init(void);
void fm_render(const fm_patch_t *p, const fm_global_t *g, int32_t *lr, uint32_t frames);
void fm_note_on(uint8_t note, uint8_t velocity);
void fm_note_off(uint8_t note);
void fm_all_notes_off(void);
void fm_bend(int16_t bend);
void fm_monitor(const fm_patch_t *p, fm_monitor_t *m);
uint8_t fm_algo_mods(int32_t algo, int32_t op);
uint8_t fm_algo_carriers(int32_t algo);

#ifdef __cplusplus
}
#endif

#endif
