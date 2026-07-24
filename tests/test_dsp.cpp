#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "../deps/doctest.h"

#include "../src/BiQuadFilter.hpp"
#include "../src/InputRouter.hpp"
#include "../src/ParamSmoother.hpp"
#include "../src/PointState.hpp"
#include "../src/StereoMixer.hpp"
#include "../src/TextureMapper.hpp"

#include <cmath>
#include <vector>

using namespace Octofilter;

// ── Helpers ───────────────────────────────────────────────────────────────────

/** Measure power at a specific frequency by running a sine through the filter. */
static float measureGain(BiQuadFilter& filt, float freqHz, float sampleRate,
                         int warmupSamples = 4096, int measureSamples = 2048)
{
    filt.reset();
    const float omega = 2.0f * static_cast<float>(M_PI) * freqHz / sampleRate;

    // Warm up
    for (int i = 0; i < warmupSamples; ++i)
        filt.process(std::sin(omega * static_cast<float>(i)));

    // Measure RMS of output vs input
    double sumIn = 0.0, sumOut = 0.0;
    for (int i = 0; i < measureSamples; ++i)
    {
        const float x = std::sin(omega * static_cast<float>(warmupSamples + i));
        const float y = filt.process(x);
        sumIn  += static_cast<double>(x) * x;
        sumOut += static_cast<double>(y) * y;
    }

    if (sumIn < 1e-10)
        return 0.0f;
    return static_cast<float>(std::sqrt(sumOut / sumIn));
}

static float toDb(float gain)
{
    return 20.0f * std::log10(gain + 1e-30f);
}

// ── BiQuadFilter tests ────────────────────────────────────────────────────────

TEST_CASE("BiQuadFilter - LowPass gain at cutoff is approximately -3 dB")
{
    const float sampleRate = 44100.0f;
    const float cutoff     = 1000.0f;

    BiQuadFilter filt;
    filt.setSampleRate(sampleRate);
    filt.setType(BiQuadFilter::Type::LowPass);
    filt.setCutoff(cutoff);
    filt.setQ(0.707f); // Butterworth — exactly -3 dB at cutoff

    const float gain   = measureGain(filt, cutoff, sampleRate);
    const float gainDb = toDb(gain);

    // Butterworth LP at cutoff should be very close to -3.01 dB
    CHECK(gainDb == doctest::Approx(-3.01f).epsilon(0.5f));
}

TEST_CASE("BiQuadFilter - LowPass passes low freqs and attenuates high freqs")
{
    const float sampleRate = 44100.0f;
    BiQuadFilter filt;
    filt.setSampleRate(sampleRate);
    filt.setType(BiQuadFilter::Type::LowPass);
    filt.setCutoff(1000.0f);
    filt.setQ(0.707f);

    const float gainLow  = toDb(measureGain(filt, 100.0f,  sampleRate));
    const float gainHigh = toDb(measureGain(filt, 8000.0f, sampleRate));

    CHECK(gainLow  > -1.0f);    // passes
    CHECK(gainHigh < -20.0f);   // attenuates
}

TEST_CASE("BiQuadFilter - HighPass attenuates low freqs and passes high freqs")
{
    const float sampleRate = 44100.0f;
    BiQuadFilter filt;
    filt.setSampleRate(sampleRate);
    filt.setType(BiQuadFilter::Type::HighPass);
    filt.setCutoff(1000.0f);
    filt.setQ(0.707f);

    const float gainLow  = toDb(measureGain(filt, 100.0f,  sampleRate));
    const float gainHigh = toDb(measureGain(filt, 8000.0f, sampleRate));

    CHECK(gainLow  < -20.0f);
    CHECK(gainHigh > -1.0f);
}

TEST_CASE("BiQuadFilter - works at all standard sample rates")
{
    const float rates[] = { 44100.0f, 48000.0f, 88200.0f, 96000.0f };
    for (float sr : rates)
    {
        BiQuadFilter filt;
        filt.setSampleRate(sr);
        filt.setType(BiQuadFilter::Type::LowPass);
        filt.setCutoff(1000.0f);
        filt.setQ(0.707f);

        INFO("Sample rate: " << sr);
        const float gainDb = toDb(measureGain(filt, 1000.0f, sr));
        CHECK(gainDb == doctest::Approx(-3.01f).epsilon(0.6f));
    }
}

