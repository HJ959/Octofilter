#pragma once

#include <cmath>
#include <cstdint>

namespace Octofilter
{

/**
 * BiQuadFilter — Direct Form II Transposed biquad filter.
 *
 * Supports LP, HP, BP (constant skirt gain), and notch filter types.
 * Coefficients are only recalculated when cutoff or Q actually change,
 * not on every sample.
 *
 * All state is per-instance, so each filter point owns one of these.
 */
class BiQuadFilter
{
public:
    enum class Type : uint8_t
    {
        LowPass = 0,
        HighPass,
        BandPass,
        Notch
    };

    BiQuadFilter() noexcept { reset(); }

    /** Set sample rate. Must be called before any processing. */
    void setSampleRate(double sampleRate) noexcept
    {
        mSampleRate = sampleRate;
        mDirty      = true;
    }

    /** Set filter type. Marks coefficients dirty. */
    void setType(Type type) noexcept
    {
        if (mType != type)
        {
            mType  = type;
            mDirty = true;
        }
    }

    /**
     * Set cutoff frequency in Hz.
     * Clamped to [20, sampleRate/2 - 1] internally.
     */
    void setCutoff(float cutoffHz) noexcept
    {
        // Hard floor to avoid coefficient instability at very low frequencies.
        const float clamped = clampCutoff(cutoffHz);
        if (clamped != mCutoffHz)
        {
            mCutoffHz = clamped;
            mDirty    = true;
        }
    }

    /**
     * Set resonance Q (0.1 – 20). Clamped internally.
     */
    void setQ(float q) noexcept
    {
        const float clamped = clampQ(q);
        if (clamped != mQ)
        {
            mQ     = clamped;
            mDirty = true;
        }
    }

    /** Clear internal delay state (z1, z2). Call on transport reset. */
    void reset() noexcept
    {
        mZ1 = mZ2 = 0.0f;
        mDirty    = true;
    }

    /** Process a single sample. Recalculates coefficients if dirty. */
    float process(float x) noexcept
    {
        if (mDirty)
            recalculate();

        // Direct Form II Transposed:
        //   y  = b0*x + z1
        //   z1 = b1*x - a1*y + z2
        //   z2 = b2*x - a2*y
        const float y = mB0 * x + mZ1;
        mZ1           = mB1 * x - mA1 * y + mZ2;
        mZ2           = mB2 * x - mA2 * y;
        return y;
    }

    Type  getType() const noexcept { return mType; }
    float getCutoff() const noexcept { return mCutoffHz; }
    float getQ() const noexcept { return mQ; }

private:
    // ── Coefficient calculation ───────────────────────────────────────────
    void recalculate() noexcept
    {
        mDirty = false;

        const double fs    = mSampleRate;
        const double f0    = static_cast<double>(mCutoffHz);
        const double q     = static_cast<double>(mQ);
        const double omega = 2.0 * M_PI * f0 / fs;
        const double sinW  = std::sin(omega);
        const double cosW  = std::cos(omega);
        const double alpha = sinW / (2.0 * q);

        double b0, b1, b2, a0, a1, a2;

        switch (mType)
        {
        case Type::LowPass:
            b0 = (1.0 - cosW) / 2.0;
            b1 = 1.0 - cosW;
            b2 = (1.0 - cosW) / 2.0;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cosW;
            a2 = 1.0 - alpha;
            break;

        case Type::HighPass:
            b0 = (1.0 + cosW) / 2.0;
            b1 = -(1.0 + cosW);
            b2 = (1.0 + cosW) / 2.0;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cosW;
            a2 = 1.0 - alpha;
            break;

        case Type::BandPass:
            // Constant skirt gain, peak gain = Q
            b0 = sinW / 2.0;
            b1 = 0.0;
            b2 = -(sinW / 2.0);
            a0 = 1.0 + alpha;
            a1 = -2.0 * cosW;
            a2 = 1.0 - alpha;
            break;

        case Type::Notch:
        default:
            b0 = 1.0;
            b1 = -2.0 * cosW;
            b2 = 1.0;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cosW;
            a2 = 1.0 - alpha;
            break;
        }

        // Normalise by a0
        mB0 = static_cast<float>(b0 / a0);
        mB1 = static_cast<float>(b1 / a0);
        mB2 = static_cast<float>(b2 / a0);
        mA1 = static_cast<float>(a1 / a0);
        mA2 = static_cast<float>(a2 / a0);
    }

    static float clampCutoff(float hz) noexcept
    {
        if (hz < 20.0f)
            return 20.0f;
        if (hz > 20000.0f)
            return 20000.0f;
        return hz;
    }

    static float clampQ(float q) noexcept
    {
        if (q < 0.1f)
            return 0.1f;
        if (q > 20.0f)
            return 20.0f;
        return q;
    }

    // ── State ─────────────────────────────────────────────────────────────
    double mSampleRate { 44100.0 };
    float  mCutoffHz { 1000.0f };
    float  mQ { 0.707f }; // Butterworth
    Type   mType { Type::LowPass };
    bool   mDirty { true };

    // Coefficients (normalised)
    float mB0 { 1.0f }, mB1 { 0.0f }, mB2 { 0.0f };
    float mA1 { 0.0f }, mA2 { 0.0f };

    // Delay state
    float mZ1 { 0.0f }, mZ2 { 0.0f };
};

} // namespace Octofilter
