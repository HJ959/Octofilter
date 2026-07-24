#pragma once

#include <cmath>
#include <cstdint>

namespace Octofilter
{

/**
 * TextureMapper
 *
 * Converts the global Texture knob value (0–1, linear) to a centre cutoff
 * frequency on a logarithmic scale (20 Hz – 20 kHz), then applies a
 * per-point semitone offset to produce each point's actual cutoff frequency.
 *
 * log mapping: freq = 20 * (20000/20)^texture = 20 * 1000^texture
 */
class TextureMapper
{
public:
    static constexpr float kMinFreqHz  = 20.0f;
    static constexpr float kMaxFreqHz  = 20000.0f;
    static constexpr float kFreqRatio  = kMaxFreqHz / kMinFreqHz; // 1000

    /**
     * Convert a normalised texture value (0–1) to centre frequency in Hz.
     */
    static float textureToCentreFreq(float texture) noexcept
    {
        // Clamp input
        if (texture < 0.0f) texture = 0.0f;
        if (texture > 1.0f) texture = 1.0f;

        return kMinFreqHz * std::pow(kFreqRatio, texture);
    }

    /**
     * Apply a semitone offset to a frequency.
     * semitones can be positive or negative.
     */
    static float applySemitoneOffset(float freqHz, float semitones) noexcept
    {
        // Each semitone = 2^(1/12)
        return freqHz * std::pow(2.0f, semitones / 12.0f);
    }

    /**
     * Full pipeline: texture (0–1) + per-point semitone offset → cutoff Hz.
     * Result is clamped to [20, 20000].
     *
     * @param texture         Global texture knob (0–1)
     * @param semitoneOffset  Per-point random offset in semitones
     * @param spreadRange     TextureSpread knob (0–1): scales the offset range.
     *                        At 0 all points sit at centre freq; at 1 offsets
     *                        are applied at full magnitude.
     */
    static float computeCutoff(float texture, float semitoneOffset,
                               float spreadRange = 1.0f) noexcept
    {
        const float centre = textureToCentreFreq(texture);
        const float scaled = applySemitoneOffset(centre, semitoneOffset * spreadRange);

        if (scaled < kMinFreqHz)  return kMinFreqHz;
        if (scaled > kMaxFreqHz)  return kMaxFreqHz;
        return scaled;
    }
};

} // namespace Octofilter
