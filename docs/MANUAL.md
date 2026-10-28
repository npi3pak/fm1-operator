# OPERATOR manual

OPERATOR turns the M-VAVE FM-1 into a polyphonic FM synth inspired by Ableton Live's Operator. It has the same
sections and options, so that Operator presets can be carried over:
- four oscillators A–D and eleven algorithms;
- seven envelopes;
- an LFO, a pitch envelope, a filter and a waveshaper.

Not affiliated with or endorsed by Ableton.

> This is an early version. The pages and the sound engine will change. The algorithm routings are provisional.

![The pages](img/operator-pages.png)

## The screen

The screen is drawn like a Live device.

| Part | What it shows |
|---|---|
| **Title bar** | The preset's name, its number (`U` for user slots) and `*` when it has unsaved changes |
| **HOME** | Four rows, one per oscillator (see below) |
| **Display** (other pages) | At the top: what the page edits, the focused group (`Times 1/3`) and its graph: the waveform, the envelope, the LFO shape, the filter response, the algorithm or the output |
| **Controls** | Below the display, three groups of four: dials, toggles and choosers. The focused group is lighter, with an orange bar and orange dials; K1–K4 edit it |
| **Status line** | Algorithm, voices sounding / available, octave, CPU load |

The oscillators keep Live's colours: A yellow, B cyan, C blue, D orange.

## Controls

| Control | Action |
|---|---|
| **HOME, EDIT, ENV, LFO, FX, SCL, GLO** | Pages. Each page remembers its focus and its object |
| **SELECT** | Moves the focus between the page's groups |
| **ALGO** | The object of the page: the oscillator on EDIT, the envelope on ENV, the MIDI source on GLO. On HOME it picks the algorithm |
| **K1–K4** | The four parameters of the focused group. Turning faster takes bigger steps |
| **PRESET** | Presets: Init and Operator's factory presets (by category: Ambient & Evolving, Bass, Brass, …), then the user slots (`U1`…). They load at once and cut the sound: every voice stops |
| **SAVE** | Opens the save page; press it again there to write |
| **OCT− / OCT+** | Keyboard octave, −3…+3 |
| MASTER | Volume |

System combinations:
- **OCT− + OCT+ for 5 seconds**: UBOOT.
- **OCT− at power-on**: USB RESCUE for reflashing.

## Pages

### HOME: the oscillators

One row per oscillator, like the left half of Operator's shell: D at the top, A at the bottom.
- **Coarse** and **Fine**, or **Freq** and **Multi** when the oscillator is fixed;
- the **Fixed** toggle;
- **Level**;
- the oscillator's square on the right, lit when it is on.

**SELECT** moves down the rows, two steps per oscillator; the lit half of the row is what K1–K4 edit:

| Step | K1 | K2 | K3 |
|---|---|---|---|
| D Tune, C Tune… | Coarse (Freq) | Fine (Multi) | Fixed |
| D Level, C Level… | Level | Oscillator on/off | |

**ALGO** picks the algorithm (1–11) and shows it over the rows for a moment: modulators sit above the oscillators
they modulate, and the carriers feed the line at the bottom. GLO shows it too.

### EDIT: one oscillator (ALGO: A–D)

| Group | K1 | K2 | K3 | K4 |
|---|---|---|---|---|
| TUNE | Coarse | Fine | Level | Wave |
| FIXED | Fixed on | Frequency | Multiplier | Feedback |
| PHASE | Phase | Retrigger | Osc<Vel | Q (quantised) |
| SCALE | Level<Vel | Level<Key | LFO on | Pitch env on |
| PARTIALS | Partial number | Its amplitude | 16/32/64 | Repeat |
| PARTIALS+ | Normalize | | | |

**Ratios**: coarse 0 means 0.5, otherwise 1–48; fine adds thousandths.

**Waveforms**: Sine, Sine 4 Bit, Sine 8 Bit, Saw 3…64, Saw D, Square 3…64, Square D, Triangle, Noise Looped,
Noise White, and User. The numbers count the harmonics.

**User partials**: changing a partial's amplitude switches the oscillator to User.

**Feedback**: works only on an oscillator that no other oscillator modulates.

### ENV: an envelope (ALGO: A, B, C, D, FILTER, PITCH, LFO)