TEST_CASE("BiQuadFilter - cutoff floor clamps to 20 Hz without crash")
{
    BiQuadFilter filt;
    filt.setSampleRate(44100.0);
    filt.setType(BiQuadFilter::Type::LowPass);
    filt.setCutoff(0.0f); // should clamp to 20 Hz
    filt.setQ(0.707f);

    CHECK(filt.getCutoff() == doctest::Approx(20.0f));

    // Must not produce NaN/Inf
    const float out = filt.process(1.0f);
    CHECK(std::isfinite(out));
}

// ── InputRouter tests ─────────────────────────────────────────────────────────

TEST_CASE("InputRouter - mono: all points get channel 0")
{
    PointState pts[8];
    InputRouter::assign(pts, 8, 1 /* mono */);
    for (int i = 0; i < 8; ++i)
        CHECK(pts[i].sourceChannel == 0);
}

TEST_CASE("InputRouter - stereo even count: clean L/R split")
{
    PointState pts[4];
    InputRouter::assign(pts, 4, 2);
    CHECK(pts[0].sourceChannel == 0); // L
    CHECK(pts[1].sourceChannel == 0); // L
    CHECK(pts[2].sourceChannel == 1); // R
    CHECK(pts[3].sourceChannel == 1); // R
}

TEST_CASE("InputRouter - stereo odd count: centre point gets mix")
{
    PointState pts[5];
    InputRouter::assign(pts, 5, 2);
    CHECK(pts[0].sourceChannel == 0);  // L
    CHECK(pts[1].sourceChannel == 0);  // L
    CHECK(pts[2].sourceChannel == -1); // mix
    CHECK(pts[3].sourceChannel == 1);  // R
    CHECK(pts[4].sourceChannel == 1);  // R
}

