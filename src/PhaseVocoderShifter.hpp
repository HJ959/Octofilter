#pragma once

#include "IPitchShifter.hpp"
#include <cmath>
#include <cstring>

namespace Octofilter
{

/**
 * PhaseVocoderShifter — tape-style pitch shifter using dual read-heads.
 *
 * This is a simple, robust pitch shifter based on two read pointers
 * that move through a delay buffer at different speeds, crossfaded
 * with a Hann envelope to eliminate clicks at the crossover point.
 *
 * This approach:
 *   - Works reliably sample-by-sample (no burst output)
 *   - Introduces a small fixed latency (bufferSize / 2 samples)
 *   - Produces warm, characterful pitch shifting well suited to
 *     the feedback spiral "boulder head" effect
 *   - Is CPU-efficient and RT-safe
 *
 * For a pitch ratio > 1 (pitch up): read heads move slower than write
 * For a pitch ratio < 1 (pitch down): read heads move faster than write
 */
class PhaseVocoderShifter final : public IPitchShifter
{
public:
    static constexpr int kDefaultWindowSize = 512; // kept for interface compat
    static constexpr int kBufSize = 1024;          // delay buffer (power of 2)
                                                   // smaller = smoother at large shifts
    static constexpr int kBufMask = kBufSize - 1;

    ~PhaseVocoderShifter() override = default;

    // ── IPitchShifter ─────────────────────────────────────────────────────

    void prepare(double /*sampleRate*/, int /*maxBlockSize*/) override
    {
        reset();
    }

    void setShift(float semitones) override
    {
        if (semitones >  24.0f) semitones =  24.0f;
        if (semitones < -24.0f) semitones = -24.0f;
        mPitchRatio = std::pow(2.0f, semitones / 12.0f);
    }

    void reset() override
    {
        std::memset(mBuf, 0, sizeof(mBuf));
        mWritePos = 0;
        mOffset1  = static_cast<float>(kBufSize / 4);       // quarter buffer behind
        mOffset2  = static_cast<float>(kBufSize * 3 / 4);   // three-quarters behind
    }

    int getLatencySamples() const override { return kBufSize / 2; }

    void process(const float* in, float* out, int numSamples) override
    {
        for (int i = 0; i < numSamples; ++i)
        {
            // Write input into circular buffer
            mBuf[mWritePos & kBufMask] = in[i];

            // Read heads are offsets BEHIND the write head (positive = further back).
            // Offset advance speed:
            //   ratio > 1 (pitch up):   advance < 1 → delay grows   → playback slowed → pitch up
            //   ratio = 1 (no shift):   advance = 1 → delay constant → no shift
            //   ratio < 1 (pitch down): advance > 1 → delay shrinks  → playback sped up → pitch down
            const float advance = 2.0f - mPitchRatio;
            mOffset1 += advance;
            mOffset2 += advance;

            // Wrap offsets within buffer size
            while (mOffset1 >= static_cast<float>(kBufSize)) mOffset1 -= static_cast<float>(kBufSize);
            while (mOffset2 >= static_cast<float>(kBufSize)) mOffset2 -= static_cast<float>(kBufSize);
            while (mOffset1 < 0.0f) mOffset1 += static_cast<float>(kBufSize);
            while (mOffset2 < 0.0f) mOffset2 += static_cast<float>(kBufSize);

            // Read positions in absolute buffer coordinates
            const float rp1 = static_cast<float>(mWritePos) - mOffset1;
            const float rp2 = static_cast<float>(mWritePos) - mOffset2;

            const float s1 = interpolate(rp1);
            const float s2 = interpolate(rp2);

            // Crossfade with Hann envelope based on where each head is in its cycle
            const float phase1 = mOffset1 / static_cast<float>(kBufSize);
            const float phase2 = mOffset2 / static_cast<float>(kBufSize);
            const float env1   = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI) * phase1));
            const float env2   = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI) * phase2));

            const float envSum = env1 + env2 + 1e-10f;
            out[i] = (s1 * env1 + s2 * env2) / envSum;

            ++mWritePos;
        }
    }

private:
    float interpolate(float pos) const noexcept
    {
        // Linear interpolation from circular buffer
        const int   idx0  = static_cast<int>(pos) & kBufMask;
        const int   idx1  = (idx0 + 1) & kBufMask;
        const float frac  = pos - std::floor(pos);
        return mBuf[idx0] + frac * (mBuf[idx1] - mBuf[idx0]);
    }

    float mBuf[kBufSize] {};
    int   mWritePos  { 0 };
    float mOffset1   { static_cast<float>(kBufSize / 4) };
    float mOffset2   { static_cast<float>(kBufSize * 3 / 4) };
    float mPitchRatio { 1.0f };
};

} // namespace Octofilter
