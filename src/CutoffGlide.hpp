#pragma once

#include <cmath>

namespace Octofilter
{

/**
 * CutoffGlide — per-point one-pole IIR smoother for filter cutoff.
 *
 * Each point has its own glide instance so they move independently
 * toward their respective targets. Higher harmonics have larger
 * frequency jumps, so they naturally take longer to settle, creating
 * a cascading sweep effect.
 *
 * The glide operates in Hz space (not log), which means higher
 * frequencies glide faster in perceived pitch — this is intentional
 * for a more organic/natural feel.
 *
 * Coefficient is pre-computed from glide time and sample rate
 * (not per-sample).
 */
struct CutoffGlide
{
    float current { 1000.0f };  // current smoothed cutoff Hz
    float target  { 1000.0f };  // target cutoff Hz
    float coeff   { 0.999f };   // one-pole coefficient (higher = slower glide)

    /**
     * Set the glide time and sample rate. Call when either changes.
     *
     * @param glideTimeMs  Glide time in milliseconds (10–2000)
     * @param sampleRate   Host sample rate
     * @param blockSize    Samples per block (glide advances once per block)
     */
    void setGlideTime(float glideTimeMs, float sampleRate, int blockSize) noexcept
    {
        // Coefficient for a one-pole filter that reaches ~63% in glideTime
        // Updated once per block, so factor in block size
        const float glideTimeSec = glideTimeMs * 0.001f;
        const float blocksPerGlide = (glideTimeSec * sampleRate)
                                     / static_cast<float>(blockSize);
        if (blocksPerGlide > 0.0f)
            coeff = std::exp(-1.0f / blocksPerGlide);
        else
            coeff = 0.0f; // instant
    }

    /**
     * Set a new target. The glide will move current toward this value.
     */
    void setTarget(float targetHz) noexcept { target = targetHz; }

    /**
     * Advance the glide by one step (call once per block).
     * Returns the current smoothed value.
     */
    float advance() noexcept
    {
        current = current + (1.0f - coeff) * (target - current);
        return current;
    }

    /**
     * Snap to target instantly (no glide). Use on reset or mode switch.
     */
    void snap() noexcept { current = target; }

    /**
     * Snap to a specific value.
     */
    void snapTo(float hz) noexcept { current = target = hz; }
};

} // namespace Octofilter
