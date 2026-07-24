#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

namespace Octofilter
{

/**
 * PeakLimiter — lookahead peak limiter, real-time safe.
 *
 * Signal path:
 *   input → lookahead delay → gain computer → output
 *
 * The lookahead buffer is pre-allocated at prepare() to a fixed maximum
 * size. No allocations occur on the audio thread.
 *
 * Parameters (fixed at compile time for RT safety):
 *   - Lookahead: ~1 ms  (kMaxLookaheadMs)
 *   - Attack:    sample-accurate (gain applied from the lookahead read point)
 *   - Release:   ~50 ms (kReleaseMs)
 *   - Ceiling:   0 dBFS (1.0 linear)
 *
 * The gain envelope is computed on the delayed signal so the limiter
 * can react before the peak arrives at the output.
 */
class PeakLimiter
{
public:
    static constexpr float kCeiling       = 1.0f;   // 0 dBFS
    static constexpr float kMaxLookaheadMs = 1.0f;
    static constexpr float kReleaseMs      = 50.0f;

    // Maximum buffer size at the lowest supported sample rate (44.1 kHz).
    // 1 ms @ 44.1 kHz = 45 samples; use 128 for headroom.
    static constexpr int kMaxBufSize = 128;

    PeakLimiter() noexcept { reset(); }

    /**
     * Call before processing. Sets up timing constants.
     * No heap allocation.
     */
    void prepare(double sampleRate) noexcept
    {
        mSampleRate    = static_cast<float>(sampleRate);
        mLookaheadSize = static_cast<int>(kMaxLookaheadMs * 0.001f * mSampleRate + 0.5f);
        if (mLookaheadSize < 1)          mLookaheadSize = 1;
        if (mLookaheadSize > kMaxBufSize) mLookaheadSize = kMaxBufSize;

        // Release coefficient: one-pole IIR
        // gainRelease = exp(-1 / (releaseTime * sampleRate))
        mReleaseCoeff = std::exp(-1.0f / (kReleaseMs * 0.001f * mSampleRate));

        reset();
    }

    void reset() noexcept
    {
        std::memset(mDelayBuf, 0, sizeof(mDelayBuf));
        std::memset(mGainBuf,  0, sizeof(mGainBuf));
        // Gain buf initialised to 1 (no limiting)
        for (int i = 0; i < kMaxBufSize; ++i)
            mGainBuf[i] = 1.0f;
        mWritePos = 0;
        mGainEnv  = 1.0f;
    }

    /**
     * Process one sample. Returns the gain-reduced output.
     * Also writes the applied gain to `gainOut` (useful for metering/debug).
     */
    float process(float x, float* gainOut = nullptr) noexcept
    {
        // The correct lookahead limiter structure:
        //
        //   gainBuf[n] = gain required to limit sample x[n]
        //   output[n]  = x[n - lookaheadSize] * min(gainBuf[n - lookaheadSize .. n])
        //
        // We store the instantaneous required gain in a parallel delay buffer,
        // then output the delayed audio multiplied by the minimum gain seen
        // across the lookahead window. This guarantees the gain is always
        // applied before the peak arrives, with no overshoot.

        // 1. Compute instantaneous required gain for this sample
        const float absX       = std::fabs(x);
        const float inputGain  = (absX > kCeiling) ? (kCeiling / absX) : 1.0f;

        // 2. Write audio and gain into their respective delay buffers
        mDelayBuf[mWritePos] = x;
        mGainBuf[mWritePos]  = inputGain;

        // 3. Find minimum gain over window covering current input through
        //    the sample about to be output (lookaheadSize + 1 entries).
        float minGain = 1.0f;
        for (int i = 0; i <= mLookaheadSize; ++i)
        {
            const int idx = (mWritePos - i + kMaxBufSize) % kMaxBufSize;
            if (mGainBuf[idx] < minGain)
                minGain = mGainBuf[idx];
        }

        // 4. Smooth the gain envelope: instant attack, smooth release
        if (minGain < mGainEnv)
            mGainEnv = minGain;
        else
            mGainEnv = 1.0f - mReleaseCoeff * (1.0f - mGainEnv);

        if (mGainEnv < 0.0f) mGainEnv = 0.0f;
        if (mGainEnv > 1.0f) mGainEnv = 1.0f;

        // 5. Read the delayed audio sample (lookaheadSize samples behind)
        const int readPos   = (mWritePos - mLookaheadSize + kMaxBufSize) % kMaxBufSize;
        const float delayed = mDelayBuf[readPos];

        // 6. Advance write position
        mWritePos = (mWritePos + 1) % kMaxBufSize;

        if (gainOut) *gainOut = mGainEnv;

        return delayed * mGainEnv;
    }

private:
    float mDelayBuf[kMaxBufSize] {};
    float mGainBuf[kMaxBufSize]  {};
    int   mWritePos     { 0 };
    int   mLookaheadSize { 44 }; // ~1 ms @ 44.1 kHz
    float mGainEnv      { 1.0f };
    float mReleaseCoeff { 0.9986f }; // ~50 ms @ 44.1 kHz
    float mSampleRate   { 44100.0f };
};

} // namespace Octofilter
