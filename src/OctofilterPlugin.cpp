#include "OctofilterPlugin.hpp"
#include <cmath>
#include <cstring>
#include <cstdio>
#include <random>

#if defined(__SSE__)
#include <xmmintrin.h>
#endif
#if defined(__SSE3__)
#include <pmmintrin.h>
#endif

START_NAMESPACE_DISTRHO

// ── Parameter index scheme ────────────────────────────────────────────────────
// Global params: indices 0–10
// Per-point params: indices 11–66 (7 params × 8 points)
enum GlobalParams : uint32_t
{
    kGlobalPointCount   = 0,
    kGlobalTexture      = 1,
    kGlobalTextureSpread= 2,
    kGlobalSpread       = 3,
    kGlobalResonance    = 4,
    kGlobalFeedback     = 5,
    kGlobalPitchShift   = 6,
    kGlobalInputGain    = 7,
    kGlobalOutputGain   = 8,
    kGlobalWetDry       = 9,
    kGlobalRandomise    = 10,
    kGlobalHarmonicMode = 11,
    kGlobalGlideTime    = 12,
};

enum PerPointOffset : uint32_t
{
    kPPFilterType   = 0,
    kPPCutoffOffset = 1,
    kPPQ            = 2,
    kPPPan          = 3,
    kPPLevel        = 4,
    kPPFeedback     = 5,
    kPPPitchShift   = 6,
};

// Returns the flat parameter index for a per-point parameter
static uint32_t ppIdx(int point, PerPointOffset offset)
{
    return static_cast<uint32_t>(kNumGlobalParams + point * kNumPerPointParams
                                 + static_cast<int>(offset));
}

// ── Constructor ───────────────────────────────────────────────────────────────
OctofilterPlugin::OctofilterPlugin()
    : Plugin(kTotalParams, 0 /* programs */, 1 /* states */)
{
    // Initialise per-point arrays to defaults
    for (int i = 0; i < 8; ++i)
    {
        mPointFilterType[i]   = 0.0f;  // LP
        mPointCutoffOffset[i] = 0.0f;
        mPointQ[i]            = 0.0f;  // 0 = use global
        mPointLevel[i]        = 1.0f;
        mPointFeedback[i]     = 0.0f;
        mPointPitchShift[i]   = 0.0f;
    }

    // Spread pan positions evenly
    rebuildRouting();
    updateFilterParams();
    prepareAllPoints();
}

// ── activate ──────────────────────────────────────────────────────────────────
void OctofilterPlugin::activate()
{
#if defined(__SSE__)
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
#endif
#if defined(__SSE3__)
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#endif
    prepareAllPoints();

    // Set smoother coefficients
    const float sr = static_cast<float>(mSampleRate);
    mSmTexture.setTargetRate(sr);
    mSmFeedback.setTargetRate(sr);
    mSmPitchShift.setTargetRate(sr);
    mSmWetDry.setTargetRate(sr);
    mSmInputGain.setTargetRate(sr);
    mSmOutputGain.setTargetRate(sr);

    // Seed smoothers with current values
    mSmTexture.set(mTexture);
    mSmFeedback.set(mFeedback);
    mSmPitchShift.set(mPitchShift);
    mSmWetDry.set(mWetDry);
    mSmInputGain.set(mInputGainLin);
    mSmOutputGain.set(mOutputGainLin);
}

// ── sampleRateChanged ─────────────────────────────────────────────────────────
void OctofilterPlugin::sampleRateChanged(double newSampleRate)
{
    mSampleRate = newSampleRate;
    prepareAllPoints();
    updateFilterParams();

    const float sr = static_cast<float>(newSampleRate);
    mSmTexture.setTargetRate(sr);
    mSmFeedback.setTargetRate(sr);
    mSmPitchShift.setTargetRate(sr);
    mSmWetDry.setTargetRate(sr);
    mSmInputGain.setTargetRate(sr);
    mSmOutputGain.setTargetRate(sr);
}

