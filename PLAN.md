# Octofilter — Implementation Plan

## Build Order Overview

```
Phase 0: Project Setup
    ↓
Phase 1: DSP Core (filters, routing, panning)
    ↓
Phase 2: Feedback System (loop, DC block, limiting)
    ↓
Phase 3: Pitch Shift (phase vocoder, IPitchShifter interface)
    ↓
Phase 4: DPF Plugin Shell (parameters, state saving)
    ↓
Phase 5: UI
    ↓
Phase 6: Integration & Polish
    ↓
Phase 7: Testing & QA
```

---

## Phase 0 — Project Setup

**Goal:** Repo is compilable, CI passes, plugin loads in a DAW as a silent pass-through.

### Tasks
- [ ] Create repo with directory structure: `src/`, `ui/`, `deps/`, `tests/`, `scripts/`
- [ ] Add DPF as a git submodule (`deps/dpf`)
- [ ] Vendor pffft (or KissFFT) into `deps/` — single file, no submodule needed
- [ ] Write top-level `CMakeLists.txt` using DPF's CMake integration
- [ ] Build targets: VST3 (all platforms), AU (macOS only), CLAP (all platforms)
- [ ] Implement a minimal pass-through DPF plugin (audio in → audio out, no processing)
- [ ] Verify plugin loads in Ableton Live 12 (VST3) and passes audio
- [ ] Set up GitHub Actions CI: build matrix (macOS 12+, Windows 10/11), run on push
- [ ] Add `clang-format` config and format check to CI

**Dependencies:** None.

---

## Phase 1 — DSP Core

**Goal:** N filter points each process their assigned input channel through a biquad filter with
correct pan law applied. No feedback yet.

### Tasks
- [ ] Implement `BiQuadFilter` class: LP, HP, BP, notch; Direct Form II transposed; coefficient
  recalculation on cutoff/Q change only (not every sample)
- [ ] Implement `InputRouter`: given N points and input channel count (1 or 2), assign each point
  its source channel per the stereo split rule (L/R/mix)
- [ ] Implement `PointState` struct: holds filter instance, cutoff offset (semitones), filter type,
  pan position, level, feedback amount, pitch shift amount
- [ ] Implement `TextureMapper`: converts Texture knob (0–1) to frequency (20Hz–20kHz,
  logarithmic), applies per-point semitone offset, outputs per-point cutoff Hz
- [ ] Implement equal-power pan law: `L = cos(pan * π/2)`, `R = sin(pan * π/2)`, pan in [-1, 1]
- [ ] Implement `StereoMixer`: accumulates panned per-point outputs into final L/R stereo bus
- [ ] Write unit tests for: BiQuadFilter frequency response at cutoff (approx), InputRouter
  channel assignment, TextureMapper log frequency mapping, pan law energy conservation

**Dependencies:** Phase 0.

**Risk:** Biquad coefficient calculation must be correct at all supported sample rates. Test at
44.1, 48, 88.2, 96 kHz. Watch for coefficient instability at very low cutoffs (<30 Hz) — add a
soft floor.

---

## Phase 2 — Feedback System

**Goal:** Per-point feedback loops run stably. Limiters prevent blowup. DC is blocked.

### Tasks
- [ ] Implement per-point feedback delay buffer (single-sample feedback is a one-sample delay;
  pre-allocate ring buffer sized to max expected pitch-shifter latency)
- [ ] Implement `DCBlocker`: first-order HPF with ~20 Hz corner, one instance per point
- [ ] Implement `PeakLimiter` (lookahead ~1 ms, fast attack <0.1 ms, medium release ~50 ms):
  - Pre-allocate lookahead ring buffer of fixed max size at `activate()` — **no alloc on audio thread**
  - One instance per point + one global instance
- [ ] Wire feedback signal path: point output → DCBlocker → PeakLimiter → feedback buffer →
  mixed with point's next input at configurable feedback amount
