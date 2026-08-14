# Octofilter

A multi-point stereo filter effect plugin with pitch-shifting feedback loops and harmonic mode. Designed for creative sound design, spatial texture generation, and live performance.

![Status](https://img.shields.io/badge/status-alpha-orange)
![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20macOS-blue)
![Formats](https://img.shields.io/badge/formats-VST3%20%7C%20AU%20%7C%20CLAP-green)

## What is Octofilter?

Octofilter splits your audio into 1–8 independent filter "points", each with its own filter type, cutoff, resonance, panning, and feedback loop. A pitch shifter inside each feedback path creates cascading spiral effects — the "boulder head" sound. A single **Texture** knob controls the openness of the entire effect, collapsing everything from silence to full spatial multi-filter chaos.

### Key Features

- **1–8 filter points** — LP, HP, BP, Notch per point
- **Per-point feedback with pitch shifting** — creates evolving pitched spirals
- **Texture knob** — single performance control that opens/closes the entire effect
- **Harmonic mode** — locks filter cutoffs to natural harmonic series
- **Stereo field visualiser** — interactive GUI showing point positions (X=pan, Y=cutoff)
- **Randomise** — scatter parameters for instant sound design inspiration
- **Safety limiters** — per-point and output DC blocking + peak limiting protects your ears

## Screenshot

*GUI features a stereo field canvas with draggable coloured nodes, per-point controls, and grouped global parameters. Blodyn Tatws colour palette (inspired by a Welsh potato flower).*

## Technology

| Component | Technology |
|-----------|-----------|
| Framework | [DPF](https://github.com/DISTRHO/DPF) (DISTRHO Plugin Framework, ISC licence) |
| GUI | NanoVG (built into DPF, OpenGL-based vector graphics) |
| Pitch Shift | Dual-head tape-style shifter with Hann crossfade |
| FFT (reserved) | [pffft](https://github.com/rtoy/pffft) (BSD licence, SIMD-optimised) |
| Filters | Biquad Direct Form II Transposed (LP/HP/BP/Notch) |
| Testing | [doctest](https://github.com/doctest/doctest) (MIT licence) |
| Build | CMake, Ninja (Linux), MSVC (Windows), Xcode/AppleClang (macOS) |
| CI | GitHub Actions — macOS universal binary (arm64 + x86_64) |

## Plugin Formats

| Format | Windows | macOS |
|--------|:-------:|:-----:|
| VST3 | ✅ | ✅ |
| AU | — | ✅ |
| CLAP | ✅ | ✅ |

## Building

### Prerequisites

**Linux (WSL):**
```bash
sudo apt-get install -y cmake ninja-build g++ pkg-config libgl-dev libx11-dev libxcursor-dev libxrandr-dev libxinerama-dev
```

**Windows:**
- CMake 4.x+
- Visual Studio 2022 Build Tools (C++ workload)

**macOS:**
- Xcode Command Line Tools
- CMake + Ninja (`brew install cmake ninja`)

### Build Commands

**Linux:**
```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

**Windows (PowerShell):**
```powershell
cmake -B build-windows -G "Visual Studio 17 2022" -A x64
cmake --build build-windows --config Release --parallel
```

**macOS (universal binary):**
```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --parallel
```

### Install

**Windows:** Copy `build-windows\bin\Octofilter.vst3` to `C:\Program Files (x86)\Common Files\VST3\`

**macOS:**
```bash
cp -r build/bin/Octofilter.vst3 ~/Library/Audio/Plug-Ins/VST3/
cp -r build/bin/Octofilter.component ~/Library/Audio/Plug-Ins/Components/
```

## Ableton Live 12 Note

Ableton requires an `Options.txt` file to show plugins with many parameters. Create this file:

**Windows:** `%APPDATA%\Ableton\Live 12.x.x\Preferences\Options.txt`
**macOS:** `~/Library/Preferences/Ableton/Live 12.x.x/Options.txt`

With the content:
```
-_PluginAutoPopulateThreshold=128
```

## Architecture

```
src/
├── OctofilterPlugin.cpp/hpp  — DPF plugin (DSP, parameters, state)
├── BiQuadFilter.hpp          — Biquad filter (LP/HP/BP/Notch)
├── PhaseVocoderShifter.hpp   — Dual-head tape pitch shifter
├── FeedbackEngine.hpp        — Per-point feedback loop (DC block + limiter + pitch)
├── PointState.hpp            — Per-point state container
├── HarmonicMapper.hpp        — Natural harmonic series assignment
├── TextureMapper.hpp         — Log frequency mapping
├── InputRouter.hpp           — Mono/stereo channel assignment
├── StereoMixer.hpp           — Equal-power pan law
├── PeakLimiter.hpp           — Lookahead peak limiter
├── DCBlocker.hpp             — First-order DC removal
├── ParamSmoother.hpp         — Parameter smoothing utility
├── IPitchShifter.hpp         — Pitch shifter interface
└── NullShifter.hpp           — Pass-through pitch shifter stub

ui/
├── OctofilterUI.cpp          — NanoVG GUI (stereo field, knobs, buttons)
├── InterFont.hpp             — Embedded Inter font (OFL licence)
└── BgImage.hpp               — Embedded background texture

tests/
└── test_dsp.cpp              — Unit tests (doctest)
```

## Licence

Plugin source code: **Proprietary** (not yet determined for distribution)

Dependencies:
- DPF: ISC
- pffft: BSD
- doctest: MIT
- Inter font: OFL
- All dependencies are compatible with closed-source commercial distribution.

## Credits

Created by Henry ([@HJ959](https://github.com/HJ959))

Colour palette: *Blodyn Tatws* — inspired by a potato flower from the garden 🌸

Built with [Kiro CLI](https://kiro.dev) AI-assisted development.