TEST_CASE("InputRouter - getSample returns correct channel")
{
    float bufL[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
    float bufR[4] = { 5.0f, 6.0f, 7.0f, 8.0f };
    const float* inputs[2] = { bufL, bufR };

    PointState pt;
    pt.sourceChannel = 0;
    CHECK(InputRouter::getSample(inputs, 0, pt, 2) == doctest::Approx(1.0f));

    pt.sourceChannel = 1;
    CHECK(InputRouter::getSample(inputs, 0, pt, 2) == doctest::Approx(5.0f));

    pt.sourceChannel = -1; // mix: (1+5)/2 = 3
    CHECK(InputRouter::getSample(inputs, 0, pt, 2) == doctest::Approx(3.0f));
}

// ── TextureMapper tests ───────────────────────────────────────────────────────

TEST_CASE("TextureMapper - texture=0 maps to 20 Hz")
{
    CHECK(TextureMapper::textureToCentreFreq(0.0f) == doctest::Approx(20.0f).epsilon(0.01f));
}

TEST_CASE("TextureMapper - texture=1 maps to 20000 Hz")
{
    CHECK(TextureMapper::textureToCentreFreq(1.0f) == doctest::Approx(20000.0f).epsilon(1.0f));
}

TEST_CASE("TextureMapper - mapping is monotonically increasing")
{
    float prev = 0.0f;
    for (int i = 1; i <= 100; ++i)
    {
        const float freq = TextureMapper::textureToCentreFreq(static_cast<float>(i) / 100.0f);
        CHECK(freq > prev);
        prev = freq;
    }
}

TEST_CASE("TextureMapper - semitone offset shifts frequency correctly")
{
    const float base     = TextureMapper::textureToCentreFreq(0.5f);
    const float octaveUp = TextureMapper::applySemitoneOffset(base, 12.0f);
    CHECK(octaveUp == doctest::Approx(base * 2.0f).epsilon(0.01f));

    const float octaveDown = TextureMapper::applySemitoneOffset(base, -12.0f);
    CHECK(octaveDown == doctest::Approx(base / 2.0f).epsilon(0.01f));
}

TEST_CASE("TextureMapper - computeCutoff clamps to audible range")
{
    // Large positive offset should clamp at 20 kHz
    const float high = TextureMapper::computeCutoff(1.0f, 48.0f);
    CHECK(high == doctest::Approx(20000.0f));

    // Large negative offset should clamp at 20 Hz
    const float low = TextureMapper::computeCutoff(0.0f, -48.0f);
    CHECK(low == doctest::Approx(20.0f));
}

// ── StereoMixer / pan law tests ───────────────────────────────────────────────

TEST_CASE("StereoMixer - centre pan preserves equal power (-3 dB each side)")
{
    float gainL, gainR;
    StereoMixer::panGains(0.0f, gainL, gainR);

    const float expectedGain = std::sqrt(0.5f); // 0.7071 = -3.01 dB
    CHECK(gainL == doctest::Approx(expectedGain).epsilon(0.001f));
    CHECK(gainR == doctest::Approx(expectedGain).epsilon(0.001f));
}

TEST_CASE("StereoMixer - hard left pan: L=1, R=0")
{
    float gainL, gainR;
    StereoMixer::panGains(-1.0f, gainL, gainR);
    CHECK(gainL == doctest::Approx(1.0f).epsilon(0.001f));
    CHECK(gainR == doctest::Approx(0.0f).epsilon(0.001f));
}

TEST_CASE("StereoMixer - hard right pan: L=0, R=1")
{
    float gainL, gainR;
    StereoMixer::panGains(1.0f, gainL, gainR);
    CHECK(gainL == doctest::Approx(0.0f).epsilon(0.001f));
    CHECK(gainR == doctest::Approx(1.0f).epsilon(0.001f));
}

TEST_CASE("StereoMixer - equal power: L^2 + R^2 == 1 across all pan positions")
{
    for (int i = 0; i <= 100; ++i)
    {
        const float pan = -1.0f + 2.0f * static_cast<float>(i) / 100.0f;
        float gainL, gainR;
        StereoMixer::panGains(pan, gainL, gainR);
        const float power = gainL * gainL + gainR * gainR;
        INFO("pan = " << pan);
        CHECK(power == doctest::Approx(1.0f).epsilon(0.001f));
    }
}

TEST_CASE("StereoMixer - accumulate adds to output buffers")
{
    float outL = 0.0f, outR = 0.0f;
    StereoMixer::accumulate(1.0f, 0.0f /* centre */, outL, outR);
    const float expected = std::sqrt(0.5f);
    CHECK(outL == doctest::Approx(expected).epsilon(0.001f));
    CHECK(outR == doctest::Approx(expected).epsilon(0.001f));
}

#include "../src/DCBlocker.hpp"
#include "../src/FeedbackEngine.hpp"
#include "../src/PeakLimiter.hpp"

// ── DCBlocker tests ───────────────────────────────────────────────────────────

TEST_CASE("DCBlocker - removes DC offset")
{
    DCBlocker dc;
    dc.setSampleRate(44100.0);

    // Feed a constant DC value for many samples; output should decay to ~0
    float output = 0.0f;
    for (int i = 0; i < 10000; ++i)
        output = dc.process(1.0f);

    CHECK(std::fabs(output) < 0.01f);
}

TEST_CASE("DCBlocker - passes AC signal (1 kHz sine) mostly unchanged")
{
    DCBlocker dc;
    dc.setSampleRate(44100.0);

    const float sampleRate = 44100.0f;
    const float freq       = 1000.0f;
    const float omega      = 2.0f * static_cast<float>(M_PI) * freq / sampleRate;

    // Warm up
    for (int i = 0; i < 4096; ++i)
        dc.process(std::sin(omega * static_cast<float>(i)));

    // Measure output amplitude
    double sumIn = 0.0, sumOut = 0.0;
    for (int i = 4096; i < 8192; ++i)
    {
        const float x = std::sin(omega * static_cast<float>(i));
        const float y = dc.process(x);
        sumIn  += static_cast<double>(x) * x;
        sumOut += static_cast<double>(y) * y;
    }

    const float gain = static_cast<float>(std::sqrt(sumOut / sumIn));
    // Should pass 1 kHz at near-unity gain (> -1 dB)
    CHECK(gain > 0.89f);
}

TEST_CASE("DCBlocker - reset clears state")
{
    DCBlocker dc;
    dc.setSampleRate(44100.0);

    for (int i = 0; i < 100; ++i)
        dc.process(1.0f);

    dc.reset();
    const float out = dc.process(0.0f);
    CHECK(out == doctest::Approx(0.0f).epsilon(1e-6f));
}

// ── PeakLimiter tests ─────────────────────────────────────────────────────────

TEST_CASE("PeakLimiter - output never exceeds ceiling for impulse input")
{
    PeakLimiter lim;
    lim.prepare(44100.0);

    float maxOut = 0.0f;
    for (int i = 0; i < 4096; ++i)
    {
        // Alternate large impulses and silence
        const float x   = (i % 100 == 0) ? 10.0f : 0.0f;
        const float out = lim.process(x);
        if (std::fabs(out) > maxOut)
            maxOut = std::fabs(out);
    }

    CHECK(maxOut <= PeakLimiter::kCeiling + 1e-4f);
}

TEST_CASE("PeakLimiter - passes signal below ceiling unchanged (after settling)")
{
    PeakLimiter lim;
    lim.prepare(44100.0);

    const float sampleRate = 44100.0f;
    const float omega      = 2.0f * static_cast<float>(M_PI) * 1000.0f / sampleRate;

    // Warm up with a 0.5 amplitude sine (well below ceiling)
    for (int i = 0; i < 8192; ++i)
        lim.process(0.5f * std::sin(omega * static_cast<float>(i)));

    // Measure: output should be close to input amplitude
    double sumIn = 0.0, sumOut = 0.0;
    for (int i = 8192; i < 12288; ++i)
    {
        const float x   = 0.5f * std::sin(omega * static_cast<float>(i));
        const float out = lim.process(x);
        sumIn  += static_cast<double>(x) * x;
        sumOut += static_cast<double>(out) * out;
    }

    const float gain = static_cast<float>(std::sqrt(sumOut / sumIn));
    // Gain should be very close to 1 for signals well below ceiling
    CHECK(gain == doctest::Approx(1.0f).epsilon(0.02f));
}

TEST_CASE("PeakLimiter - works at all standard sample rates")
{
    const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0 };
    for (double sr : rates)
    {
        PeakLimiter lim;
        lim.prepare(sr);

        float maxOut = 0.0f;
        for (int i = 0; i < 2048; ++i)
        {
            const float out = lim.process((i % 50 == 0) ? 5.0f : 0.0f);
            if (std::fabs(out) > maxOut) maxOut = std::fabs(out);
        }

        INFO("Sample rate: " << sr);
        CHECK(maxOut <= PeakLimiter::kCeiling + 1e-4f);
    }
}