- [ ] Add denormal prevention: flush-to-zero on all accumulator variables; or use `_MM_SET_FLUSH_ZERO_MODE`
  on x86 in `activate()`
- [ ] Test feedback stability: ramp feedback to 100%, confirm no blowup; verify limiter engages

**Dependencies:** Phase 1.

**Risk:** Single-sample feedback loops are algebraically problematic — the feedback signal at
sample N depends on the output at sample N, which depends on it. The standard solution is to use
the previous sample's output (one-sample delay), which is what the ring buffer achieves. Document
this explicitly.

---

## Phase 3 — Pitch Shift (Phase Vocoder)

**Goal:** A real-time-safe phase vocoder sits in each per-point feedback path behind a clean
interface. Quality is good from day one; no future algorithm swap needed.

### Tasks
- [ ] Choose FFT backend: **pffft** (BSD, SIMD-optimised, single-file vendored) — preferred for
  performance; fallback to **KissFFT** (BSD, simpler) if pffft causes platform issues
- [ ] Vendor chosen FFT library into `deps/` with licence file
- [ ] Define `IPitchShifter` abstract interface: `prepare(sampleRate, maxBlockSize)`,
  `setShift(semitones)`, `process(float* in, float* out, int numSamples)`, `reset()`
  — keeping the interface means a stub can be used during phases 0–2, and future experimentation
  is isolated behind it
- [ ] Implement `PhaseVocoderShifter : IPitchShifter`:
  - Hann analysis window, **512-sample size** (fast mode, default for feedback use — ~11 ms
    latency at 44.1 kHz); 2048-sample high-quality mode available as a parameter
  - Overlap factor: 4× (hop = window / 4)
  - Phase accumulation with **phase locking** (bin phases locked to nearest peak) to reduce
    phasiness on tonal material and improve the bubbly feedback character
  - Synthesis window: Hann, with correct OLA normalisation
- [ ] Pre-allocate all FFT buffers, overlap buffers, and phase accumulators in `prepare()` —
  **zero allocations on the audio thread**
- [ ] Insert `IPitchShifter` into feedback path: point output → pitch shifter → DCBlocker →
  PeakLimiter → feedback mix
- [ ] Expose per-point pitch shift amount (±24 semitones) and window size toggle as plugin params
- [ ] Test: with feedback=50% and pitch shift=+2 semitones, confirm ascending pitch spiral;
  test at ±12 and ±24 semitones for artefact level
- [ ] Implement `NullShifter : IPitchShifter` (pass-through, zero shift) as the stub used during
  earlier phases and when shift=0 (saves CPU)

**Dependencies:** Phase 2.

**Risk:** Phase vocoder latency in the feedback loop = one analysis window (512 samples ≈ 11 ms
at 44.1 kHz). This affects the timing of the feedback spiral — document as a design characteristic.
The 2048-sample mode adds ~46 ms; make clear in the UI that it changes feedback timing.

---

## Phase 4 — DPF Plugin Shell

**Goal:** All parameters exposed to the host, state saves/restores correctly, Randomise works.

### Tasks

#### Parameters
DPF requires a fixed parameter count declared at compile time. With 8 points × ~7 per-point
params + ~10 global params = ~66 parameters. Declare all 8×N slots; mark inactive point params
as hidden when point count < 8.

- [ ] Define full parameter list (suggested flat index scheme):
  - Global (indices 0–9): PointCount, Texture, TextureSpread, Spread, SpreadShape, Resonance,
    GlobalFeedback, FeedbackLPFreq, WetDry, Randomise (momentary)
  - Per-point × 8 (indices 10–73, 8 params each): FilterType, CutoffOffset, Q, Pan, Level,
    FeedbackAmount, PitchShift, [reserved]
