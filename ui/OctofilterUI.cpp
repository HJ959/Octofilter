#include "DistrhoUI.hpp"
#include "InterFont.hpp"
#include <cstdio>
#include <cmath>

START_NAMESPACE_DISTRHO

// ── Blodyn Tatws colour palette ───────────────────────────────────────────────
namespace Palette
{
    static const Color bg         {0.16f, 0.16f, 0.16f};
    static const Color fieldBg    {0.60f, 0.65f, 0.46f, 0.3f};
    static const Color text       {0.99f, 0.98f, 0.85f};
    static const Color accent     {0.655f, 0.498f, 0.682f};
    static const Color accentLight{0.929f, 0.792f, 0.914f};
    static const Color highlight  {0.992f, 0.753f, 0.027f};
    static const Color outline    {0.631f, 0.439f, 0.275f};
    static const Color sage       {0.60f, 0.65f, 0.46f};

    static const Color filterLP   {0.655f, 0.498f, 0.682f};
    static const Color filterHP   {0.992f, 0.753f, 0.027f};
    static const Color filterBP   {0.60f, 0.65f, 0.46f};
    static const Color filterNotch{0.929f, 0.792f, 0.914f};

    static Color filterColor(int type)
    {
        switch (type)
        {
        case 0: return filterLP;
        case 1: return filterHP;
        case 2: return filterBP;
        case 3: return filterNotch;
        default: return text;
        }
    }
}

// ── Parameter indices (must match plugin) ─────────────────────────────────────
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

static constexpr int kNumGlobalParams   = 13;
static constexpr int kNumPerPointParams = 7;

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

static uint32_t ppIdx(int point, PerPointOffset off)
{
    return kNumGlobalParams + point * kNumPerPointParams + static_cast<int>(off);
}

// ── UI dimensions ─────────────────────────────────────────────────────────────
static constexpr uint kDefaultWidth  = 700;
static constexpr uint kDefaultHeight = 450;

// ── Knob helper struct ────────────────────────────────────────────────────────
struct Knob
{
    float x, y, radius;
    float min, max, value;
    uint32_t paramIndex;
    const char* label;
    bool dragging = false;
};

// ═══════════════════════════════════════════════════════════════════════════════
class OctofilterUI : public UI
{
public:
    OctofilterUI()
        : UI(kDefaultWidth, kDefaultHeight)
    {
        fFontId = createFontFromMemory("inter", kInterFontData, kInterFontDataSize, false);
        setGeometryConstraints(600, 400, true);
        setupGlobalKnobs();
        setupPerPointKnobs();

        for (int i = 0; i < 8; ++i)
        {
            fPointFilterType[i] = 0.0f;
            fPointPan[i]        = 0.0f;
            fPointLevel[i]      = 1.0f;
            fPointFeedback[i]   = 0.0f;
            fPointPitchShift[i] = 0.0f;
            fPointQ[i]          = 0.0f;
            fPointCutoffZone[i] = 1; // default to top zone
        }
    }

protected:
    void parameterChanged(uint32_t index, float value) override
    {
        if (index == kGlobalPointCount)    { fPointCount = static_cast<int>(value + 0.5f); repaint(); return; }
        if (index == kGlobalTexture)       { fTexture = value; updateGlobalKnob(0, value); repaint(); return; }
        if (index == kGlobalResonance)     { fResonance = value; updateGlobalKnob(1, value); repaint(); return; }
        if (index == kGlobalFeedback)      { fFeedback = value; updateGlobalKnob(2, value); repaint(); return; }
        if (index == kGlobalPitchShift)    { fPitchShift = value; updateGlobalKnob(3, value); repaint(); return; }
        if (index == kGlobalWetDry)        { fWetDry = value; updateGlobalKnob(4, value); repaint(); return; }
        if (index == kGlobalInputGain)     { fInputGain = value; updateGlobalKnob(5, value); repaint(); return; }
        if (index == kGlobalOutputGain)    { fOutputGain = value; updateGlobalKnob(6, value); repaint(); return; }
        if (index == kGlobalGlideTime)     { fGlideTime = value; updateGlobalKnob(7, value); repaint(); return; }
        if (index == kGlobalHarmonicMode)  { fHarmonicMode = value; repaint(); return; }

        if (index >= static_cast<uint32_t>(kNumGlobalParams))
        {
            const int rel   = static_cast<int>(index) - kNumGlobalParams;
            const int point = rel / kNumPerPointParams;
            const int off   = rel % kNumPerPointParams;
            if (point >= 8) return;

            switch (off)
            {
            case kPPFilterType:   fPointFilterType[point] = value; break;
            case kPPCutoffOffset: fPointCutoffOffset[point] = value; break;
            case kPPPan:          fPointPan[point] = value; break;
            case kPPLevel:        fPointLevel[point] = value; break;
            case kPPFeedback:     fPointFeedback[point] = value; break;
            case kPPPitchShift:   fPointPitchShift[point] = value; break;
            case kPPQ:            fPointQ[point] = value; break;
            default: break;
            }
            if (point == fSelectedPoint) syncPerPointKnobs();
            repaint();
        }
    }