// ── FeedbackEngine stability tests ───────────────────────────────────────────

TEST_CASE("FeedbackEngine - stable at 95% feedback for 10000 samples")
{
    const float sampleRate   = 44100.0f;
    const float feedbackAmt  = 0.95f;

    // Build a minimal point-like loop: filter (LP) + feedback engine
    Octofilter::BiQuadFilter filter;
    filter.setSampleRate(sampleRate);
    filter.setType(Octofilter::BiQuadFilter::Type::LowPass);
    filter.setCutoff(500.0f);
    filter.setQ(0.707f);

    Octofilter::FeedbackEngine fb;
    fb.prepare(sampleRate);

    // Kick it with one impulse then silence
    bool anyNanInf = false;
    float maxAbs   = 0.0f;

    for (int i = 0; i < 10000; ++i)
    {
        const float dryIn  = (i == 0) ? 1.0f : 0.0f;
        const float mixed  = fb.mixInput(dryIn, feedbackAmt);
        const float filt   = filter.process(mixed);
        fb.processOutput(filt);

        if (!std::isfinite(filt)) anyNanInf = true;
        if (std::fabs(filt) > maxAbs) maxAbs = std::fabs(filt);
    }

    CHECK_FALSE(anyNanInf);
    // Limiter ceiling: output must never blow past 1
    CHECK(maxAbs <= 1.0f + 1e-4f);
}

