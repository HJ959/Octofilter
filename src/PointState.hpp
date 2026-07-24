#pragma once

#include "BiQuadFilter.hpp"
#include "FeedbackEngine.hpp"
#include "NullShifter.hpp"
#include "PhaseVocoderShifter.hpp"
#include <cstdint>

namespace Octofilter
{

static constexpr int kMaxPoints = 8;

/**
 * PointState — all per-point data owned in one place.
 *
 * Each of the 2–8 filter points has one PointState. The plugin
 * maintains an array of kMaxPoints of these; only the first
 * activePointCount are processed.
 */
struct PointState
{
    // ── DSP components ────────────────────────────────────────────────────
    BiQuadFilter        filter;
    PhaseVocoderShifter shifter;
    FeedbackEngine      feedback;

    // ── Parameters ────────────────────────────────────────────────────────

    BiQuadFilter::Type filterType { BiQuadFilter::Type::LowPass };

    /** Randomised semitone offset from global Texture. */
    float cutoffOffsetSemitones { 0.0f };

    /** Pan position [-1, 1]. */
    float pan { 0.0f };

    /** Per-point output gain 0–2. */
    float level { 1.0f };

    /** Feedback send amount 0–1. */
    float feedbackAmount { 0.0f };

    /** Pitch shift in semitones ±24. */
    float pitchShiftSemitones { 0.0f };

    /**
     * Source channel: 0=L, 1=R, -1=L+R mix.
     * Set by InputRouter.
     */
    int sourceChannel { 0 };

    // ── Helpers ───────────────────────────────────────────────────────────

    void applyFilterParams(float cutoffHz, float q) noexcept
    {
        filter.setType(filterType);
        filter.setCutoff(cutoffHz);
        filter.setQ(q);
    }

    /** Prepare all DSP for a given sample rate. */
    void prepare(double sampleRate) noexcept
    {
        filter.setSampleRate(sampleRate);
        shifter.prepare(sampleRate, 512 /* maxBlockSize hint */);
        shifter.setShift(pitchShiftSemitones);
        feedback.setPitchShifter(&shifter);
        feedback.prepare(sampleRate);
    }

    /** Update pitch shift amount. */
    void setPitchShift(float semitones) noexcept
    {
        pitchShiftSemitones = semitones;
        shifter.setShift(semitones);
    }

    /** Reset all DSP state. */
    void reset() noexcept
    {
        filter.reset();
        feedback.reset(); // also resets shifter via FeedbackEngine
    }
};

} // namespace Octofilter