- [ ] Implement `getParameterCount()`, `getParameter()`, `setParameterValue()` in DPF plugin class
- [ ] Implement parameter smoothing for all audio-rate params (one-pole IIR, ~10 ms time constant)
  to prevent zipper noise on automation

#### State Saving
- [ ] Implement `setState()` / `getState()` in DPF: serialise per-point cutoff offsets (the random
  values), RNG seed, and any unlocked pan overrides as a JSON or simple key=value string
- [ ] Thread safety: `setState()` is called from the UI/host thread; use a double-buffer (pending
  state struct + atomic flag), swap at the top of the next `run()` call on the audio thread
- [ ] Randomise button: regenerate per-point cutoff offsets from a new RNG seed, store seed in state

#### Misc
- [ ] Implement `activate()` / `deactivate()` for SoundTouch pre-warming and limiter buffer alloc
- [ ] Implement `sampleRateChanged()` to rebuild filter coefficients and reinitialise SoundTouch

**Dependencies:** Phases 1–3.

**Risk:** DPF's fixed parameter count means unused per-point params must still be declared. Use a
naming convention like `Point 5 Filter Type` and set `PARAMETER_IS_HIDDEN` when point 5 is
inactive. Ableton does not always hide these cleanly — test with 2-point and 8-point configs.

---

## Phase 5 — Harmonic Mode

**Goal:** An optional mode where filter cutoffs are tuned to the natural harmonic series of the
Texture frequency, with independent per-point glide to each new target. Spread controls the
harmonic range. Random mode remains available as a toggle.

### Concept

| Mode | Behaviour |
|------|-----------|
| **Random** (default) | Per-point cutoff offsets are random semitone values, set by Randomise |
| **Harmonic** | Each point is assigned a harmonic of the Texture frequency: point 1 = 1st harmonic (fundamental), point 2 = 2nd harmonic, point 3 = 3rd, etc. Spread scales how far up the series the points reach |

**Harmonic assignment formula:**
```
harmonic[i] = round(1 + i * spreadFactor)
cutoff[i]   = textureFreq * harmonic[i]
```
Where `spreadFactor` is derived from the Spread knob: at Spread=1 all N points span harmonics
1 through N; at Spread=0.5 they cluster in the lower harmonics (1 through N/2); at Spread=0
all points sit at the fundamental.

**Glide:** When Texture changes in Harmonic mode, each point's filter cutoff doesn't jump
instantly — it glides independently to its new target using a one-pole IIR smoother with a
user-controlled glide time. Since higher harmonics are larger intervals, faster-moving harmonics
will reach their target at different times, creating an organic cascading sweep effect.

### Tasks
- [ ] Add `HarmonicMode` enum to `PointState`: `kRandom`, `kHarmonic`
- [ ] Implement `HarmonicMapper` class:
  - `assignHarmonics(points, pointCount, spreadFactor)` — sets each point's target cutoff Hz
    to its harmonic of the current Texture frequency
  - Uses natural harmonic series: `targetHz = textureHz * harmonicNumber`
  - Harmonic numbers derived from spread: `harmonicNumber[i] = 1 + round(i * spread * (N-1))`
- [ ] Implement per-point cutoff glide using one-pole IIR smoother:
  - `currentCutoff` tracks actual filter cutoff (smoothed)
  - `targetCutoff` is the harmonic target (set instantly on Texture change)
  - Glide coefficient computed from `GlideTime` parameter (10ms–2000ms)
  - Each point updates independently each block: `current += coeff * (target - current)`
- [ ] Add parameters:
  - `HarmonicMode` — toggle (0=Random, 1=Harmonic)
  - `GlideTime` — 10ms to 2000ms (logarithmic), only active in Harmonic mode
- [ ] Update `Randomise` behaviour:
  - In **Random mode**: randomises all per-point cutoff offsets, filter types, pan overrides,
    pitch shifts, feedback amounts — everything except buttons and mode toggles
  - In **Harmonic mode**: randomises filter types, pan overrides, pitch shifts, feedback amounts
    but NOT cutoff offsets (those are set by harmonics)