// ── initParameter ─────────────────────────────────────────────────────────────
void OctofilterPlugin::initParameter(uint32_t index, Parameter& p)
{
    // ── Globals ───────────────────────────────────────────────────────────
    if (index == kGlobalPointCount)
    {
        p.name = "Point Count"; p.symbol = "point_count";
        p.ranges.min = 1.0f; p.ranges.max = 8.0f; p.ranges.def = 2.0f;
        p.hints = kParameterIsAutomatable | kParameterIsInteger;
        return;
    }
    if (index == kGlobalTexture)
    {
        p.name = "Texture"; p.symbol = "texture";
        p.ranges.min = 0.0f; p.ranges.max = 1.0f; p.ranges.def = 0.5f;
        p.hints = kParameterIsAutomatable; return;
    }
    if (index == kGlobalTextureSpread)
    {
        p.name = "Texture Spread"; p.symbol = "texture_spread";
        p.ranges.min = 0.0f; p.ranges.max = 1.0f; p.ranges.def = 1.0f;
        p.hints = kParameterIsHidden; return;
    }
    if (index == kGlobalSpread)
    {
        p.name = "Spread"; p.symbol = "spread";
        p.ranges.min = 0.0f; p.ranges.max = 1.0f; p.ranges.def = 1.0f;
        p.hints = kParameterIsHidden; return;
    }
    if (index == kGlobalResonance)
    {
        p.name = "Resonance"; p.symbol = "resonance";
        p.ranges.min = 0.1f; p.ranges.max = 20.0f; p.ranges.def = 0.707f;
        p.hints = kParameterIsAutomatable; return;
    }
    if (index == kGlobalFeedback)
    {
        p.name = "Feedback"; p.symbol = "feedback";
        p.ranges.min = 0.0f; p.ranges.max = 1.0f; p.ranges.def = 0.3f;
        p.hints = kParameterIsAutomatable; return;
    }
    if (index == kGlobalPitchShift)
    {
        p.name = "Pitch Shift"; p.symbol = "pitch_shift"; p.unit = "st";
        p.ranges.min = -24.0f; p.ranges.max = 24.0f; p.ranges.def = 0.0f;
        p.hints = kParameterIsAutomatable; return;
    }
    if (index == kGlobalInputGain)
    {
        p.name = "Input Gain"; p.symbol = "input_gain"; p.unit = "dB";
        p.ranges.min = -24.0f; p.ranges.max = 24.0f; p.ranges.def = 0.0f;
        p.hints = kParameterIsAutomatable; return;
    }
    if (index == kGlobalOutputGain)
    {
        p.name = "Output Gain"; p.symbol = "output_gain"; p.unit = "dB";
        p.ranges.min = -24.0f; p.ranges.max = 24.0f; p.ranges.def = 0.0f;
        p.hints = kParameterIsAutomatable; return;
    }
    if (index == kGlobalWetDry)
    {
        p.name = "Wet/Dry"; p.symbol = "wet_dry";
        p.ranges.min = 0.0f; p.ranges.max = 1.0f; p.ranges.def = 1.0f;
        p.hints = kParameterIsAutomatable; return;
    }
    if (index == kGlobalRandomise)
    {
        p.name = "Randomise"; p.symbol = "randomise";
        p.ranges.min = 0.0f; p.ranges.max = 1.0f; p.ranges.def = 0.0f;
        p.hints = kParameterIsAutomatable | kParameterIsBoolean | kParameterIsInteger;
        return;
    }
    if (index == kGlobalHarmonicMode)
    {
        p.name = "Harmonic Mode"; p.symbol = "harmonic_mode";
        p.ranges.min = 0.0f; p.ranges.max = 1.0f; p.ranges.def = 0.0f;
        p.hints = kParameterIsAutomatable | kParameterIsBoolean | kParameterIsInteger;
        return;
    }
    if (index == kGlobalGlideTime)
    {
        p.name = "Glide Time"; p.symbol = "glide_time"; p.unit = "ms";
        p.ranges.min = 10.0f; p.ranges.max = 2000.0f; p.ranges.def = 200.0f;
        p.hints = kParameterIsAutomatable | kParameterIsLogarithmic;
        return;
    }

    // ── Per-point params ──────────────────────────────────────────────────
    if (index >= static_cast<uint32_t>(kNumGlobalParams))
    {
        const int rel   = static_cast<int>(index) - kNumGlobalParams;
        const int point = rel / kNumPerPointParams;
        const int off   = rel % kNumPerPointParams;

        char name[64], sym[64];

        switch (off)
        {
        case kPPFilterType:
            std::snprintf(name, sizeof(name), "P%d Filter Type", point + 1);
            std::snprintf(sym,  sizeof(sym),  "p%d_filter_type", point + 1);
            p.name = name; p.symbol = sym;
            p.ranges.min = 0.0f; p.ranges.max = 3.0f; p.ranges.def = 0.0f;
            p.hints = kParameterIsAutomatable | kParameterIsInteger;
            break;
        case kPPCutoffOffset:
            std::snprintf(name, sizeof(name), "P%d Cutoff Offset", point + 1);
            std::snprintf(sym,  sizeof(sym),  "p%d_cutoff_offset", point + 1);
            p.name = name; p.symbol = sym; p.unit = "st";
            p.ranges.min = -24.0f; p.ranges.max = 24.0f; p.ranges.def = 0.0f;
            p.hints = kParameterIsAutomatable;
            break;
        case kPPQ:
            std::snprintf(name, sizeof(name), "P%d Q", point + 1);
            std::snprintf(sym,  sizeof(sym),  "p%d_q", point + 1);
            p.name = name; p.symbol = sym;
            p.ranges.min = 0.0f; p.ranges.max = 20.0f; p.ranges.def = 0.0f;
            p.hints = kParameterIsAutomatable;
            break;
        case kPPPan:
            std::snprintf(name, sizeof(name), "P%d Pan", point + 1);
            std::snprintf(sym,  sizeof(sym),  "p%d_pan", point + 1);
            p.name = name; p.symbol = sym;
            p.ranges.min = -1.0f; p.ranges.max = 1.0f; p.ranges.def = 0.0f;
            p.hints = kParameterIsAutomatable;
            break;
        case kPPLevel:
            std::snprintf(name, sizeof(name), "P%d Level", point + 1);
            std::snprintf(sym,  sizeof(sym),  "p%d_level", point + 1);
            p.name = name; p.symbol = sym;
            p.ranges.min = 0.0f; p.ranges.max = 2.0f; p.ranges.def = 1.0f;
            p.hints = kParameterIsAutomatable;
            break;
        case kPPFeedback:
            std::snprintf(name, sizeof(name), "P%d Feedback", point + 1);
            std::snprintf(sym,  sizeof(sym),  "p%d_feedback", point + 1);
            p.name = name; p.symbol = sym;
            p.ranges.min = 0.0f; p.ranges.max = 1.0f; p.ranges.def = 0.0f;
            p.hints = kParameterIsAutomatable;
            break;
        case kPPPitchShift:
            std::snprintf(name, sizeof(name), "P%d Pitch Shift", point + 1);
            std::snprintf(sym,  sizeof(sym),  "p%d_pitch_shift", point + 1);
            p.name = name; p.symbol = sym; p.unit = "st";
            p.ranges.min = -24.0f; p.ranges.max = 24.0f; p.ranges.def = 0.0f;
            p.hints = kParameterIsAutomatable;
            break;
        default: break;
        }
    }
}