    void stateChanged(const char*, const char*) override {}

    // ── Drawing ───────────────────────────────────────────────────────────
    void onNanoDisplay() override
    {
        const float w = static_cast<float>(getWidth());
        const float h = static_cast<float>(getHeight());

        // Background
        beginPath();
        rect(0, 0, w, h);
        fillColor(Palette::bg);
        fill();

        // Layout: field on left (50% width, 65% height), per-point panel right, globals bottom
        const float topBarH   = 32.0f;
        const float globalH   = 80.0f;
        const float fieldW    = w * 0.50f;
        const float fieldH    = h - topBarH - globalH - 20.0f;
        const float fieldX    = 10.0f;
        const float fieldY    = topBarH + 5.0f;
        const float ppX       = fieldX + fieldW + 10.0f;
        const float ppW       = w - ppX - 10.0f;
        const float ppY       = fieldY;
        const float ppH       = fieldH;
        const float globalX   = 10.0f;
        const float globalY   = h - globalH - 5.0f;
        const float globalW   = w - 20.0f;

        drawTopBar(w, topBarH);
        drawStereoField(fieldX, fieldY, fieldW, fieldH);
        drawPerPointPanel(ppX, ppY, ppW, ppH);
        drawGlobalStrip(globalX, globalY, globalW, globalH);
    }

    // ── Top bar (title + point count + Randomise + Harmonic) ────────────
    void drawTopBar(float w, float h)
    {
        fontSize(15.0f);
        fontFaceId(fFontId);
        fillColor(Palette::text);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(12.0f, h * 0.5f, "OCTOFILTER", nullptr);

        const float btnH = 18.0f;
        const float btnY = h * 0.5f - btnH * 0.5f;

        // ── Randomise button (after title) ────────────────────────────────
        const float rndX = 120.0f;
        const float rndW = 60.0f;
        fRandomiseBtnX = rndX;
        fRandomiseBtnY = btnY;
        fRandomiseBtnW = rndW;
        fRandomiseBtnH = btnH;

        beginPath();
        roundedRect(rndX, btnY, rndW, btnH, 3.0f);
        fillColor(Palette::highlight);
        fill();
        fontSize(10.0f);
        fillColor(Palette::bg);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(rndX + rndW * 0.5f, btnY + btnH * 0.5f, "RANDOM", nullptr);

        // ── Harmonic Mode toggle ──────────────────────────────────────────
        const float harmX = rndX + rndW + 10.0f;
        const float harmW = 70.0f;
        fHarmonicBtnX = harmX;
        fHarmonicBtnY = btnY;
        fHarmonicBtnW = harmW;
        fHarmonicBtnH = btnH;

        const bool harmActive = (fHarmonicMode > 0.5f);
        beginPath();
        roundedRect(harmX, btnY, harmW, btnH, 3.0f);
        fillColor(harmActive ? Palette::accent : Palette::outline);
        fill();
        fontSize(10.0f);
        fillColor(harmActive ? Palette::text : Palette::accentLight);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(harmX + harmW * 0.5f, btnY + btnH * 0.5f, "HARMONIC", nullptr);

        // ── Point count selector: [-] N [+] (far right) ──────────────────
        const float pcBtnSize = 18.0f;
        const float rightX = w - 12.0f;
        fPtCountPlusX   = rightX - pcBtnSize;
        fPtCountMinusX  = fPtCountPlusX - pcBtnSize * 2.5f;
        fPtCountBtnY    = btnY;
        fPtCountBtnSize = pcBtnSize;

        // [-] button
        beginPath();
        roundedRect(fPtCountMinusX, fPtCountBtnY, pcBtnSize, pcBtnSize, 3.0f);
        fillColor(Palette::outline);
        fill();
        fontSize(14.0f);
        fillColor(Palette::text);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(fPtCountMinusX + pcBtnSize * 0.5f, fPtCountBtnY + pcBtnSize * 0.5f, "-", nullptr);

        // Count display
        char pcBuf[8];
        std::snprintf(pcBuf, sizeof(pcBuf), "%d", fPointCount);
        fontSize(14.0f);
        fillColor(Palette::highlight);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(fPtCountMinusX + pcBtnSize * 1.75f, fPtCountBtnY + pcBtnSize * 0.5f, pcBuf, nullptr);

        // [+] button
        beginPath();
        roundedRect(fPtCountPlusX, fPtCountBtnY, pcBtnSize, pcBtnSize, 3.0f);
        fillColor(Palette::outline);
        fill();
        fontSize(14.0f);
        fillColor(Palette::text);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(fPtCountPlusX + pcBtnSize * 0.5f, fPtCountBtnY + pcBtnSize * 0.5f, "+", nullptr);
    }

