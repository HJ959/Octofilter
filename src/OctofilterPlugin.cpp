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

// ── Constants ─────────────────────────────────────────────────────────────────
// Feedback headroom: allows feedback above unity for creative self-oscillation.
// Protected by per-point and output limiters.
static constexpr float kFeedbackHeadroom = 1.1f;

// ── Parameter index scheme ────────────────────────────────────────────────────
// Global params: indices 0–11
// Per-point params: indices 12–67 (7 params × 8 points)
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
    kGlobalStereoCollapse = 12,
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
        mPointQ[i]            = 5.0f;
        mPointLevel[i]        = 1.0f;
        mPointFeedback[i]     = 0.7f;
        mPointPitchShift[i]   = 0.0f;

        // Sync to DSP state (apply feedback curve)
        mPoints[i].feedbackAmount = std::pow(mPointFeedback[i] / 1.1f, 0.6f) * 1.1f;
        mPoints[i].level = mPointLevel[i];
        mPoints[i].filterType = Octofilter::BiQuadFilter::Type::LowPass;
    }

    // Spread pan positions evenly
    rebuildRouting();
    updateFilterParams();
    prepareAllPoints();

    // Seed smoothers immediately so feedback works from first sample
    mSmTexture.set(mTexture);
    mSmFeedback.set(std::pow(mFeedback, 0.6f));
    mSmPitchShift.set(mPitchShift);
    mSmWetDry.set(mWetDry);
    mSmInputGain.set(mInputGainLin);
    mSmOutputGain.set(mOutputGainLin);
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
    mSmTexture.setBlockRate(sr, 512);
    mSmFeedback.setBlockRate(sr, 512);
    mSmPitchShift.setBlockRate(sr, 512);
    mSmWetDry.setTargetRate(sr);  // per-sample for click-free crossfade
    mSmInputGain.setBlockRate(sr, 512);
    mSmOutputGain.setBlockRate(sr, 512);

    // Seed smoothers with current values
    mSmTexture.set(mTexture);
    mSmFeedback.set(std::pow(mFeedback, 0.6f));
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
    mSmTexture.setBlockRate(sr, 512);
    mSmFeedback.setBlockRate(sr, 512);
    mSmPitchShift.setBlockRate(sr, 512);
    mSmWetDry.setTargetRate(sr);
    mSmInputGain.setBlockRate(sr, 512);
    mSmOutputGain.setBlockRate(sr, 512);
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
        p.hints = kParameterIsHidden; return;
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
    if (index == kGlobalStereoCollapse)
    {
        p.name = "Stereo Width"; p.symbol = "stereo_collapse";
        p.ranges.min = 0.0f; p.ranges.max = 1.0f; p.ranges.def = 1.0f;
        p.hints = kParameterIsAutomatable;
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
            p.ranges.min = 0.0f; p.ranges.max = 2.0f; p.ranges.def = 0.0f;
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
            p.ranges.min = 2.5f; p.ranges.max = 20.0f; p.ranges.def = 5.0f;
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
            p.ranges.min = 0.0f; p.ranges.max = 1.1f; p.ranges.def = 0.7f;
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
    case kGlobalPointCount:    return static_cast<float>(mTargetPoints);
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
    case kGlobalStereoCollapse: return mStereoCollapse;
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
        mTargetPoints = (n < 1) ? 1 : (n > 8) ? 8 : n;
        // If increasing, activate new points immediately (fade them in)
        if (mTargetPoints > mActivePoints)
        {
            mActivePoints = mTargetPoints;
            rebuildRouting();
            updateFilterParams();
        }
        // If decreasing, run() will fade them out and then reduce mActivePoints
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
    case kGlobalStereoCollapse:
        mStereoCollapse = value;
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
            // Exponential curve: knob midpoint gives ~66% feedback (more time in the sweet spot)
            mPoints[point].feedbackAmount = std::pow(value / 1.1f, 0.6f) * 1.1f;
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
    // Parse simple key=value lines
    const char* p = value;
    while (*p)
    {
        char kbuf[32] = {}, vbuf[32] = {};
        int ki = 0, vi = 0;
        while (*p && *p != '=' && ki < 31) kbuf[ki++] = *p++;
        if (*p == '=') ++p;
        while (*p && *p != '\n' && vi < 31) vbuf[vi++] = *p++;
        if (*p == '\n') ++p;

        // Globals
        if (std::strcmp(kbuf, "texture") == 0)   { ps.texture = static_cast<float>(std::atof(vbuf)); ps.hasGlobals = true; }
        else if (std::strcmp(kbuf, "feedback") == 0)  ps.feedback = static_cast<float>(std::atof(vbuf));
        else if (std::strcmp(kbuf, "pitch") == 0)     ps.pitchShift = static_cast<float>(std::atof(vbuf));
        else if (std::strcmp(kbuf, "wetdry") == 0)    ps.wetDry = static_cast<float>(std::atof(vbuf));
        else if (std::strcmp(kbuf, "width") == 0)     ps.stereoWidth = static_cast<float>(std::atof(vbuf));
        else if (std::strcmp(kbuf, "harmonic") == 0)  ps.harmonicMode = static_cast<float>(std::atof(vbuf));
        else if (std::strcmp(kbuf, "points") == 0)    ps.pointCount = std::atoi(vbuf);
        else if (std::strcmp(kbuf, "seed") == 0)      ps.rngSeed = static_cast<uint64_t>(std::atoll(vbuf));
        // Per-point: co0..co7, ft0..ft7, ps0..ps7, fb0..fb7, q0..q7, pn0..pn7, lv0..lv7
        else if (kbuf[0] == 'c' && kbuf[1] == 'o' && kbuf[2] >= '0' && kbuf[2] <= '7')
            ps.cutoffOffsets[kbuf[2] - '0'] = static_cast<float>(std::atof(vbuf));
        else if (kbuf[0] == 'f' && kbuf[1] == 't' && kbuf[2] >= '0' && kbuf[2] <= '7')
            ps.filterTypes[kbuf[2] - '0'] = static_cast<float>(std::atof(vbuf));
        else if (kbuf[0] == 'p' && kbuf[1] == 's' && kbuf[2] >= '0' && kbuf[2] <= '7')
            ps.pitchShifts[kbuf[2] - '0'] = static_cast<float>(std::atof(vbuf));
        else if (kbuf[0] == 'f' && kbuf[1] == 'b' && kbuf[2] >= '0' && kbuf[2] <= '7')
            ps.feedbackAmts[kbuf[2] - '0'] = static_cast<float>(std::atof(vbuf));
        else if (kbuf[0] == 'q' && kbuf[1] >= '0' && kbuf[1] <= '7')
            ps.q[kbuf[1] - '0'] = static_cast<float>(std::atof(vbuf));
        else if (kbuf[0] == 'p' && kbuf[1] == 'n' && kbuf[2] >= '0' && kbuf[2] <= '7')
            ps.pan[kbuf[2] - '0'] = static_cast<float>(std::atof(vbuf));
        else if (kbuf[0] == 'l' && kbuf[1] == 'v' && kbuf[2] >= '0' && kbuf[2] <= '7')
            ps.level[kbuf[2] - '0'] = static_cast<float>(std::atof(vbuf));
    }

    ps.valid       = true;
    mPendingState  = ps;
    mHasPendingState.store(true, std::memory_order_release);
}

