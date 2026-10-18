# Octofilter — Project Scope & Agent Steering

> **Last updated:** 2026-10-18  
> **Status:** Alpha — Phases 0–6d complete, Phase 7 (Integration & Polish) in progress.  
> **Latest release:** v0.9.1 macOS universal binary (arm64 + x86_64)

---

## What This Project Is

Octofilter is a multi-point stereo filter effect audio plugin for creative sound design and live performance. It splits an incoming signal into 1–8 independent filter "points", each with its own filter type, cutoff, resonance, pan position, and a **feedback loop containing a pitch shifter**. The pitch-shifted feedback creates evolving pitched spirals — the "boulder head" sound.

**Format:** VST3 (Windows + macOS), AU (macOS only), CLAP (Windows + macOS)  
**Framework:** DPF (DISTRHO Plugin Framework, ISC licence)  
**GUI:** NanoVG (built into DPF), OpenGL-backed vector graphics  
**Language:** C++17  
**Build:** CMake, Ninja (Linux/macOS), MSVC Visual Studio 17 2022 (Windows)

---

## Repository Layout

```
Octofilter/
├── src/                    DSP-only source (compiled into the plugin DSP lib)
│   ├── OctofilterPlugin.cpp/hpp   DPF plugin class — parameters, state, run()
│   ├── DistrhoPluginInfo.h        DPF compile-time plugin identity & I/O config
│   ├── BiQuadFilter.hpp           Biquad filter (LP/HP/BP/Notch, DF-II Transposed)
│   ├── PhaseVocoderShifter.hpp    Dual-head tape pitch shifter (NOT a phase vocoder)
│   ├── FeedbackEngine.hpp         Per-point feedback path (pitch→DC→limiter→1-sample delay)
│   ├── PointState.hpp             Per-point DSP state container
│   ├── HarmonicMapper.hpp         Maps texture to harmonic series cutoffs
│   ├── TextureMapper.hpp          Log freq mapping 20Hz–20kHz for Texture knob
│   ├── InputRouter.hpp            Stereo L/R channel assignment per point
│   ├── StereoMixer.hpp            Equal-power (cos/sin) pan law accumulator
│   ├── PeakLimiter.hpp            Lookahead peak limiter (~1 ms, fixed static buffers)
│   ├── DCBlocker.hpp              First-order HPF for DC removal
│   ├── ParamSmoother.hpp          One-pole IIR parameter smoother (10 ms TC)
│   ├── IPitchShifter.hpp          Abstract pitch shifter interface
│   └── NullShifter.hpp            Pass-through stub (zero shift, zero cost)
├── ui/                     GUI-only source (compiled into the plugin UI lib)
│   ├── OctofilterUI.cpp           NanoVG GUI: stereo field canvas, knobs, buttons
│   ├── InterFont.hpp              Embedded Inter font as C array (OFL licence)
│   └── BgImage.hpp                Embedded background PNG as C array
├── tests/
│   └── test_dsp.cpp               Unit tests (doctest header-only)
├── deps/
│   ├── dpf/                       DPF git submodule (ISC licence)
│   ├── pffft/                     pffft git submodule (BSD — reserved, not yet used in DSP)
│   └── doctest.h                  doctest header (MIT — test runner)
├── presets/                       .octopreset example preset files
├── testing_feedback/              User testing notes and preset snapshots
│   ├── first_phase/               Three external tester reports (v0.8.x)
│   └── second_phase/              Self-testing + user presets capturing bugs
├── macOs/                         Release binaries (not for editing)
├── .github/workflows/build.yml    CI: macOS universal binary on push
├── CMakeLists.txt                 Top-level build
├── .clang-format                  Clang-format style config (always respect this)
├── PLAN.md                        Full phase-by-phase implementation plan
├── REQUIREMENTS.md                Functional & non-functional requirements
├── code-review.md                 Code review findings (2026-10-17) — READ BEFORE EDITING DSP
└── code-review-tasklist.md        Actionable items from the code review
```

---

## Parameter Architecture

**Total parameters: 69** (13 globals + 7 per-point × 8 points)