    // ── Stereo field ──────────────────────────────────────────────────────
    void drawStereoField(float x, float y, float w, float h)
    {
        fFieldX = x; fFieldY = y; fFieldW = w; fFieldH = h;

        beginPath();
        roundedRect(x, y, w, h, 6.0f);
        fillColor(Palette::fieldBg);
        fill();
        strokeColor(Palette::outline);
        strokeWidth(1.0f);
        stroke();

        // L/R labels
        fontSize(10.0f);
        fontFaceId(fFontId);
        fillColor(Palette::text);
        textAlign(ALIGN_LEFT | ALIGN_BOTTOM);
        text(x + 4.0f, y + h - 4.0f, "L", nullptr);
        textAlign(ALIGN_RIGHT | ALIGN_BOTTOM);
        text(x + w - 4.0f, y + h - 4.0f, "R", nullptr);

        // Centre line vertical
        beginPath();
        moveTo(x + w * 0.5f, y + 4.0f);
        lineTo(x + w * 0.5f, y + h - 4.0f);
        strokeColor(Color(1.0f, 1.0f, 1.0f, 0.25f));
        strokeWidth(1.0f);
        stroke();

        // Centre line horizontal (cutoff midpoint)
        beginPath();
        moveTo(x + 4.0f, y + h * 0.5f);
        lineTo(x + w - 4.0f, y + h * 0.5f);
        strokeColor(Color(1.0f, 1.0f, 1.0f, 0.25f));
        strokeWidth(1.0f);
        stroke();

        // Draw point nodes
        for (int i = 0; i < fPointCount; ++i)
        {
            const float pan = fPointPan[i] * fTexture; // pan collapses with texture
            const float px  = x + (pan + 1.0f) * 0.5f * w;

            // Y = cutoff offset. Centre line = 0. Up = positive offset. Down = negative.
            // Map [-24, +24] to [bottom, top] with 0 at centre
            const float cutoffNorm = fPointCutoffOffset[i] / 24.0f; // -1 to +1
            const float scaledY = 0.5f + cutoffNorm * 0.5f * fTexture; // collapse to centre at texture=0
            const float py  = y + h - scaledY * h;

            // Size = global feedback × per-point feedback
            const float effectiveFB = fFeedback * fPointFeedback[i];
            const float baseSize = 10.0f;
            const float fbSize   = baseSize + effectiveFB * 14.0f;

            const int ftype = static_cast<int>(fPointFilterType[i] + 0.5f) % 4;
            Color nodeColor = Palette::filterColor(ftype);

            // Selection ring
            if (i == fSelectedPoint)
            {
                beginPath();
                circle(px, py, fbSize + 5.0f);
                strokeColor(Palette::highlight);
                strokeWidth(2.5f);
                stroke();
            }

            // Node fill
            beginPath();
            circle(px, py, fbSize);
            fillColor(nodeColor);
            fill();

            // Pitch shift ring
            const float pitchRing = std::fabs(fPointPitchShift[i]) * 0.12f;
            if (pitchRing > 0.5f)
            {
                beginPath();
                circle(px, py, fbSize + 2.5f);
                strokeColor(Palette::text);
                strokeWidth(pitchRing);
                stroke();
            }

            // Point number
            fontSize(10.0f);
            fillColor(Palette::bg);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            char num[4];
            std::snprintf(num, sizeof(num), "%d", i + 1);
            text(px, py, num, nullptr);

            // Store position for hit testing
            fPointScreenX[i] = px;
            fPointScreenY[i] = py;
            fPointScreenR[i] = fbSize;
        }
    }

    // ── Per-point panel (right side) ──────────────────────────────────────
    void drawPerPointPanel(float x, float y, float w, float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 4.0f);
        fillColor(Color(0.13f, 0.13f, 0.13f));
        fill();

        if (fSelectedPoint < 0 || fSelectedPoint >= fPointCount)
        {
            fontSize(22.0f);
            fontFaceId(fFontId);
            fillColor(Palette::accentLight);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            text(x + w * 0.5f, y + h * 0.5f, "Click a point\nto edit", nullptr);
            return;
        }

        // Header
        fontSize(15.0f);
        fontFaceId(fFontId);
        fillColor(Palette::highlight);
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        char hdr[32];
        std::snprintf(hdr, sizeof(hdr), "POINT %d", fSelectedPoint + 1);
        text(x + w * 0.5f, y + 8.0f, hdr, nullptr);

        // Group labels
        fontSize(9.0f);
        fillColor(Palette::sage);
        textAlign(ALIGN_LEFT | ALIGN_TOP);

        const float row1Y = y + 35.0f;
        const float row2Y = y + h * 0.37f + 15.0f;
        const float row3Y = y + h * 0.65f + 10.0f;

        text(x + 8.0f, row1Y - 12.0f, "FILTER", nullptr);
        text(x + 8.0f, row2Y - 12.0f, "SPATIAL / FB", nullptr);
        text(x + 8.0f, row3Y - 12.0f, "MODULATION", nullptr);

        // Knob size
        const float knobR = 22.0f;