// ── getParameterValue ─────────────────────────────────────────────────────────
float OctofilterPlugin::getParameterValue(uint32_t index) const
{
    switch (index)
    {
    case kGlobalPointCount:    return static_cast<float>(mActivePoints);
    case kGlobalTexture:       return mTexture;
    case kGlobalTextureSpread: return mTextureSpread;
    case kGlobalSpread:        return mSpread;
    case kGlobalResonance:     return mResonance;
    case kGlobalFeedback:      return mFeedback;
    case kGlobalPitchShift:    return mPitchShift;
    case kGlobalInputGain:     return mInputGainDb;
    case kGlobalOutputGain:    return mOutputGainDb;
    case kGlobalWetDry:        return mWetDry;
    case kGlobalRandomise:     return 0.0f;
    case kGlobalHarmonicMode:  return mHarmonicMode;
    case kGlobalGlideTime:     return mGlideTimeMs;
    default: break;
    }

    if (index >= static_cast<uint32_t>(kNumGlobalParams))
    {
        const int rel   = static_cast<int>(index) - kNumGlobalParams;
        const int point = rel / kNumPerPointParams;
        const int off   = rel % kNumPerPointParams;
        if (point >= 8) return 0.0f;

        switch (off)
        {
        case kPPFilterType:   return mPointFilterType[point];
        case kPPCutoffOffset: return mPointCutoffOffset[point];
        case kPPQ:            return mPointQ[point];
        case kPPPan:          return mPointPan[point];
        case kPPLevel:        return mPointLevel[point];
        case kPPFeedback:     return mPointFeedback[point];
        case kPPPitchShift:   return mPointPitchShift[point];
        default: break;
        }
    }
    return 0.0f;
}