### Global Parameters (indices 0–12)

| Index | Symbol | Range | Default | Notes |
|-------|--------|-------|---------|-------|
| 0 | `point_count` | 1–8 (int) | 2 | Active filter points |
| 1 | `texture` | 0–1 | 0.5 | Main performance knob — frequency ceiling |
| 2 | `texture_spread` | 0–1 | 1.0 | Hidden — spread scale |
| 3 | `spread` | 0–1 | 1.0 | Hidden |
| 4 | `resonance` | 0.1–20 | 0.707 | Hidden — global Q |
| 5 | `feedback` | 0–1 | 0.3 | Global feedback amount |
| 6 | `pitch_shift` | ±24 st | 0 | Global pitch offset (additive) |
| 7 | `input_gain` | ±24 dB | 0 | Pre-filter input gain |
| 8 | `output_gain` | ±24 dB | 0 | Post-mix output gain |
| 9 | `wet_dry` | 0–1 | 1.0 | Wet/dry blend |
| 10 | `randomise` | 0/1 | 0 | Momentary trigger — edge-detected |
| 11 | `harmonic_mode` | 0/1 | 0 | 0=random offsets, 1=harmonic series |
| 12 | `stereo_collapse` | 0–1 | 1.0 | 0=mono, 1=full width |

### Per-Point Parameters (indices 13–68, 7 params each)

Index formula: `13 + point * 7 + offset`

| Offset | Symbol | Range | Default |
|--------|--------|-------|---------|
| 0 | `p{n}_filter_type` | 0–3 (int) | 0 = LP |
| 1 | `p{n}_cutoff` | 0–1 | 0.5 = stored vertical position |
| 2 | `p{n}_q` | 2.5–20 | 5.0 |
| 3 | `p{n}_pan` | -1–1 | 0 |
| 4 | `p{n}_level` | 0–2 | 1.0 |
| 5 | `p{n}_feedback` | 0–1.1 | 0.7 |
| 6 | `p{n}_pitch_shift` | ±24 st | 0 |

**Filter types:** 0=LowPass, 1=HighPass, 2=BandPass, 3=Notch  
**Note:** `initParameter` currently declares filter type max as 2.0 — this is a known bug (should be 3.0). Do not add a `case 3` to filter selection without also fixing the range.

---

## DSP Signal Flow (per audio block)

```
run(inputs, outputs, frames)
│
├─ applyPendingState()           // apply preset loaded from host/UI thread
│
├─ Per-block updates (all active points):
│   ├─ Q ceiling calculation     // reduces Q at high feedback+pitch (anti-pierce)
│   ├─ Global pitch offset apply // totalPitch = perPoint + globalSmoothed
│   └─ fbComp calculation        // resonance-aware feedback gain reduction
│
├─ Smooth all global params      // ParamSmoother.setTarget() per-block
│
├─ updateFilterParams()          // recalculate cutoffs from smoothed Texture
│                                // (Texture acts as a ceiling on per-point position)
│
└─ Per-sample loop:
    └─ For each active point p:
        ├─ inputSample = InputRouter.getSample(inputs, frame, pt)
        ├─ inputSample *= smoothInputGain
        ├─ mixedIn = FeedbackEngine.mixInput(dry, fbAmt)    // dry + prev feedback
        ├─ filtered = BiQuadFilter.process(mixedIn)
        ├─ FeedbackEngine.processOutput(filtered)
        │     └─ PhaseVocoderShifter → DCBlocker → PeakLimiter → store
        └─ StereoMixer.accumulate(filtered * level * fade, pan)
    │
    ├─ normalise by activePointCount
    ├─ wet/dry blend (mSmWetDry advanced per-sample for click-free crossfade)
    ├─ multiply by smoothOutputGain
    ├─ output DC block (mOutputDCL/R)
    ├─ output peak limiter (mOutputLimiterL/R)
    └─ write to waveform ring buffer (mWaveformBuf, 2048 samples)
```

---

## Texture Knob — How It Works

The Texture knob is the main performance control. It is NOT a simple cutoff.