        // Row 1: Filter Type buttons + Q + Cutoff
        // Filter type as 4 clickable buttons: [LP] [HP] [BP] [NT]
        const float ftBtnW = (w - 20.0f) / 4.0f;
        const float ftBtnH = 16.0f;
        const float ftY    = row1Y;
        fFilterBtnX = x + 10.0f;
        fFilterBtnY = ftY;
        fFilterBtnW = ftBtnW;
        fFilterBtnH = ftBtnH;

        const char* ftLabels[] = {"LP", "HP", "BP", "NT"};
        const int currentType = static_cast<int>(fPointFilterType[fSelectedPoint] + 0.5f) % 4;

        for (int t = 0; t < 4; ++t)
        {
            const float bx = fFilterBtnX + t * ftBtnW;
            beginPath();
            roundedRect(bx + 1.0f, ftY, ftBtnW - 2.0f, ftBtnH, 3.0f);
            fillColor(t == currentType ? Palette::filterColor(t) : Palette::outline);
            fill();

            fontSize(10.0f);
            fontFaceId(fFontId);
            fillColor(t == currentType ? Palette::bg : Palette::text);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            text(bx + ftBtnW * 0.5f, ftY + ftBtnH * 0.5f, ftLabels[t], nullptr);
        }

        // Q and Cutoff knobs below the filter buttons
        const float qcY = ftY + ftBtnH + knobR + 8.0f;
        const float col1 = x + w * 0.3f;
        const float col2 = x + w * 0.7f;

        fPPKnobs[1].x = col1; fPPKnobs[1].y = qcY; fPPKnobs[1].radius = knobR;
        fPPKnobs[6].x = col2; fPPKnobs[6].y = qcY; fPPKnobs[6].radius = knobR;
        drawKnob(fPPKnobs[1]);
        drawKnob(fPPKnobs[6]);

        // Row 2: Pan + Feedback
        const float col1r2 = x + w * 0.3f;
        const float col2r2 = x + w * 0.7f;
        fPPKnobs[2].x = col1r2; fPPKnobs[2].y = row2Y + knobR + 5.0f; fPPKnobs[2].radius = knobR;
        fPPKnobs[4].x = col2r2; fPPKnobs[4].y = row2Y + knobR + 5.0f; fPPKnobs[4].radius = knobR;
        drawKnob(fPPKnobs[2]);
        drawKnob(fPPKnobs[4]);