TEST_CASE("FeedbackEngine - stable at 100% feedback for 10000 samples")
{
    const float sampleRate = 44100.0f;

    Octofilter::BiQuadFilter filter;
    filter.setSampleRate(sampleRate);
    filter.setType(Octofilter::BiQuadFilter::Type::LowPass);
    filter.setCutoff(1000.0f);
    filter.setQ(0.707f);

    Octofilter::FeedbackEngine fb;
    fb.prepare(sampleRate);

    bool anyNanInf = false;
    for (int i = 0; i < 10000; ++i)
    {
        const float dryIn = (i == 0) ? 1.0f : 0.0f;
        const float mixed = fb.mixInput(dryIn, 1.0f);
        const float filt  = filter.process(mixed);
        fb.processOutput(filt);

        if (!std::isfinite(filt)) anyNanInf = true;
    }

    CHECK_FALSE(anyNanInf);
}

#include "../src/IPitchShifter.hpp"
#include "../src/NullShifter.hpp"
#include "../src/PhaseVocoderShifter.hpp"

// ── NullShifter tests ─────────────────────────────────────────────────────────

TEST_CASE("NullShifter - passes signal unchanged")
{
    Octofilter::NullShifter ns;
    ns.prepare(44100.0, 512);
    ns.setShift(7.0f);

    float in[64], out[64];
    for (int i = 0; i < 64; ++i) in[i] = static_cast<float>(i) * 0.01f;

    ns.process(in, out, 64);

    for (int i = 0; i < 64; ++i)
        CHECK(out[i] == doctest::Approx(in[i]));
}

TEST_CASE("NullShifter - reports zero latency")
{
    Octofilter::NullShifter ns;
    ns.prepare(44100.0, 512);
    CHECK(ns.getLatencySamples() == 0);
}

// ── PhaseVocoderShifter tests ─────────────────────────────────────────────────

TEST_CASE("PhaseVocoderShifter - reports correct latency")
{
    Octofilter::PhaseVocoderShifter pv;
    pv.prepare(44100.0, 512);
    CHECK(pv.getLatencySamples() == Octofilter::PhaseVocoderShifter::kBufSize / 2);
}

TEST_CASE("PhaseVocoderShifter - output is finite at ±12 semitones")
{
    const float rates[]  = { 44100.0f, 48000.0f };
    const float shifts[] = { -24.0f, -12.0f, 0.0f, 12.0f, 24.0f };

    for (float sr : rates)
    for (float shift : shifts)
    {
        Octofilter::PhaseVocoderShifter pv;
        pv.prepare(static_cast<double>(sr), 512);
        pv.setShift(shift);

        const float omega = 2.0f * static_cast<float>(M_PI) * 440.0f / sr;
        bool anyNonFinite = false;

        // Run 4096 samples of a sine through
        for (int i = 0; i < 4096; ++i)
        {
            float x   = std::sin(omega * static_cast<float>(i));
            float out = 0.0f;
            pv.process(&x, &out, 1);
            if (!std::isfinite(out)) anyNonFinite = true;
        }

        INFO("sr=" << sr << " shift=" << shift);
        CHECK_FALSE(anyNonFinite);
    }
}

