#pragma once

#include "DistrhoPlugin.hpp"
#include "BiQuadFilter.hpp"
#include "CutoffGlide.hpp"
#include "HarmonicMapper.hpp"
#include "InputRouter.hpp"
#include "ParamSmoother.hpp"
#include "PointState.hpp"
#include "StereoMixer.hpp"
#include "TextureMapper.hpp"

#include <atomic>
#include <cstdint>
#include <random>

START_NAMESPACE_DISTRHO

// ── Parameter count ───────────────────────────────────────────────────────────
// 13 globals + 7 per-point × 8 points = 69
static constexpr int kNumGlobalParams   = 13;
static constexpr int kNumPerPointParams = 7;
static constexpr int kNumPoints         = 8;
static constexpr int kTotalParams       = kNumGlobalParams + kNumPerPointParams * kNumPoints;

/**
 * OctofilterPlugin — Phase 4: full parameter set, state saving, randomise.
 */
class OctofilterPlugin : public Plugin
{
public:
    OctofilterPlugin();

protected:
    // ── Info ──────────────────────────────────────────────────────────────
    const char* getLabel()    const override { return "Octofilter"; }
    const char* getMaker()    const override { return "Henry"; }
    const char* getLicense()  const override { return "Proprietary"; }
    uint32_t    getVersion()  const override { return d_version(0, 4, 0); }
    int64_t     getUniqueId() const override { return d_cconst('O', 'c', 't', 'o'); }

    // ── Lifecycle ─────────────────────────────────────────────────────────
    void activate() override;
    void sampleRateChanged(double newSampleRate) override;

    // ── Parameters ────────────────────────────────────────────────────────
    void  initParameter(uint32_t index, Parameter& parameter) override;
    float getParameterValue(uint32_t index) const override;
    void  setParameterValue(uint32_t index, float value) override;

    // ── State (preset save/restore) ───────────────────────────────────────
    void        initState(uint32_t index, State& state) override;
    void        setState(const char* key, const char* value) override;
    String      getState(const char* key) const override;

    // ── Process ───────────────────────────────────────────────────────────
    void run(const float** inputs, float** outputs, uint32_t frames) override;

private:
    // ── Helpers ───────────────────────────────────────────────────────────
    void rebuildRouting() noexcept;
    void updateFilterParams() noexcept;
    void prepareAllPoints() noexcept;
    void doRandomise() noexcept;
    void applyPendingState() noexcept;

    // Per-point parameter accessors (flat index helpers)
    int  pointParamBase(int point) const noexcept
    {
        return kNumGlobalParams + point * kNumPerPointParams;
    }

    // ── DSP state ─────────────────────────────────────────────────────────
    Octofilter::PointState mPoints[Octofilter::kMaxPoints];
    Octofilter::DCBlocker  mOutputDCL;  // output DC blocker, left
    Octofilter::DCBlocker  mOutputDCR;  // output DC blocker, right
    Octofilter::PeakLimiter mOutputLimiterL; // safety limiter, left
    Octofilter::PeakLimiter mOutputLimiterR; // safety limiter, right
    int    mActivePoints { 2 };
    double mSampleRate   { 44100.0 };

    // ── Global parameter values ───────────────────────────────────────────
    float mTexture        { 0.5f };
    float mTextureSpread  { 1.0f };
    float mSpread         { 1.0f };
    float mResonance      { 0.707f };
    float mFeedback       { 0.3f };
    float mPitchShift     { 0.0f };
    float mInputGainDb    { 0.0f };
    float mInputGainLin   { 1.0f };
    float mOutputGainDb   { 0.0f };
    float mOutputGainLin  { 1.0f };
    float mWetDry         { 1.0f };

    // ── Per-point parameter values (mirrored for DPF get/set) ─────────────
    // Stored as flat arrays indexed [0..7]
    float mPointFilterType[8]     {};  // 0=LP,1=HP,2=BP,3=Notch
    float mPointCutoffOffset[8]   {};  // semitones, set by randomise
    float mPointQ[8]              {};  // per-point Q override (0=use global)
    float mPointPan[8]            {};  // -1..1
    float mPointLevel[8]          {};  // 0..2
    float mPointFeedback[8]       {};  // 0..1
    float mPointPitchShift[8]     {};  // ±24 semitones

    // ── Parameter smoothers (for audio-rate params) ───────────────────────
    Octofilter::ParamSmoother mSmTexture;
    Octofilter::ParamSmoother mSmFeedback;
    Octofilter::ParamSmoother mSmPitchShift;
    Octofilter::ParamSmoother mSmWetDry;
    Octofilter::ParamSmoother mSmInputGain;
    Octofilter::ParamSmoother mSmOutputGain;

    // ── Randomise / state ─────────────────────────────────────────────────
    float     mLastRandomise { 0.0f };
    uint64_t  mRngSeed { 12345 };

    // ── Harmonic mode ─────────────────────────────────────────────────────
    float mHarmonicMode  { 0.0f };  // 0=Random, 1=Harmonic
    float mGlideTimeMs   { 200.0f };  // 10–2000 ms
    Octofilter::CutoffGlide mGlide[8]; // per-point cutoff glide

    // Double-buffer for thread-safe setState from host thread
    struct PendingState
    {
        float cutoffOffsets[8] {};
        float filterTypes[8]   {};
        float pitchShifts[8]   {};
        float feedbackAmts[8]  {};
        uint64_t rngSeed       { 12345 };
        bool  valid            { false };
    };
    PendingState              mPendingState;
    std::atomic<bool>         mHasPendingState { false };

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OctofilterPlugin)
};

END_NAMESPACE_DISTRHO