// ── setParameterValue ─────────────────────────────────────────────────────────
void OctofilterPlugin::setParameterValue(uint32_t index, float value)
{
    switch (index)
    {
    case kGlobalPointCount:
    {
        const int n = static_cast<int>(value + 0.5f);
        mActivePoints = (n < 1) ? 1 : (n > 8) ? 8 : n;
        rebuildRouting();
        updateFilterParams();
        return;
    }
    case kGlobalTexture:
        mTexture = value;
        updateFilterParams();
        return;
    case kGlobalTextureSpread:
        mTextureSpread = value;
        updateFilterParams();
        return;
    case kGlobalSpread:
        mSpread = value;
        updateFilterParams();
        return;
    case kGlobalResonance:
        mResonance = value;
        updateFilterParams();
        return;
    case kGlobalFeedback:
        mFeedback = value;
        return;
    case kGlobalPitchShift:
        mPitchShift = value;
        // Global pitch is applied as an offset in run(), not here
        return;
    case kGlobalInputGain:
        mInputGainDb  = value;
        mInputGainLin = std::pow(10.0f, value / 20.0f);
        return;
    case kGlobalOutputGain:
        mOutputGainDb  = value;
        mOutputGainLin = std::pow(10.0f, value / 20.0f);
        return;
    case kGlobalWetDry:
        mWetDry = value;
        return;
    case kGlobalRandomise:
        if (value > 0.5f && mLastRandomise <= 0.5f)
            doRandomise();
        mLastRandomise = value;
        return;
    case kGlobalHarmonicMode:
        mHarmonicMode = value;
        updateFilterParams();
        return;
    case kGlobalGlideTime:
        mGlideTimeMs = value;
        for (int i = 0; i < 8; ++i)
            mGlide[i].setGlideTime(mGlideTimeMs,
                                    static_cast<float>(mSampleRate), 512);
        return;
    default: break;
    }

    // Per-point
    if (index >= static_cast<uint32_t>(kNumGlobalParams))
    {
        const int rel   = static_cast<int>(index) - kNumGlobalParams;
        const int point = rel / kNumPerPointParams;
        const int off   = rel % kNumPerPointParams;
        if (point >= 8) return;

        switch (off)
        {
        case kPPFilterType:
        {
            mPointFilterType[point] = value;
            const int t = static_cast<int>(value + 0.5f) % 4;
            mPoints[point].filterType =
                static_cast<Octofilter::BiQuadFilter::Type>(t);
            updateFilterParams();
            return;
        }
        case kPPCutoffOffset:
            mPointCutoffOffset[point] = value;
            mPoints[point].cutoffOffsetSemitones = value;
            updateFilterParams();
            return;
        case kPPQ:
            mPointQ[point] = value;
            updateFilterParams();
            return;
        case kPPPan:
            mPointPan[point] = value;
            mPoints[point].pan = value;
            return;
        case kPPLevel:
            mPointLevel[point] = value;
            mPoints[point].level = value;
            return;
        case kPPFeedback:
            mPointFeedback[point] = value;
            mPoints[point].feedbackAmount = value * 1.3f;
            return;
        case kPPPitchShift:
            mPointPitchShift[point] = value;
            mPoints[point].setPitchShift(value);
            return;
        default: break;
        }
    }
}

// ── State (preset save/restore) ───────────────────────────────────────────────
#if DISTRHO_PLUGIN_WANT_STATE
void OctofilterPlugin::initState(uint32_t /*index*/, State& state)
{
    state.key          = "octofilter_state";
    state.defaultValue = "";
}

