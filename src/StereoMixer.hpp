#pragma once

#include <cmath>

namespace Octofilter
{

/**
 * StereoMixer
 *
 * Provides equal-power panning and accumulates per-point signals
 * into a stereo L/R output bus.
 *
 * Pan law: L = cos(pan * π/2), R = sin(pan * π/2)
 *   pan = -1  → L=1,    R=0    (hard left)
 *   pan =  0  → L=0.707, R=0.707 (centre, -3 dB on each side)
 *   pan = +1  → L=0,    R=1    (hard right)
 *
 * The pan argument is normalised: [-1, 1] maps to [0°, 90°] of the
 * cosine/sine argument via angle = (pan + 1) * π/4.
 */
class StereoMixer
{
public:
    /**
     * Compute equal-power pan gains for a given pan position.
     *
     * @param pan     Pan position in [-1, 1]
     * @param gainL   Output: left channel gain
     * @param gainR   Output: right channel gain
     */
    static void panGains(float pan, float& gainL, float& gainR) noexcept
    {
        // Map [-1, 1] → [0, π/2]
        const float angle = (pan + 1.0f) * (static_cast<float>(M_PI) / 4.0f);
        gainL = std::cos(angle);
        gainR = std::sin(angle);
    }

    /**
     * Accumulate one point's filtered sample into the L/R bus.
     *
     * @param sample  Filtered, level-scaled sample from one point
     * @param pan     Pan position for this point [-1, 1]
     * @param outL    Accumulation buffer for left channel (in/out)
     * @param outR    Accumulation buffer for right channel (in/out)
     */
    static void accumulate(float  sample,
                           float  pan,
                           float& outL,
                           float& outR) noexcept
    {
        float gainL, gainR;
        panGains(pan, gainL, gainR);
        outL += sample * gainL;
        outR += sample * gainR;
    }

    /**
     * Zero the output buffers for one frame.
     * Call at the start of each sample's accumulation pass.
     */
    static void clearFrame(float& outL, float& outR) noexcept
    {
        outL = 0.0f;
        outR = 0.0f;
    }
};

} // namespace Octofilter
