#pragma once

#include <cmath>

namespace Octofilter
{

/**
 * ParamSmoother — one-pole IIR parameter smoother.
 * Prevents zipper noise on automated parameters.
 * Time constant ~10 ms at 44.1 kHz by default.
 */
struct ParamSmoother
{
    float current { 0.0f };
    float coeff   { 0.99f };

    void setTargetRate(float sampleRate) noexcept
    {
        coeff = std::exp(-1.0f / (0.010f * sampleRate));
    }

    /** Snap to value immediately (no smoothing). */
    void set(float v) noexcept { current = v; }

    /** Advance one sample toward target. */
    void setTarget(float target) noexcept
    {
        current += (1.0f - coeff) * (target - current);
    }

    float get() const noexcept { return current; }
};

} // namespace Octofilter