- [ ] Update `OctofilterPlugin::updateFilterParams()` to branch on mode:
  - Random: existing behaviour (Texture + random semitone offset)
  - Harmonic: use HarmonicMapper targets, advanced by per-point glide smoother each block
- [ ] Unit tests:
  - HarmonicMapper assigns correct harmonic numbers at various spread values
  - Glide smoother reaches target within expected time (±10%)
  - Mode switch doesn't cause clicks (glide smooths the transition)

**Dependencies:** Phase 4 (per-point parameter set must exist).

**Risk:** Glide smoothing runs on the audio thread — coefficient must be pre-computed in
`setParameterValue`, not recalculated per sample. Watch for zipper noise on Texture automation
in Harmonic mode; the glide smoother should absorb this naturally.

---

## Phase 6 — UI

**Goal:** Functional dark-theme UI with stereo field visualiser and per-point detail panel.

### Tasks

#### Layout
- [ ] Main panel (min 600×400 px, resizable): stereo field canvas occupying ~60% width,
  global controls strip on the right, per-point detail panel below or as an overlay
- [ ] DPF's NanoVG (built-in) for all custom drawing — no external UI library needed

#### Stereo Field Widget
- [ ] Draw horizontal stereo axis with L/R labels
- [ ] Draw each active point as a filled circle, positioned by its pan value
- [ ] Colour-code by filter type (e.g. LP=blue, HP=red, BP=green, notch=amber)
- [ ] Label each circle with its point number
- [ ] Click a point to select it and show its detail panel
- [ ] Drag a point horizontally to set its pan (unlocks from global spread)
- [ ] Show a subtle vertical bar indicating Texture-mapped cutoff frequency for each point

#### Global Controls
- [ ] Texture knob
- [ ] Spread knob + Linear/Arc toggle
- [ ] Resonance knob
- [ ] Global Feedback knob
- [ ] Feedback LP frequency knob
- [ ] Point count selector (2–8, stepped)
- [ ] Wet/Dry knob
- [ ] Randomise button

#### Per-Point Detail Panel (shown on point selection)
- [ ] Filter type selector (LP/HP/BP/notch)
- [ ] Cutoff offset display (read-only semitone value from randomisation)
- [ ] Q override knob
- [ ] Pan knob (with lock/unlock toggle for global spread)
- [ ] Level knob
- [ ] Feedback amount knob
- [ ] Pitch shift knob (±24 semitones)

**Dependencies:** Phase 4.

**Risk:** NanoVG font rendering requires a bundled font file (.ttf). Include a permissively-licenced
font (e.g. Roboto or Inter) in `ui/fonts/`. DPF handles NanoVG context lifecycle — do not create
or destroy it manually.

---

## Phase 6c — UI Iteration (User Feedback)

**Goal:** Implement UI improvements from first user testing session.

### Tasks

#### Randomise Variations
- [ ] **Randomise All** — replaces current "RANDOM" button in top bar. Randomises ALL parameters
  including globals (Texture, Feedback, Pitch, Resonance, Glide) plus all per-point params.
- [ ] **Per-point Randomise** — small button in the per-point panel (right side). Randomises only
  the currently selected point's params (filter type, cutoff offset, Q, pan, level, FB, pitch).
- [ ] **Global Randomise** — small button in the global strip (bottom). Randomises only global
  params (Texture, Resonance, Feedback, Pitch, Wet/Dry, Glide).

#### Multi-Select Points
- [ ] Ctrl+click on points to add/remove from selection (toggle behaviour)
- [ ] Dragging with multiple points selected moves all selected points together (X=pan, Y=cutoff)
- [ ] Per-point panel shows the first selected point's params (last clicked = "primary" selection)
- [ ] Visual: all selected points get the highlight ring, primary gets a thicker/brighter ring
- [ ] Clicking empty space or clicking without Ctrl deselects all

