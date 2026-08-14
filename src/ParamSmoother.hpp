#pragma once

#include <cmath>

namespace Octofilter
{

/**
 * ParamSmoother — one-pole IIR parameter smoother.
 * Prevents zipper noise on automated parameters.
 *
 * Can operate at sample-rate (setTarget per sample) or block-rate
 * (setTargetBlock once per block). Use setBlockRate() to configure
 * for block-rate operation.
 */
struct ParamSmoother
{
    float current { 0.0f };
    float coeff   { 0.5f };

    /** Configure for sample-rate operation (~10ms time constant). */
    void setTargetRate(float sampleRate) noexcept
    {
        coeff = std::exp(-1.0f / (0.010f * sampleRate));
    }

    /** Configure for block-rate operation (~10ms time constant). */
    void setBlockRate(float sampleRate, int blockSize) noexcept
    {
        const float blocksPerSec = sampleRate / static_cast<float>(blockSize);
        coeff = std::exp(-1.0f / (0.010f * blocksPerSec));
    }

    /** Snap to value immediately (no smoothing). */
    void set(float v) noexcept { current = v; }

    /** Advance one step toward target. Call once per sample or once per block. */
    void setTarget(float target) noexcept
    {
        current += (1.0f - coeff) * (target - current);
    }

    float get() const noexcept { return current; }
};

} // namespace Octofilter
