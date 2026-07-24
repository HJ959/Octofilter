#pragma once

#include <cmath>

namespace Octofilter
{

/**
 * HarmonicMapper
 *
 * Assigns filter cutoff targets based on the natural harmonic series.
 *
 * Given a fundamental frequency (from the Texture knob) and a spread factor,
 * computes the harmonic number for each point and returns the target cutoff
 * in Hz.
 *
 * Natural harmonic series: 1×, 2×, 3×, 4×, 5×, ...
 *
 * Spread controls how far up the series the points reach:
 *   spread = 1.0: points span harmonics 1 through N
 *   spread = 0.5: points cluster in lower harmonics (1 through ~N/2)
 *   spread = 0.0: all points sit at the fundamental
 *
 * Formula: harmonicNumber[i] = 1 + round(i * spread * (N-1) / (N-1))
 *        simplified: harmonicNumber[i] = 1 + round(i * spread)
 *        for i in [0, N-1]
 */
class HarmonicMapper
{
public:
    static constexpr float kMinFreqHz = 20.0f;
    static constexpr float kMaxFreqHz = 20000.0f;

    /**
     * Compute the harmonic number for a given point.
     *
     * @param pointIndex   0-based point index
     * @param pointCount   Total active points (2–8)
     * @param spread       Spread factor (0–1)
     * @return             Harmonic number (1 = fundamental, 2 = 2nd harmonic, etc.)
     */
    static int harmonicNumber(int pointIndex, int pointCount, float spread) noexcept
    {
        if (pointCount <= 1 || spread <= 0.0f)
            return 1;

        // Distribute points across harmonics 1..maxHarmonic
        // maxHarmonic scales with spread: at spread=1 we reach harmonic N
        const float maxHarmonic = 1.0f + spread * static_cast<float>(pointCount - 1);
        const float t = static_cast<float>(pointIndex) / static_cast<float>(pointCount - 1);
        const int   h = static_cast<int>(std::round(1.0f + t * (maxHarmonic - 1.0f)));
        return (h < 1) ? 1 : h;
    }

    /**
     * Compute the target cutoff Hz for a point in harmonic mode.
     *
     * @param fundamentalHz  Base frequency from Texture knob
     * @param pointIndex     0-based point index
     * @param pointCount     Total active points
     * @param spread         Spread factor (0–1)
     * @return               Target cutoff frequency in Hz, clamped to [20, 20000]
     */
    static float targetCutoff(float fundamentalHz, int pointIndex,
                              int pointCount, float spread) noexcept
    {
        const int h = harmonicNumber(pointIndex, pointCount, spread);
        float freq  = fundamentalHz * static_cast<float>(h);

        if (freq < kMinFreqHz)  freq = kMinFreqHz;
        if (freq > kMaxFreqHz)  freq = kMaxFreqHz;
        return freq;
    }

    /**
     * Compute the fundamental frequency from a Texture knob value (0–1).
     * Same log mapping as TextureMapper.
     */
    static float textureToFundamental(float texture) noexcept
    {
        if (texture < 0.0f) texture = 0.0f;
        if (texture > 1.0f) texture = 1.0f;
        return kMinFreqHz * std::pow(kMaxFreqHz / kMinFreqHz, texture);
    }
};

} // namespace Octofilter
