#pragma once

#include <cstdint>

namespace Octofilter
{

/**
 * IPitchShifter — abstract interface for pitch shift implementations.
 *
 * The concrete implementation (PhaseVocoderShifter) is swappable behind
 * this interface. NullShifter provides a zero-cost pass-through when
 * pitch shift is 0 or the feature is bypassed.
 *
 * Threading: all methods called from the audio thread only.
 * RT contract: process() must never allocate, lock, or block.
 */
class IPitchShifter
{
public:
    virtual ~IPitchShifter() = default;

    /**
     * Prepare for processing. Called from activate() / sampleRateChanged().
     * Implementations must pre-allocate all buffers here.
     *
     * @param sampleRate    Host sample rate in Hz
     * @param maxBlockSize  Maximum number of samples per process() call
     */
    virtual void prepare(double sampleRate, int maxBlockSize) = 0;

    /**
     * Set the pitch shift amount.
     * @param semitones  Shift in semitones, range [-24, +24]
     */
    virtual void setShift(float semitones) = 0;

    /**
     * Process a block of audio in-place.
     * in and out may alias (implementations must handle this).
     *
     * @param in          Input buffer
     * @param out         Output buffer
     * @param numSamples  Number of samples to process
     */
    virtual void process(const float* in, float* out, int numSamples) = 0;

    /** Reset internal state (e.g. on transport restart). */
    virtual void reset() = 0;

    /** Return the latency introduced by this shifter in samples. */
    virtual int getLatencySamples() const = 0;
};

} // namespace Octofilter