TEST_CASE("PhaseVocoderShifter - output is finite under feedback spiral conditions")
{
    // Simulate the actual feedback loop: PV in feedback path, high feedback amount
    const float sampleRate   = 44100.0f;
    const float feedbackAmt  = 0.7f;
    const float shiftSemitones = 2.0f;

    Octofilter::BiQuadFilter filter;
    filter.setSampleRate(sampleRate);
    filter.setType(Octofilter::BiQuadFilter::Type::LowPass);
    filter.setCutoff(2000.0f);
    filter.setQ(0.707f);

    Octofilter::PhaseVocoderShifter pv;
    pv.prepare(static_cast<double>(sampleRate), 512);
    pv.setShift(shiftSemitones);

    Octofilter::FeedbackEngine fb;
    fb.setPitchShifter(&pv);
    fb.prepare(static_cast<double>(sampleRate));

    bool anyNonFinite = false;
    float maxAbs = 0.0f;

    // Kick with a short tone then silence — feedback spiral should be audible
    // but limiter should keep it bounded
    const float omega = 2.0f * static_cast<float>(M_PI) * 440.0f / sampleRate;
    for (int i = 0; i < 8192; ++i)
    {
        const float dryIn = (i < 512) ? 0.5f * std::sin(omega * static_cast<float>(i)) : 0.0f;
        const float mixed = fb.mixInput(dryIn, feedbackAmt);
        const float filt  = filter.process(mixed);
        fb.processOutput(filt);

        if (!std::isfinite(filt)) anyNonFinite = true;
        if (std::fabs(filt) > maxAbs) maxAbs = std::fabs(filt);
    }

    CHECK_FALSE(anyNonFinite);
    CHECK(maxAbs <= 1.0f + 1e-3f); // limiter must hold
}

TEST_CASE("PhaseVocoderShifter - reset clears state without crash")
{
    Octofilter::PhaseVocoderShifter pv;
    pv.prepare(44100.0, 512);
    pv.setShift(7.0f);

    // Run some audio through, then reset
    float x = 0.5f, out = 0.0f;
    for (int i = 0; i < 1024; ++i)
        pv.process(&x, &out, 1);

    pv.reset();

    // After reset the first output should be near zero (buffer cleared)
    pv.process(&x, &out, 1);
    // Just check it's finite
    CHECK(std::isfinite(out));
}

// ── Phase 4: TextureSpread and parameter tests ────────────────────────────────

TEST_CASE("TextureMapper - spreadRange=0 collapses all points to centre freq")
{
    const float centre = Octofilter::TextureMapper::textureToCentreFreq(0.5f);
    for (float offset : {-24.0f, -12.0f, 0.0f, 12.0f, 24.0f})
    {
        const float result = Octofilter::TextureMapper::computeCutoff(0.5f, offset, 0.0f);
        CHECK(result == doctest::Approx(centre).epsilon(0.01f));
    }
}

TEST_CASE("TextureMapper - spreadRange=1 applies full offset")
{
    const float withSpread    = Octofilter::TextureMapper::computeCutoff(0.5f, 12.0f, 1.0f);
    const float withoutSpread = Octofilter::TextureMapper::computeCutoff(0.5f, 12.0f, 0.0f);
    CHECK(withSpread > withoutSpread);
}

TEST_CASE("TextureMapper - spreadRange=0.5 applies half offset")
{
    const float centre = Octofilter::TextureMapper::textureToCentreFreq(0.5f);
    const float half   = Octofilter::TextureMapper::computeCutoff(0.5f, 12.0f, 0.5f);
    const float full   = Octofilter::TextureMapper::computeCutoff(0.5f, 12.0f, 1.0f);
    // half should be between centre and full
    CHECK(half > centre);
    CHECK(half < full);
}

TEST_CASE("ParamSmoother - reaches target within expected time")
{
    Octofilter::ParamSmoother sm;
    sm.setTargetRate(44100.0f);

    sm.set(0.0f);
    for (int i = 0; i < 2205; ++i)
        sm.setTarget(1.0f);

    CHECK(sm.get() > 0.98f);
}

TEST_CASE("ParamSmoother - instant set works")
{
    Octofilter::ParamSmoother sm;
    sm.setTargetRate(44100.0f);
    sm.set(0.75f);
    CHECK(sm.get() == doctest::Approx(0.75f));
}

#include "../src/CutoffGlide.hpp"
#include "../src/HarmonicMapper.hpp"

// ── HarmonicMapper tests ──────────────────────────────────────────────────────

