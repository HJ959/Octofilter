# Octofilter — Code Review Action Tasklist

Generated from code-review.md (2026-10-17). Items are ordered by priority.

---

## 🔴 Blocking (fix before any release)

### #1 — Race condition on `mPendingState` (thread safety) ✅
**File:** `src/OctofilterPlugin.hpp` + `src/OctofilterPlugin.cpp`

- [x] Replace `mPendingState` + `mHasPendingState` in the header with:
  ```cpp
  PendingState       mPendingSlots[2];
  std::atomic<int>   mWriteSlot { 0 };
  std::atomic<bool>  mHasPendingState { false };
  ```
- [x] Update `setState()` (host thread) to write to the inactive slot, then publish via `mWriteSlot` + `mHasPendingState` with `memory_order_release`
- [x] Update `applyPendingState()` (audio thread) to read slot index via `mWriteSlot.load(memory_order_acquire)` before clearing the flag
- [ ] Verify: rapid back-to-back `setState()` calls (e.g. DAW undo/redo) no longer lose intermediate states (covered by issue #10 as a side-effect)

---

### #2 — RT violation: heap allocation in `doRandomise()` called from audio thread
**File:** `src/OctofilterPlugin.cpp`

- [x] Add `bool mRandomisePending { false }` member to the plugin class
- [x] In `setParameterValue()`, replace the direct call to `doRandomise()` with `mRandomisePending = true`
- [x] At the top of `run()`, check `if (mRandomisePending) { doRandomise(); mRandomisePending = false; }` alongside `applyPendingState()`
- [x] Verify: `doRandomise()` is never called directly on the audio thread path

---

## 🟡 Warnings (fix before public/production release)

### #3 — Signed integer overflow UB in `PhaseVocoderShifter::mWritePos`
**File:** `src/PhaseVocoderShifter.hpp`

- [x] Change the increment to mask-and-wrap:
  ```cpp
  // Before:
  mBuf[mWritePos & kBufMask] = in[i];
  ++mWritePos;
  // After:
  mBuf[mWritePos] = in[i];
  mWritePos = (mWritePos + 1) & kBufMask;
  ```
- [x] Verify: `mWritePos` type remains appropriate (e.g. `int` or `uint32_t`); masking works correctly with the chosen type

---

### #4 — Misleading class name `PhaseVocoderShifter`
**File:** `src/PhaseVocoderShifter.hpp`, `src/IPitchShifter.hpp`, `src/FeedbackEngine.hpp`, `src/OctofilterPlugin.cpp/hpp`, `CMakeLists.txt`, README

- [ ] Decide on new name: `TapeShifter` or `GranularShifter` (recommendation: `TapeShifter` — matches the dual-head tape metaphor)
- [ ] Rename the file `PhaseVocoderShifter.hpp` → `TapeShifter.hpp`
- [ ] Rename class `PhaseVocoderShifter` → `TapeShifter` across all files
- [ ] Update `#include` directives everywhere the old header is included
- [ ] Update `CMakeLists.txt` source list
- [ ] Update `README.md` architecture table and technology table
- [ ] Run build to confirm no broken includes

---

### #5 — `PeakLimiter::process` O(N) window scan per sample
**File:** `src/PeakLimiter.hpp`

- [ ] Replace the linear minimum-gain scan with either:
  - A **monotone deque** (sliding window minimum, O(1) amortised), or
  - A **segment tree** over the fixed 128-entry buffer (trivial at this size)
- [ ] Verify: limiter behaviour is unchanged (unit test or A/B against the old implementation)

---

### #6 — Fragile temporary mutation of `mTexture` in `run()`
**File:** `src/OctofilterPlugin.cpp`

- [x] Add a `textureOverride` parameter to `updateFilterParams()`:
  ```cpp
  void updateFilterParams(float textureOverride = -1.0f) noexcept;
  ```
- [x] Inside `updateFilterParams()`, use `textureOverride >= 0.0f ? textureOverride : mTexture` in place of `mTexture` where appropriate
- [x] In `run()`, replace the save/restore pattern with:
  ```cpp
  updateFilterParams(mSmTexture.get());
  ```
- [x] Remove the now-redundant `savedTexture` lines from `run()`
- [x] Verify: build, run, and confirm filter parameter updates are consistent with previous behaviour

---

### #7 — Filter type parameter range is 0–2 but 4 types exist
**File:** `src/OctofilterPlugin.cpp` (`initParameter`)

- [x] Find the per-point filter type parameter initialisation block
- [x] Change `p.ranges.max = 2.0f` → `p.ranges.max = 3.0f`
- [ ] Verify: Notch type is now selectable from DAW automation lane / parameter list (test in at least one host)

---

### #8 — `getState()` buffer is barely large enough; silent truncation risk
**File:** `src/OctofilterPlugin.cpp`

- [x] Change `char buf[2048]` → `char buf[4096]`
- [x] Add an assert after `snprintf` to detect if the buffer was too small:
  ```cpp
  assert(written < (int)sizeof(buf) - 1 && "getState buffer overflow");
  ```
- [x] Verify: state round-trips correctly (save → load) with the new buffer size

---

### #9 — `BiQuadFilter::clampCutoff` doc comment contradicts implementation
**File:** `src/BiQuadFilter.hpp`

- [x] Find the `clampCutoff` function comment that says *"Clamped to [20, sampleRate/2 - 1] internally"*
- [x] Update the comment to accurately describe the actual behaviour (fixed 20–20000 Hz ceiling regardless of sample rate)
- [x] Decide whether to also fix the implementation to actually track Nyquist (not required at ≤96 kHz, but correct practice)

---

## 🟢 Suggestions (quality improvements, no functional risk)

### #11 — Remove or use the dead `ppIdx` helper
**File:** `src/OctofilterPlugin.cpp` (line ~53)

- [x] **Used it:** replaced raw index arithmetic in `doRandomise()` and `applyPendingState()` with `ppIdx()` calls

---

### #12 — Mark `BiQuadFilter::recalculate()` as `noexcept`
**File:** `src/BiQuadFilter.hpp`

- [x] Add `noexcept` to the `recalculate()` declaration and definition
- [x] Verify: build succeeds (no `noexcept` violations in the function body)

---

### #13 — `PointState::reset()` has ordering dependency on `prepare()`
**File:** `src/PointState.hpp`

- [x] Add an explicit `shifter.reset()` call in `reset()` before `feedback.reset()`:
  ```cpp
  void reset() noexcept {
      filter.reset();
      shifter.reset();   // explicit, no ordering dependency on prepare()
      feedback.reset();
  }
  ```
- [x] Verify: tests that call `reset()` before `prepare()` no longer silently skip the shifter reset

---

### #14 — Cache `mSmWetDry.get()` in `run()` hot loop
**File:** `src/OctofilterPlugin.cpp`

- [x] `mSmWetDry.get()` is called once per frame, stored in a `const float wet`, and reused for both `outL` and `outR` calculations

---

### #15 — `DCBlocker` default coefficient is wrong for non-44.1 kHz rates
**File:** `src/DCBlocker.hpp`

- [x] Constructor now calls `setSampleRate(44100.0f)` to make initial state explicit and remove the silent footgun

---

## Completion Summary

| Priority | Count | Description |
|----------|-------|-------------|
| 🔴 Blocking | 2 | Must fix before release |
| 🟡 Warning | 7 | Fix before public/production use |
| 🟢 Suggestion | 5 | Quality improvements, low risk |
| **Total** | **14** | |

**Suggested work order:**
1. #7 (10 min) — one-liner, immediate DAW display fix
2. #3 (10 min) — one-liner, eliminates UB
3. #9 (10 min) — comment fix only
4. #8 (10 min) — buffer size bump + assert
5. #2 (30 min) — defer doRandomise to run()
6. #1 (45 min) — double-buffer PendingState
7. #6 (30 min) — updateFilterParams override param
8. #12, #13, #14, #15 (30 min total) — small quality fixes
9. #11 (20 min) — use or delete ppIdx
10. #4 (45 min) — rename PhaseVocoderShifter (touches many files)
11. #5 (60 min) — PeakLimiter sliding window minimum