- Each point stores a **position** (0–1) via `p{n}_cutoff` — this is a vertical position on the stereo field canvas (0=bottom/dark, 1=top/open).
- Texture acts as a **ceiling**: `effectivePosition = min(storedPosition, texture)`
- At Texture=1.0: all points sit at their stored positions (full effect)
- At Texture=0.0: all points collapse to 20 Hz regardless of stored positions
- At intermediate values: points at or below the Texture value retain their positions; points above are dragged down

**Critical implication:** When Texture is at maximum (1.0), it has no effect — this is correct behaviour but caused user confusion. The per-point cutoff *does* work; it's just invisible unless Texture < the point's stored position. This is documented as an expected behaviour to clarify (not a bug), but UX guidance is needed.

**In Harmonic Mode:** Texture-derived positions are then snapped to the nearest multiple of 55 Hz (A1), creating harmonic-series quantisation.

---

## Feedback System

The feedback path for each point is:

```
filtered output
    └→ PhaseVocoderShifter (dual-head tape, 1024-sample buffer)
    └→ DCBlocker (60 Hz corner — higher than default to suppress boomy low-freq buildup)
    └→ PeakLimiter (lookahead ~1ms, release ~50ms, ceiling 0 dBFS)
    └→ stored as feedbackSample (one-sample delay)
    └→ injected at next sample: mixedIn = dryInput + fbAmt * feedbackSample
```

**Feedback amount:** `fbAmt = pt.feedbackAmount * smoothGlobalFeedback * kFeedbackHeadroom * mPointFbComp[p]`

- `pt.feedbackAmount` is stored with an exponential curve: `pow(knobValue / 1.1, 0.6) * 1.1` — midpoint of the knob ≈ 66% feedback
- `kFeedbackHeadroom = 1.1` — allows just above unity for self-oscillation, protected by limiters
- `mPointFbComp[p]` = resonance-aware feedback compensation: at Q=20, up to 50% reduction; scales further with pitch shift amount

---

## Pitch Shifter

`PhaseVocoderShifter` is **not** a phase vocoder despite its name (known issue from code review). It is a **dual-head tape-style granular pitch shifter**:

- 1024-sample circular buffer
- Two read heads offset by quarter-buffer and three-quarter-buffer behind the write head
- Each head advances at `2.0 - pitchRatio` per sample (ratio>1 = pitch up, ratio<1 = pitch down)
- Hann envelope crossfade between heads eliminates clicks at head boundaries
- Latency: 512 samples
- Linear interpolation for fractional read positions
- `mWritePos` is always kept in `[0, kBufMask]` via masked increment — no overflow risk

---

## State & Preset System

**State key:** `octofilter_state`  
**File extension:** `.octopreset`  
**Format:** Custom line-delimited `key=value` text

### State keys

| Key | Meaning |
|-----|---------|
| `texture` | Global texture value |
| `feedback` | Global feedback |
| `pitch` | Global pitch shift |
| `wetdry` | Wet/dry |
| `width` | Stereo collapse |
| `harmonic` | Harmonic mode toggle |
| `points` | Point count |
| `seed` | RNG seed (for deterministic Randomise) |
| `co0`–`co7` | Per-point cutoff positions (0–1) |
| `ft0`–`ft7` | Per-point filter types (0–3) |
| `ps0`–`ps7` | Per-point pitch shifts |
| `fb0`–`fb7` | Per-point feedback amounts |
| `q0`–`q7` | Per-point Q values |
| `pn0`–`pn7` | Per-point pan positions |
| `lv0`–`lv7` | Per-point level values |

**Thread safety:** `setState()` runs on the host/UI thread. It writes to an inactive `mPendingSlots[]` slot, then publishes the slot index atomically. `applyPendingState()` runs at the top of `run()` on the audio thread and reads the published slot. This is a correct double-buffer scheme — do not collapse it to a single struct.

---

## GUI Architecture