String OctofilterPlugin::getState(const char* key) const
{
    if (std::strcmp(key, "octofilter_state") != 0) return String();

    char buf[2048] = {};
    int  pos = 0;

    // Globals
    pos += std::snprintf(buf + pos, sizeof(buf) - pos,
        "texture=%.4f\nfeedback=%.4f\npitch=%.4f\nwetdry=%.4f\n"
        "width=%.4f\nharmonic=%.0f\npoints=%d\n",
        mTexture, mFeedback, mPitchShift, mWetDry,
        mStereoCollapse, mHarmonicMode, mTargetPoints);

    // Per-point
    for (int i = 0; i < 8; ++i)
    {
        pos += std::snprintf(buf + pos, sizeof(buf) - pos,
            "co%d=%.4f\nft%d=%.0f\nps%d=%.4f\nfb%d=%.4f\nq%d=%.4f\npn%d=%.4f\nlv%d=%.4f\n",
            i, mPointCutoffOffset[i],
            i, mPointFilterType[i],
            i, mPointPitchShift[i],
            i, mPointFeedback[i],
            i, mPointQ[i],
            i, mPointPan[i],
            i, mPointLevel[i]);
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
    std::uniform_real_distribution<float> pitchDist(-7.0f, 7.0f);
    std::uniform_real_distribution<float> feedDist(0.4f, 0.9f);

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
        mPoints[i].feedbackAmount = std::pow(mPointFeedback[i] / 1.1f, 0.6f) * 1.1f;

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

    // Restore globals (if present in the state data — backwards compat)
    if (ps.hasGlobals)
    {
        mTexture = ps.texture;
        mFeedback = ps.feedback;
        mPitchShift = ps.pitchShift;
        mWetDry = ps.wetDry;
        mStereoCollapse = ps.stereoWidth;
        mHarmonicMode = ps.harmonicMode;
        mTargetPoints = (ps.pointCount < 1) ? 1 : (ps.pointCount > 8) ? 8 : ps.pointCount;
        mActivePoints = mTargetPoints;

        requestParameterValueChange(kGlobalTexture, mTexture);
        requestParameterValueChange(kGlobalFeedback, mFeedback);
        requestParameterValueChange(kGlobalPitchShift, mPitchShift);
        requestParameterValueChange(kGlobalWetDry, mWetDry);
        requestParameterValueChange(kGlobalStereoCollapse, mStereoCollapse);
        requestParameterValueChange(kGlobalHarmonicMode, mHarmonicMode);
        requestParameterValueChange(kGlobalPointCount, static_cast<float>(mTargetPoints));
    }

    // Restore per-point
    for (int i = 0; i < 8; ++i)
    {
        mPointCutoffOffset[i] = ps.cutoffOffsets[i];
        mPointFilterType[i]   = ps.filterTypes[i];
        mPointPitchShift[i]   = ps.pitchShifts[i];
        mPointFeedback[i]     = ps.feedbackAmts[i];
        mPointQ[i]            = ps.q[i] > 0.0f ? ps.q[i] : 5.0f;
        mPointPan[i]          = ps.pan[i];
        mPointLevel[i]        = ps.level[i] > 0.0f ? ps.level[i] : 1.0f;

        mPoints[i].cutoffOffsetSemitones = ps.cutoffOffsets[i];
        mPoints[i].filterType =
            static_cast<Octofilter::BiQuadFilter::Type>(
                static_cast<int>(ps.filterTypes[i]));
        mPoints[i].setPitchShift(ps.pitchShifts[i]);
        mPoints[i].feedbackAmount = std::pow(ps.feedbackAmts[i] / 1.1f, 0.6f) * 1.1f;
        mPoints[i].pan = ps.pan[i];
        mPoints[i].level = ps.level[i] > 0.0f ? ps.level[i] : 1.0f;

        // Notify UI
        requestParameterValueChange(kNumGlobalParams + i * kNumPerPointParams + 0, mPointFilterType[i]);
        requestParameterValueChange(kNumGlobalParams + i * kNumPerPointParams + 1, mPointCutoffOffset[i]);
        requestParameterValueChange(kNumGlobalParams + i * kNumPerPointParams + 2, mPointQ[i]);
        requestParameterValueChange(kNumGlobalParams + i * kNumPerPointParams + 3, mPointPan[i]);
        requestParameterValueChange(kNumGlobalParams + i * kNumPerPointParams + 4, mPointLevel[i]);
        requestParameterValueChange(kNumGlobalParams + i * kNumPerPointParams + 5, mPointFeedback[i]);
        requestParameterValueChange(kNumGlobalParams + i * kNumPerPointParams + 6, mPointPitchShift[i]);
    }

    rebuildRouting();
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

    // Per-point block-rate updates: Q ceiling + global pitch offset
    for (int i = 0; i < mActivePoints; ++i)
    {
        float q = mPointQ[i];

        // Safety Q ceiling: only kicks in at high feedback + high pitch to prevent piercing.
        // Normal use (feedback < 0.7, moderate pitch) is unaffected.
        const float totalPitch = mPointPitchShift[i] + mSmPitchShift.get();
        const float fbLevel = std::pow(mFeedback, 0.6f);
        const float pitchDanger = std::fabs(totalPitch) / 24.0f;
        const float combinedDanger = fbLevel * 0.7f + pitchDanger * 0.3f;

        // Only limit Q when combined danger is above 0.6 (high feedback + significant pitch)
        if (combinedDanger > 0.6f)
        {
            const float excess = (combinedDanger - 0.6f) / 0.4f; // 0-1 in danger zone
            const float qCeiling = 20.0f - excess * 16.0f;       // 20 down to 4 at max danger
            if (q > qCeiling) q = qCeiling;
        }

        // Re-apply Q ceiling (cutoff is already set by updateFilterParams)
        mPoints[i].filter.setQ(q);

        // Apply global pitch shift as offset on top of per-point pitch
        mPoints[i].setPitchShift(totalPitch);
    }

    // All globals smoothed per-block for zipper-free automation
    mSmTexture.setTarget(mTexture);
    mSmFeedback.setTarget(std::pow(mFeedback, 0.6f));
    mSmPitchShift.setTarget(mPitchShift);
    mSmInputGain.setTarget(mInputGainLin);
    mSmOutputGain.setTarget(mOutputGainLin);

    // Recalculate filter cutoffs from smoothed Texture each block (prevents zipper noise)
    {
        const float savedTexture = mTexture;
        mTexture = mSmTexture.get();
        updateFilterParams();
        mTexture = savedTexture;
    }

    const float smoothFeedback = mSmFeedback.get() * kFeedbackHeadroom;
    const float smoothInGain   = mSmInputGain.get();
    const float smoothOutGain  = mSmOutputGain.get();

    // Point fade: advance per-point fade multipliers toward target
    // ~10ms fade at 44.1kHz/512 = ~1 block, so use a fast per-block step
    const float fadeStep = 512.0f / (0.010f * static_cast<float>(mSampleRate)); // reach 0 in ~10ms
    bool canReduce = true;
    for (int i = 0; i < mActivePoints; ++i)
    {
        if (i < mTargetPoints)
        {
            // Fade in (or stay at 1)
            mPointFade[i] += fadeStep;
            if (mPointFade[i] > 1.0f) mPointFade[i] = 1.0f;
        }
        else
        {
            // Fade out
            mPointFade[i] -= fadeStep;
            if (mPointFade[i] < 0.0f) mPointFade[i] = 0.0f;
            if (mPointFade[i] > 0.001f) canReduce = false;
        }
    }
    // Once all removed points have fully faded, reduce active count
    if (mActivePoints > mTargetPoints && canReduce)
    {
        mActivePoints = mTargetPoints;
        rebuildRouting();
        updateFilterParams();
    }

    for (uint32_t f = 0; f < frames; ++f)
    {
        float wetL = 0.0f, wetR = 0.0f;

        for (int p = 0; p < mActivePoints; ++p)
        {
            Octofilter::PointState& pt = mPoints[p];

            // Feedback: per-point × global (headroom applied once via global)
            const float fbAmt = pt.feedbackAmount * smoothFeedback;

            // Input + input gain + feedback mix
            const float dryIn =
                Octofilter::InputRouter::getSample(inputs, f, pt, numInputs)
                * smoothInGain;
            const float mixedIn = pt.feedback.mixInput(dryIn, fbAmt);

            // Filter
            const float filtered = pt.filter.process(mixedIn);

            // Store through feedback path
            pt.feedback.processOutput(filtered);

            // Level, fade, and pan
            const float scaled = filtered * pt.level * mPointFade[p];
            // Stereo width: 0=mono (collapsed), 1=full width
            const float effPan = pt.pan * mStereoCollapse;
            Octofilter::StereoMixer::accumulate(scaled, effPan, wetL, wetR);
        }

        // Normalise by point count
        const float norm = 1.0f / static_cast<float>(mActivePoints);
        wetL *= norm;
        wetR *= norm;

        // Wet/dry + output gain
        const float dryL = inputs[0][f];
        const float dryR = (numInputs > 1) ? inputs[1][f] : inputs[0][f];

        // Advance wet/dry smoother per-sample for click-free crossfade
        mSmWetDry.setTarget(mWetDry);
        outL[f] = (dryL * (1.0f - mSmWetDry.get()) + wetL * mSmWetDry.get()) * smoothOutGain;
        outR[f] = (dryR * (1.0f - mSmWetDry.get()) + wetR * mSmWetDry.get()) * smoothOutGain;

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

    // Assign pan directly — Texture handles scaling at runtime in run()
    for (int i = 0; i < mActivePoints; ++i)
    {
        mPoints[i].pan = mPointPan[i];
    }
}

void OctofilterPlugin::updateFilterParams() noexcept
{
    const bool harmonic = (mHarmonicMode > 0.5f);

    // Texture = centre frequency control (log scale):
    //   0 = 20 Hz (dark, everything filtered)
    //   1 = 20 kHz (bright, fully open)
    // Per-point offsets spread above/below in semitones.
    const float centreFreq = 20.0f * std::pow(1000.0f, mTexture); // 20 Hz – 20 kHz log

    for (int i = 0; i < mActivePoints; ++i)
    {
        Octofilter::PointState& pt = mPoints[i];
        const float q = (mPointQ[i] >= 2.5f) ? mPointQ[i] : 2.5f;

        float cutoff;
        if (harmonic)
        {
            // Harmonic mode: Texture sets the fundamental, harmonics multiply above it.
            cutoff = Octofilter::HarmonicMapper::targetCutoff(
                centreFreq, i, mActivePoints, mTexture);
        }
        else
        {
            // Random mode: centre frequency + per-point semitone offset.
            // Each point spreads above or below the centre, giving a rich
            // spectral field. Offset ±24 semitones = ±2 octaves from centre.
            cutoff = centreFreq * std::pow(2.0f, pt.cutoffOffsetSemitones / 12.0f);
        }

        // Clamp to safe range
        if (cutoff < 20.0f) cutoff = 20.0f;
        if (cutoff > 20000.0f) cutoff = 20000.0f;

        pt.applyFilterParams(cutoff, q);
    }
}

void OctofilterPlugin::prepareAllPoints() noexcept
{
    for (int i = 0; i < Octofilter::kMaxPoints; ++i)
    {
        mPoints[i].prepare(mSampleRate);
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
