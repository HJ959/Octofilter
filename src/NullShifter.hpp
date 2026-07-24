#pragma once

#include "IPitchShifter.hpp"

namespace Octofilter
{

/**
 * NullShifter — zero-cost pass-through pitch shifter.
 *
 * Used when pitchShift == 0 or as a compile-time placeholder.
 * Introduces no latency and does no work.
 */
class NullShifter final : public IPitchShifter
{
public:
    void prepare(double /*sampleRate*/, int /*maxBlockSize*/) override {}
    void setShift(float /*semitones*/) override {}
    void reset() override {}
    int  getLatencySamples() const override { return 0; }

    void process(const float* in, float* out, int numSamples) override
    {
        if (in == out) return;
        for (int i = 0; i < numSamples; ++i)
            out[i] = in[i];
    }
};

} // namespace Octofilter