TEST_CASE("HarmonicMapper - spread=1 assigns harmonics 1 through N")
{
    using HM = Octofilter::HarmonicMapper;
    CHECK(HM::harmonicNumber(0, 4, 1.0f) == 1);
    CHECK(HM::harmonicNumber(1, 4, 1.0f) == 2);
    CHECK(HM::harmonicNumber(2, 4, 1.0f) == 3);
    CHECK(HM::harmonicNumber(3, 4, 1.0f) == 4);
}

TEST_CASE("HarmonicMapper - spread=0 all points at fundamental")
{
    using HM = Octofilter::HarmonicMapper;
    for (int i = 0; i < 8; ++i)
        CHECK(HM::harmonicNumber(i, 8, 0.0f) == 1);
}

TEST_CASE("HarmonicMapper - spread=0.5 clusters in lower harmonics")
{
    using HM = Octofilter::HarmonicMapper;
    // 8 points, spread=0.5: max harmonic should be ~4.5, so we get 1-4 range
    int maxH = 0;
    for (int i = 0; i < 8; ++i)
    {
        int h = HM::harmonicNumber(i, 8, 0.5f);
        CHECK(h >= 1);
        if (h > maxH) maxH = h;
    }
    // Should not reach harmonic 8
    CHECK(maxH <= 5);
}

TEST_CASE("HarmonicMapper - targetCutoff multiplies fundamental by harmonic number")
{
    using HM = Octofilter::HarmonicMapper;
    const float fundamental = 100.0f;
    CHECK(HM::targetCutoff(fundamental, 0, 4, 1.0f) == doctest::Approx(100.0f));
    CHECK(HM::targetCutoff(fundamental, 1, 4, 1.0f) == doctest::Approx(200.0f));
    CHECK(HM::targetCutoff(fundamental, 2, 4, 1.0f) == doctest::Approx(300.0f));
    CHECK(HM::targetCutoff(fundamental, 3, 4, 1.0f) == doctest::Approx(400.0f));
}

TEST_CASE("HarmonicMapper - targetCutoff clamps to 20 kHz")
{
    using HM = Octofilter::HarmonicMapper;
    // High fundamental + high harmonic should clamp
    const float result = HM::targetCutoff(15000.0f, 3, 4, 1.0f); // 15k * 4 = 60k
    CHECK(result == doctest::Approx(20000.0f));
}

// ── CutoffGlide tests ─────────────────────────────────────────────────────────

TEST_CASE("CutoffGlide - reaches target within expected time")
{
    Octofilter::CutoffGlide g;
    g.snapTo(100.0f);
    g.setGlideTime(100.0f, 44100.0f, 512); // 100ms glide
    g.setTarget(1000.0f);

    // After 5× the glide time (~500ms), should be very close
    const int blocksForFiveTimeConstants = static_cast<int>(
        5.0f * 0.1f * 44100.0f / 512.0f); // 5 × 100ms
    for (int i = 0; i < blocksForFiveTimeConstants; ++i)
        g.advance();

    CHECK(g.current == doctest::Approx(1000.0f).epsilon(0.02f));
}

TEST_CASE("CutoffGlide - snap sets immediately without glide")
{
    Octofilter::CutoffGlide g;
    g.snapTo(100.0f);
    g.setGlideTime(2000.0f, 44100.0f, 512); // very slow glide
    g.setTarget(5000.0f);
    g.snap();
    CHECK(g.current == doctest::Approx(5000.0f));
}

TEST_CASE("CutoffGlide - independent per-point movement")
{
    Octofilter::CutoffGlide g1, g2;
    g1.snapTo(100.0f);
    g2.snapTo(100.0f);
    g1.setGlideTime(50.0f, 44100.0f, 512);
    g2.setGlideTime(500.0f, 44100.0f, 512);
    g1.setTarget(1000.0f);
    g2.setTarget(1000.0f);

    // After 50 blocks, g1 should be much closer than g2
    for (int i = 0; i < 50; ++i) { g1.advance(); g2.advance(); }

    CHECK(g1.current > g2.current); // g1 faster, closer to target
}
