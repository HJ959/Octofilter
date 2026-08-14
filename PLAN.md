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
Texture frequency. Spread controls the harmonic range. Random mode remains available as a toggle.

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

### Tasks
- [ ] Add `HarmonicMode` enum to `PointState`: `kRandom`, `kHarmonic`
- [ ] Implement `HarmonicMapper` class:
  - `assignHarmonics(points, pointCount, spreadFactor)` — sets each point's target cutoff Hz
    to its harmonic of the current Texture frequency
  - Uses natural harmonic series: `targetHz = textureHz * harmonicNumber`
  - Harmonic numbers derived from spread: `harmonicNumber[i] = 1 + round(i * spread * (N-1))`
- [ ] Add parameter:
  - `HarmonicMode` — toggle (0=Random, 1=Harmonic)
- [ ] Update `Randomise` behaviour:
  - In **Random mode**: randomises all per-point cutoff offsets, filter types, pan overrides,
    pitch shifts, feedback amounts — everything except buttons and mode toggles
  - In **Harmonic mode**: randomises filter types, pan overrides, pitch shifts, feedback amounts
    but NOT cutoff offsets (those are set by harmonics)
- [ ] Update `OctofilterPlugin::updateFilterParams()` to branch on mode:
  - Random: existing behaviour (Texture + random semitone offset)
  - Harmonic: use HarmonicMapper targets applied directly (instant)
- [ ] Unit tests:
  - HarmonicMapper assigns correct harmonic numbers at various spread values
  - Mode switch doesn't cause clicks

**Dependencies:** Phase 4 (per-point parameter set must exist).

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
  including globals (Texture, Feedback, Pitch, Resonance) plus all per-point params.
- [ ] **Per-point Randomise** — small button in the per-point panel (right side). Randomises only
  the currently selected point's params (filter type, cutoff offset, Q, pan, level, FB, pitch).
- [ ] **Global Randomise** — small button in the global strip (bottom). Randomises only global
  params (Texture, Resonance, Feedback, Pitch, Wet/Dry).

**Dependencies:** Phase 6 (core UI complete).

**Effort estimate:** ~1 hour total (randomise variations: 1hr, bug fix: 15min).

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

**Goal:** Plugin performs within CPU budget, sounds clean under automation, builds on all platforms.

### Tasks
- [x] ~~Implement global wet/dry mix~~ (done in Phase 2)
- [x] ~~macOS universal binary~~ (confirmed in CI: arm64 + x86_64)
- [ ] Profile CPU usage at 44.1kHz/512 samples, 8 points, all feedback + pitch shift active.
  Target: <5% single core. Optimise filter inner loop with SIMD if needed.
- [ ] Parameter smoothing audit: check for zipper noise on Texture, Feedback, Pitch automation.
  Currently all globals are direct (no smoothing) — add per-sample interpolation if needed.
- [ ] Graceful point count change: when user reduces point count mid-playback, mute removed points
  over ~10 ms crossfade to avoid clicks.
- [ ] Update DPF submodule to latest (fixes Windows CI OpenGL header issue).
- [ ] Re-enable Windows CI build after DPF update.
- [ ] AU validation: run `auval -v aufx Octo Hnry` on macOS and fix any reported issues.
- [ ] Ableton Live 12 validation: load, automate all parameters, save/load project, confirm state
  restores correctly. Test on both Windows and macOS.
- [ ] Test single-point mode (1 point) works correctly in all DAWs.
- [ ] Verify Q ceiling doesn't over-limit in normal use cases (musical Q values).

**Dependencies:** Phase 6d complete.

---

## Phase 8 — Testing & QA

**Goal:** Confidence in correctness, stability, and performance before release.

### Tasks
- [x] ~~Unit tests for BiQuadFilter, InputRouter, TextureMapper, PeakLimiter, Feedback stability~~
  (done in Phases 1–3, all passing)
- [ ] **Expand unit tests**:
  - HarmonicMapper: harmonic assignment at all spread values
  - Q ceiling: verify output stays below 0dBFS at high feedback+pitch+Q
  - PhaseVocoderShifter: pitch up/down produces finite output at all settings
  - Single-point mode: DSP works with pointCount=1