- **Window:** 700×450 px base, scales with `getScaleFactor()`
- **Stereo field canvas:** ~60% of the width. X-axis = pan, Y-axis = cutoff frequency (inverted: top = high freq).
- **Nodes:** Filled circles coloured by filter type (LP=purple, HP=yellow, BP=sage green, Notch=lavender)
- **Interaction:** Click to select, drag horizontally to set pan, drag vertically to set per-point cutoff
- **Knobs:** Custom NanoVG arc-style knobs; dragged vertically to change value
- **Palette (Blodyn Tatws):** Welsh potato flower. bg=`#282828`, field=sage green 30% alpha, text=cream, accent=purple, highlight=golden yellow, outline=brown

**Parameter sync:** The UI mirrors all parameters from `parameterChanged()`. It does NOT own DSP state — it sends parameter changes to the plugin via DPF's `setParameterValue()`.

---

## Build System

```cmake
# Build plugin (Linux/macOS):
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

# Build plugin (Windows MSVC):
cmake -B build-windows -G "Visual Studio 17 2022" -A x64
cmake --build build-windows --config Release --parallel

# Run tests:
./build/OctofilterTests        # Linux/macOS
.\build-windows\Release\OctofilterTests.exe  # Windows
```

**Key CMake targets:**
- `Octofilter` — main plugin (produces VST3 + CLAP + AU)
- `Octofilter-dsp` — DSP library (src/OctofilterPlugin.cpp)
- `Octofilter-ui` — UI library (ui/OctofilterUI.cpp)
- `OctofilterTests` — unit test executable

**MSVC quirk:** Must define `_USE_MATH_DEFINES` for M_PI. Already in CMakeLists.

**Source location:** `C:\Users\Henry\Dropbox\Plugin_ideas\Octofilter` (Dropbox — single source of truth). Build dirs (`build/`, `build-windows/`) are gitignored and local.

**Ableton Live 12:** Requires `Options.txt` with `-_PluginAutoPopulateThreshold=128` to show plugins with > 16 parameters.

---

## Known Issues & Open Tasks (as of 2026-10-18)

### Code Review Items (code-review.md — read before editing DSP)

| Priority | Issue | Location | Status |
|----------|-------|----------|--------|
| 🔴 High | `mRandomisePending` flag missing — `doRandomise()` may heap-alloc on audio thread | `OctofilterPlugin.cpp` | ✅ Fixed |
| 🟡 Med | `mWritePos` signed int overflow after ~13h (UB) | `PhaseVocoderShifter.hpp` | ✅ Fixed |
| 🟡 Med | `PhaseVocoderShifter` misleading name (not a phase vocoder) | `PhaseVocoderShifter.hpp` | Open |
| 🟡 Med | `PeakLimiter::process` O(N) min scan per sample | `PeakLimiter.hpp` | Open |
| 🟡 Med | `updateFilterParams` fragile `mTexture` temp mutation | `OctofilterPlugin.cpp` | ✅ Fixed |
| 🟡 Med | Filter type param range 0–2 should be 0–3 | `OctofilterPlugin.cpp:initParameter` | ✅ Fixed |
| ✅ Done | Double-buffer for `PendingState` thread safety | `OctofilterPlugin.hpp` | Fixed |

### User-Reported Bugs (testing_feedback/)

| Bug | Root Cause | Status |
|-----|-----------|--------|
| Piercing resonance at Q+feedback+pitch | Q ceiling partially implemented; fbComp added | Mitigated, needs more testing |
| Texture at max overrides per-point cutoff | Expected behaviour — needs UX clarification | Design issue |
| Input gain doesn't affect dry signal | Dry path now uses `smoothInGain` | Fixed in Phase 6d |
| Input/output knob graphic disappears at extremes | UI clamp issue | Needs investigation |
| Bypass resets DSP state | FeedbackEngine.reset() called on deactivate | Open |

### Phase 7 Remaining Tasks

- [ ] Ableton Live 12 full validation (manual)
- [ ] AU validation (`auval -v aufx Octo Hnry`)
- [ ] CPU profiling at 8 points, all feedback + pitch active (target < 5% single core)
- [ ] Re-enable Windows CI (DPF submodule update needed)
- [ ] REAPER VST3 + CLAP testing

---

## Conventions & Constraints Agents Must Follow