        // Row 3: Level + Pitch
        fPPKnobs[3].x = col1r2; fPPKnobs[3].y = row3Y + knobR + 5.0f; fPPKnobs[3].radius = knobR;
        fPPKnobs[5].x = col2r2; fPPKnobs[5].y = row3Y + knobR + 5.0f; fPPKnobs[5].radius = knobR;
        drawKnob(fPPKnobs[3]);
        drawKnob(fPPKnobs[5]);
    }

    // ── Global controls (bottom strip) ────────────────────────────────────
    void drawGlobalStrip(float x, float y, float w, float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 4.0f);
        fillColor(Color(0.12f, 0.12f, 0.12f));
        fill();

        const float knobR = 20.0f;
        const float cy = y + h * 0.5f + 2.0f;

        // Group positions: FILTER | EFFECT | OUTPUT
        // FILTER: Texture, Reso
        // EFFECT: Feedback, Pitch, Glide
        // OUTPUT: Wet/Dry, In, Out
        const float groupW = w / 3.0f;

        // Group labels
        fontSize(8.0f);
        fontFaceId(fFontId);
        fillColor(Palette::sage);
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        text(x + groupW * 0.5f, y + 3.0f, "FILTER", nullptr);
        text(x + groupW * 1.5f, y + 3.0f, "EFFECT", nullptr);
        text(x + groupW * 2.5f, y + 3.0f, "OUTPUT", nullptr);

        // FILTER group: Texture, Reso (2 knobs)
        const float fSpacing = groupW / 3.0f;
        fGlobalKnobs[0].x = x + fSpacing;       fGlobalKnobs[0].y = cy; fGlobalKnobs[0].radius = knobR;
        fGlobalKnobs[1].x = x + fSpacing * 2.0f; fGlobalKnobs[1].y = cy; fGlobalKnobs[1].radius = knobR;

        // EFFECT group: Feedback, Pitch, Glide (3 knobs)
        const float eStart = x + groupW;
        const float eSpacing = groupW / 4.0f;
        fGlobalKnobs[2].x = eStart + eSpacing;       fGlobalKnobs[2].y = cy; fGlobalKnobs[2].radius = knobR;
        fGlobalKnobs[3].x = eStart + eSpacing * 2.0f; fGlobalKnobs[3].y = cy; fGlobalKnobs[3].radius = knobR;
        fGlobalKnobs[7].x = eStart + eSpacing * 3.0f; fGlobalKnobs[7].y = cy; fGlobalKnobs[7].radius = knobR;

        // OUTPUT group: Wet/Dry, In, Out (3 knobs)
        const float oStart = x + groupW * 2.0f;
        const float oSpacing = groupW / 4.0f;
        fGlobalKnobs[4].x = oStart + oSpacing;       fGlobalKnobs[4].y = cy; fGlobalKnobs[4].radius = knobR;
        fGlobalKnobs[5].x = oStart + oSpacing * 2.0f; fGlobalKnobs[5].y = cy; fGlobalKnobs[5].radius = knobR;
        fGlobalKnobs[6].x = oStart + oSpacing * 3.0f; fGlobalKnobs[6].y = cy; fGlobalKnobs[6].radius = knobR;

        for (int i = 0; i < fNumGlobalKnobs; ++i)
            drawKnob(fGlobalKnobs[i]);
    }

    // ── Knob drawing ──────────────────────────────────────────────────────
    void drawKnob(const Knob& k)
    {
        // Standard knob: starts bottom-left (7 o'clock), sweeps CW to bottom-right (5 o'clock)
        const float startAngle = 0.75f * 3.14159f;  // 135° = 7 o'clock
        const float endAngle   = 2.25f * 3.14159f;  // 405° = 5 o'clock
        const float range      = endAngle - startAngle;
        const float normalized = (k.max > k.min) ? (k.value - k.min) / (k.max - k.min) : 0.0f;
        const float valueAngle = startAngle + normalized * range;

        // Is this a bipolar parameter? (min is negative, max is positive, zero is centre)
        const bool bipolar = (k.min < 0.0f && k.max > 0.0f);
        const float centreAngle = bipolar ? startAngle + (-k.min / (k.max - k.min)) * range : startAngle;

        // Track background (full sweep)
        beginPath();
        arc(k.x, k.y, k.radius, startAngle, endAngle, NanoVG::CW);
        strokeColor(Palette::outline);
        strokeWidth(3.5f);
        stroke();

        // Value arc
        if (bipolar)
        {
            // Draw from centre to current value
            if (normalized > 0.001f || normalized < -0.001f)
            {
                const float fromAngle = (valueAngle < centreAngle) ? valueAngle : centreAngle;
                const float toAngle   = (valueAngle < centreAngle) ? centreAngle : valueAngle;
                beginPath();
                arc(k.x, k.y, k.radius, fromAngle, toAngle, NanoVG::CW);
                strokeColor(k.dragging ? Palette::highlight : Palette::accent);
                strokeWidth(3.5f);
                stroke();
            }
        }
        else
        {
            // Unipolar: draw from start to value
            if (normalized > 0.001f)
            {
                beginPath();
                arc(k.x, k.y, k.radius, startAngle, valueAngle, NanoVG::CW);
                strokeColor(k.dragging ? Palette::highlight : Palette::accent);
                strokeWidth(3.5f);
                stroke();
            }
        }

        // Centre dot
        beginPath();
        circle(k.x, k.y, 3.5f);
        fillColor(Palette::text);
        fill();

        // Label
        fontSize(9.0f);
        fontFaceId(fFontId);
        fillColor(Palette::text);
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        text(k.x, k.y + k.radius + 5.0f, k.label, nullptr);
    }

    // ── Mouse interaction ─────────────────────────────────────────────────
    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1) return false;

        const float mx = ev.pos.getX();
        const float my = ev.pos.getY();

        if (ev.press)
        {
            // Randomise button — do it from UI side so visuals update immediately
            if (mx >= fRandomiseBtnX && mx <= fRandomiseBtnX + fRandomiseBtnW &&
                my >= fRandomiseBtnY && my <= fRandomiseBtnY + fRandomiseBtnH)
            {
                doUIRandomise();
                repaint();
                return true;
            }

            // Harmonic Mode toggle
            if (mx >= fHarmonicBtnX && mx <= fHarmonicBtnX + fHarmonicBtnW &&
                my >= fHarmonicBtnY && my <= fHarmonicBtnY + fHarmonicBtnH)
            {
                fHarmonicMode = (fHarmonicMode > 0.5f) ? 0.0f : 1.0f;
                setParameterValue(kGlobalHarmonicMode, fHarmonicMode);
                repaint();
                return true;
            }

            // Filter type buttons (only when a point is selected)
            if (fSelectedPoint >= 0 &&
                mx >= fFilterBtnX && mx <= fFilterBtnX + fFilterBtnW * 4.0f &&
                my >= fFilterBtnY && my <= fFilterBtnY + fFilterBtnH)
            {
                const int btn = static_cast<int>((mx - fFilterBtnX) / fFilterBtnW);
                if (btn >= 0 && btn < 4)
                {
                    fPointFilterType[fSelectedPoint] = static_cast<float>(btn);
                    setParameterValue(ppIdx(fSelectedPoint, kPPFilterType), static_cast<float>(btn));
                    syncPerPointKnobs();
                    repaint();
                    return true;
                }
            }

            // Point count buttons
            if (mx >= fPtCountMinusX && mx <= fPtCountMinusX + fPtCountBtnSize &&
                my >= fPtCountBtnY && my <= fPtCountBtnY + fPtCountBtnSize)
            {
                if (fPointCount > 2)
                {
                    fPointCount--;
                    setParameterValue(kGlobalPointCount, static_cast<float>(fPointCount));
                    if (fSelectedPoint >= fPointCount) fSelectedPoint = -1;
                    repaint();
                }
                return true;
            }
            if (mx >= fPtCountPlusX && mx <= fPtCountPlusX + fPtCountBtnSize &&
                my >= fPtCountBtnY && my <= fPtCountBtnY + fPtCountBtnSize)
            {
                if (fPointCount < 8)
                {
                    fPointCount++;
                    setParameterValue(kGlobalPointCount, static_cast<float>(fPointCount));
                    repaint();
                }
                return true;
            }

            // Global knobs
            for (int i = 0; i < fNumGlobalKnobs; ++i)
            {
                Knob& k = fGlobalKnobs[i];
                const float dx = mx - k.x;
                const float dy = my - k.y;
                if (dx * dx + dy * dy < (k.radius + 5.0f) * (k.radius + 5.0f))
                {
                    k.dragging = true;
                    fDragStartY = my;
                    fDragStartValue = k.value;
                    fActiveKnob = &k;
                    repaint();
                    return true;
                }
            }

            // Per-point knobs
            if (fSelectedPoint >= 0)
            {
                for (int i = 0; i < 7; ++i)
                {
                    Knob& k = fPPKnobs[i];
                    const float dx = mx - k.x;
                    const float dy = my - k.y;
                    if (dx * dx + dy * dy < (k.radius + 5.0f) * (k.radius + 5.0f))
                    {
                        k.dragging = true;
                        fDragStartY = my;
                        fDragStartValue = k.value;
                        fActiveKnob = &k;
                        repaint();
                        return true;
                    }
                }
            }

            // Point node hits
            for (int i = 0; i < fPointCount; ++i)
            {
                const float dx = mx - fPointScreenX[i];
                const float dy = my - fPointScreenY[i];
                const float r  = fPointScreenR[i] + 5.0f;
                if (dx * dx + dy * dy < r * r)
                {
                    fSelectedPoint = i;
                    fDraggingPoint = true;
                    syncPerPointKnobs();
                    repaint();
                    return true;
                }
            }

            // Clicked empty space — deselect
            fSelectedPoint = -1;
            repaint();
        }
        else
        {
            // Release
            if (fActiveKnob)
            {
                fActiveKnob->dragging = false;
                fActiveKnob = nullptr;
                repaint();
            }
            fDraggingPoint = false;
        }
        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        const float my = ev.pos.getY();
        const float mx = ev.pos.getX();

        // Knob dragging
        if (fActiveKnob)
        {
            Knob& k = *fActiveKnob;
            const float dy = fDragStartY - my;
            const float range = k.max - k.min;
            const float sensitivity = range / 200.0f;
            float newVal = fDragStartValue + dy * sensitivity;
            if (newVal < k.min) newVal = k.min;
            if (newVal > k.max) newVal = k.max;
            k.value = newVal;

            // For cutoff knob: send signed value using remembered zone direction
            if (fSelectedPoint >= 0 && k.paramIndex == ppIdx(fSelectedPoint, kPPCutoffOffset))
            {
                const float sign = (fPointCutoffZone[fSelectedPoint] >= 0) ? 1.0f : -1.0f;
                setParameterValue(k.paramIndex, newVal * sign);
            }
            else
            {
                setParameterValue(k.paramIndex, newVal);
            }

            syncKnobToState(k);
            repaint();
            return true;
        }

        // Point dragging — X=pan, Y=cutoff offset
        if (fDraggingPoint && fSelectedPoint >= 0)
        {
            // Pan from X
            float pan = (mx - fFieldX) / fFieldW * 2.0f - 1.0f;
            if (pan < -1.0f) pan = -1.0f;
            if (pan >  1.0f) pan =  1.0f;
            fPointPan[fSelectedPoint] = pan;
            setParameterValue(ppIdx(fSelectedPoint, kPPPan), pan);

            // Cutoff offset from Y: centre=0, up=+24, down=-24
            float normY = 1.0f - (my - fFieldY) / fFieldH; // 0=bottom, 1=top
            float offset = (normY - 0.5f) * 48.0f; // map 0.5 centre → 0, top → +24, bottom → -24
            if (offset < -24.0f) offset = -24.0f;
            if (offset >  24.0f) offset =  24.0f;
            fPointCutoffOffset[fSelectedPoint] = offset;
            // Remember which zone the point is in
            if (offset > 0.0f)       fPointCutoffZone[fSelectedPoint] =  1;
            else if (offset < 0.0f)  fPointCutoffZone[fSelectedPoint] = -1;
            setParameterValue(ppIdx(fSelectedPoint, kPPCutoffOffset), offset);

            syncPerPointKnobs();
            repaint();
            return true;
        }
        return false;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        const float mx = ev.pos.getX();
        const float my = ev.pos.getY();

        // Check if mouse is over a point node
        for (int i = 0; i < fPointCount; ++i)
        {
            const float dx = mx - fPointScreenX[i];
            const float dy = my - fPointScreenY[i];
            const float r  = fPointScreenR[i] + 8.0f;
            if (dx * dx + dy * dy < r * r)
            {
                // Scroll changes feedback (size)
                float fb = fPointFeedback[i] + ev.delta.getY() * 0.05f;
                if (fb < 0.0f) fb = 0.0f;
                if (fb > 1.0f) fb = 1.0f;
                fPointFeedback[i] = fb;
                setParameterValue(ppIdx(i, kPPFeedback), fb);
                if (i == fSelectedPoint) syncPerPointKnobs();
                repaint();
                return true;
            }
        }
        return false;
    }

