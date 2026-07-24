#pragma once

#include "PointState.hpp"
#include <cstdint>

namespace Octofilter
{

/**
 * InputRouter
 *
 * Assigns each filter point its source channel based on the plugin's
 * input configuration and point count:
 *
 *   Mono (1 channel):
 *     All points get channel 0.
 *
 *   Stereo (2 channels):
 *     First half of points → channel 0 (L)
 *     Second half of points → channel 1 (R)
 *     If pointCount is odd, the middle point gets sourceChannel = -1
 *     (meaning equal L+R mix, handled in the processing loop).
 *
 * Call assign() whenever pointCount or inputChannelCount changes.
 * It writes directly into the sourceChannel field of each PointState.
 */
class InputRouter
{
public:
    /**
     * Assign source channels to all active points.
     *
     * @param points           Array of PointState (length >= pointCount)
     * @param pointCount       Number of active points (2–8)
     * @param inputChannelCount 1 = mono, 2 = stereo
     */
    static void assign(PointState*   points,
                       int           pointCount,
                       int           inputChannelCount) noexcept
    {
        for (int i = 0; i < pointCount; ++i)
        {
            if (inputChannelCount < 2)
            {
                // Mono: everything goes to channel 0
                points[i].sourceChannel = 0;
            }
            else
            {
                // Stereo split
                if (pointCount == 1)
                {
                    points[i].sourceChannel = -1; // mix
                }
                else if (pointCount % 2 == 0)
                {
                    // Even count: clean 50/50 split
                    points[i].sourceChannel = (i < pointCount / 2) ? 0 : 1;
                }
                else
                {
                    // Odd count: middle point gets mix, rest split evenly
                    const int half = pointCount / 2;
                    if (i < half)
                        points[i].sourceChannel = 0;
                    else if (i > half)
                        points[i].sourceChannel = 1;
                    else
                        points[i].sourceChannel = -1; // centre point = mix
                }
            }
        }
    }

    /**
     * Read the input sample for a given point, handling the mix case.
     *
     * @param inputs      Audio input buffer array (inputs[0]=L, inputs[1]=R)
     * @param frame       Sample index within the current block
     * @param point       The PointState whose sourceChannel to use
     * @param numInputs   Number of available input channels
     */
    static float getSample(const float** inputs,
                           uint32_t      frame,
                           const PointState& point,
                           int           numInputs) noexcept
    {
        if (point.sourceChannel == -1)
        {
            // Equal L+R mix (normalised to preserve loudness)
            const float l = (numInputs > 0) ? inputs[0][frame] : 0.0f;
            const float r = (numInputs > 1) ? inputs[1][frame] : l;
            return (l + r) * 0.5f;
        }

        if (point.sourceChannel < numInputs)
            return inputs[point.sourceChannel][frame];

        return 0.0f;
    }
};

} // namespace Octofilter
