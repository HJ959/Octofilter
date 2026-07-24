# Octofilter — Requirements (Updated 2026-07-20)

## Overview

Octofilter is an audio effect plugin combining a multi-point stereo filter with per-point panning
and a feedback loop system with pitch shifting. It targets Ableton Live 12 on macOS and Windows.
Built using DPF (DISTRHO Plugin Framework, ISC licence). Distributed as VST3, AU, and CLAP.

---

## Concept

The user configures 2–8 independent filter "points", each with its own filter type, cutoff, pan,
feedback amount, and pitch shift. A single **Texture** knob controls how "open" the effect is —
sweeping from collapsed (mono, all filters at minimum, no spread) to the fully configured state.
Feedback loops with pitch shifting in each point create cascading "boulder head" pitch spirals.

---

## Functional Requirements

### 1. Input / Output

- Accept mono or stereo audio input. Produce stereo output.
- **Mono input**: copied equally to all N filter points.
- **Stereo input**: first half of points receive L, second half receive R, odd centre gets L+R mix.
- Output DC blocker + safety limiter (hard ceiling at 0 dBFS) to prevent ear damage.
- Input Gain and Output Gain controls (±24 dB each).

### 2. Filter Points

- User selectable: **2 to 8 points** (default: 2).
- Each point has:
  - **Filter type** — LP, HP, BP, Notch (integer 0–3).
  - **Cutoff frequency** — set by Texture knob (see §3) with per-point random offset.
  - **Q (Resonance)** — per-point override; if 0, uses global Resonance.
  - **Pan position** — set manually per point, collapsed by Texture.
  - **Level** — per-point output gain (0–200%).
  - **Feedback amount** — per-point, scaled by global Feedback.
  - **Pitch Shift** — per-point (±24 st), global Pitch Shift adds on top as offset.

### 3. Texture (Main Performance Knob)

- **Texture** controls the "openness" of the entire effect:
  - At **0**: all filters collapse to 20 Hz, all pans collapse to centre (mono/unison).
  - At **1**: all points at their fully configured positions (cutoffs, pans, spread).
  - Intermediate values: linear interpolation between collapsed and configured.
- In **Harmonic mode**: Texture scales the fundamental frequency and harmonic spread.
- In **Random mode**: Texture interpolates between 20 Hz and the per-point configured cutoffs.
- Texture is the primary knob for live performance / automation.

### 4. Feedback System

- Per-point feedback loops: each point feeds back into itself (preserves pan/spatial character).
- **Global Feedback** knob (0–1): applied directly to all points at ×1.3 gain factor.
  When per-point feedback is explicitly set, global scales it.
- **DC Blocker** (first-order HPF ~20 Hz) on each feedback path.
- **Per-point peak limiter** (lookahead ~1 ms) at feedback injection point.
- **Global output safety limiter** (0 dBFS ceiling) on final stereo output.

### 5. Pitch Shift in Feedback Path