#### Waveform Display
- [ ] Add a ring buffer in the DSP (size ~2048 samples) that stores recent output L+R mixed
- [ ] UI reads from this buffer each frame and draws an oscilloscope-style waveform
- [ ] Positioned bottom-right of the stereo field as a small inset box (~120×60 px)
- [ ] Styled: dark inset background, waveform drawn in accent colour (purple or gold)
- [ ] Non-interactive (display only)

#### Bug Fix
- [ ] **Wet/Dry glide bug** — Wet/Dry parameter is incorrectly affected by the cutoff glide
  system. Fix: wet/dry should always be applied directly (instant), never glided. Ensure the
  raw `mWetDry` value is used in run() without any smoothing or glide interaction.

**Dependencies:** Phase 6 (core UI complete).

**Effort estimate:** ~1.5 days total (randomise variations: 1hr, multi-select: 4hr, waveform: 4hr, bug fix: 15min).

---

## Phase 6d — User Testing Feedback Fixes

**Goal:** Address bugs, UX issues, and quick wins from first round of user testing (3 users).

### Bugs

- [ ] **Fix Q ceiling / piercing resonance** — when Q is high + feedback is high + pitch shift is
  up, the output becomes dangerously piercing. Fix: apply a soft ceiling to Q that reduces
  automatically when feedback × pitch shift is high. Or: add a secondary limiter specifically on
  the filter resonance output before it enters the feedback path.
- [ ] **Preserve DSP state on bypass** — feedback buffers and pitch shifter state clear when the
  plugin is bypassed, losing the evolved sound character. Fix: don't reset buffers on
  deactivate/bypass. Only reset on explicit user action (Randomise) or project load.
- [ ] **Allow 1 point minimum** — change point count range from 2–8 to 1–8. Enables simpler
  use cases (single filtered feedback with automation).

### UI / UX

- [ ] **macOS UI scaling** — knobs and text are tiny on Mac (Retina). DPF provides
  `getScaleFactor()` — multiply all sizes by this factor. Test on both 1× and 2× displays.
- [ ] **Increase base knob/text sizes** — general 15-20% size bump across all UI elements.
  Reduce empty space, make controls fill the available area better.
- [ ] **Make Randomise more prominent** — larger button, brighter colour, maybe a brief visual
  flash/pulse animation on click to confirm it fired.
- [ ] **Feedback "activation" indicator** — when feedback=0, dim the point nodes or show a subtle
  label/tooltip indicating "increase feedback to hear filter differences". Helps discoverability.
- [ ] **Hover tooltips (stretch)** — an "i" button in the top bar toggles tooltip mode. When
  active, hovering over any knob/button shows a short description of what it does. Low priority
  but high usability value.

### Feature

- [ ] **Single point mode** — point count minimum changed to 1. Ensure the DSP, UI stereo field,
  and per-point panel all work correctly with a single point.

**Dependencies:** Phase 6c complete.

**Effort estimate:** ~2 days total.

**User testing sources:** @cosmojamsoun (User 1), @estero_connor (User 2), @nicholasfaris (User 3)

---

## Phase 7 — Integration & Polish

**Goal:** Plugin sounds good, performs within CPU budget, ships clean.

### Tasks
- [ ] Implement global wet/dry mix (dry = original stereo input, wet = filter bank output)
- [ ] Profile CPU usage at 44.1kHz/512 samples, 8 points, all feedback + pitch shift active.
  Target: <5% single core. Optimise filter inner loop with SIMD if needed (std::valarray or
  manual SSE/NEON); pffft already uses SIMD for FFT
- [ ] Parameter smoothing audit: confirm no zipper noise on Texture, Spread, Feedback sweeps
- [ ] Graceful point count change: when user reduces point count mid-playback, mute removed points
  over ~10 ms to avoid clicks