### Real-Time Safety (Critical)
- **No heap allocation on the audio thread.** All buffers must be pre-allocated in `activate()` or `prepare()`.
- **No mutex locks on the audio thread.** Use atomic operations only for cross-thread communication.
- **No `std::` containers on the audio thread** unless you can prove they don't allocate (e.g. `std::array`).
- `run()` is the audio thread. `setParameterValue()` MAY be called from the audio thread (DPF host-dependent). `setState()` is always called from the host/UI thread.

### Thread Safety Pattern
When adding new state that must be communicated host→audio thread, follow the existing double-buffer pattern:
1. Host thread writes to inactive slot → publishes slot index atomically → sets `mHasPending` flag
2. Audio thread checks flag at top of `run()` → reads published slot → clears flag

Never do a plain struct assignment protected only by a single atomic flag.

### Namespace
All DSP classes live in `namespace Octofilter {}`. DPF plugin and UI classes use `START_NAMESPACE_DISTRHO` / `END_NAMESPACE_DISTRHO`.

### Header-only DSP
All DSP components except `OctofilterPlugin.cpp` are header-only (`.hpp`). Keep it that way — it simplifies the CMake setup and enables the test executable to include headers directly without linking to the plugin lib.

### Parameter Index Constants
`GlobalParams` enum and `PerPointOffset` enum are defined **in both** `OctofilterPlugin.cpp` and `OctofilterUI.cpp` (they must stay in sync). If you add a parameter, update both files. Do not rely on one including the other.

### Code Style
Respect `.clang-format` at root. Run `clang-format` before committing. The style is 4-space indent, braces on new lines for functions, inline for small methods in headers.

### Per-Point Parameter Curves
- **Feedback:** `pow(knobValue / 1.1, 0.6) * 1.1` — NOT a linear mapping. Raw `mPointFeedback[i]` is the knob value; `mPoints[i].feedbackAmount` is the curve-applied value. Always maintain both.
- **Cutoff offset:** stored as 0–1 vertical position in `mPointCutoffOffset[i]` and `pt.cutoffOffsetSemitones`. Despite the field name, the value is in the 0–1 range (not semitones). The TextureMapper converts this to Hz.
- **Filter type:** stored as float (0–3) in `mPointFilterType[i]`, cast to `BiQuadFilter::Type` for DSP.

---

## Dependency Licences

| Dependency | Licence | Usage |
|-----------|---------|-------|
| DPF (deps/dpf) | ISC | Plugin framework, GUI context |
| pffft (deps/pffft) | BSD | Reserved — future FFT use |
| doctest (deps/doctest.h) | MIT | Unit test runner |
| Inter font (ui/fonts/) | OFL | Embedded UI font |
| NanoVG | zlib/MIT | Built into DPF, used for all GUI drawing |

All dependencies are compatible with closed-source commercial distribution. No GPL/LGPL.

---

## Artist / Identity

- **Creator:** Henry ([@HJ959](https://github.com/HJ959))
- **Plugin name:** Octofilter
- **Colour palette:** Blodyn Tatws (Welsh for "potato flower") — inspired by a potato flower from the garden
- **CLAP ID:** `com.henry.octofilter`
- **VST3 unique ID:** `d_cconst('O','c','t','o')`
- **DPF BRAND_ID:** `Hnry` (bare 4-char token, no quotes — DPF requirement)
- **DPF UNIQUE_ID:** `Octo` (bare 4-char token)

---

## Quick Reference: Adding a New Global Parameter

1. Add the index to `GlobalParams` enum in both `OctofilterPlugin.cpp` and `OctofilterUI.cpp`
2. Update `kNumGlobalParams` constant if adding at the end (currently 13)
3. Add the member variable to `OctofilterPlugin` class
4. Implement the three cases in `initParameter()`, `getParameterValue()`, `setParameterValue()`
5. Add it to `PendingState` struct and the `setState()` / `applyPendingState()` parser if it should be saved in presets
6. Add `parameterChanged()` handling in `OctofilterUI.cpp`
7. Update `getState()` serialisation

**Do not** change the flat index of any existing parameter — this would break saved presets and automation in host DAWs.
