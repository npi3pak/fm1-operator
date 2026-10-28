# OPERATOR manual

OPERATOR turns the M-VAVE FM-1 into a polyphonic FM synth inspired by Ableton Live's Operator: four sine operators
(A–D), eleven algorithms that route them into each other, and an envelope for every operator. Not affiliated with or
endorsed by Ableton.

> This is an early version: the pages and the sound engine will change.

![The pages](img/operator-pages.png)

## Buttons

| Button | Action |
|---|---|
| **HOME** | the main page: the four operators' levels and the algorithm |
| **EDIT** | OSC: the selected operator's ratio and level |
| **ENV** | the selected operator's envelope |
| **GLO** | global settings |
| **SAVE** | the save page; press again to write the preset |
| **OCT− / OCT+** | keyboard octave −3…+3 |
| **PRESET** encoder | presets 1–16 on any page but SAVE (loaded at once) |
| MASTER | volume |

System combinations: **OCT− + OCT+ for 5 seconds**: UBOOT; **OCT− at power-on**: USB RESCUE for reflashing.

## Pages

### HOME

K1–K4: the levels of operators A–D. Each row says whether the operator is a carrier (heard) or a modulator in the
current algorithm. **SELECT**: the algorithm (1–11), **ALGO**: the feedback of operator D.

### OSC

**SELECT** picks the operator (A–D). K1: COARSE (the frequency ratio, 0.5 and 1–48), K2: FINE (adds 0–1.000 to
the ratio), K3: LEVEL.

### ENV

**SELECT** picks the operator. K1–K4: ATTACK, DECAY, SUSTAIN, RELEASE (1 ms … 10 s).

### GLO

TRANSPOSE (saved with the preset), BEND RANGE, FINE TUNE, MIDI CH (OMNI or 1–16).

### SAVE

16 slots in flash. **SELECT** picks the slot, SAVE writes. The status line's `P3*` marks unsaved changes. Sound
stops for a fraction of a second while the flash is written.

## MIDI

USB-MIDI and TRS MIDI IN (3.5 mm): notes with velocity, pitch bend, All Notes Off (CC 120 / 123).