- [ ] macOS universal binary: confirm both arm64 and x86_64 slices present in plugin (`lipo -info` in CI)
- [ ] AU validation: run `auval -v aufx Octo Henr` and fix any reported issues
- [ ] Ableton Live 12 validation: load, automate all parameters, save/load project, confirm state
  restores correctly

**Dependencies:** Phases 1–5.

---

## Phase 8 — Testing & QA

**Goal:** Confidence in correctness, stability, and performance before release.

### Tasks
- [ ] **Unit tests** (catch2 or doctest, added to CI):
  - BiQuadFilter: frequency response at cutoff ±3 dB
  - InputRouter: all point counts 2–8, mono and stereo
  - TextureMapper: output in valid Hz range, monotonically increasing
  - PeakLimiter: output never exceeds ceiling under impulse input
  - Feedback stability: 1000-sample run at feedback=95%, assert no NaN/Inf
- [ ] **Integration tests**: render 10 seconds of white noise through plugin at each sample rate,
  assert output is finite and within [-1, 1] (with safety limiter active)
- [ ] **Performance regression**: add CI step that builds in Release, runs the render test, and
  fails if wall-clock time exceeds threshold
- [ ] **Manual DAW testing checklist**: automation recording, preset save/load, plugin scan, bypass,
  mono track, stereo track, sidechain track (verify sidechain is correctly unavailable)
- [ ] **AU notarisation** (macOS): set up `xcrun notarytool` in CI for signed builds

**Dependencies:** Phases 0–6.

---

## Phase 9 — Presets, Menu & Distribution

**Goal:** Preset system, settings menu, info screen, and preparation for distribution.

### Tasks
- [ ] **Pop-up menu system** — overlay UI triggered by a menu button (hamburger/gear icon)
- [ ] **Preset browser** — load/save/browse preset files (.octopreset JSON files)
  - Factory presets bundled with the plugin
  - User presets saved to user directory
  - Preset name displayed in top bar
- [ ] **Output limiter control** — optional panel in the menu to adjust safety limiter ceiling,
  enable/disable, and set threshold. For advanced users.
- [ ] **Info screen** — credits, special thanks, email, links to artist/developer pages
- [ ] **Legalities & distribution** — determine licensing for distribution:
  - All dependencies are MIT/ISC/BSD (no GPL issues)
  - Plugin can be distributed as closed-source commercial product
  - Add EULA/licence text to installer
  - Determine pricing model (free / paid / donationware)
- [ ] **Installer** — Windows: NSIS or Inno Setup installer. macOS: .pkg or .dmg.

**Dependencies:** Phase 8 (stable, tested plugin).

---

## Technical Risks Summary

| Risk | Mitigation | Status |
|------|-----------|--------|
| Pitch shifter quality in feedback | Replaced phase vocoder with simpler tape-style dual-head (better for feedback) | ✅ Resolved |
| Per-point feedback instability | One-sample delay + PeakLimiter + DCBlocker on every loop | ✅ Resolved |
| Output DC / ear fatigue | Output DC blockers + output safety limiter (0 dBFS ceiling) | ✅ Resolved |
| DPF fixed parameter count | 69 params declared; unused hidden with `kParameterIsHidden` | ✅ Resolved |
| Ableton doesn't show params | `Options.txt` with `-_PluginAutoPopulateThreshold=128` | ✅ Resolved |
| DPF can't notify host of param changes | Randomise works sonically; visual update needs GUI (Phase 6) | Known limitation |
| `setState()` thread safety | Double-buffer pending state, swap atomically at block start | ✅ Implemented |
| Denormal CPU spikes in feedback | Flush-to-zero mode in `activate()` (SSE/SSE3) | ✅ Implemented |
| Windows NTFS build issues | Build dir on native Linux fs; source in Dropbox | ✅ Resolved |
| DPF BRAND_ID/UNIQUE_ID | Must be bare 4-char tokens (no quotes) | ✅ Resolved |