private:
    // ── Knob setup ────────────────────────────────────────────────────────
    void setupGlobalKnobs()
    {
        fNumGlobalKnobs = 0;
        auto add = [&](uint32_t idx, float mn, float mx, float def, const char* lbl) {
            Knob k{}; k.paramIndex = idx; k.min = mn; k.max = mx; k.value = def; k.label = lbl;
            fGlobalKnobs[fNumGlobalKnobs++] = k;
        };
        add(kGlobalTexture,    0.0f,  1.0f,  0.5f,   "Texture");
        add(kGlobalResonance,  0.1f,  20.0f, 0.707f, "Reso");
        add(kGlobalFeedback,   0.0f,  1.0f,  0.3f,   "Feedback");
        add(kGlobalPitchShift, -24.f, 24.f,  0.0f,   "Pitch");
        add(kGlobalWetDry,     0.0f,  1.0f,  1.0f,   "Wet/Dry");
        add(kGlobalInputGain,  -24.f, 24.f,  0.0f,   "In");
        add(kGlobalOutputGain, -24.f, 24.f,  0.0f,   "Out");
        add(kGlobalGlideTime,  10.f,  2000.f,200.f,  "Glide");
    }

    void setupPerPointKnobs()
    {
        auto set = [&](int i, float mn, float mx, float def, const char* lbl, uint32_t paramIdx) {
            fPPKnobs[i] = Knob{}; 
            fPPKnobs[i].min = mn; fPPKnobs[i].max = mx; fPPKnobs[i].value = def;
            fPPKnobs[i].label = lbl; fPPKnobs[i].paramIndex = paramIdx;
        };
        set(0, 0.0f, 3.0f, 0.0f,   "Type",   0);
        set(1, 0.0f, 20.0f, 0.0f,  "Q",      0);
        set(2, -1.0f, 1.0f, 0.0f,  "Pan",    0);
        set(3, 0.0f, 2.0f, 1.0f,   "Level",  0);
        set(4, 0.0f, 1.0f, 0.0f,   "FB",     0);
        set(5, -24.f, 24.f, 0.0f,  "Pitch",  0);
        set(6, 0.0f, 24.f, 0.0f,  "Cutoff", 0);  // magnitude only (0=centre, 24=edge)
    }

    void syncPerPointKnobs()
    {
        if (fSelectedPoint < 0 || fSelectedPoint >= 8) return;
        const int p = fSelectedPoint;
        fPPKnobs[0].value = fPointFilterType[p];    fPPKnobs[0].paramIndex = ppIdx(p, kPPFilterType);
        fPPKnobs[1].value = fPointQ[p];             fPPKnobs[1].paramIndex = ppIdx(p, kPPQ);
        fPPKnobs[2].value = fPointPan[p];           fPPKnobs[2].paramIndex = ppIdx(p, kPPPan);
        fPPKnobs[3].value = fPointLevel[p];         fPPKnobs[3].paramIndex = ppIdx(p, kPPLevel);
        fPPKnobs[4].value = fPointFeedback[p];      fPPKnobs[4].paramIndex = ppIdx(p, kPPFeedback);
        fPPKnobs[5].value = fPointPitchShift[p];    fPPKnobs[5].paramIndex = ppIdx(p, kPPPitchShift);
        fPPKnobs[6].value = std::fabs(fPointCutoffOffset[p]);  fPPKnobs[6].paramIndex = ppIdx(p, kPPCutoffOffset);
    }

    void updateGlobalKnob(int idx, float value)
    {
        if (idx >= 0 && idx < fNumGlobalKnobs)
            fGlobalKnobs[idx].value = value;
    }

    void doUIRandomise()
    {
        // Simple LCG random for UI-side randomisation
        fRngState = fRngState * 1103515245u + 12345u;
        auto rndFloat = [&](float mn, float mx) -> float {
            fRngState = fRngState * 1103515245u + 12345u;
            const float t = static_cast<float>(fRngState & 0xFFFF) / 65535.0f;
            return mn + t * (mx - mn);
        };
        auto rndInt = [&](int mn, int mx) -> int {
            fRngState = fRngState * 1103515245u + 12345u;
            return mn + static_cast<int>(fRngState % static_cast<uint32_t>(mx - mn + 1));
        };

        const bool harmonic = (fHarmonicMode > 0.5f);

        for (int i = 0; i < 8; ++i)
        {
            // Cutoff offset (only in random mode)
            if (!harmonic)
            {
                const float offset = rndFloat(-24.0f, 24.0f);
                fPointCutoffOffset[i] = offset;
                fPointCutoffZone[i] = (offset >= 0.0f) ? 1 : -1;
                setParameterValue(ppIdx(i, kPPCutoffOffset), offset);
            }

            // Filter type
            const float ft = static_cast<float>(rndInt(0, 3));
            fPointFilterType[i] = ft;
            setParameterValue(ppIdx(i, kPPFilterType), ft);

            // Feedback
            const float fb = rndFloat(0.0f, 0.7f);
            fPointFeedback[i] = fb;
            setParameterValue(ppIdx(i, kPPFeedback), fb);

            // Pitch shift
            const float ps = rndFloat(-12.0f, 12.0f);
            fPointPitchShift[i] = ps;
            setParameterValue(ppIdx(i, kPPPitchShift), ps);
        }

        // Sync per-point knobs if one is selected
        if (fSelectedPoint >= 0) syncPerPointKnobs();
    }

    void syncKnobToState(const Knob& k)
    {
        // Update mirrored state when a knob changes
        if (k.paramIndex == kGlobalTexture)    fTexture = k.value;
        if (k.paramIndex == kGlobalResonance)  fResonance = k.value;
        if (k.paramIndex == kGlobalFeedback)   fFeedback = k.value;
        if (k.paramIndex == kGlobalPitchShift) fPitchShift = k.value;
        if (k.paramIndex == kGlobalWetDry)     fWetDry = k.value;
        if (k.paramIndex == kGlobalInputGain)  fInputGain = k.value;
        if (k.paramIndex == kGlobalOutputGain) fOutputGain = k.value;
        if (k.paramIndex == kGlobalGlideTime)  fGlideTime = k.value;

        // Per-point
        if (fSelectedPoint >= 0 && fSelectedPoint < 8)
        {
            const int p = fSelectedPoint;
            if (k.paramIndex == ppIdx(p, kPPFilterType))   fPointFilterType[p] = k.value;
            if (k.paramIndex == ppIdx(p, kPPQ))            fPointQ[p] = k.value;
            if (k.paramIndex == ppIdx(p, kPPPan))          fPointPan[p] = k.value;
            if (k.paramIndex == ppIdx(p, kPPLevel))        fPointLevel[p] = k.value;
            if (k.paramIndex == ppIdx(p, kPPFeedback))     fPointFeedback[p] = k.value;
            if (k.paramIndex == ppIdx(p, kPPPitchShift))   fPointPitchShift[p] = k.value;
            if (k.paramIndex == ppIdx(p, kPPCutoffOffset))
            {
                // Knob shows magnitude; use stored zone for sign
                const float sign = (fPointCutoffZone[p] >= 0) ? 1.0f : -1.0f;
                fPointCutoffOffset[p] = k.value * sign;
            }
        }
    }

    // ── State ─────────────────────────────────────────────────────────────
    int    fFontId { -1 };
    int    fPointCount { 2 };
    int    fSelectedPoint { -1 };
    bool   fDraggingPoint { false };
    float  fDragStartY { 0.0f };
    float  fDragStartValue { 0.0f };
    Knob*  fActiveKnob { nullptr };

    // Field geometry (stored for hit testing)
    float fFieldX{0}, fFieldY{0}, fFieldW{0}, fFieldH{0};
    float fPointScreenX[8]{}, fPointScreenY[8]{}, fPointScreenR[8]{};

    // Point count button positions
    float fPtCountMinusX{0}, fPtCountPlusX{0}, fPtCountBtnY{0}, fPtCountBtnSize{18};

    // Randomise & Harmonic button positions
    float fRandomiseBtnX{0}, fRandomiseBtnY{0}, fRandomiseBtnW{0}, fRandomiseBtnH{0};
    float fHarmonicBtnX{0}, fHarmonicBtnY{0}, fHarmonicBtnW{0}, fHarmonicBtnH{0};
    uint32_t fRngState { 42 };

    // Filter type button positions
    float fFilterBtnX{0}, fFilterBtnY{0}, fFilterBtnW{0}, fFilterBtnH{0};

    // Global params mirrored
    float fTexture{0.5f}, fResonance{0.707f}, fFeedback{0.3f};
    float fPitchShift{0}, fWetDry{1}, fInputGain{0}, fOutputGain{0};
    float fHarmonicMode{0}, fGlideTime{200};

    // Per-point params mirrored
    float fPointFilterType[8]{};
    float fPointCutoffOffset[8]{};
    int   fPointCutoffZone[8]{};  // -1=bottom, +1=top (remembered direction)
    float fPointPan[8]{};
    float fPointLevel[8]{};
    float fPointFeedback[8]{};
    float fPointPitchShift[8]{};
    float fPointQ[8]{};

    // Knobs
    static constexpr int kMaxGlobalKnobs = 12;
    Knob  fGlobalKnobs[kMaxGlobalKnobs]{};
    int   fNumGlobalKnobs{0};
    Knob  fPPKnobs[7]{};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OctofilterUI)
};

// ── DPF UI entry point ────────────────────────────────────────────────────────
UI* createUI()
{
    return new OctofilterUI();
}

END_NAMESPACE_DISTRHO
