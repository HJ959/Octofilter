Octofilter — DSP Code Review

Date: 2026-10-17 | Files: src/*.hpp, src/OctofilterPlugin.cpp/hpp

Overall this is clean, well-structured plugin code. RT safety is largely respected, the DSP math is correct, and the architecture is
sensible for a DPF plugin of this complexity. There are two genuine blocking issues around threading, several warnings worth fixing before a
release, and a batch of minor suggestions.

────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────

🔴 Blocking Issues

1. applyPendingState — race condition on mPendingState

setState() (UI/host thread) does a plain struct assignment to mPendingState, then stores true to mHasPendingState with memory_order_release.
applyPendingState() (audio thread) acquires the flag, then reads the struct. The acquire/release pair does establish happens-before — so the
first write is safe. But if the host calls setState a second time while the audio thread is still reading from the struct (between the
atomic load and the end of rebuildRouting()), the struct is torn with no protection.

Fix: double-buffer the PendingState slots:

// Header: replace mPendingState + mHasPendingState with:
PendingState       mPendingSlots[2];
std::atomic<int>   mWriteSlot { 0 };
std::atomic<bool>  mHasPendingState { false };

// setState (host thread):
int slot = 1 - mWriteSlot.load(std::memory_order_relaxed);
mPendingSlots[slot] = ps;
mWriteSlot.store(slot, std::memory_order_release);
mHasPendingState.store(true, std::memory_order_release);

// applyPendingState (audio thread):
if (!mHasPendingState.load(std::memory_order_acquire)) return;
const PendingState& ps = mPendingSlots[mWriteSlot.load(std::memory_order_acquire)];
mHasPendingState.store(false, std::memory_order_release);

────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────

2. doRandomise() — potential RT violation when called from setParameterValue

setParameterValue is called from the audio thread in many DPF hosts. doRandomise() constructs std::mt19937_64 and two
std::uniform_*_distribution objects — the standard library implementations may heap-allocate (this is implementation-defined but common). It
also calls updateFilterParams() which is fine, but the allocation is not.

Fix: set a bool mRandomisePending flag in setParameterValue, then execute doRandomise() at the top of run(), alongside applyPendingState().

────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────

🟡 Warnings

3. PhaseVocoderShifter — mWritePos signed integer overflow (UB)

mWritePos is an int that is incremented every sample with ++mWritePos and never wrapped. At 44.1 kHz this overflows in ~13.5 hours — signed
integer overflow is undefined behaviour in C++.

Fix: keep it always in range:

// Before (UB after ~13h):
mBuf[mWritePos & kBufMask] = in[i];
// ...
++mWritePos;

// After:
mBuf[mWritePos] = in[i];
// ...
mWritePos = (mWritePos + 1) & kBufMask;

4. PhaseVocoderShifter — misleading class name

This is not a phase vocoder. It's a dual-head tape-style granular shifter with Hann crossfade. Naming it PhaseVocoderShifter will confuse
any future maintainer who opens the file expecting FFT bins. Consider renaming to TapeShifter or GranularShifter.

5. PeakLimiter::process — O(N) window scan per sample

The minimum-gain search iterates up to mLookaheadSize + 1 entries on every sample. With 10 limiter instances (8 per-point + 2 output) at 96
kHz, that is ~970 iterations per sample. The buffer is cache-friendly so it won't kill performance, but it is unnecessary work. The standard
solution is a sliding window minimum with a monotone deque (O(1) amortised), or at this fixed buffer size (128), a segment tree of size 128
is trivial.

6. run() — temporary mutation of mTexture is fragile

const float savedTexture = mTexture;
mTexture = mSmTexture.get();
updateFilterParams();
mTexture = savedTexture;

This overwrites the authoritative member to smuggle a value into updateFilterParams(). It works, but if updateFilterParams is ever called
from another path while this is in flight (not currently possible, but easy to introduce), mTexture is corrupted. Pass the value as a
parameter instead:

void updateFilterParams(float textureOverride = -1.0f) noexcept;
// In run():
updateFilterParams(mSmTexture.get());

7. initParameter — per-point filter type range is 0–2, but 4 types exist

p.ranges.min = 0.0f; p.ranges.max = 2.0f; p.ranges.def = 0.0f;

BiQuadFilter::Type has LP=0, HP=1, BP=2, Notch=3. The parameter max should be 3.0f. Any DAW that uses the range to build a selector (e.g.
Bitwig, Live) will never show Notch to the user. The DSP path does % 4 so it works internally, but the automation range is wrong.

8. getState() — state buffer is just barely large enough

char buf[2048] with 8 points × 7 params × ~20 chars/param ≈ 1120 bytes + globals + seed ≈ 1260 bytes total. It fits now but any addition
(e.g. a new per-point parameter) will cause silent truncation since snprintf stops writing but doesn't error. Increase to char buf[4096].

9. BiQuadFilter::clampCutoff — ceiling doesn't track Nyquist; comment is wrong

The function clamps to 20000 Hz regardless of sample rate, but the doc comment says "Clamped to [20, sampleRate/2 - 1] internally". At
standard rates (≤96 kHz) 20 kHz is safely below Nyquist, so the behaviour is fine. But the comment is wrong and should be corrected to match
what the code actually does.

10. setState — second setState call before audio thread consumes the first loses the first preset

Covered by issue #1, but worth calling out separately: with the current single-slot design, rapid consecutive preset loads (e.g. from a DAW
undo/redo chain) will lose all but the last write. The double-buffer fix resolves this.

────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────

🟢 Suggestions

11. ppIdx helper is defined but never used

OctofilterPlugin.cpp line 53: static uint32_t ppIdx(int point, PerPointOffset offset) — defined but never called. This will produce a
-Wunused-function warning. Either use it to replace the raw index arithmetic in doRandomise and applyPendingState (which would be cleaner),
or delete it.

12. BiQuadFilter::recalculate() should be noexcept

It is called from process() which is noexcept. std::sin/std::cos are noexcept for floating-point arguments in C++11+. Mark recalculate() as
noexcept so the compiler can verify the chain and emit better code.

13. PointState::reset() has an ordering dependency for the shifter

void reset() noexcept {
    filter.reset();
    feedback.reset(); // calls mShifter->reset() only if mShifter != nullptr
}

feedback.mShifter is only set by setPitchShifter(), which is called in prepare(). If reset() is called before prepare() (e.g. in a test),
the PhaseVocoderShifter is never reset. Add a direct shifter.reset() call:

void reset() noexcept {
    filter.reset();
    shifter.reset();   // explicit, no ordering dependency
    feedback.reset();
}

14. run() — cache mSmWetDry.get() to avoid four calls per sample

mSmWetDry.setTarget(mWetDry);
const float wet = mSmWetDry.get();
const float dry = 1.0f - wet;
outL[f] = (dryL * dry + wetL * wet) * smoothOutGain;
outR[f] = (dryR * dry + wetR * wet) * smoothOutGain;

Minor, but the compiler may not inline get() as aggressively in a hot loop.

15. DCBlocker — default mR is a magic number; consider calling setSampleRate in constructor

mR { 0.99715f } is correct for 20 Hz @ 44.1 kHz, but an instance created at a different rate and used before setSampleRate() is called will
silently use the wrong coefficient. Either call setSampleRate(44100.0) in the constructor, or add an assert(mSampleRateSet) guard.

────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────

What Is Done Well

- RT allocation safety: All buffers are fixed-size arrays on the stack or in members. No new/delete anywhere in the audio path. 
- Denormal handling: _MM_SET_FLUSH_ZERO_MODE and _MM_SET_DENORMALS_ZERO_MODE set in activate() — essential for the high-Q feedback path,
correctly handled.
- Double-precision coefficient calculation: BiQuadFilter::recalculate() uses double throughout and only casts to float at the final
normalisation. This is exactly the right approach for biquad numerical stability.
- Dirty flag caching: Coefficients only recalculate when parameters change, not on every sample.
- Q ceiling + feedback compensation: mPointFbComp and the dynamic Q clamp in run() are thoughtful safety mechanisms that protect against the
squeal-spiral without obviously limiting creative use. Well-commented.
- Thread-safe pending state intent: The atomic flag + struct pattern is the right approach in principle; it just needs the double-buffer
- Equal-power panning: StereoMixer::panGains implements the standard cos/sin law correctly, and the tests verify it.
- InputRouter: Clean, correct, and well-tested for all mono/stereo/odd-count cases.
- Code quality: Comments explain the why, not just the what. The codebase is readable and the architecture is coherent.

────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────

Priority order for fixes:

1. Issue #1 (threading race) — before any public/production release
2. Issue #2 (RT allocation in randomise) — before audio thread use of setParameterValue  
3. Issue #3 (signed overflow UB in mWritePos) — straightforward one-liner
4. Issue #7 (filter type range) — affects DAW parameter display now
 