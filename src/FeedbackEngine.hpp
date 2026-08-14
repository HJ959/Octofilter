#pragma once

#include "DCBlocker.hpp"
#include "IPitchShifter.hpp"
#include "NullShifter.hpp"
#include "PeakLimiter.hpp"

namespace Octofilter
{

/**
 * FeedbackEngine — per-point feedback path.
 *
 * Signal flow each sample:
 *
 *   filteredOutput
 *       │
 *       ▼
 *   IPitchShifter      ← pitch shift (PhaseVocoderShifter or NullShifter)
 *       │
 *       ▼
 *   DCBlocker          ← removes DC build-up
 *       │
 *       ▼
 *   PeakLimiter        ← prevents blowup
 *       │
 *       ▼
 *   feedbackSample     ← one-sample delay (stored, injected next sample)
 *
 * The stored feedbackSample is injected back into the point's input
 * at the START of the next sample:
 *
 *   pointInput[n] = dryInput[n] + feedbackAmount * feedbackSample[n-1]
 *
 * Note: the pitch shifter introduces latency (windowSize samples for the
 * phase vocoder). This is a design characteristic of the effect — the
 * feedback spiral timing is determined by this latency.
 *
 * prepare() must be called before processing (e.g. from activate()).
 */
class FeedbackEngine
{
public:
    FeedbackEngine() noexcept = default;

    /** Call from activate() / sampleRateChanged(). No heap allocs here. */
    void prepare(double sampleRate) noexcept
    {
        mDCBlocker.setSampleRate(sampleRate);
        mLimiter.prepare(sampleRate);
        // Pitch shifter is prepared separately via setPitchShifter()
        reset();
    }

    void reset() noexcept
    {
        mDCBlocker.reset();
        mLimiter.reset();
        if (mShifter) mShifter->reset();
        mFeedbackSample = 0.0f;
    }

    /**
     * Assign the pitch shifter to use. Call before prepare().
     * The FeedbackEngine does NOT own the shifter's lifetime —
     * PointState owns it.
     */
    void setPitchShifter(IPitchShifter* shifter) noexcept
    {
        mShifter = shifter;
    }

    /**
     * Mix dry input with the stored (previous-sample) feedback.
     * Call BEFORE the filter.
     */
    float mixInput(float dryInput, float feedbackAmount) const noexcept
    {
        return dryInput + feedbackAmount * mFeedbackSample;
    }

    /**
     * Process the filter output through the full feedback path.
     * Call AFTER the filter, once per sample.
     *
     * The pitch shifter works on one sample at a time here — it accumulates
     * internally and produces output with latency equal to its window size.
     */
    void processOutput(float filteredOutput) noexcept
    {
        // 1. Pitch shift (one sample in, one sample out — shifter buffers internally)
        float shifted = filteredOutput;
        if (mShifter)
        {
            mShifter->process(&filteredOutput, &shifted, 1);
        }

        // 2. DC block
        const float dcBlocked = mDCBlocker.process(shifted);

        // 3. Limit
        mFeedbackSample = mLimiter.process(dcBlocked);
    }

    /** Stored feedback value (read-only, for testing/metering). */
    float getFeedbackSample() const noexcept { return mFeedbackSample; }

private:
    DCBlocker    mDCBlocker;
    PeakLimiter  mLimiter;
    IPitchShifter* mShifter { nullptr };
    float        mFeedbackSample { 0.0f };
};

} // namespace Octofilter