// Called from host/UI thread — write to pending buffer, signal audio thread
void OctofilterPlugin::setState(const char* key, const char* value)
{
    if (std::strcmp(key, "octofilter_state") != 0) return;

    PendingState ps;
    // Parse simple key=value lines: "co0=-3.5\nco1=7.2\n..." etc.
    const char* p = value;
    while (*p)
    {
        char kbuf[32] = {}, vbuf[32] = {};
        int ki = 0, vi = 0;
        while (*p && *p != '=') kbuf[ki++] = *p++;
        if (*p == '=') ++p;
        while (*p && *p != '\n') vbuf[vi++] = *p++;
        if (*p == '\n') ++p;

        // Cutoff offsets: co0..co7
        if (kbuf[0] == 'c' && kbuf[1] == 'o' && kbuf[2] >= '0' && kbuf[2] <= '7')
            ps.cutoffOffsets[kbuf[2] - '0'] = static_cast<float>(std::atof(vbuf));
        // Filter types: ft0..ft7
        else if (kbuf[0] == 'f' && kbuf[1] == 't' && kbuf[2] >= '0' && kbuf[2] <= '7')
            ps.filterTypes[kbuf[2] - '0'] = static_cast<float>(std::atof(vbuf));
        // Pitch shifts: ps0..ps7
        else if (kbuf[0] == 'p' && kbuf[1] == 's' && kbuf[2] >= '0' && kbuf[2] <= '7')
            ps.pitchShifts[kbuf[2] - '0'] = static_cast<float>(std::atof(vbuf));
        // Feedback amounts: fb0..fb7
        else if (kbuf[0] == 'f' && kbuf[1] == 'b' && kbuf[2] >= '0' && kbuf[2] <= '7')
            ps.feedbackAmts[kbuf[2] - '0'] = static_cast<float>(std::atof(vbuf));
        // RNG seed
        else if (std::strcmp(kbuf, "seed") == 0)
            ps.rngSeed = static_cast<uint64_t>(std::atoll(vbuf));
    }

    ps.valid       = true;
    mPendingState  = ps;
    mHasPendingState.store(true, std::memory_order_release);
}

String OctofilterPlugin::getState(const char* key) const
{
    if (std::strcmp(key, "octofilter_state") != 0) return String();

    char buf[1024] = {};
    int  pos = 0;
    for (int i = 0; i < 8; ++i)
    {
        pos += std::snprintf(buf + pos, sizeof(buf) - pos,
                             "co%d=%.4f\nft%d=%.0f\nps%d=%.4f\nfb%d=%.4f\n",
                             i, mPointCutoffOffset[i],
                             i, mPointFilterType[i],
                             i, mPointPitchShift[i],
                             i, mPointFeedback[i]);
    }
    std::snprintf(buf + pos, sizeof(buf) - pos, "seed=%llu\n",
                  static_cast<unsigned long long>(mRngSeed));
    return String(buf);
}
#endif // DISTRHO_PLUGIN_WANT_STATE

// ── doRandomise ───────────────────────────────────────────────────────────────
void OctofilterPlugin::doRandomise() noexcept
{
    std::mt19937_64 rng(++mRngSeed);

    std::uniform_real_distribution<float> offsetDist(-24.0f, 24.0f);
    std::uniform_int_distribution<int>    typeDist(0, 3);
    std::uniform_real_distribution<float> pitchDist(-12.0f, 12.0f);
    std::uniform_real_distribution<float> feedDist(0.0f, 0.7f);

    const bool harmonic = (mHarmonicMode > 0.5f);

    for (int i = 0; i < 8; ++i)
    {
        // In harmonic mode: don't randomise cutoff offsets (harmonics own them)
        if (!harmonic)
        {
            mPointCutoffOffset[i] = offsetDist(rng);
            mPoints[i].cutoffOffsetSemitones = mPointCutoffOffset[i];
            requestParameterValueChange(
                kNumGlobalParams + i * kNumPerPointParams + 1, mPointCutoffOffset[i]);
        }

        mPointFilterType[i]   = static_cast<float>(typeDist(rng));
        mPointPitchShift[i]   = pitchDist(rng);
        mPointFeedback[i]     = feedDist(rng);

        mPoints[i].filterType =
            static_cast<Octofilter::BiQuadFilter::Type>(
                static_cast<int>(mPointFilterType[i]));
        mPoints[i].setPitchShift(mPointPitchShift[i]);
        mPoints[i].feedbackAmount = mPointFeedback[i] * 1.3f;

        // Notify UI of changed values
        requestParameterValueChange(
            kNumGlobalParams + i * kNumPerPointParams + 0, mPointFilterType[i]);
        requestParameterValueChange(
            kNumGlobalParams + i * kNumPerPointParams + 5, mPointFeedback[i]);
        requestParameterValueChange(
            kNumGlobalParams + i * kNumPerPointParams + 6, mPointPitchShift[i]);
    }
    updateFilterParams();
}

