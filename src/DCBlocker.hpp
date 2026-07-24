#pragma once

#include <cmath>

namespace Octofilter
{

/**
 * DCBlocker — first-order high-pass filter for DC removal.
 *
 * Uses the classic one-pole IIR structure:
 *   y[n] = x[n] - x[n-1] + R * y[n-1]
 *
 * where R = 1 - (2π * cornerHz / sampleRate).
 *
 * At 44.1 kHz with a 20 Hz corner, R ≈ 0.99715.
 * One instance lives on each per-point feedback path.
 */
class DCBlocker
{
public:
    static constexpr float kDefaultCornerHz = 20.0f;

    DCBlocker() noexcept = default;

    void setSampleRate(double sampleRate, float cornerHz = kDefaultCornerHz) noexcept
    {
        mR  = 1.0f - (2.0f * static_cast<float>(M_PI) * cornerHz
                      / static_cast<float>(sampleRate));
        if (mR < 0.0f) mR = 0.0f;
        if (mR > 0.9999f) mR = 0.9999f;
    }

    void reset() noexcept
    {
        mXprev = 0.0f;
        mYprev = 0.0f;
    }

    float process(float x) noexcept
    {
        const float y = x - mXprev + mR * mYprev;
        mXprev = x;
        mYprev = y;
        return y;
    }

private:
    float mR     { 0.99715f }; // default for 20 Hz @ 44.1 kHz
    float mXprev { 0.0f };
    float mYprev { 0.0f };
};

} // namespace Octofilter
