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


// ── CPU Benchmark ─────────────────────────────────────────────────────────────

#include "../src/FeedbackEngine.hpp"
#include "../src/PhaseVocoderShifter.hpp"
#include "../src/DCBlocker.hpp"
#include "../src/PeakLimiter.hpp"
#include <chrono>
#include <cstdio>

TEST_CASE("CPU Benchmark - 8 points, full feedback + pitch, 10 seconds at 44.1kHz/512")
{
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 512;
    constexpr int numPoints = 8;
    constexpr float duration = 10.0f; // seconds
    constexpr int totalFrames = static_cast<int>(sampleRate * duration);
    constexpr int numBlocks = totalFrames / blockSize;

    // Set up 8 points with filters, feedback engines, and pitch shifters
    PointState points[numPoints];
    DCBlocker outputDCL, outputDCR;
    PeakLimiter outputLimL, outputLimR;

    outputDCL.setSampleRate(sampleRate);
    outputDCR.setSampleRate(sampleRate);
    outputLimL.prepare(sampleRate);
    outputLimR.prepare(sampleRate);

    for (int i = 0; i < numPoints; ++i)
    {
        points[i].prepare(sampleRate);
        points[i].setPitchShift(3.0f + static_cast<float>(i)); // varied pitch shifts
        points[i].feedbackAmount = 0.8f;
        points[i].level = 1.0f;
        points[i].pan = -1.0f + 2.0f * static_cast<float>(i) / static_cast<float>(numPoints - 1);
        points[i].filterType = static_cast<BiQuadFilter::Type>(i % 4);
        points[i].filter.setSampleRate(sampleRate);
        points[i].filter.setType(points[i].filterType);
        points[i].filter.setCutoff(200.0f + static_cast<float>(i) * 500.0f);
        points[i].filter.setQ(5.0f);
    }

    // Generate white noise input
    std::vector<float> inputBuf(blockSize);
    std::vector<float> outL(blockSize), outR(blockSize);
    unsigned int rng = 12345;

    const float feedbackGlobal = 0.9f * 1.1f; // curved feedback × headroom

    auto start = std::chrono::high_resolution_clock::now();

    for (int b = 0; b < numBlocks; ++b)
    {
        // Fill input with white noise
        for (int s = 0; s < blockSize; ++s)
        {
            rng = rng * 1664525u + 1013904223u;
            inputBuf[s] = (static_cast<float>(rng) / 4294967296.0f - 0.5f) * 0.5f;
        }

        // Process block (mirrors run() logic)
        for (int s = 0; s < blockSize; ++s)
        {
            float wetL = 0.0f, wetR = 0.0f;

            for (int p = 0; p < numPoints; ++p)
            {
                PointState& pt = points[p];
                const float fbAmt = pt.feedbackAmount * feedbackGlobal;
                const float dryIn = inputBuf[s];
                const float mixedIn = pt.feedback.mixInput(dryIn, fbAmt);
                const float filtered = pt.filter.process(mixedIn);
                pt.feedback.processOutput(filtered);

                const float scaled = filtered * pt.level;
                // Simple pan accumulation
                const float panR = (pt.pan + 1.0f) * 0.5f;
                const float panL = 1.0f - panR;
                wetL += scaled * panL;
                wetR += scaled * panR;
            }

            const float norm = 1.0f / static_cast<float>(numPoints);
            wetL *= norm;
            wetR *= norm;

            outL[s] = outputLimL.process(outputDCL.process(wetL));
            outR[s] = outputLimR.process(outputDCR.process(wetR));
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    double elapsedMs = std::chrono::duration<double, std::milli>(end - start).count();
    double realtimeMs = duration * 1000.0;
    double cpuPercent = (elapsedMs / realtimeMs) * 100.0;

    std::printf("\n=== CPU BENCHMARK ===\n");
    std::printf("  Config: %d points, feedback=0.9, pitch active, 44.1kHz/512\n", numPoints);
    std::printf("  Duration: %.1f seconds of audio\n", duration);
    std::printf("  Processing time: %.1f ms\n", elapsedMs);
    std::printf("  Realtime budget: %.0f ms\n", realtimeMs);
    std::printf("  CPU usage: %.2f%%\n", cpuPercent);
    std::printf("  Target: <5%% — %s\n", cpuPercent < 5.0 ? "PASS" : "FAIL");
    std::printf("=====================\n\n");

    // The test passes as long as output is finite (not NaN/inf)
    CHECK(std::isfinite(outL[0]));
    CHECK(std::isfinite(outR[0]));

    // Warn if over budget but don't fail the test (CI machines vary)
    if (cpuPercent > 5.0)
        MESSAGE("WARNING: CPU usage ", cpuPercent, "% exceeds 5% target");
}

// ═══════════════════════════════════════════════════════════════════════════════
// Phase 8 — Expanded Unit Tests
// ═══════════════════════════════════════════════════════════════════════════════

// ── HarmonicMapper: spread edge cases ─────────────────────────────────────────

TEST_CASE("HarmonicMapper - spread=0.25 clusters tightly")
{
    using HM = Octofilter::HarmonicMapper;
    int maxH = 0;
    for (int i = 0; i < 8; ++i)
    {
        int h = HM::harmonicNumber(i, 8, 0.25f);
        CHECK(h >= 1);
        if (h > maxH) maxH = h;
    }
    // At spread=0.25 with 8 points, max harmonic should be ~2-3
    CHECK(maxH <= 3);
}

TEST_CASE("HarmonicMapper - single point returns harmonic 1")
{
    using HM = Octofilter::HarmonicMapper;
    CHECK(HM::harmonicNumber(0, 1, 1.0f) == 1);
    CHECK(HM::harmonicNumber(0, 1, 0.0f) == 1);
}

TEST_CASE("HarmonicMapper - targetCutoff clamps to 20 Hz minimum")
{
    using HM = Octofilter::HarmonicMapper;
    // Very low fundamental
    const float result = HM::targetCutoff(5.0f, 0, 4, 1.0f);
    CHECK(result >= 20.0f);
}

// ── Q Ceiling: output stays bounded ──────────────────────────────────────────

TEST_CASE("Q ceiling - high feedback + high pitch + high Q produces finite bounded output")
{
    // Simulate the worst case: max Q, max feedback, max pitch shift
    PointState pt;
    pt.prepare(44100.0);
    pt.filterType = BiQuadFilter::Type::BandPass;
    pt.filter.setSampleRate(44100.0);
    pt.filter.setType(BiQuadFilter::Type::BandPass);
    pt.filter.setCutoff(1000.0f);
    pt.filter.setQ(20.0f); // max Q
    pt.setPitchShift(24.0f); // max pitch
    pt.feedbackAmount = 1.1f; // max per-point
    pt.level = 1.0f;

    // Process 44100 samples (1 second) with white noise
    unsigned int rng = 99999;
    float maxOut = 0.0f;
    for (int s = 0; s < 44100; ++s)
    {
        rng = rng * 1664525u + 1013904223u;
        float noise = (static_cast<float>(rng) / 4294967296.0f - 0.5f) * 0.5f;

        // Apply Q ceiling logic (same as run())
        float q = 20.0f;
        const float pitchDanger = 24.0f / 24.0f; // 1.0
        const float fbLevel = 1.0f; // max
        const float combinedDanger = fbLevel * 0.7f + pitchDanger * 0.3f; // 1.0
        const float excess = (combinedDanger - 0.6f) / 0.4f; // 1.0
        const float qCeiling = 20.0f - excess * 16.0f; // 4.0
        if (q > qCeiling) q = qCeiling;
        pt.filter.setQ(q);

        const float mixedIn = pt.feedback.mixInput(noise, pt.feedbackAmount * 1.1f);
        const float filtered = pt.filter.process(mixedIn);
        pt.feedback.processOutput(filtered);

        if (std::fabs(filtered) > maxOut) maxOut = std::fabs(filtered);
    }

    CHECK(std::isfinite(maxOut));
    // The per-point limiter in FeedbackEngine should keep things bounded
    // Output might exceed 1.0 momentarily (before the output limiter), but should be finite
    CHECK(maxOut < 100.0f); // Sanity: not blowing up to infinity
}

// ── PhaseVocoderShifter: finite output at all settings ────────────────────────

#include "../src/PhaseVocoderShifter.hpp"

TEST_CASE("PhaseVocoderShifter - pitch up produces finite output")
{
    Octofilter::PhaseVocoderShifter shifter;
    shifter.prepare(44100.0, 512);
    shifter.setShift(12.0f); // +1 octave

    float in[512], out[512];
    unsigned int rng = 77777;
    for (int i = 0; i < 512; ++i)
    {
        rng = rng * 1664525u + 1013904223u;
        in[i] = (static_cast<float>(rng) / 4294967296.0f - 0.5f) * 0.5f;
    }

    // Process several blocks to warm up
    for (int b = 0; b < 20; ++b)
        shifter.process(in, out, 512);

    for (int i = 0; i < 512; ++i)
    {
        CHECK(std::isfinite(out[i]));
    }
}

TEST_CASE("PhaseVocoderShifter - pitch down produces finite output")
{
    Octofilter::PhaseVocoderShifter shifter;
    shifter.prepare(44100.0, 512);
    shifter.setShift(-12.0f); // -1 octave

    float in[512], out[512];
    unsigned int rng = 55555;
    for (int i = 0; i < 512; ++i)
    {
        rng = rng * 1664525u + 1013904223u;
        in[i] = (static_cast<float>(rng) / 4294967296.0f - 0.5f) * 0.5f;
    }

    for (int b = 0; b < 20; ++b)
        shifter.process(in, out, 512);

    for (int i = 0; i < 512; ++i)
    {
        CHECK(std::isfinite(out[i]));
    }
}

TEST_CASE("PhaseVocoderShifter - extreme pitch ±24st produces finite output")
{
    Octofilter::PhaseVocoderShifter shifter;
    shifter.prepare(44100.0, 512);

    float in[512], out[512];
    unsigned int rng = 33333;
    for (int i = 0; i < 512; ++i)
    {
        rng = rng * 1664525u + 1013904223u;
        in[i] = (static_cast<float>(rng) / 4294967296.0f - 0.5f) * 0.5f;
    }

    // Test +24
    shifter.setShift(24.0f);
    for (int b = 0; b < 20; ++b)
        shifter.process(in, out, 512);
    for (int i = 0; i < 512; ++i)
        CHECK(std::isfinite(out[i]));

    // Test -24
    shifter.reset();
    shifter.setShift(-24.0f);
    for (int b = 0; b < 20; ++b)
        shifter.process(in, out, 512);
    for (int i = 0; i < 512; ++i)
        CHECK(std::isfinite(out[i]));
}

// ── Single-point mode: DSP correctness ────────────────────────────────────────

TEST_CASE("Single-point mode - 1 point produces finite stereo output")
{
    PointState pt;
    pt.prepare(44100.0);
    pt.filterType = BiQuadFilter::Type::LowPass;
    pt.filter.setSampleRate(44100.0);
    pt.filter.setType(BiQuadFilter::Type::LowPass);
    pt.filter.setCutoff(1000.0f);
    pt.filter.setQ(5.0f);
    pt.setPitchShift(5.0f);
    pt.feedbackAmount = 0.7f;
    pt.level = 1.0f;
    pt.pan = 0.0f; // centre

    unsigned int rng = 11111;
    for (int s = 0; s < 4096; ++s)
    {
        rng = rng * 1664525u + 1013904223u;
        float noise = (static_cast<float>(rng) / 4294967296.0f - 0.5f) * 0.5f;

        const float mixedIn = pt.feedback.mixInput(noise, pt.feedbackAmount * 0.8f);
        const float filtered = pt.filter.process(mixedIn);
        pt.feedback.processOutput(filtered);

        CHECK(std::isfinite(filtered));
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Phase 8 — Integration Tests (multi-sample-rate)
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Integration - 10s render at 44100 Hz produces bounded output")
{
    constexpr double sr = 44100.0;
    constexpr int totalSamples = static_cast<int>(sr * 10.0);
    constexpr int numPoints = 8;

    PointState points[numPoints];
    DCBlocker dcL, dcR;
    PeakLimiter limL, limR;
    dcL.setSampleRate(sr); dcR.setSampleRate(sr);
    limL.prepare(sr); limR.prepare(sr);

    for (int i = 0; i < numPoints; ++i)
    {
        points[i].prepare(sr);
        points[i].setPitchShift(static_cast<float>(i) - 4.0f);
        points[i].feedbackAmount = 0.7f;
        points[i].level = 1.0f;
        points[i].pan = -1.0f + 2.0f * static_cast<float>(i) / 7.0f;
        points[i].filter.setSampleRate(sr);
        points[i].filter.setType(static_cast<BiQuadFilter::Type>(i % 4));
        points[i].filter.setCutoff(200.0f + static_cast<float>(i) * 400.0f);
        points[i].filter.setQ(5.0f);
    }

    unsigned int rng = 42;
    float maxAbsOut = 0.0f;
    bool allFinite = true;

    for (int s = 0; s < totalSamples; ++s)
    {
        rng = rng * 1664525u + 1013904223u;
        float noise = (static_cast<float>(rng) / 4294967296.0f - 0.5f) * 0.3f;

        float wetL = 0.0f, wetR = 0.0f;
        for (int p = 0; p < numPoints; ++p)
        {
            PointState& pt = points[p];
            const float mixedIn = pt.feedback.mixInput(noise, pt.feedbackAmount * 0.9f);
            const float filtered = pt.filter.process(mixedIn);
            pt.feedback.processOutput(filtered);
            const float scaled = filtered * pt.level / static_cast<float>(numPoints);
            const float panR = (pt.pan + 1.0f) * 0.5f;
            wetL += scaled * (1.0f - panR);
            wetR += scaled * panR;
        }

        float outL = limL.process(dcL.process(wetL));
        float outR = limR.process(dcR.process(wetR));

        if (!std::isfinite(outL) || !std::isfinite(outR)) allFinite = false;
        if (std::fabs(outL) > maxAbsOut) maxAbsOut = std::fabs(outL);
        if (std::fabs(outR) > maxAbsOut) maxAbsOut = std::fabs(outR);
    }

    CHECK(allFinite);
    CHECK(maxAbsOut <= 1.01f); // limiter should keep below 1.0 (tiny overshoot OK)
}

TEST_CASE("Integration - 10s render at 48000 Hz produces bounded output")
{
    constexpr double sr = 48000.0;
    constexpr int totalSamples = static_cast<int>(sr * 10.0);

    PointState points[8];
    DCBlocker dcL, dcR;
    PeakLimiter limL, limR;
    dcL.setSampleRate(sr); dcR.setSampleRate(sr);
    limL.prepare(sr); limR.prepare(sr);

    for (int i = 0; i < 8; ++i)
    {
        points[i].prepare(sr);
        points[i].setPitchShift(static_cast<float>((i % 5) - 2) * 3.0f);
        points[i].feedbackAmount = 0.8f;
        points[i].level = 1.0f;
        points[i].filter.setSampleRate(sr);
        points[i].filter.setType(static_cast<BiQuadFilter::Type>(i % 4));
        points[i].filter.setCutoff(300.0f + static_cast<float>(i) * 300.0f);
        points[i].filter.setQ(7.0f);
    }

    unsigned int rng = 101;
    bool allFinite = true;
    float maxAbs = 0.0f;

    for (int s = 0; s < totalSamples; ++s)
    {
        rng = rng * 1664525u + 1013904223u;
        float noise = (static_cast<float>(rng) / 4294967296.0f - 0.5f) * 0.3f;
        float wetL = 0.0f, wetR = 0.0f;

        for (int p = 0; p < 8; ++p)
        {
            const float mixedIn = points[p].feedback.mixInput(noise, points[p].feedbackAmount * 0.9f);
            const float filtered = points[p].filter.process(mixedIn);
            points[p].feedback.processOutput(filtered);
            wetL += filtered / 8.0f;
            wetR += filtered / 8.0f;
        }

        float outL = limL.process(dcL.process(wetL));
        float outR = limR.process(dcR.process(wetR));
        if (!std::isfinite(outL) || !std::isfinite(outR)) allFinite = false;
        if (std::fabs(outL) > maxAbs) maxAbs = std::fabs(outL);
        if (std::fabs(outR) > maxAbs) maxAbs = std::fabs(outR);
    }

    CHECK(allFinite);
    CHECK(maxAbs <= 1.01f);
}

TEST_CASE("Integration - 10s render at 96000 Hz produces bounded output")
{
    constexpr double sr = 96000.0;
    constexpr int totalSamples = static_cast<int>(sr * 10.0);

    PointState points[8];
    DCBlocker dcL, dcR;
    PeakLimiter limL, limR;
    dcL.setSampleRate(sr); dcR.setSampleRate(sr);
    limL.prepare(sr); limR.prepare(sr);

    for (int i = 0; i < 8; ++i)
    {
        points[i].prepare(sr);
        points[i].setPitchShift(static_cast<float>(i) * 2.0f - 7.0f);
        points[i].feedbackAmount = 0.9f;
        points[i].level = 1.0f;
        points[i].filter.setSampleRate(sr);
        points[i].filter.setType(static_cast<BiQuadFilter::Type>(i % 4));
        points[i].filter.setCutoff(500.0f + static_cast<float>(i) * 500.0f);
        points[i].filter.setQ(10.0f);
    }

    unsigned int rng = 7777;
    bool allFinite = true;
    float maxAbs = 0.0f;

    for (int s = 0; s < totalSamples; ++s)
    {
        rng = rng * 1664525u + 1013904223u;
        float noise = (static_cast<float>(rng) / 4294967296.0f - 0.5f) * 0.3f;
        float wetL = 0.0f, wetR = 0.0f;

        for (int p = 0; p < 8; ++p)
        {
            const float mixedIn = points[p].feedback.mixInput(noise, points[p].feedbackAmount * 1.0f);
            const float filtered = points[p].filter.process(mixedIn);
            points[p].feedback.processOutput(filtered);
            wetL += filtered / 8.0f;
            wetR += filtered / 8.0f;
        }

        float outL = limL.process(dcL.process(wetL));
        float outR = limR.process(dcR.process(wetR));
        if (!std::isfinite(outL) || !std::isfinite(outR)) allFinite = false;
        if (std::fabs(outL) > maxAbs) maxAbs = std::fabs(outL);
        if (std::fabs(outR) > maxAbs) maxAbs = std::fabs(outR);
    }

    CHECK(allFinite);
    CHECK(maxAbs <= 1.01f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Phase 8 — Crash Testing (extreme parameters)
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Crash test - all params at extreme values simultaneously")
{
    PointState points[8];
    DCBlocker dcL, dcR;
    PeakLimiter limL, limR;
    dcL.setSampleRate(44100.0); dcR.setSampleRate(44100.0);
    limL.prepare(44100.0); limR.prepare(44100.0);

    for (int i = 0; i < 8; ++i)
    {
        points[i].prepare(44100.0);
        points[i].setPitchShift(24.0f);   // max pitch
        points[i].feedbackAmount = 1.1f;  // max feedback
        points[i].level = 2.0f;           // max level
        points[i].filter.setSampleRate(44100.0);
        points[i].filter.setType(BiQuadFilter::Type::BandPass);
        points[i].filter.setCutoff(20000.0f); // max cutoff
        points[i].filter.setQ(20.0f);         // max Q
    }

    unsigned int rng = 666;
    bool crashed = false;

    for (int s = 0; s < 44100; ++s) // 1 second
    {
        rng = rng * 1664525u + 1013904223u;
        float noise = (static_cast<float>(rng) / 4294967296.0f - 0.5f);

        float wetL = 0.0f, wetR = 0.0f;
        for (int p = 0; p < 8; ++p)
        {
            const float mixedIn = points[p].feedback.mixInput(noise, points[p].feedbackAmount * 1.1f);
            const float filtered = points[p].filter.process(mixedIn);
            points[p].feedback.processOutput(filtered);
            wetL += filtered / 8.0f;
            wetR += filtered / 8.0f;
        }

        float outL = limL.process(dcL.process(wetL));
        float outR = limR.process(dcR.process(wetR));

        if (!std::isfinite(outL) || !std::isfinite(outR))
        {
            crashed = true;
            break;
        }
    }

    CHECK_FALSE(crashed);
}

TEST_CASE("Crash test - rapid pitch shift changes")
{
    PointState pt;
    pt.prepare(44100.0);
    pt.feedbackAmount = 0.9f;
    pt.level = 1.0f;
    pt.filter.setSampleRate(44100.0);
    pt.filter.setType(BiQuadFilter::Type::LowPass);
    pt.filter.setCutoff(2000.0f);
    pt.filter.setQ(10.0f);

    unsigned int rng = 12321;
    bool allFinite = true;

    for (int s = 0; s < 44100; ++s)
    {
        // Change pitch every 32 samples (extremely rapid)
        if (s % 32 == 0)
        {
            float newPitch = (static_cast<float>(s % 48) - 24.0f);
            pt.setPitchShift(newPitch);
        }

        rng = rng * 1664525u + 1013904223u;
        float noise = (static_cast<float>(rng) / 4294967296.0f - 0.5f) * 0.3f;

        const float mixedIn = pt.feedback.mixInput(noise, pt.feedbackAmount * 0.9f);
        const float filtered = pt.filter.process(mixedIn);
        pt.feedback.processOutput(filtered);

        if (!std::isfinite(filtered)) { allFinite = false; break; }
    }

    CHECK(allFinite);
}

TEST_CASE("Crash test - rapid filter cutoff changes")
{
    PointState pt;
    pt.prepare(44100.0);
    pt.feedbackAmount = 0.8f;
    pt.level = 1.0f;
    pt.filter.setSampleRate(44100.0);
    pt.filter.setType(BiQuadFilter::Type::HighPass);
    pt.filter.setQ(15.0f);
    pt.setPitchShift(7.0f);

    unsigned int rng = 54321;
    bool allFinite = true;

    for (int s = 0; s < 44100; ++s)
    {
        // Sweep cutoff rapidly every sample
        float cutoff = 20.0f + static_cast<float>(s % 1000) * 20.0f; // 20-20000 Hz sweep
        pt.filter.setCutoff(cutoff);

        rng = rng * 1664525u + 1013904223u;
        float noise = (static_cast<float>(rng) / 4294967296.0f - 0.5f) * 0.3f;

        const float mixedIn = pt.feedback.mixInput(noise, pt.feedbackAmount * 1.0f);
        const float filtered = pt.filter.process(mixedIn);
        pt.feedback.processOutput(filtered);

        if (!std::isfinite(filtered)) { allFinite = false; break; }
    }

    CHECK(allFinite);
}

TEST_CASE("Crash test - zero-length and minimum cutoff")
{
    BiQuadFilter f;
    f.setSampleRate(44100.0);
    f.setType(BiQuadFilter::Type::LowPass);
    f.setCutoff(20.0f); // minimum
    f.setQ(20.0f);      // maximum

    float out = 0.0f;
    for (int i = 0; i < 1000; ++i)
        out = f.process(1.0f); // constant DC input

    CHECK(std::isfinite(out));
}