// ── applyPendingState ─────────────────────────────────────────────────────────
void OctofilterPlugin::applyPendingState() noexcept
{
    if (!mHasPendingState.load(std::memory_order_acquire)) return;

    const PendingState& ps = mPendingState;
    mRngSeed = ps.rngSeed;

    for (int i = 0; i < 8; ++i)
    {
        mPointCutoffOffset[i] = ps.cutoffOffsets[i];
        mPointFilterType[i]   = ps.filterTypes[i];
        mPointPitchShift[i]   = ps.pitchShifts[i];
        mPointFeedback[i]     = ps.feedbackAmts[i];

        mPoints[i].cutoffOffsetSemitones = ps.cutoffOffsets[i];
        mPoints[i].filterType =
            static_cast<Octofilter::BiQuadFilter::Type>(
                static_cast<int>(ps.filterTypes[i]));
        mPoints[i].setPitchShift(ps.pitchShifts[i]);
        mPoints[i].feedbackAmount = ps.feedbackAmts[i] * 1.3f;
    }

    updateFilterParams();
    mHasPendingState.store(false, std::memory_order_release);
}

// ── run ───────────────────────────────────────────────────────────────────────
void OctofilterPlugin::run(const float** inputs, float** outputs, uint32_t frames)
{
    // Apply any pending state from host thread (preset load)
    applyPendingState();

    const int numInputs = (DISTRHO_PLUGIN_NUM_INPUTS >= 2) ? 2 : 1;
    float* outL = outputs[0];
    float* outR = outputs[1];

    // Advance per-point cutoff glide (once per block)
    for (int i = 0; i < mActivePoints; ++i)
    {
        const float glidedCutoff = mGlide[i].advance();
        float q = (mPointQ[i] > 0.0f) ? mPointQ[i] : mResonance;

        // Dynamic Q ceiling: reduce Q when feedback×pitch is high to prevent piercing
        const float totalPitch = mPointPitchShift[i] + mPitchShift;
        const float fbPitchDanger = mFeedback * (std::fabs(totalPitch) / 24.0f);
        if (fbPitchDanger > 0.2f)
        {
            // Scale Q down: at max danger (fb=1, pitch=24) Q is capped to ~2
            const float qCeiling = 20.0f * (1.0f - fbPitchDanger * 0.9f);
            if (q > qCeiling) q = qCeiling;
        }

        mPoints[i].applyFilterParams(glidedCutoff, q);

        // Apply global pitch shift as offset on top of per-point pitch
        mPoints[i].setPitchShift(totalPitch);
    }

    // All globals used directly — no smoothing (glide handles cutoff transitions)
    const float smoothFeedback = mFeedback * 1.3f;
    const float smoothWetDry   = mWetDry;
    const float smoothInGain   = mInputGainLin;
    const float smoothOutGain  = mOutputGainLin;

    for (uint32_t f = 0; f < frames; ++f)
    {
        float wetL = 0.0f, wetR = 0.0f;

        for (int p = 0; p < mActivePoints; ++p)
        {
            Octofilter::PointState& pt = mPoints[p];

            // Feedback: direct and aggressive like pre-Phase 4
            // Global feedback applies to all points at full strength
            const float fbAmt = smoothFeedback;

            // Input + input gain + feedback mix
            const float dryIn =
                Octofilter::InputRouter::getSample(inputs, f, pt, numInputs)
                * smoothInGain;
            const float mixedIn = pt.feedback.mixInput(dryIn, fbAmt);

            // Filter
            const float filtered = pt.filter.process(mixedIn);

            // Store through feedback path
            pt.feedback.processOutput(filtered);

            // Level and pan
            const float scaled = filtered * pt.level;
            Octofilter::StereoMixer::accumulate(scaled, pt.pan, wetL, wetR);
        }

        // Normalise by point count
        const float norm = 1.0f / static_cast<float>(mActivePoints);
        wetL *= norm;
        wetR *= norm;

        // Wet/dry + output gain
        const float dryL = inputs[0][f];
        const float dryR = (numInputs > 1) ? inputs[1][f] : inputs[0][f];

        outL[f] = (dryL * (1.0f - smoothWetDry) + wetL * smoothWetDry) * smoothOutGain;
        outR[f] = (dryR * (1.0f - smoothWetDry) + wetR * smoothWetDry) * smoothOutGain;

        // Output safety: DC block then limit to prevent ear damage
        outL[f] = mOutputLimiterL.process(mOutputDCL.process(outL[f]));
        outR[f] = mOutputLimiterR.process(mOutputDCR.process(outR[f]));

        // Write to waveform display buffer (L+R mix)
        mWaveformBuf[mWaveformWritePos] = (outL[f] + outR[f]) * 0.5f;
        mWaveformWritePos = (mWaveformWritePos + 1) % kWaveformBufSize;
    }
}