- [ ] **Integration tests**: render 10 seconds of white noise through plugin at each sample rate
  (44.1, 48, 88.2, 96 kHz), assert output is finite and within [-1, 1]
- [ ] **Performance regression**: CI step that builds Release, runs render test, fails if
  wall-clock time exceeds threshold (< 200ms for 10s of audio at 44.1kHz)
- [ ] **Manual DAW testing checklist**:
  - Ableton Live 12 (Windows + macOS): automation, preset save/load, plugin scan, bypass
  - Logic Pro (macOS): AU format, automation, save/load
  - REAPER (Windows): VST3 + CLAP, parameter display, automation
  - Test on mono track, stereo track
- [ ] **AU notarisation** (macOS): set up Apple Developer account, `xcrun notarytool` in CI
- [ ] **Crash testing**: rapid parameter changes, fast point count toggling, extreme values

**Dependencies:** Phase 7 complete.

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
| DPF fixed parameter count | 68 params declared; unused hidden with `kParameterIsHidden` | ✅ Resolved |
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
| 4 | Full parameter set, state save/restore | ✅ Complete (68 params, 58 visible) |
| 5 | Harmonic mode | ✅ Complete |
| 6 | Complete UI | 🔶 In progress (core layout done, iterating) |
| 6c | UI iteration from user feedback | ✅ Complete |
| 6d | User testing feedback fixes | ✅ Complete |
| 7 | Integration, CPU budget, AU/DAW validation | 🔲 Next |
| 8 | Test suite, QA, release | 🔲 |
| 9 | Presets, menu, distribution | ✅ Complete |

## Current State (2026-08-14)

Working VST3/CLAP plugin with custom NanoVG GUI, deployable to Ableton Live 12 (Windows).
macOS universal binary (arm64 + x86_64) built via GitHub Actions CI.

### What's complete:
- **Phases 0–6d**: DSP core, feedback, pitch shift, full parameter set, harmonic mode, complete UI with all user feedback addressed
- **Phase 7 (in progress)**: Integration & polish
  - ✅ CPU benchmark test added (8 points worst-case)
  - ✅ Parameter smoothing: Texture, Feedback, Pitch, WetDry, Gains all smoothed per-block
  - ✅ Graceful point count changes (10ms fade in/out)
  - ✅ Q ceiling verified safe for musical use (2.5–10 never limited)
  - ✅ Single-point mode verified correct
  - 🔲 Ableton Live 12 validation (manual testing needed)

### Architecture:
- 13 global params + 7 per-point × 8 points = **69 parameters**
- Texture = centre frequency (log 20Hz–20kHz), per-point offsets spread ±24st above/below
- Stereo Width = independent pan collapse control (0=mono, 1=full)
- Feedback: exponential knob curve (pow 0.6) so midpoint ≈ 66% — easier to reach the sweet spot
- Q range: 2.5–20 (always resonant)
- Headroom: 1.1× (just above unity for self-oscillation)
- Randomise ranges: feedback 0.4–0.9, pitch ±7st (always musically interesting)

### Build setup:
- Source: `C:\Users\Henry\Dropbox\Plugin_ideas\Octofilter` (Dropbox, single source of truth)
- Windows build: `build-windows/` folder (MSVC Visual Studio 17 2022)
- Deploy: copy VST3 bundle to `C:\Program Files (x86)\Common Files\VST3\`
- Ableton requires `Options.txt` with `-_PluginAutoPopulateThreshold=128` in latest Live 12.x.x prefs folder
- Close Ableton before deploying new builds (locks the DLL)

### Key technical decisions:
- Texture and pan are fully decoupled (Texture=vertical/frequency, Width=horizontal/stereo)
- Glide system removed — all cutoff changes are instant (smoothed at block-rate by ParamSmoother)
- Feedback knob uses exponential curve for more time in the expressive high-feedback zone
- Per-point fade system for click-free point count changes
- Pitch shift lives inside feedback loop only (creates spirals, not dry pitch shift)