- Dual-head tape-style pitch shifter in each feedback loop (post-filter, pre-DC block).
- **Global Pitch Shift** (±24 semitones): additive offset applied to all points.
- **Per-point Pitch Shift** (±24 semitones): individual shift per point.
- Effective shift = per-point + global.
- Algorithm: dual read-head with Hann crossfade (1024-sample buffer).
- Pitch shift only audible when feedback > 0 (by design — it's a feedback effect).

### 6. Harmonic Mode

- **Harmonic Mode** toggle (0=Random, 1=Harmonic):
  - **Random mode** (default): per-point cutoff offsets are random semitone values from Randomise.
  - **Harmonic mode**: each point is assigned a natural harmonic of the Texture frequency.
    Harmonic numbers scale with Texture openness.
- **Glide Time** (10ms–2000ms, logarithmic): controls how fast filter cutoffs transition.
  Each point glides independently, creating organic cascading sweeps.
- Glide applies in both modes (smooth transitions whenever cutoffs change).

### 7. Randomise

- **Randomise** button: generates new random values for per-point parameters.
- In **Random mode**: randomises cutoff offsets, filter types, pitch shifts, feedback amounts.
- In **Harmonic mode**: randomises filter types, pitch shifts, feedback amounts (NOT cutoffs).
- Parameter values change internally; host display updates when GUI is implemented (Phase 6).

### 8. Modulation (Stretch Goal)

- LFO assignable to: Texture, per-point Pan, Feedback amount, Pitch Shift amount.
- LFO shapes: sine, triangle, square, sample-and-hold.
- LFO rate: 0.01 Hz – 20 Hz; optionally tempo-synced to host.

---

## Parameter List (Visible in Host)

### Globals (11 visible)
| Parameter | Range | Default | Notes |
|-----------|-------|---------|-------|
| Point Count | 2–8 | 2 | Integer |
| Texture | 0–1 | 0.5 | Main openness knob |
| Resonance | 0.1–20 | 0.707 | Global Q |
| Feedback | 0–1 | 0.3 | Scales all feedback ×1.3 |
| Pitch Shift | ±24 st | 0 | Additive offset to all points |
| Input Gain | ±24 dB | 0 | Pre filter bank |
| Output Gain | ±24 dB | 0 | Post wet/dry |
| Wet/Dry | 0–1 | 1 | |
| Randomise | 0/1 | 0 | Edge-triggered |
| Harmonic Mode | 0/1 | 0 | Toggle |
| Glide Time | 10–2000 ms | 200 | Logarithmic |

### Per-Point × 8 (6 visible each = 48)
| Parameter | Range | Default | Notes |
|-----------|-------|---------|-------|
| Filter Type | 0–3 | 0 (LP) | 0=LP, 1=HP, 2=BP, 3=Notch |
| Q | 0–20 | 0 | 0 = use global Resonance |
| Pan | -1 to 1 | 0 | Collapsed by Texture |
| Level | 0–2 | 1 | |
| Feedback | 0–1 | 0 | Per-point, scaled by global |
| Pitch Shift | ±24 st | 0 | Per-point, global adds on top |

### Hidden (internal)
- Texture Spread (absorbed into Texture)
- Spread (absorbed into Texture)
- Per-point Cutoff Offsets (set by Randomise internally)

---

## Non-Functional Requirements

### Plugin Format
- **VST3** (Windows & macOS), **AU** (macOS), **CLAP** (bonus).
- **Framework: DPF** (ISC licence).

### Performance
- Real-time safe audio thread: no allocations, no locks, no blocking.
- CPU target: < 5% single core at 44.1 kHz / 512 samples, 8 points active.

### Compatibility
- Windows 10/11 (64-bit), macOS 12+ (Apple Silicon + Intel).
- Ableton Live 12 primary target (requires `Options.txt` with
  `-_PluginAutoPopulateThreshold=128` for parameter visibility).

### Licensing
- All deps MIT/ISC/BSD. No GPL/LGPL.
  - DPF: ISC | pffft: BSD | doctest: MIT | Font (UI): OFL

---

## Resolved Design Decisions

| # | Decision |
|---|----------|
| 1 | Framework: DPF (ISC) |
| 2 | Pitch algorithm: dual-head tape shifter (simple, robust, characterful) |
| 3 | Feedback: per-point loops, global knob applies directly |
| 4 | Signal split: equal copies per point; stereo L/R split evenly |
| 5 | Limiting: per-point + global output safety limiter + output DC blocker |
| 6 | Texture: single "openness" knob controlling cutoff + pan + spread |
| 7 | Pitch shift lives in feedback path only (not on dry signal) |
| 8 | Ableton requires Options.txt threshold for >16 params |

---

## Out of Scope (v1)

- MIDI note input or MPE
- Sidechain input
- Mobile targets
- Standalone application mode
- Quantise-to-scale for pitch shift (stretch goal)
