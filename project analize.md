# OPERATOR: project analysis

Working notes for the concept of OPERATOR, an FM synth firmware for the M-VAVE FM-1 inspired by Ableton Live's
Operator (not affiliated with or endorsed by Ableton). Three goals from the brief:

1. Understand the original: its history, ideas and structure (Robert Henke's article as the introduction).
2. Use the same colours as Ableton Live.
3. Preset compatibility: Operator presets (`.adv`) will be carried over, so OPERATOR needs every option they use.

Sources:
- R. Henke, "Operator: Twenty Years of FM Synthesis in Ableton Live" (https://roberthenke.com/technology/operator.html).
- Live 12 Reference Manual, 31.9 Operator (https://www.ableton.com/en/manual/live-instrument-reference/).
- Ableton Live 12.4 Suite, installed on this machine:
  - the themes (`Contents/App-Resources/Themes/*.ask`);
  - the 332 factory Operator presets (`Core Library/Devices/Instruments/Operator`);
  - the parameter names compiled into the application.
- The example preset "Air Space Pad" from the brief.

Every fact below is marked as one of:
- **[article]**, **[manual]**: what Henke or the manual says;
- **[presets]**: counted over the 332 factory presets;
- **[binary]**: read from the strings in Live's executable;
- **[guess]**: my inference, to be checked in Live before the code depends on it.

---

## 1. History and ideas

### FM

- **1967, John Chowning** (Stanford, CCRMA) [article]:
  - He discovered FM by accident, while modelling the spatialisation of two sine oscillators.
  - When he raised the vibrato rate beyond anything an acoustic instrument can do, the pitch wobble turned into
    timbre.
- **Stanford patented it and Yamaha licensed it**: the DX7 (1983) followed [article]. The manual dates the start of
  the Yamaha relationship to 1973 [manual].
- **What drives the timbre** [article]:
  - the modulator's amplitude and the carrier : modulator frequency ratio;
  - whole-number ratios give harmonic spectra, fractional ones inharmonic (bells, metal).
- **No filter or VCA is needed**: every oscillator has its own envelope [article].
  - Some oscillators are heard (carriers), the others shape the timbre (modulators).
  - "Lots of classic FM synthesizers create fantastic sounds without using filters at all" [manual].

### Operator (2004)

- **Author** [article]:
  - Robert Henke (Monolake), Ableton co-founder and a long-time Yamaha FM user (DX27, SY77, TG77).
  - He calls hearing Chowning's pieces and meeting a DX7 as a teenager "a quasi-religious moment".
- **Name and goals**: the prototype was called "Onyx" and had three goals [article]:
  - "It should not be heavy on CPU…, it should offer a huge sonic palette, and it should be fun to program."
- **Lineage**: Yamaha SY77, TX81Z and NED Synclavier II [manual].
- **People** [article]: C++ by Matthias Mayrock, UI by Torsten Slama.

**Inside the engine** [article]:
- The oscillators are lookup tables of about **1024 samples** per cycle.
- The modulation is **phase modulation** (cheaper than true FM, and the same thing Yamaha does).
- **Tone** is a low-pass filter on every oscillator's *modulation input*: it tames the aliasing of complex modulators.

**UI**: one context display in the middle shows one section at a time; the "shell" around it holds the main
controls of everything [article, manual]. This is the paradigm to carry over to the FM-1's 240×240 screen (§6).

**Features Henke names as essential** [article]:
- **Time**: one knob scales all seven envelopes. Velocity, key, the LFO and the envelopes can modulate it.
  "Every synth needs this."
- **Spread** (from the Synclavier II): two voices per note, panned hard left and right.
  - All random sources (free-running phases, the LFO's random waves) differ between the two channels.
- **Noise Looped**: a repeating block of noise, for lo-fi game noises. A little FM breaks the repetition.
- **Additive waveforms (2009)**: up to 64 drawn partials per oscillator.
- **Zero Hz mode**: an oscillator can stop and then acts as a waveshaper on its modulation input.
- **Envelope modes**: Loop, Beat, Sync and Trigger (the note length is ignored).
- **Aliasing**: "technically better is not always sounding better".
  - Metallic and cymbal sounds rely on aliasing and sound different at 44.1 and 96 kHz.
  - That is why Interpolation and Antialias are switches, and they are part of the sound.

**What this means for OPERATOR**:
- **Phase modulation through tables** is what our boilerplate already does (`fm.cc`). The sample rate is fixed
  at 48 kHz, so the aliasing will sound one way only.
- **Interpolation and Antialias are not quality settings**: they change the sound. If the presets are to sound
  the same, we must honour both switches, not just pick the best option.
- **The 20th-anniversary preset pack** (Henke and Christian Kleine) is a good listening reference.

---

## 2. The structure of Operator (complete parameter set)

The ranges come from the brief's XML and the factory presets. The units are those stored in the `.adv` file.

### 2.1 Oscillators A–D (`Operator.0` … `Operator.3`)

| XML | Range / units | Meaning |
|---|---|---|
| `IsOn` | bool | oscillator on |
| `WaveForm` | 0…22 | waveform, see §2.2 |
| `Tune/Coarse` | 0…48 | ratio, 0 = 0.5 [manual: whole-number ratio] |
| `Tune/Fine` | 0…1000 | fractional part of the ratio |
| `Tune/FixedFrequencyOn` | bool | fixed mode: ignore the note |
| `Tune/FixedFrequency` | 10…2000 Hz | × Multi |
| `Tune/FixedFrequencyMulti` | 0…5 | [guess] ×0.01, 0.1, 1, 10, 100, 1000 (the manual says down to 0.1 Hz) |
| `Tune/VelCoarseScale` | −100…100 | Osc < Vel: velocity changes the frequency |
| `Tune/VelCoarseScaleQuantized` | bool | Q: only in whole ratio steps |
| `Volume` | 0.000316…1, linear gain | Level; 0.000316 = −70 dB = "−inf" |
| `Feedback` | 0…100 | self-modulation, only for oscillators no other one modulates [manual] |
| `Phase` | 0…100 % of a cycle | start phase |
| `Retrigger` | bool | R: phase restarts on note-on, otherwise free-running |
| `VelScale` | −100…100 | level < velocity |
| `KeyScale` | −100…100 | level < key, centred on C3 |
| `LfoOn` | bool | LFO Dest. A switch for this oscillator |
| `PitchEnvOn` | bool | pitch envelope Dest. A switch |
| `UserHarmonics` | 64 × 0…1, `NumHarmonics` 0…2, `ExpansionMode`, `Normalize` | additive partials. [guess] NumHarmonics = 16/32/64, ExpansionMode = Repeat |
| `Envelope` | see §2.3 | volume envelope |

### 2.2 Waveforms (`WaveForm` 0…22)

The binary holds the names in this order:
- Sine 4 Bit, Sine 8 Bit;
- Saw 3, 4, 6, 8, 16, 32, 64, Saw D;
- Square 3 … 64, Square D;
- Noise Looped, Noise White.

The manual adds Sine, Triangle and User. Together that makes 23, which is exactly the 0…22 range.

[guess] The index order:

| # | Wave | # | Wave | # | Wave |
|---|---|---|---|---|---|
| 0 | Sine | 8 | Saw 32 | 16 | Square 32 |
| 1 | Sine 4 Bit | 9 | Saw 64 | 17 | Square 64 |
| 2 | Sine 8 Bit | 10 | Saw D | 18 | Square D |
| 3 | Saw 3 | 11 | Square 3 | 19 | Triangle |
| 4 | Saw 4 | 12 | Square 4 | 20 | Noise Looped |
| 5 | Saw 6 | 13 | Square 6 | 21 | Noise White |
| 6 | Saw 8 | 14 | Square 8 | 22 | User |
| 7 | Saw 16 | 15 | Square 16 | | |

**22 = User is confirmed [presets]**: 267 of the 299 oscillators set to 22 have drawn partials beyond the first.
The place of Triangle is the weak point of the table.

**How often each waveform is used** [presets, 1328 oscillators]:

| Waveform | Uses |
|---|---|
| Sine | 554 |
| User | 299 |
| #8 (Saw 32) | 61 |
| #19 (Triangle?) | 50 |
| #3 (Saw 3) | 40 |
| any other | 6–30 each |

Every one of the 23 is used somewhere.

The numbered saws and squares are resynthesised from that many harmonics [manual]. For us they are additive
tables, the same mechanism as User. The "D" waves are digital (naive, aliasing) shapes. Sine 4/8 Bit are
quantised sines.

### 2.3 Envelopes (seven: 4 oscillators, filter, pitch, LFO)

| XML | Range / units |
|---|---|
| `AttackTime` | 0.1…20000 ms |
| `DecayTime`, `ReleaseTime` | 1…60000 ms |
| `AttackLevel` (Initial), `DecayLevel` (Peak), `SustainLevel`, `ReleaseLevel` (End) | linear 0.000316…1 for volume and LFO; 0…1 for the filter; −48…48 semitones for pitch |
| `AttackSlope`, `DecaySlope`, `ReleaseSlope` | −1…1 (the curve is editable only on the filter and pitch envelopes) |
| `LoopMode` | 0…4: [guess, order as in the manual] None, Loop, Beat, Sync, Trigger |
| `LoopTime` | 0.2…20000 ms |
| `RepeatTime` | 0…14, a beat-time list (1/48 … 2 bars) for Beat and Sync |
| `TimeVelScale` | −100…100: Time < Vel |

**The volume envelopes' shapes are fixed** [manual]: a linear attack and exponential decay and release. The release
always ends at −inf.

**Loop**: after the decay, the envelope goes back to Initial in LoopTime. A restart begins at the current level, so
it does not click.

**LoopMode in the presets**: 0 ×1154, 2 ×80, 4 ×45, 3 ×26, 1 ×23.

**Beat and Sync need a tempo.** On the FM-1 that means MIDI clock or the internal SEQ/ARP tempo. Without one, Sync
behaves like Beat [manual].

### 2.4 Algorithms (`Globals/Algorithm` 0…10)

There are 11 fixed routings, and signals flow from top to bottom [manual]. The routings themselves are not stored
anywhere as text: they exist only in the icons.

**Usage in the presets**:

| Index | 7 | 10 | 0 | 8 | 6 | 4 | 3 | 9 | 1 | 2 | 5 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Presets | 87 | 54 | 46 | 40 | 20 | 19 | 16 | 16 | 15 | 12 | 7 |

[guess] The first eight look like the TX81Z's (D at the top):
1. D→C→B→A
2. C and D → B → A
3. C→B→A and D→A
4. B→A and D→C→A
5. two pairs, B→A and D→C
6. D → A, B, C
7. D→C with A and B
8. four carriers

Alg. 11 (index 10, the second most used) is probably "four parallel carriers".

**The boilerplate's `MODS` / `CARRIERS` table is provisional.** It must be replaced from a screenshot of Live's
global display before any preset converter is written: one wrong routing breaks every preset that uses it.

**Feedback** applies to every oscillator that nothing modulates [manual]. In our boilerplate it applies only to D,
and that has to change.

### 2.5 LFO (`Lfo`)

| XML | Range |
|---|---|
| `LfoOn` | bool |
| `LfoType` | 0…6. [binary, guess about the order] Sine, Square, Triangle, Saw Up, Saw Down, S&H, Noise (band-pass) |
| `LfoRange` | 0 Low (50 s … 30 Hz), 1 High (8 Hz … 12 kHz), 2 Sync [manual] |
| `LfoRate` | 0…127, the curve is unknown; `LfoRateSynced` 0…14 |
| `LfoRateKeyScale` | 0…1: 1 = follows the note like an oscillator |
| `LfoRetrigger` | bool |
| `LfoAmount` | 0…1, scales A and B; `LfoAmountVelScale` −1…1 |
| Dest. A: `LfoAmountA` | −100…100. The targets are each oscillator's `LfoOn` (pitch) and `Filter/LfoOn` (cutoff) |
| Dest. B: `ModDst/Connection` + `Amount` | the destinations of §2.8 |
| `LfoEnv` | its own envelope: the LFO's amount |
| `LfoPitchEnvOn` | the pitch envelope modulates the LFO's rate |

**The LFO reaches 12 kHz, so it acts as a fifth oscillator** [manual]. With the noise waveform it is the key to
hi-hats and snares.

**Usage** [presets]:
- on in 262 of 332;
- type 0 in 218;
- range Low 262, High 45, Sync 25.

### 2.6 Pitch envelope (`PitchEnv`)

`PitchEnvOn`, an envelope in semitones (−48…48) with slopes, `PitchEnvAmountA` −100…100 to the A–D/LFO switches,
and Dest. B + Amount. It is on in 190 presets.

### 2.7 Filter and Shaper (`Filter`, `Shaper`)

| XML | Range |
|---|---|
| `OnOff` | bool (on in 251 presets) |
| `Type` | 0…4. [manual] LP, HP, BP, Notch, Morph |
| `CircuitLpHp` | 0…4: [manual order] Clean, OSR, MS2, SMP, PRD |
| `CircuitBpNoMo` | 0…1: Clean, OSR |
| `Slope` | bool: 12 / 24 dB |
| `LegacyType` + `LegacyQ` 0.3…10 | the filters before Live 10. [binary] Low/Band/High in 12dB, SVF, Ladder and 24dB variants, plus Notch 12/24 = 14 types (0…13) |
| `Frequency` | 30…18500 Hz |
| `Resonance` | 0…1.25 |
| `Morph` | 0…1 |
| `Drive` | 0…24 dB |
| `FrequencyVelScale` | −100…100 |
| `FrequencyKeyScale` | −200…200 (100 % = an octave per octave, centred on C3) |
| `EnvelopeAmount` | −100…100 (100 % ≈ 9 octaves) + its own envelope (0…1 levels) |
| `LfoOn` | the LFO Dest. A switch |
| `Shaper/Type` | 0…4. [guess] Off, Soft, Hard, Sine, 4-bit. Presets: 0 ×268, 1 ×39, 3 ×17, 2 ×8 |
| `Shaper/Drive` | −12…12 dB |
| `Shaper/DryWet` | 0…100 |

**Puzzle** [presets]: `Type = 4` appears in 302 of 332 presets.
- In those presets `LegacyType` varies across all 14 values, while `LegacyMode` is false everywhere.
- Either the factory library really is "Morph" almost everywhere (unlikely), or `Type` and `LegacyType` work
  together differently.

To check, open two or three presets in Live and see which filter the display shows. Until then, a converter cannot
choose our filter mode.

### 2.8 Modulation destinations (`ModDst/Connection`, MIDI routing) 0…23

The order follows the manual's list. The count matches the range, and the short names in the binary are in the
same order.

| # | Destination | # | Destination |
|---|---|---|---|
| 0 | Off | 12 | Filter Res |
| 1–4 | OSC Volume A–D | 13 | Filter Morph |
| 5 | OSC Crossfade A/C | 14 | Filter Drive |
| 6 | OSC Crossfade B/D | 15 | Filter Envelope Amount |
| 7 | OSC Feedback | 16 | Shaper Drive |
| 8 | OSC Fixed Frequency | 17 | LFO Rate |
| 9 | FM Drive (all modulators' levels) | 18 | LFO Amount |
| 10 | Filter Freq | 19 | Pitch Envelope Amount |
| 11 | Filter Q (Legacy) | 20 | Volume |
| | | 21 | Panorama |
| | | 22 | Tone |
| | | 23 | Time |

**MIDI routing (`MidiCtrl`)**:
- **Sources and destinations**: `KeyDst`, `VelDst`, `MidiCtrl.0`, `MidiCtrl.1`, `MidiCtrl.2`, with two
  destinations and an Amount each. `NeutralKey` = 60.
- **Sources** [manual]: Velocity, Key, Aftertouch, Pitch Bend and Mod Wheel. [guess] `MidiCtrl.1` is the pitch bend:
  it is used only 6 times, and the bend's main target is fixed by PB Range.
- **Which of `.0` and `.2` is the wheel and which the aftertouch is unknown.**
- **Usage**:
  - `.0` → LFO Amount (92), Filter Freq (85);
  - `.2` → Time (112), Filter Freq (88);
  - velocity → Crossfade A/C (17), Volume (16);
  - key → Tone (10).

### 2.9 Global (`Globals`, `EnvScale`)

| XML | Range | Note |
|---|---|---|
| `NumVoices` | an index, 0…12 in presets | [guess] 1,2,3,4,5,6,8,10,12,14,16,24,32. The default preset has 5 (= 6 voices?). Presets: 5 ×123, 0 ×74 (mono!), 7 ×31, 9 ×29 |
| `RetriggerMode` | bool | the same note retriggers instead of taking a new voice |
| `Algorithm` | 0…10 | |
| `Interpolation`, `AntiAlias` | bool | presets: interpolation on 240, antialias on only 129 of 332 |
| `Transpose` | −48…48 | |
| `PitchBendRange` | 0…24 | |
| `Volume` | 0.000316…1.995 linear (up to +6 dB) | |
| `Panorama` | −1…1 | |
| `PanoramaKeyScale` | 0…100 | |
| `PanoramaRndScale` | 0…100 | |
| `Tone` | 0…1 | default 0.7 |
| `SpreadAmount` | 0…100 | **used in 176 of 332 presets** |
| `PortamentoOn` | bool | Glide, 61 presets |
| `PortamentoTime` | 0.1…10000 ms | |
| `EnvScale/EnvTime` | −100…100 | the global Time, 66 presets |
| `EnvTimeKeyScale` | −100…100 | |
| `EnvTimeIncludeAttack` | bool | |
| `PerNotePitchBendOn` (MPE), `LegacyMode`, `ModulationResolution` | | MPE is not relevant to the FM-1; the other two are constant in presets |

### 2.10 The example preset "Air Space Pad"

| Setting | Value |
|---|---|
| Algorithm | index 8 |
| Waveforms | A, B and C are #8 (Saw 32?), D is Sine |
| Levels | A −10.5 dB, B −19.7 dB, C −11.4 dB, D −27.7 dB |
| Ratios | A 0.5 with fine 992, B 1.000, C 1.005, D 1.028 (detuned layers) |
| Envelopes | A, B and C have release 19.5 s. D has decay 23 ms to 0.83 and sustain 0.4, which is a short "chiff" on the attack |
| Filter | 593 Hz, resonance 0.15, filter envelope +22 % |
| LFO | sine, rate 88.7/127 (Low range), to A–D and the filter (Dest. A 100 %), amount 0.27 |
| Spread | 87 |
| Pitch envelope | +12 → −3 → 0 semitones over 600 ms, but `PitchEnvAmountA` = 100 with `EnvelopeAmount` 0 |
| Voices | index 4, Tone 0.7 |
| Mod wheel? | `MidiCtrl.0` → LFO Amount 100 % |

A typical "subtractive" Operator patch: saw layers with detune, a filter and Spread. These presets need the filter,
Spread and the LFO; four sines would not be enough.

---

## 3. Ableton Live's colours

Taken from Live 12.4's themes (`.ask`).

**The oscillator colours are the same in all 18 themes**:

| Token | Hex | RGB565 | Role |
|---|---|---|---|
| `Operator1` | `#e0d825` | `0xE6C4` | yellow |
| `Operator2` | `#29d6cd` | `0x2EB9` | cyan |
| `Operator3` | `#6571f6` | `0x639E` | blue-violet |
| `Operator4` | `#f3751b` | `0xF3A3` | orange |

[guess] Operator1–4 are oscillators A–D. Check this against a screenshot of Live.

**The display ("Retro Display") in the default dark theme** (Default Dark Neutral Medium):

| Token | Hex | Role for us |
|---|---|---|
| `RetroDisplayBackground` | `#181818` | screen background |
| `RetroDisplayBackgroundLine` | `#3e3e3e` | grid, dividers |
| `RetroDisplayForeground` | `#ffad56` | main curves and values (Live's orange) |
| `RetroDisplayForeground2` | `#03c3d5` | second curve, bipolar values |
| `RetroDisplayHandle2` | `#ff697f` | the selected point |
| `RetroDisplayScaleText` | `#868686` | scales, secondary text |
| `RetroDisplayTitle` | `#b5b5b5` | headings |
| `ControlForeground` | `#b5b5b5` | text |
| `TextDisabled` | `#757575` | off, disabled |
| `SurfaceBackground` | `#363636` | panels around the display ("shell") |
| `ChosenAlert` | `#e76942` | warnings (CPU, SAVE) |

The light themes swap the accent for `#ffb901` with `#00eeff`.

**For the FM-1**:
- Use the dark theme.
- Colour each oscillator everywhere with its own colour: the algorithm diagrams, the ENV page and the level bars.
- The current logo and pages use different colours, so they need repainting.

Colours are not trademarks, but the rule stays: no Ableton name or logo in the product's look.

---

## 4. The hardware: what we have to work with

| | |
|---|---|
| Chip | JieLi AC79 (pi32v2). The FPU is used only by C++. The audio ISR has 256 frames per 5.3 ms at 48 kHz |
| Screen | 240×240 RGB565 (ST7789), drawn in strips |
| Encoders | SELECT, ALGO, PRESET, K1–K4, and a MASTER pot |
| Buttons | FX, SCL, ENV, LFO, EDIT, GLO, HOME, SAVE, ARP, SEQ, PLAY, REC, OCT−, OCT+ |
| Keyboard | 27 keys |
| MIDI | USB-MIDI and TRS (notes, bend, CC; aftertouch and the mod wheel only arrive from outside) |
| Preset storage | **2 slots × 3840 bytes** (`FW_STORE_SLOTS`, `FW_STORE_MAX`). Today the bank holds 16 small presets |

**Size of a full preset**:
- About 4 × 30 oscillator parameters and 7 envelopes × 14 fields.
- About 60 LFO, filter, global and MIDI parameters.
- Stored as int16, that is about 600–700 bytes.
- User partials add 4 × 64, as 8 bits each, which is +256 bytes.

**About 1 KB per preset means only 6–7 presets fit in 7.5 KB.** Options:
- **(a)** Read-only presets in the firmware image (flash is plentiful, the image is ~110 KB now) plus a few user
  slots.
- **(b)** Compact encoding (8-bit wherever the step allows) plus partials only for User waveforms.
- **(c)** More storage. That is a change to the ORBIT `storage.c`, and it needs a reason and a check on hardware.

**Decided (2026-10-28): (a).**
- **The read-only bank** is compiled into the firmware from a list the user will provide.
- **The user slots** live in the two storage slots, as many as the preset format allows.
  - Today that is ~7 at ~1 KB each.
  - Applying (b) on top raises the count without touching `storage.c`.

---

## 5. Preset compatibility: what it costs

### 5.1 Principle

**Store everything, reproduce as much as possible.**
- **The preset format**: OPERATOR's preset format holds *all* Operator parameters in its own units (ms, dB, Hz,
  semitones, %).
- **The engine**: can render some parameters approximately (for example the Cytomic filter circuits through a single
  SVF/ladder).
- **The data**: is not lost, and the sound improves without converting the presets again.

### 5.2 Converter

**`tools/adv2op.py`**:
- `.adv` (gzip + XML) → our binary format, with a check of ranges and of every enum.
- The same parser in JS on the web page: drop an `.adv`, get the preset on the device.

**The web protocol has no preset upload yet.** It has to be added (sys/usb.c and the installer).

**The legal side**:
- Converting *your own* presets is fine.
- **Ableton's factory library** (Core Library, packs) is Ableton's content: do not ship it in the public firmware
  or on the site.
- Factory presets for OPERATOR must be our own.

### 5.3 Features by cost (CPU on the AC79)

| Feature | Cost | Need for compatibility |
|---|---|---|
| 4 oscillators, phase modulation, 11 algorithms, feedback | base | required |
| 23 waveforms (tables), User partials, Phase, Retrigger | memory: 23 × 1024 × int16 ≈ 47 KB, or generate in RAM on load | required, every waveform is used |
| Fixed frequency, Osc < Vel, Vel/Key scale | cheap | required |
| Envelope loop, beat, sync and trigger modes, slopes, Time/Time<Key/Time<Vel | cheap (per block) | required |
| LFO with 7 shapes, up to 12 kHz | the audio-rate LFO is per sample | required |
| Pitch envelope | per block | required |
| Filter: 5 types, 12/24 dB, Morph, Drive | one SVF/voice ≈ +1 oscillator; 24 dB ×2 | required (251 presets) |
| Circuits OSR/MS2/SMP/PRD, Legacy filters | expensive or approximated | approximate |
| Shaper | cheap | required |
| Spread | **×2 voices** | used in 176 presets: a stereo pair or cheap detune within one voice |
| Glide, Retrigger, mono/legato | cheap | required |
| Antialias, Interpolation | antialias = oversampling or band-limited tables | honour the switches as far as possible |
| Voices up to 32 | linear in CPU | decided: as in the original, see below |
| MPE, per-note bend | | not needed |

**Nothing is measured yet**: the CPU usage on the hardware is unknown.
- The first prototype milestone: 4 oscillators × N voices + filter, measuring CPU % in the status line.
- From that, decide whether Spread is a true second voice.

**Polyphony, decided (2026-10-28): as in the original.**
- `Voices` is a preset parameter: 1–32, the same menu as Operator. Voices = 1 plays mono/legato. With Retrigger, a
  repeated note reuses its voice.
- When more notes are asked for than `Voices`, the oldest is cut, as in Operator.
- Whether the AC79 can actually render 32 four-operator voices with the filter is unknown until it is measured.
  - The engine keeps the preset's number and renders as many voices as fit in the audio budget.
  - Under overload it steals the oldest voice (a hard limit `FM_VOICES_MAX` plus the CPU guard) instead of
    dropping the audio block.
  - The status line shows the real number of voices sounding.

---

## 6. UI on the FM-1

Decided (2026-10-28): the interface is split into pages. There are far more parameters than knobs, so the
**SELECT** encoder moves the focus between groups of parameters.

### 6.1 Controls (the same on every page)

| Control | Role |
|---|---|
| Page buttons | HOME, EDIT, ENV, LFO, FX, SCL, GLO, SAVE pick the page. Every page keeps its own focus and object |
| **SELECT** | **Focus**: which group of four parameters K1–K4 edit. The focused group is lit in orange (`#ffad56`); the others are dim |
| **ALGO** | **Object**: what the page edits. Oscillator A–D, which envelope, which MIDI source. On HOME it is the algorithm |
| **K1–K4** | The four values of the focused group. Switches turn on above the knob's middle; choosers step through their list |
| **PRESET** | Presets: the read-only bank, then the user slots. On SAVE it picks the slot |
| OCT− / OCT+ | Octave of the built-in keyboard |
| MASTER | Volume (the hardware pot, outside the preset) |

The knobs are encoders, so a value does not jump when the focus changes. K1–K4 always start from the stored value.

**Screen (240×240) on every page**:
- **The header**: page name and object. The object is coloured with its oscillator's colour: A–D use Operator1–4,
  and the filter, pitch and LFO use orange and cyan.
- **A list of the page's groups**: a row of four cells each, with name and value. The focused row is highlighted.
  When the groups do not fit, the list scrolls with the focus.
- **A graphic of the object**:
  - the algorithm diagram;
  - the envelope curve, with the current stage marked;
  - the waveform or partials;
  - the filter response;
  - the LFO shape.
- **The status line**: voices sounding / `Voices`, preset (`*` = changed), octave, CPU %.

### 6.2 Pages and groups

Every Operator parameter from §2 has a place. "—" marks a free knob.

**HOME: the shell.** ALGO = algorithm 1–11, drawn as a diagram in the oscillator colours.

| Group | K1 | K2 | K3 | K4 |
|---|---|---|---|---|
| Level | A | B | C | D |
| Coarse | A | B | C | D |
| Fine | A | B | C | D |
| Wave | A | B | C | D |
| On | A | B | C | D |

**EDIT: an oscillator.** ALGO = A / B / C / D.

| Group | K1 | K2 | K3 | K4 |
|---|---|---|---|---|
| Tune | Coarse | Fine | Level | Wave |
| Fixed | Fixed on | Freq | Multi | Feedback |
| Phase | Phase | Retrigger | Osc<Vel | Q |
| Scale | Level<Vel | Level<Key | LFO on | Pitch env on |
| Partials | Harmonic # | Amplitude | 16/32/64 | Repeat |
| Partials+ | Normalize | All/Even/Odd | — | — |

**ENV: an envelope.** ALGO = A / B / C / D / FILTER / PITCH / LFO.

| Group | K1 | K2 | K3 | K4 |
|---|---|---|---|---|
| Times | Attack | Decay | Sustain | Release |
| Levels | Initial | Peak | End¹ | Time<Vel |
| Mode | Mode | Loop time | Repeat | — |
| Slopes¹ | A.Slope | D.Slope | R.Slope | — |

¹ Only the filter, pitch and LFO envelopes have End; only the filter and pitch envelopes have slopes. The groups
are hidden where they do not apply.

**LFO.**

| Group | K1 | K2 | K3 | K4 |
|---|---|---|---|---|
| Main | On | Type | Rate | Amount |
| Rate | Range | Synced rate | Rate<Key | Retrigger |
| Dest A | to A | to B | to C | to D |
| Dest A+ | to Filter | Amount A | Amt<Vel | — |
| Dest B | Destination | Amount B | — | — |

**FX: filter and shaper.**

| Group | K1 | K2 | K3 | K4 |
|---|---|---|---|---|
| Filter | Freq | Res | Type | Env amount |
| Circuit | On | Circuit | 12/24 dB | Drive |
| Mod | Morph | Freq<Vel | Freq<Key | LFO on |
| Shaper | Type | Drive | Dry/Wet | — |

**SCL: pitch.**

| Group | K1 | K2 | K3 | K4 |
|---|---|---|---|---|
| Pitch env | On | Amount A | Destination B | Amount B |
| Dest A | to A | to B | to C | to D |
| Pitch | to LFO | Spread | Transpose | — |
| Glide | Glide on | Glide time | PB range | — |

**GLO: global and MIDI routing.** ALGO = GLOBAL / VEL / KEY / AFTERTOUCH / PITCH BEND / WHEEL.

On GLOBAL:

| Group | K1 | K2 | K3 | K4 |
|---|---|---|---|---|
| Voice | Voices | Retrigger | Volume | Pan |
| Time | Time | Time<Key | Incl. attack | Tone |
| Pan | Pan<Key | Pan<Rnd | Interpolation | Antialias |
| Device² | MIDI channel | Fine tune | — | — |

On a MIDI source:

| Group | K1 | K2 | K3 | K4 |
|---|---|---|---|---|
| Routing | Destination 1 | Amount 1 | Destination 2 | Amount 2 |

² Device settings are saved apart from the presets, as now.

**SAVE.** PRESET picks a user slot. SAVE writes the preset; a second press confirms.

The read-only bank cannot be overwritten. Saving an edited bank preset puts it into a user slot.

ARP, SEQ, PLAY and REC are free for now. Later they could drive a tempo for the Beat/Sync modes and an arpeggiator.

### 6.3 Questions for the design

- **A shortcut to one oscillator**: should holding a page button and turning ALGO jump the object directly? For
  example, ENV + ALGO to go straight to the A envelope.
- **Acceleration**: K1–K4 change the stored value relatively, with the acceleration `app.c` already has
  (`knob_accel`). The same is needed for long ranges such as 0.1…20000 ms and 30…18500 Hz, which should change along
  a log curve.

---

## 7. Roadmap

1. **Pin down the facts** (questions in §8): the algorithm routings, the order of the enums, the filter puzzle,
   MidiCtrl.0/2.
2. **Data model**: `op_patch_t` with every parameter, a converter `adv2op.py`, and a test that converts all 332
   factory presets locally, *without committing them*, and checks the ranges.
3. **Engine, core**:
   - the real algorithms;
   - feedback for every unmodulated oscillator;
   - the 23 waveforms and User;
   - envelopes with their modes;
   - the global Time.
4. **Measure the CPU on the FM-1** and choose the voice count.
5. LFO, pitch envelope, filter + shaper, Spread, Glide, MIDI routing.
6. **UI pages** in Live's colours; repaint the logo.
7. Upload presets from the web page; our own factory presets.
8. **Listening comparisons with Live**: render the same preset in Live and on the FM-1 (sim / recording).

---

## 8. Open questions

**Things to check in Live** (5 minutes each):
1. **The 11 algorithm routings**: a screenshot of the global display (the icons).
2. **The waveform order**: in a new Operator, set A's Wave to Triangle, save, and see the `WaveForm` number. The
   same check works for LFO Type, Shaper and LoopMode (Trigger).
3. **Filter**: in a preset with `Type = 4` (for example "Air Space Pad"), see what Live shows: Morph or something
   else.
4. **Mod wheel vs Aftertouch**: which one is `MidiCtrl.0` and which `MidiCtrl.2`.
5. **The `NumVoices` index → number of voices** (the menu in the global display).
6. **Operator1–4 colours = A–D?** (a screenshot of the shell).

**Decided (2026-10-28)**:
- **Preset storage**: a read-only bank in the firmware (the list comes from the user) plus user slots (§4).
- **Polyphony**: as in the original: `Voices` 1–32 per preset (§5.3).
- **UI**: pages, with SELECT moving the focus between groups of four parameters (§6).

**Still open**:
7. **The read-only preset list.**
   - Ableton's factory presets must not go into the public firmware (§5.2).
   - Presets made by the user themselves can.
8. **Spread**: a true stereo voice pair (twice the CPU) or a cheaper imitation.
9. **The version and identity**: v1.0 → FM-1_910, so as not to coincide with GATOR's v0.x.
10. **The language of this file**: the repository's rule is English everywhere; tell me if you want it in Russian.

---

## 9. Implementation status (2026-10-28)

Decided:
- **Spread** is a true stereo pair (expensive). It gets cut down later if the hardware cannot keep up.
- **The preset name** sits at the top, in a title bar styled like a Live device.

### Done

**Data model** (`firmware/apps/operator/fm.h`)
- `fm_patch_t` (792 B) holds every Operator parameter.
- `tools/oppreset.py` converts `.adv` files and our own JSON presets.
- All 331 factory presets convert without a value outside the ranges.
- INIT matches Live's default Operator exactly.

**Bank**
- `presets/bank.txt` lists 8 presets of our own; `gen.py` compiles them into the firmware.
- There are 8 user slots: 4 in each storage slot.

**Engine** (`fm.cc`)
- 23 waveforms plus User, built from the partials.
- 11 algorithms, with feedback on every oscillator nothing modulates.
- Fixed frequency, Osc<Vel with Q, Level<Vel and Level<Key, Phase and Retrigger.
- 7 envelopes with Loop, Beat, Sync and Trigger, slopes, and Time, Time<Key and Time<Vel.
- An LFO running per sample (7 shapes, Low, High and Sync ranges, its own envelope).
- The pitch envelope.
- The filter: a TPT SVF with LP, HP, BP, Notch and Morph, 12 and 24 dB, and drive.
- The shaper.
- Spread, glide, mono/legato with a note stack, Retrigger, and voices 1–32 stolen oldest first.
- MIDI routing: five sources × two destinations, plus LFO B and pitch-envelope B, onto 24 destinations.
- The voice limit follows the CPU load (`adapt_voices`).

**UI**
- 7 pages of four-parameter groups, as planned in §6.
- The title bar is styled like a Live device: activator, name, slot, `*`.
- Live's colours throughout.
- Graphics: the algorithm, the waveform, the envelope, the LFO, the filter and the scope.
- The SAVE page can rename the preset.

### Checked (host only)

- The simulator under ASan/UBSan.
- All 331 factory presets played through the engine locally: no findings and no clipping. The bank was not
  committed.
- The target build: image 141 KB, RAM 15 KB of 98 KB, pool 145 KB of 344 KB, no soft-float.

### Not checked

- **Sound and CPU on the FM-1**: nothing has run on the hardware yet.
- **Matching the sound to Live by ear**: the depth constants at the top of `fm.cc` are guesses.

### Next

1. The algorithm routings from a Live screenshot (§8.1). The enum orders (§8.2–8.5).
2. On the FM-1: CPU per voice, with and without Spread and the filter.
3. Calibrate the depths against Live: render the same preset in both.
4. The Repeat partials, the Antialias switch, the filter circuits' character, the legacy filter types.
5. Upload presets from the web page.
6. Repaint the logo in Live's colours.

### UI restyled after the first review (2026-10-28)

**Feedback**: the UI was "beautiful, but not in the Ableton style at all".

**HOME**
- Grey device body, four rows of oscillators.
- Each row: the oscillator square, a Coarse dial, a Fine dial, the Fixed toggle, Level with a bar.
- A fixed oscillator shows Freq and Multi instead of Coarse and Fine, as in Live.
- ALGO shows the algorithm over the rows.

**The other pages**
- The display with its graph is at the top: envelopes, LFO, filter, wave.
- Below it are Live-style controls: dials with an arc, orange toggles, choosers.

**Font**: IBM Plex Sans Medium (SIL OFL 1.1), 11 and 13 px, for labels, values and the name.