// ── Private helpers ───────────────────────────────────────────────────────────
void OctofilterPlugin::rebuildRouting() noexcept
{
    const int numInputs = DISTRHO_PLUGIN_NUM_INPUTS;
    Octofilter::InputRouter::assign(mPoints, mActivePoints, numInputs);

    // Spread interpolates per-point pan toward centre (0.0)
    // At spread=1: point uses its full individual pan position
    // At spread=0: all points are centred
    for (int i = 0; i < mActivePoints; ++i)
    {
        const float individualPan = mPointPan[i];
        mPoints[i].pan = individualPan * mSpread;
    }
}

void OctofilterPlugin::updateFilterParams() noexcept
{
    const bool harmonic = (mHarmonicMode > 0.5f);

    // Texture = "openness" knob:
    //   0 = collapsed (all filters at 20Hz, all panned centre)
    //   1 = fully configured state (per-point positions, full spread)
    const float openness = mTexture;
    const float collapsedFreq = 20.0f;

    for (int i = 0; i < mActivePoints; ++i)
    {
        Octofilter::PointState& pt = mPoints[i];
        const float q = (mPointQ[i] > 0.0f) ? mPointQ[i] : mResonance;

        float targetCutoff;
        if (harmonic)
        {
            // Harmonic mode: fundamental scales with openness
            const float fundamental = collapsedFreq + openness * (
                Octofilter::HarmonicMapper::textureToFundamental(openness) - collapsedFreq);
            targetCutoff = Octofilter::HarmonicMapper::targetCutoff(
                fundamental, i, mActivePoints, openness);
        }
        else
        {
            // Random mode: interpolate from collapsed (20Hz) to configured position
            const float configuredCutoff = Octofilter::TextureMapper::computeCutoff(
                1.0f, pt.cutoffOffsetSemitones, 1.0f);
            targetCutoff = collapsedFreq + openness * (configuredCutoff - collapsedFreq);
        }

        mGlide[i].setTarget(targetCutoff);

        // At texture=0 (full collapse), snap instantly — don't glide
        if (openness < 0.01f)
            mGlide[i].snap();

        pt.applyFilterParams(mGlide[i].current, q);

        // Pan collapses with texture: 0=centre, 1=configured position
        mPoints[i].pan = mPointPan[i] * openness;
    }
}

void OctofilterPlugin::prepareAllPoints() noexcept
{
    for (int i = 0; i < Octofilter::kMaxPoints; ++i)
    {
        mPoints[i].prepare(mSampleRate);
        mGlide[i].setGlideTime(mGlideTimeMs, static_cast<float>(mSampleRate), 512);
        mGlide[i].snapTo(1000.0f); // start at 1kHz, will move to target on first block
    }

    mOutputDCL.setSampleRate(mSampleRate);
    mOutputDCR.setSampleRate(mSampleRate);
    mOutputLimiterL.prepare(mSampleRate);
    mOutputLimiterR.prepare(mSampleRate);
}

// ── DPF entry point ───────────────────────────────────────────────────────────
Plugin* createPlugin()
{
    return new OctofilterPlugin();
}

END_NAMESPACE_DISTRHO