| Group | K1 | K2 | K3 | K4 |
|---|---|---|---|---|
| TIMES | Attack | Decay | Sustain | Release |
| LEVELS | Initial | Peak | End¹ | Time<Vel |
| MODE | Mode | Loop time | Repeat | |
| SLOPES² | Attack slope | Decay slope | Release slope | |

¹ Only the filter, pitch and LFO envelopes have End; the oscillators always fade to silence.
² Only the filter and pitch envelopes have slopes.

**Modes**:
- **Loop** goes back to Initial after the decay while the key is held.
- **Beat** and **Sync** restart the envelope every Repeat. The tempo is GLO → DEVICE → TEMPO.
- **Trigger** ignores the key release.

### LFO

| Group | K1 | K2 | K3 | K4 |
|---|---|---|---|---|
| MAIN | On | Type | Rate | Amount |
| RATE | Range (Low, High, Sync) | Synced rate | Rate<Key | Retrigger |
| DEST A | To A | To B | To C | To D |
| DEST A+ | To the filter | Amount A | Amount<Vel | |
| DEST B | Destination B | Amount B | | |

**Types**: sine, square, triangle, saw up, saw down, sample & hold, noise.

**Range**:
- **Low**: 0.02 Hz … 30 Hz.
- **High**: 8 Hz … 12 kHz, so the LFO can act as a fifth oscillator.
- **Sync**: follows the tempo. The RATE knob then picks a note length.

### FX: filter and shaper

| Group | K1 | K2 | K3 | K4 |
|---|---|---|---|---|
| FILTER | Frequency | Resonance | Type (LP, HP, BP, Notch, Morph) | Envelope amount |
| CIRCUIT | On | Circuit | 12/24 dB | Drive |
| MOD | Morph | Freq<Vel | Freq<Key | LFO on |
| SHAPER | Shaper (Off, Soft, Hard, Sine, 4-Bit) | Drive | Dry/Wet | BP/Notch circuit |

The circuits other than Clean add drive and saturation. Their character is a sketch, not a model of the original.

### SCL: pitch

| Group | K1 | K2 | K3 | K4 |
|---|---|---|---|---|
| PITCH ENV | On | Amount | Amount A | |
| DEST A | To A | To B | To C | To D |
| DEST | To the LFO rate | Destination B | Amount B | Pitch bend range |
| PITCH | Spread | Transpose | Glide | Glide time |

**Spread** plays two voices per note, detuned and panned hard left and right. That costs twice the CPU.

### GLO: global and MIDI routing (ALGO: GLOBAL, VELOCITY, KEY, AFTERTOUCH, PITCH BEND, MOD WHEEL)

On GLOBAL:

| Group | K1 | K2 | K3 | K4 |
|---|---|---|---|---|
| VOICE | Voices (1–32) | Retrigger | Volume | Pan |
| TIME | Time | Time<Key | Time includes attack | Tone |
| PAN | Pan<Key | Pan<Random | Interpolation | Antialias |
| DEVICE | MIDI channel | Tuning | Tempo | |

**Voices**: the FM-1 plays at most 6. A preset may ask for up to 32, as in Operator, and keeps that number, but
the synth plays no more than 6, and fewer when the CPU runs short: a new note then takes the oldest one. The status
line shows how many are available.

**DEVICE**: these settings belong to the FM-1, not to the preset.

On a MIDI source:

| Group | K1 | K2 | K3 | K4 |
|---|---|---|---|---|
| ROUTING | Destination 1 | Amount 1 | Destination 2 | Amount 2 |

**Destinations**:
- **Oscillators**: the volume of A–D, the crossfades A/C and B/D, feedback, the fixed frequency, FM drive (all
  modulators).
- **Filter**: frequency, Q, resonance, morph, drive, envelope amount; the shaper drive.
- **Modulation**: LFO rate and amount, pitch envelope amount.
- **Output**: volume, pan, tone, time.

### SAVE

The rows list the user slots.
- **SELECT** or **PRESET** picks the slot.
- **K1** moves the cursor in the name and **K2** changes the letter.
- **SAVE** writes.

The presets in the firmware cannot be overwritten: an edited one is saved into a user slot. Sound stops for a
fraction of a second while the flash is written.

## MIDI

USB-MIDI and TRS MIDI IN (3.5 mm):
- notes with velocity;
- pitch bend;
- mod wheel (CC 1);
- channel aftertouch;
- All Notes Off (CC 120 / 123).