---

## Milestone Summary

| Phase | Deliverable | Status |
|-------|------------|--------|
| 0 | Pass-through plugin builds and loads | ✅ Complete |
| 1 | DSP core: filters, routing, panning | ✅ Complete |
| 2 | Stable feedback loops with limiting | ✅ Complete |
| 3 | Pitch shift in feedback path | ✅ Complete (tape-style dual-head) |
| 4 | Full parameter set, state save/restore | ✅ Complete (69 params, 59 visible) |
| 5 | Harmonic mode with per-point glide | ✅ Complete |
| 6 | Complete UI | 🔶 In progress (core layout done, iterating) |
| 6c | UI iteration from user feedback | ✅ Complete |
| 6d | User testing feedback fixes | 🔲 Next |
| 7 | Integration, CPU budget, AU/DAW validation | 🔲 |
| 8 | Test suite, QA, release | 🔲 |
| 9 | Presets, menu, distribution | 🔲 |

## Current State (2026-07-24)

Working VST3 plugin with custom NanoVG GUI, deployable to Ableton Live 12 (Windows).

### What's complete:
- **Phases 0–5**: DSP core, feedback, pitch shift, full parameter set, harmonic mode
- **Phase 6 (in progress)**: GUI functional and iterating. Core layout working:
  - Stereo field canvas with draggable point nodes (X=pan, Y=cutoff offset from centre)
  - Point nodes: colour=filter type, size=global×per-point feedback, ring=pitch shift
  - Per-point panel on right side (FILTER: Type/Q/Cutoff, SPATIAL: Pan/FB, MOD: Level/Pitch)
  - Global controls strip at bottom, grouped: FILTER | EFFECT | OUTPUT
  - Point count ±buttons in top-right
  - Bipolar knobs for signed params, unipolar for 0-max params
  - Cutoff knob shows magnitude with zone memory (top/bottom half remembered)
  - Scroll wheel on points controls feedback (size)
  - Blodyn Tatws colour palette applied throughout

### Remaining Phase 6 tasks:
- Randomise button in GUI
- Harmonic Mode toggle in GUI
- Visual polish (consistent spacing, resizing behaviour)
- Double-click to reset knob to default

### Known issues / next steps:
- Texture controls cutoff spread (working) but glide time needs user testing
- Randomise doesn't visually update knob positions (DPF limitation without UI→host notification)
- Per-point params (Type, Q, Cutoff, Pan, Level, FB, Pitch) all interactive via knobs
- Global feedback directly scales per-point feedback for consistent mental model
- Phase 7 (Integration/Polish) and Phase 8 (Testing/QA) remain after Phase 6

### Build setup:
- Source: `C:\Users\Henry\Dropbox\Plugin_ideas\Octofilter` (Dropbox, single source of truth)
- Linux build: `~/dev/Octofilter-build` (Ninja, from Dropbox source)
- Windows build: `build-windows/` folder in Dropbox (MSVC Visual Studio 17 2022)
- Deploy: copy VST3 bundle to `C:\Program Files (x86)\Common Files\VST3\`
- Ableton requires `Options.txt` with `-_PluginAutoPopulateThreshold=128` in latest Live 12.x.x prefs folder
- Close Ableton before deploying new builds (locks the DLL)

### Key technical decisions made during this phase:
- Removed parameter smoothers (were too slow at block-rate, caused wet/dry and texture to not respond)
- Glide system handles cutoff smoothing; all other params are direct/instant
- Point Y-axis = cutoff offset (±24st from centre line), not pitch shift
- Cutoff knob shows magnitude (0–24) with stored zone memory for sign direction
- Global feedback = `mFeedback × 1.3` applied directly; per-point feedback scales on top
- Global pitch shift = additive offset on all points
- Texture = single "openness" knob collapsing cutoffs + pans toward zero/centre
