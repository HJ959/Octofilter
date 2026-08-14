#include "DistrhoUI.hpp"
#include "InterFont.hpp"
#include "BgImage.hpp"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

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
    kGlobalStereoCollapse = 12,
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
        fScale = getScaleFactor();
        if (fScale < 1.0f) fScale = 1.0f;

        fFontId = createFontFromMemory("inter", kInterFontData, kInterFontDataSize, false);

        // Apply scale to window constraints
        setGeometryConstraints(
            static_cast<uint>(600 * fScale),
            static_cast<uint>(400 * fScale), true);

        if (fScale > 1.0f)
        {
            const uint sw = static_cast<uint>(kDefaultWidth * fScale);
            const uint sh = static_cast<uint>(kDefaultHeight * fScale);
            setSize(sw, sh);
        }
        setupGlobalKnobs();
        setupPerPointKnobs();

        // Load background image from embedded data
        fBgImage = createImageFromMemory(kBgImageData, kBgImageDataSize, 0);

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
        if (index == kGlobalTexture)       { fTexture = value; updateGlobalKnob(0, value); clampPointsToTexture(); repaint(); return; }
        if (index == kGlobalResonance)     { fResonance = value; repaint(); return; }
        if (index == kGlobalFeedback)      { fFeedback = value; updateGlobalKnob(1, value); repaint(); return; }
        if (index == kGlobalPitchShift)    { fPitchShift = value; updateGlobalKnob(2, value); repaint(); return; }
        if (index == kGlobalWetDry)        { fWetDry = value; updateGlobalKnob(3, value); repaint(); return; }
        if (index == kGlobalInputGain)     { fInputGain = value; updateGlobalKnob(4, value); repaint(); return; }
        if (index == kGlobalOutputGain)    { fOutputGain = value; updateGlobalKnob(5, value); repaint(); return; }
        if (index == kGlobalHarmonicMode)  { fHarmonicMode = value; repaint(); return; }
        if (index == kGlobalStereoCollapse){ fStereoCollapse = value; updateGlobalKnob(6, value); repaint(); return; }

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
        if (fBgImage.isValid())
        {
            Paint bgPaint = imagePattern(0, 0, w, h, 0.0f, fBgImage, 1.0f);
            fillPaint(bgPaint);
        }
        else
        {
            fillColor(Palette::bg);
        }
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

        // Menu overlay (drawn on top of everything)
        if (fMenuOpen)
            drawMenuOverlay(w, h);
        if (fPresetsOpen)
            drawPresetsOverlay(w, h);
    }

    // ── Top bar (title + RND ALL + HARMONIC + [◄] Preset [►] + MENU) ────
    void drawTopBar(float w, float h)
    {
        fontSize(15.0f);
        fontFaceId(fFontId);
        fillColor(Palette::text);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(12.0f, h * 0.5f, "OCTOFILTER", nullptr);

        const float btnH = 20.0f;
        const float btnY = h * 0.5f - btnH * 0.5f;

        // ── Randomise All button ──────────────────────────────────────────
        const float rndX = 120.0f;
        const float rndW = 75.0f;
        fRandomiseBtnX = rndX;
        fRandomiseBtnY = btnY;
        fRandomiseBtnW = rndW;
        fRandomiseBtnH = btnH;

        beginPath();
        roundedRect(rndX, btnY, rndW, btnH, 3.0f);
        fillColor(Palette::highlight);
        fill();
        strokeColor(Palette::text);
        strokeWidth(1.0f);
        stroke();
        fontSize(12.0f);
        fillColor(Palette::bg);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(rndX + rndW * 0.5f, btnY + btnH * 0.5f, "RND ALL", nullptr);

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

        // ── Point count: [-] N [+] ───────────────────────────────────────
        const float pcX = harmX + harmW + 12.0f;
        const float pcBtnSz = 18.0f;
        fPtCountMinusX = pcX;
        fPtCountBtnY   = btnY + 1.0f;
        fPtCountBtnSize = pcBtnSz;

        beginPath();
        roundedRect(pcX, fPtCountBtnY, pcBtnSz, pcBtnSz, 3.0f);
        fillColor(Palette::outline);
        fill();
        fontSize(13.0f);
        fillColor(Palette::text);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(pcX + pcBtnSz * 0.5f, fPtCountBtnY + pcBtnSz * 0.5f, "-", nullptr);

        char pcBuf[8];
        std::snprintf(pcBuf, sizeof(pcBuf), "%d", fPointCount);
        fontSize(13.0f);
        fillColor(Palette::highlight);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(pcX + pcBtnSz + 10.0f, fPtCountBtnY + pcBtnSz * 0.5f, pcBuf, nullptr);

        fPtCountPlusX = pcX + pcBtnSz + 20.0f;
        beginPath();
        roundedRect(fPtCountPlusX, fPtCountBtnY, pcBtnSz, pcBtnSz, 3.0f);
        fillColor(Palette::outline);
        fill();
        fontSize(13.0f);
        fillColor(Palette::text);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(fPtCountPlusX + pcBtnSz * 0.5f, fPtCountBtnY + pcBtnSz * 0.5f, "+", nullptr);

        // ── MENU button (far right) ──────────────────────────────────────
        const float rightEdge = w - 12.0f;
        const float menuW = 52.0f;
        const float menuX = rightEdge - menuW;
        fMenuBtnX = menuX;
        fMenuBtnY = btnY;
        fMenuBtnW = menuW;
        fMenuBtnH = btnH;

        beginPath();
        roundedRect(menuX, btnY, menuW, btnH, 3.0f);
        fillColor(fMenuOpen ? Palette::accent : Palette::outline);
        fill();
        fontSize(11.0f);
        fillColor(Palette::text);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(menuX + menuW * 0.5f, btnY + btnH * 0.5f, "MENU", nullptr);

        // ── Preset strip: [◄] Name [►] ──────────────────────────────────
        const float presetStripX = menuX - 190.0f;
        const float arrowW = 20.0f;
        const float nameW = 146.0f;
        const float stripW = arrowW + nameW + arrowW;

        fPresetStripX = presetStripX;
        fPresetStripY = btnY;
        fPresetStripW = stripW;
        fPresetStripH = btnH;
        fPresetArrowW = arrowW;
        fPresetNameW  = nameW;

        // Left arrow [◄]
        beginPath();
        roundedRect(presetStripX, btnY, arrowW, btnH, 3.0f);
        fillColor(Palette::outline);
        fill();
        fontSize(12.0f);
        fillColor(Palette::text);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(presetStripX + arrowW * 0.5f, btnY + btnH * 0.5f, "\xe2\x97\x84", nullptr);

        // Name (clickable to open browser)
        beginPath();
        rect(presetStripX + arrowW, btnY, nameW, btnH);
        fillColor(Color(0.1f, 0.1f, 0.1f, 1.0f));
        fill();
        fontSize(10.0f);
        fillColor(Palette::text);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        const char* displayName = (fCurrentPresetIdx >= 0 && fCurrentPresetIdx < fPresetCount)
            ? fPresetNames[fCurrentPresetIdx] : "(init)";
        text(presetStripX + arrowW + nameW * 0.5f, btnY + btnH * 0.5f, displayName, nullptr);

        // Right arrow [►]
        beginPath();
        roundedRect(presetStripX + arrowW + nameW, btnY, arrowW, btnH, 3.0f);
        fillColor(Palette::outline);
        fill();
        fontSize(12.0f);
        fillColor(Palette::text);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(presetStripX + arrowW + nameW + arrowW * 0.5f, btnY + btnH * 0.5f, "\xe2\x96\xba", nullptr);
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

        // Stereo width boundary — shows the horizontal extent
        // Width = fStereoCollapse (0=mono, 1=full)
        const float panWidth = fStereoCollapse;
        const float hBoxX = x + (1.0f - panWidth) * 0.5f * w;
        const float hBoxW = w * panWidth;

        if (fStereoCollapse < 0.99f && hBoxW > 2.0f)
        {
            beginPath();
            roundedRect(hBoxX, y, hBoxW, h, 3.0f);
            strokeColor(Color(Palette::highlight.red, Palette::highlight.green,
                              Palette::highlight.blue, 0.4f));
            strokeWidth(1.5f);
            stroke();
        }

        // Texture boundary — shows the vertical collapse area
        // Texture scales the vertical spread of points
        const float vertExtent = fTexture;
        const float vBoxY = y + (1.0f - vertExtent) * 0.5f * h;
        const float vBoxH = h * vertExtent;

        if (fTexture < 0.99f && vBoxH > 2.0f)
        {
            beginPath();
            roundedRect(x, vBoxY, w, vBoxH, 3.0f);
            strokeColor(Color(Palette::accent.red, Palette::accent.green,
                              Palette::accent.blue, 0.4f));
            strokeWidth(1.5f);
            stroke();
        }

        // Draw point nodes
        for (int i = 0; i < fPointCount; ++i)
        {
            // Display: pan collapses with StereoCollapse, cutoff offset shown directly
            float effPan = fPointPan[i] * fStereoCollapse;

            // Scale offset display by Texture: at texture=0 all points collapse to centre,
            // at texture=1 full vertical spread is shown
            float effOffset = fPointCutoffOffset[i] * fTexture;

            const float px = x + (effPan + 1.0f) * 0.5f * w;
            const float cutoffNorm = effOffset / 24.0f;
            const float scaledY = 0.5f + cutoffNorm * 0.5f;
            const float py = y + h - scaledY * h;

            // Size = global feedback × per-point feedback
            const float effectiveFB = fFeedback * fPointFeedback[i];
            const float baseSize = 10.0f;
            const float fbSize   = baseSize + effectiveFB * 14.0f;

            const int ftype = static_cast<int>(fPointFilterType[i] + 0.5f) % 4;
            Color nodeColor = Palette::filterColor(ftype);

            // Selection ring (all selected get ring, primary gets thicker)
            if (fPointSelected[i])
            {
                beginPath();
                circle(px, py, fbSize + 5.0f);
                strokeColor(Palette::highlight);
                strokeWidth(i == fSelectedPoint ? 3.0f : 1.5f);
                stroke();
            }

            // Node fill — dim when feedback is very low (visual hint that feedback activates the effect)
            const float nodeOpacity = (fFeedback < 0.05f) ? 0.35f : 1.0f;
            beginPath();
            circle(px, py, fbSize);
            fillColor(Color(nodeColor.red, nodeColor.green, nodeColor.blue, nodeOpacity));
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

            // Filter type label inside node
            fontSize(11.0f);
            fillColor(Palette::bg);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            const char* ftLabelsField[] = {"LP", "HP", "BP"};
            const int ftIdx = static_cast<int>(fPointFilterType[i] + 0.5f) % 3;
            text(px, py, ftLabelsField[ftIdx], nullptr);

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

        // Header + Per-point randomise button
        fontSize(15.0f);
        fontFaceId(fFontId);
        fillColor(Palette::highlight);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        char hdr[32];
        std::snprintf(hdr, sizeof(hdr), "POINT %d", fSelectedPoint + 1);
        text(x + 8.0f, y + 8.0f, hdr, nullptr);

        // Per-point RND button (top-right of panel)
        fPPRndBtnX = x + w - 45.0f;
        fPPRndBtnY = y + 6.0f;
        fPPRndBtnW = 38.0f;
        fPPRndBtnH = 16.0f;
        beginPath();
        roundedRect(fPPRndBtnX, fPPRndBtnY, fPPRndBtnW, fPPRndBtnH, 3.0f);
        fillColor(Palette::highlight);
        fill();
        fontSize(9.0f);
        fillColor(Palette::bg);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(fPPRndBtnX + fPPRndBtnW * 0.5f, fPPRndBtnY + fPPRndBtnH * 0.5f, "RND", nullptr);

        // Group labels
        fontSize(9.0f);
        fillColor(Palette::sage);
        textAlign(ALIGN_LEFT | ALIGN_TOP);

        const float row1Y = y + 35.0f;
        text(x + 8.0f, row1Y - 12.0f, "FILTER", nullptr);

        // Knob size
        const float knobR = 24.0f;

        // Row 1: Filter Type buttons + Q + Cutoff
        // Filter type as 4 clickable buttons: [LP] [HP] [BP] [NT]
        const float ftBtnW = (w - 20.0f) / 3.0f;
        const float ftBtnH = 16.0f;
        const float ftY    = row1Y;
        fFilterBtnX = x + 10.0f;
        fFilterBtnY = ftY;
        fFilterBtnW = ftBtnW;
        fFilterBtnH = ftBtnH;

        const char* ftLabels[] = {"LP", "HP", "BP"};
        const int currentType = static_cast<int>(fPointFilterType[fSelectedPoint] + 0.5f) % 3;

        for (int t = 0; t < 3; ++t)
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
        // Start just below buttons, then fixed spacing for each row
        const float qcY = ftY + ftBtnH + knobR + 12.0f;
        const float rowSpacing = knobR * 2.0f + 39.0f; // 18% more than original
        const float col1 = x + w * 0.3f;
        const float col2 = x + w * 0.7f;

        // Section labels (FILTER is already above the buttons)
        fontSize(9.0f);
        fillColor(Palette::sage);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(x + 8.0f, qcY + rowSpacing - knobR - 10.0f, "SPATIAL / FB", nullptr);
        text(x + 8.0f, qcY + rowSpacing * 2.0f - knobR - 10.0f, "MODULATION", nullptr);

        fPPKnobs[1].x = col1; fPPKnobs[1].y = qcY; fPPKnobs[1].radius = knobR;
        fPPKnobs[6].x = col2; fPPKnobs[6].y = qcY; fPPKnobs[6].radius = knobR;
        drawKnob(fPPKnobs[1]);
        drawKnob(fPPKnobs[6]);

        // Row 2: Pan + Feedback
        const float col1r2 = x + w * 0.3f;
        const float col2r2 = x + w * 0.7f;
        const float row2KnobY = qcY + rowSpacing;
        fPPKnobs[2].x = col1r2; fPPKnobs[2].y = row2KnobY; fPPKnobs[2].radius = knobR;
        fPPKnobs[4].x = col2r2; fPPKnobs[4].y = row2KnobY; fPPKnobs[4].radius = knobR;
        drawKnob(fPPKnobs[2]);
        drawKnob(fPPKnobs[4]);

        // Row 3: Level + Pitch
        const float row3KnobY = qcY + rowSpacing * 2.0f;
        fPPKnobs[3].x = col1r2; fPPKnobs[3].y = row3KnobY; fPPKnobs[3].radius = knobR;
        fPPKnobs[5].x = col2r2; fPPKnobs[5].y = row3KnobY; fPPKnobs[5].radius = knobR;
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

        const float knobR = 24.0f;
        const float cy = y + h * 0.5f + 2.0f;

        // Group positions: FILTER | EFFECT | OUTPUT
        // FILTER: Texture, Width
        // EFFECT: Feedback, Pitch
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

        // FILTER group: Texture, Width (2 knobs)
        const float fSpacing = groupW / 3.0f;
        fGlobalKnobs[0].x = x + fSpacing;            fGlobalKnobs[0].y = cy; fGlobalKnobs[0].radius = knobR;
        fGlobalKnobs[6].x = x + fSpacing * 2.0f;     fGlobalKnobs[6].y = cy; fGlobalKnobs[6].radius = knobR;

        // EFFECT group: Feedback, Pitch (2 knobs)
        const float eStart = x + groupW;
        const float eSpacing = groupW / 3.0f;
        fGlobalKnobs[1].x = eStart + eSpacing;       fGlobalKnobs[1].y = cy; fGlobalKnobs[1].radius = knobR;
        fGlobalKnobs[2].x = eStart + eSpacing * 2.0f; fGlobalKnobs[2].y = cy; fGlobalKnobs[2].radius = knobR;

        // OUTPUT group: Wet/Dry, In, Out (3 knobs)
        const float oStart = x + groupW * 2.0f;
        const float oSpacing = groupW / 4.0f;
        fGlobalKnobs[3].x = oStart + oSpacing;       fGlobalKnobs[3].y = cy; fGlobalKnobs[3].radius = knobR;
        fGlobalKnobs[4].x = oStart + oSpacing * 2.0f; fGlobalKnobs[4].y = cy; fGlobalKnobs[4].radius = knobR;
        fGlobalKnobs[5].x = oStart + oSpacing * 3.0f; fGlobalKnobs[5].y = cy; fGlobalKnobs[5].radius = knobR;

        for (int i = 0; i < fNumGlobalKnobs; ++i)
            drawKnob(fGlobalKnobs[i]);

        // Global RND button (left side of strip)
        fGlobalRndBtnX = x + 8.0f;
        fGlobalRndBtnY = y + 6.0f;
        fGlobalRndBtnW = 40.0f;
        fGlobalRndBtnH = 16.0f;
        beginPath();
        roundedRect(fGlobalRndBtnX, fGlobalRndBtnY, fGlobalRndBtnW, fGlobalRndBtnH, 3.0f);
        fillColor(Palette::sage);
        fill();
        fontSize(9.0f);
        fontFaceId(fFontId);
        fillColor(Palette::bg);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(fGlobalRndBtnX + fGlobalRndBtnW * 0.5f, fGlobalRndBtnY + fGlobalRndBtnH * 0.5f, "RND", nullptr);
    }

    // ── Menu overlay ──────────────────────────────────────────────────────
    void drawMenuOverlay(float w, float h)
    {
        // Semi-transparent background
        beginPath();
        rect(0, 0, w, h);
        fillColor(Color(0.0f, 0.0f, 0.0f, 0.7f));
        fill();

        // Panel centred — larger
        const float panelW = w * 0.8f;
        const float panelH = h * 0.85f;
        const float panelX = (w - panelW) * 0.5f;
        const float panelY = (h - panelH) * 0.5f;

        beginPath();
        roundedRect(panelX, panelY, panelW, panelH, 8.0f);
        fillColor(Color(0.12f, 0.12f, 0.12f, 1.0f));
        fill();
        strokeColor(Palette::outline);
        strokeWidth(1.0f);
        stroke();

        // Tab bar at top of panel (2 tabs: Settings, Info)
        const float tabH = 34.0f;
        const float tabW = panelW / 2.0f;
        const char* tabLabels[] = {"SETTINGS", "INFO"};

        for (int t = 0; t < 2; ++t)
        {
            const float tx = panelX + t * tabW;
            beginPath();
            rect(tx, panelY, tabW, tabH);
            fillColor(fMenuTab == t ? Palette::accent : Color(0.18f, 0.18f, 0.18f, 1.0f));
            fill();

            fontSize(15.4f);  // 11 * 1.4
            fontFaceId(fFontId);
            fillColor(fMenuTab == t ? Palette::bg : Palette::text);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            text(tx + tabW * 0.5f, panelY + tabH * 0.5f, tabLabels[t], nullptr);
        }

        // Content area
        const float contentX = panelX + 20.0f;
        const float contentY = panelY + tabH + 20.0f;
        const float contentW = panelW - 40.0f;
        const float contentH = panelH - tabH - 40.0f;

        if (fMenuTab == 0)
            drawMenuSettings(contentX, contentY, contentW, contentH);
        else
            drawMenuInfo(contentX, contentY, contentW, contentH);

        // Close hint
        fontSize(12.6f);  // 9 * 1.4
        fillColor(Color(1.0f, 1.0f, 1.0f, 0.4f));
        textAlign(ALIGN_CENTER | ALIGN_BOTTOM);
        text(w * 0.5f, panelY + panelH - 8.0f, "Click outside or press ESC to close", nullptr);
    }

    void drawMenuSettings(float x, float y, float w, float h)
    {
        (void)w; (void)h;
        fontSize(16.8f);  // 12 * 1.4
        fontFaceId(fFontId);
        fillColor(Palette::text);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(x, y, "Point Count", nullptr);

        // Point count: [-] N [+]
        const float pcY = y + 28.0f;
        const float pcBtnSz = 28.0f;

        // [-]
        fPtCountMinusX = x;
        fPtCountBtnY = pcY;
        fPtCountBtnSize = pcBtnSz;
        beginPath();
        roundedRect(x, pcY, pcBtnSz, pcBtnSz, 4.0f);
        fillColor(Palette::outline);
        fill();
        fontSize(19.6f);  // 14 * 1.4
        fillColor(Palette::text);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(x + pcBtnSz * 0.5f, pcY + pcBtnSz * 0.5f, "-", nullptr);

        // Count
        char pcBuf[8];
        std::snprintf(pcBuf, sizeof(pcBuf), "%d", fPointCount);
        fontSize(19.6f);
        fillColor(Palette::highlight);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(x + pcBtnSz + 20.0f, pcY + pcBtnSz * 0.5f, pcBuf, nullptr);

        // [+]
        fPtCountPlusX = x + pcBtnSz + 40.0f;
        beginPath();
        roundedRect(fPtCountPlusX, pcY, pcBtnSz, pcBtnSz, 4.0f);
        fillColor(Palette::outline);
        fill();
        fontSize(19.6f);
        fillColor(Palette::text);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(fPtCountPlusX + pcBtnSz * 0.5f, pcY + pcBtnSz * 0.5f, "+", nullptr);

        // Limiter info
        fontSize(16.8f);
        fillColor(Palette::text);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(x, pcY + 55.0f, "Output Limiter", nullptr);

        fontSize(14.0f);  // 10 * 1.4
        fillColor(Palette::sage);
        text(x, pcY + 80.0f, "Ceiling: 0 dBFS | Always active", nullptr);
        text(x, pcY + 100.0f, "Protects speakers and ears from feedback spikes.", nullptr);
    }

    void drawMenuInfo(float x, float y, float w, float h)
    {
        (void)w; (void)h;
        fontSize(19.6f);  // 14 * 1.4
        fontFaceId(fFontId);
        fillColor(Palette::accent);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(x, y, "OCTOFILTER", nullptr);

        fontSize(14.0f);  // 10 * 1.4
        fillColor(Palette::text);
        text(x, y + 28.0f, "Version 0.9.0", nullptr);
        text(x, y + 48.0f, "Multi-point stereo filter with pitch-shifted feedback", nullptr);

        fillColor(Palette::sage);
        text(x, y + 78.0f, "Created by Henry (@HJ959)", nullptr);
        text(x, y + 98.0f, "Built with Kiro CLI", nullptr);

        text(x, y + 130.0f, "Special Thanks:", nullptr);
        fillColor(Palette::text);
        text(x, y + 150.0f, "  @cosmojamsoun — testing & feedback", nullptr);
        text(x, y + 170.0f, "  @estero_connor — testing & feedback", nullptr);
        text(x, y + 190.0f, "  @nicholasfaris — testing & feedback", nullptr);

        fillColor(Palette::sage);
        text(x, y + 225.0f, "Colour palette: Blodyn Tatws", nullptr);
        text(x, y + 245.0f, "(inspired by a potato flower from the garden)", nullptr);
    }

    // ── Presets overlay (dropdown browser) ───────────────────────────────
    void drawPresetsOverlay(float w, float h)
    {
        // Semi-transparent background
        beginPath();
        rect(0, 0, w, h);
        fillColor(Color(0.0f, 0.0f, 0.0f, 0.5f));
        fill();

        // Dropdown panel below the top bar
        const float panelX = 20.0f;
        const float panelY = 36.0f;
        const float panelW = w - 40.0f;
        const float panelH = h - 56.0f;

        beginPath();
        roundedRect(panelX, panelY, panelW, panelH, 6.0f);
        fillColor(Color(0.1f, 0.1f, 0.1f, 0.97f));
        fill();
        strokeColor(Palette::outline);
        strokeWidth(1.0f);
        stroke();

        // Content area with scroll
        const float contentX = panelX + 10.0f;
        float cy = panelY + 10.0f;
        const float rowH = 22.0f;
        const float folderH = 26.0f;
        const float maxY = panelY + panelH - 40.0f; // leave room for SAVE button

        fPresetSaveBtnX = -1; // reset

        if (fPresetCount == 0)
        {
            fontSize(14.0f);
            fontFaceId(fFontId);
            fillColor(Palette::sage);
            textAlign(ALIGN_LEFT | ALIGN_TOP);
            text(contentX, cy, "No presets found. Click SAVE to create one.", nullptr);
            cy += 25.0f;
            fontSize(11.0f);
            fillColor(Color(1.0f, 1.0f, 1.0f, 0.4f));
            #ifdef _WIN32
            text(contentX, cy, "Presets folder: %APPDATA%\\Octofilter\\Presets\\", nullptr);
            #else
            text(contentX, cy, "Presets folder: ~/Library/Application Support/Octofilter/Presets/", nullptr);
            #endif
        }
        else
        {
            // Draw folder-grouped list
            for (int fi = 0; fi < fNumFolders && cy < maxY; ++fi)
            {
                // Folder header
                fontSize(11.0f);
                fontFaceId(fFontId);
                fillColor(Palette::sage);
                textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
                text(contentX, cy + folderH * 0.5f, fFolderNames[fi], nullptr);
                cy += folderH;

                // Presets in this folder
                for (int pi = 0; pi < fFolderCount[fi] && cy < maxY; ++pi)
                {
                    const int idx = fFolderStart[fi] + pi;
                    const bool isCurrent = (idx == fCurrentPresetIdx);
                    const bool isHovered = (idx == fPresetHoverIdx);
                    const float rowW = panelW - 20.0f;

                    beginPath();
                    roundedRect(contentX, cy, rowW, rowH - 2.0f, 2.0f);
                    if (isCurrent)
                        fillColor(Color(Palette::accent.red, Palette::accent.green, Palette::accent.blue, 0.3f));
                    else if (isHovered)
                        fillColor(Color(0.22f, 0.22f, 0.22f, 1.0f));
                    else
                        fillColor(Color(0.15f, 0.15f, 0.15f, 1.0f));
                    fill();

                    fontSize(12.0f);
                    fillColor(isCurrent ? Palette::accent : Palette::text);
                    textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
                    text(contentX + 8.0f, cy + (rowH - 2.0f) * 0.5f, fPresetNames[idx], nullptr);

                    if (isCurrent)
                    {
                        // Checkmark
                        fillColor(Palette::accent);
                        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
                        text(contentX + rowW - 28.0f, cy + (rowH - 2.0f) * 0.5f, "\xe2\x9c\x93", nullptr);
                    }

                    // X delete button (always visible on hover, dimmed otherwise)
                    if (isHovered || isCurrent)
                    {
                        fontSize(11.0f);
                        fillColor(isHovered ? Color(1.0f, 0.4f, 0.4f, 1.0f) : Color(0.5f, 0.3f, 0.3f, 1.0f));
                        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
                        text(contentX + rowW - 12.0f, cy + (rowH - 2.0f) * 0.5f, "X", nullptr);
                    }

                    cy += rowH;
                }

                cy += 4.0f; // gap between folders
            }
        }

        // Bottom bar: SAVE button or naming input
        const float bottomY = panelY + panelH - 34.0f;

        if (fNamingPreset)
        {
            // Show text input field
            const float inputX = panelX + 15.0f;
            const float inputW = panelW - 30.0f;
            const float inputH = 26.0f;

            beginPath();
            roundedRect(inputX, bottomY, inputW, inputH, 4.0f);
            fillColor(Color(0.05f, 0.05f, 0.05f, 1.0f));
            fill();
            strokeColor(Palette::accent);
            strokeWidth(1.5f);
            stroke();

            // Label
            fontSize(10.0f);
            fillColor(Palette::sage);
            textAlign(ALIGN_LEFT | ALIGN_BOTTOM);
            text(inputX + 4.0f, bottomY - 2.0f, "Name your preset (Enter to save, Esc to cancel):", nullptr);

            // Input text with cursor
            fontSize(13.0f);
            fillColor(Palette::text);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            char displayBuf[64];
            std::snprintf(displayBuf, sizeof(displayBuf), "%s|", fPresetNameInput);
            text(inputX + 8.0f, bottomY + inputH * 0.5f, displayBuf, nullptr);

            fPresetSaveBtnX = -1; // disable save button hit area
        }
        else
        {
            // Show SAVE button
            const float saveBtnW = 80.0f;
            const float saveBtnH = 26.0f;
            const float saveBtnX = panelX + panelW - saveBtnW - 15.0f;
            fPresetSaveBtnX = saveBtnX;
            fPresetSaveBtnY = bottomY;
            fPresetSaveBtnW = saveBtnW;
            fPresetSaveBtnH = saveBtnH;

            beginPath();
            roundedRect(saveBtnX, bottomY, saveBtnW, saveBtnH, 4.0f);
            fillColor(Palette::highlight);
            fill();
            fontSize(12.0f);
            fillColor(Palette::bg);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            text(saveBtnX + saveBtnW * 0.5f, bottomY + saveBtnH * 0.5f, "SAVE", nullptr);
        }
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
        fontSize(13.0f);
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
            // ── Menu overlay interactions (when open, intercept all clicks) ──
            if (fMenuOpen)
            {
                const float w = static_cast<float>(getWidth());
                const float h = static_cast<float>(getHeight());
                const float panelW = w * 0.8f;
                const float panelH = h * 0.85f;
                const float panelX = (w - panelW) * 0.5f;
                const float panelY = (h - panelH) * 0.5f;

                // Click inside panel?
                if (mx >= panelX && mx <= panelX + panelW &&
                    my >= panelY && my <= panelY + panelH)
                {
                    // Tab clicks
                    const float tabH = 34.0f;
                    if (my <= panelY + tabH)
                    {
                        const float tabW = panelW / 2.0f;
                        int clickedTab = static_cast<int>((mx - panelX) / tabW);
                        if (clickedTab >= 0 && clickedTab < 2)
                        {
                            fMenuTab = clickedTab;
                            repaint();
                        }
                    }
                    // Point count [-][+] buttons (in Settings tab)
                    else if (fMenuTab == 0)
                    {
                        if (mx >= fPtCountMinusX && mx <= fPtCountMinusX + fPtCountBtnSize &&
                            my >= fPtCountBtnY && my <= fPtCountBtnY + fPtCountBtnSize)
                        {
                            if (fPointCount > 1) {
                                fPointCount--;
                                setParameterValue(kGlobalPointCount, static_cast<float>(fPointCount));
                                if (fSelectedPoint >= fPointCount) fSelectedPoint = -1;
                                repaint();
                            }
                        }
                        else if (mx >= fPtCountPlusX && mx <= fPtCountPlusX + fPtCountBtnSize &&
                                 my >= fPtCountBtnY && my <= fPtCountBtnY + fPtCountBtnSize)
                        {
                            if (fPointCount < 8) {
                                fPointCount++;
                                setParameterValue(kGlobalPointCount, static_cast<float>(fPointCount));
                                repaint();
                            }
                        }
                    }
                    return true;
                }
                else
                {
                    // Click outside panel — close menu
                    fMenuOpen = false;
                    repaint();
                    return true;
                }
            }

            // ── Presets overlay interactions ──────────────────────────────────
            if (fPresetsOpen)
            {
                const float w = static_cast<float>(getWidth());
                const float h = static_cast<float>(getHeight());
                const float panelX = 20.0f;
                const float panelY = 36.0f;
                const float panelW = w - 40.0f;
                const float panelH = h - 56.0f;

                if (mx >= panelX && mx <= panelX + panelW &&
                    my >= panelY && my <= panelY + panelH)
                {
                    // Save button — enter naming mode
                    if (fPresetSaveBtnX >= 0 && !fNamingPreset &&
                        mx >= fPresetSaveBtnX && mx <= fPresetSaveBtnX + fPresetSaveBtnW &&
                        my >= fPresetSaveBtnY && my <= fPresetSaveBtnY + fPresetSaveBtnH)
                    {
                        fNamingPreset = true;
                        fPresetNameLen = 0;
                        fPresetNameInput[0] = '\0';
                        repaint();
                        return true;
                    }

                    // Preset list clicks — iterate through folders to find which one was clicked
                    const float contentX = panelX + 10.0f;
                    const float rowW = panelW - 20.0f;
                    float cy = panelY + 10.0f;
                    const float rowH = 22.0f;
                    const float folderH = 26.0f;

                    for (int fi = 0; fi < fNumFolders; ++fi)
                    {
                        cy += folderH; // skip folder header
                        for (int pi = 0; pi < fFolderCount[fi]; ++pi)
                        {
                            if (my >= cy && my < cy + rowH)
                            {
                                const int idx = fFolderStart[fi] + pi;

                                // X button is the rightmost 20px of the row
                                if (mx >= contentX + rowW - 20.0f)
                                {
                                    deletePreset(idx);
                                    repaint();
                                    return true;
                                }

                                // Otherwise load the preset
                                loadFactoryPreset(idx);
                                fPresetsOpen = false;
                                repaint();
                                return true;
                            }
                            cy += rowH;
                        }
                        cy += 4.0f; // gap
                    }
                    return true;
                }
                else
                {
                    fPresetsOpen = false;
                    repaint();
                    return true;
                }
            }

            // ── Menu button ──────────────────────────────────────────────────
            if (mx >= fMenuBtnX && mx <= fMenuBtnX + fMenuBtnW &&
                my >= fMenuBtnY && my <= fMenuBtnY + fMenuBtnH)
            {
                fMenuOpen = !fMenuOpen;
                fPresetsOpen = false;
                repaint();
                return true;
            }

            // ── Preset strip: [◄] Name [►] ──────────────────────────────────
            if (my >= fPresetStripY && my <= fPresetStripY + fPresetStripH &&
                mx >= fPresetStripX && mx <= fPresetStripX + fPresetStripW)
            {
                const float leftArrowEnd = fPresetStripX + fPresetArrowW;
                const float rightArrowStart = fPresetStripX + fPresetArrowW + fPresetNameW;

                if (mx < leftArrowEnd)
                {
                    // Left arrow — previous preset
                    if (fPresetCount == 0) scanPresets();
                    if (fPresetCount > 0)
                    {
                        if (fCurrentPresetIdx <= 0)
                            fCurrentPresetIdx = fPresetCount - 1;
                        else
                            fCurrentPresetIdx--;
                        loadFactoryPreset(fCurrentPresetIdx);
                    }
                    repaint();
                    return true;
                }
                else if (mx >= rightArrowStart)
                {
                    // Right arrow — next preset
                    if (fPresetCount == 0) scanPresets();
                    if (fPresetCount > 0)
                    {
                        if (fCurrentPresetIdx >= fPresetCount - 1)
                            fCurrentPresetIdx = 0;
                        else
                            fCurrentPresetIdx++;
                        loadFactoryPreset(fCurrentPresetIdx);
                    }
                    repaint();
                    return true;
                }
                else
                {
                    // Click on name — open/close browser
                    fPresetsOpen = !fPresetsOpen;
                    fMenuOpen = false;
                    if (fPresetsOpen) scanPresets();
                    repaint();
                    return true;
                }
            }

            // Randomise button — do it from UI side so visuals update immediately
            if (mx >= fRandomiseBtnX && mx <= fRandomiseBtnX + fRandomiseBtnW &&
                my >= fRandomiseBtnY && my <= fRandomiseBtnY + fRandomiseBtnH)
            {
                doUIRandomise();
                clampPointsToTexture();
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
                mx >= fFilterBtnX && mx <= fFilterBtnX + fFilterBtnW * 3.0f &&
                my >= fFilterBtnY && my <= fFilterBtnY + fFilterBtnH)
            {
                const int btn = static_cast<int>((mx - fFilterBtnX) / fFilterBtnW);
                if (btn >= 0 && btn < 3)
                {
                    fPointFilterType[fSelectedPoint] = static_cast<float>(btn);
                    setParameterValue(ppIdx(fSelectedPoint, kPPFilterType), static_cast<float>(btn));
                    syncPerPointKnobs();
                    repaint();
                    return true;
                }
            }

            // Per-point RND button
            if (fSelectedPoint >= 0 &&
                mx >= fPPRndBtnX && mx <= fPPRndBtnX + fPPRndBtnW &&
                my >= fPPRndBtnY && my <= fPPRndBtnY + fPPRndBtnH)
            {
                doRandomisePointOnly();
                clampPointsToTexture();
                repaint();
                return true;
            }

            // Global RND button
            if (mx >= fGlobalRndBtnX && mx <= fGlobalRndBtnX + fGlobalRndBtnW &&
                my >= fGlobalRndBtnY && my <= fGlobalRndBtnY + fGlobalRndBtnH)
            {
                doRandomiseGlobalsOnly();
                clampPointsToTexture();
                repaint();
                return true;
            }

            // Point count [-] [+] buttons (top bar)
            if (mx >= fPtCountMinusX && mx <= fPtCountMinusX + fPtCountBtnSize &&
                my >= fPtCountBtnY && my <= fPtCountBtnY + fPtCountBtnSize)
            {
                if (fPointCount > 1) {
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
                if (fPointCount < 8) {
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
                    if (ev.mod & kModifierControl)
                    {
                        // Ctrl+click: toggle this point in multi-select
                        fPointSelected[i] = !fPointSelected[i];
                        if (fPointSelected[i])
                            fSelectedPoint = i; // make it primary
                    }
                    else
                    {
                        // Normal click: select only this point
                        for (int j = 0; j < 8; ++j) fPointSelected[j] = false;
                        fPointSelected[i] = true;
                        fSelectedPoint = i;
                    }
                    fDraggingPoint = true;
                    // Store starting positions for relative drag
                    fDragStartMX = mx;
                    fDragStartMY = my;
                    for (int j = 0; j < 8; ++j)
                    {
                        fDragStartPan[j] = fPointPan[j];
                        fDragStartOffset[j] = fPointCutoffOffset[j];
                    }
                    syncPerPointKnobs();
                    repaint();
                    return true;
                }
            }

            // Clicked empty space — deselect all
            for (int j = 0; j < 8; ++j) fPointSelected[j] = false;
            fSelectedPoint = -1;
            repaint();
            return true;
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
            return true;
        }
        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        const float my = ev.pos.getY();
        const float mx = ev.pos.getX();

        // Preset browser hover tracking
        if (fPresetsOpen)
        {
            const float w = static_cast<float>(getWidth());
            const float panelX = 20.0f;
            const float panelY = 36.0f;
            const float panelW = w - 40.0f;
            const float contentX = panelX + 10.0f;
            const float rowH = 22.0f;
            const float folderH = 26.0f;

            int newHover = -1;
            float cy = panelY + 10.0f;
            for (int fi = 0; fi < fNumFolders; ++fi)
            {
                cy += folderH;
                for (int pi = 0; pi < fFolderCount[fi]; ++pi)
                {
                    if (mx >= contentX && mx <= contentX + panelW - 20.0f &&
                        my >= cy && my < cy + rowH)
                    {
                        newHover = fFolderStart[fi] + pi;
                    }
                    cy += rowH;
                }
                cy += 4.0f;
            }

            if (newHover != fPresetHoverIdx)
            {
                fPresetHoverIdx = newHover;
                repaint();
            }
        }

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

        // Point dragging — moves ALL selected points relative to start
        if (fDraggingPoint && fSelectedPoint >= 0)
        {
            // Calculate delta in normalised space
            const float deltaPan = (mx - fDragStartMX) / fFieldW * 2.0f;
            const float deltaOffset = -(my - fDragStartMY) / fFieldH * 48.0f;

            // Send FULL-RANGE values — DSP applies texture scaling internally.
            // Clamp only to parameter bounds (not texture bounds).
            for (int i = 0; i < fPointCount; ++i)
            {
                if (!fPointSelected[i]) continue;

                float newPan = fDragStartPan[i] + deltaPan;
                if (newPan < -1.0f) newPan = -1.0f;
                if (newPan >  1.0f) newPan =  1.0f;
                fPointPan[i] = newPan;
                setParameterValue(ppIdx(i, kPPPan), newPan);

                float newOffset = fDragStartOffset[i] + deltaOffset;
                if (newOffset < -24.0f) newOffset = -24.0f;
                if (newOffset >  24.0f) newOffset =  24.0f;
                fPointCutoffOffset[i] = newOffset;
                if (newOffset > 0.0f)       fPointCutoffZone[i] =  1;
                else if (newOffset < 0.0f)  fPointCutoffZone[i] = -1;
                setParameterValue(ppIdx(i, kPPCutoffOffset), newOffset);
            }

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
                if (fb > 1.1f) fb = 1.1f;
                fPointFeedback[i] = fb;
                setParameterValue(ppIdx(i, kPPFeedback), fb);
                if (i == fSelectedPoint) syncPerPointKnobs();
                repaint();
                return true;
            }
        }
        return false;
    }

    bool onKeyboard(const KeyboardEvent& ev) override
    {
        if (ev.press && ev.key == 27) // ESC
        {
            if (fNamingPreset)
            {
                fNamingPreset = false;
                repaint();
                return true;
            }
            if (fMenuOpen || fPresetsOpen)
            {
                fMenuOpen = false;
                fPresetsOpen = false;
                repaint();
                return true;
            }
        }

        // Preset naming: Enter confirms, Backspace deletes, printable chars appended
        if (fNamingPreset && ev.press)
        {
            if (ev.key == 13 || ev.key == 10) // Enter
            {
                if (fPresetNameLen > 0)
                {
                    savePresetWithName(fPresetNameInput);
                    fNamingPreset = false;
                    repaint();
                }
                return true;
            }
            else if (ev.key == 8 || ev.key == 127) // Backspace / Delete
            {
                if (fPresetNameLen > 0)
                {
                    fPresetNameLen--;
                    fPresetNameInput[fPresetNameLen] = '\0';
                    repaint();
                }
                return true;
            }
            else if (ev.key >= 32 && ev.key < 127 && fPresetNameLen < 50)
            {
                // Printable ASCII character
                fPresetNameInput[fPresetNameLen++] = static_cast<char>(ev.key);
                fPresetNameInput[fPresetNameLen] = '\0';
                repaint();
                return true;
            }
            return true; // consume all keys when naming
        }

        return false;
    }

    bool onCharacterInput(const CharacterInputEvent& ev) override
    {
        // Handle character input for preset naming (covers non-ASCII via DPF)
        if (fNamingPreset)
        {
            // Already handled in onKeyboard for ASCII
            (void)ev;
            return true;
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
        add(kGlobalFeedback,   0.0f,  1.0f,  0.3f,   "Feedback");
        add(kGlobalPitchShift, -24.f, 24.f,  0.0f,   "Pitch");
        add(kGlobalWetDry,     0.0f,  1.0f,  1.0f,   "Wet/Dry");
        add(kGlobalInputGain,  -24.f, 24.f,  0.0f,   "In");
        add(kGlobalOutputGain, -24.f, 24.f,  0.0f,   "Out");
        add(kGlobalStereoCollapse, 0.0f, 1.0f, 1.0f, "Width");
    }

    void setupPerPointKnobs()
    {
        auto set = [&](int i, float mn, float mx, float def, const char* lbl, uint32_t paramIdx) {
            fPPKnobs[i] = Knob{}; 
            fPPKnobs[i].min = mn; fPPKnobs[i].max = mx; fPPKnobs[i].value = def;
            fPPKnobs[i].label = lbl; fPPKnobs[i].paramIndex = paramIdx;
        };
        set(0, 0.0f, 3.0f, 0.0f,   "Type",   0);
        set(1, 2.5f, 20.0f, 5.0f,  "Q",      0);
        set(2, -1.0f, 1.0f, 0.0f,  "Pan",    0);
        set(3, 0.0f, 2.0f, 1.0f,   "Level",  0);
        set(4, 0.0f, 1.1f, 0.0f,   "FB",     0);
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

    void loadFactoryPreset(int idx)
    {
        if (idx < 0 || idx >= fPresetCount) return;

        char dir[512];
        getPresetDir(dir, sizeof(dir));
        char path[512];

        if (fPresetFolders[idx][0] != '\0')
        {
            #ifdef _WIN32
            std::snprintf(path, sizeof(path), "%s\\%s\\%s.octopreset", dir, fPresetFolders[idx], fPresetNames[idx]);
            #else
            std::snprintf(path, sizeof(path), "%s/%s/%s.octopreset", dir, fPresetFolders[idx], fPresetNames[idx]);
            #endif
        }
        else
        {
            #ifdef _WIN32
            std::snprintf(path, sizeof(path), "%s\\%s.octopreset", dir, fPresetNames[idx]);
            #else
            std::snprintf(path, sizeof(path), "%s/%s.octopreset", dir, fPresetNames[idx]);
            #endif
        }

        FILE* f = std::fopen(path, "r");
        if (!f) return;

        char buf[2048] = {};
        size_t len = std::fread(buf, 1, sizeof(buf) - 1, f);
        std::fclose(f);
        buf[len] = '\0';

        setState("octofilter_state", buf);
        fCurrentPresetIdx = idx;
    }

    void savePreset()
    {
        // Fallback: auto-generate name
        static int saveCounter = 0;
        char name[64];
        std::snprintf(name, sizeof(name), "Preset %03d", ++saveCounter);
        savePresetWithName(name);
    }

    void savePresetWithName(const char* name)
    {
        char dir[512];
        getPresetDir(dir, sizeof(dir));

        // Save into "User" subfolder
        char userDir[512];
        #ifdef _WIN32
        {
            const char* appdata = std::getenv("APPDATA");
            char base[512];
            std::snprintf(base, sizeof(base), "%s\\Octofilter", appdata ? appdata : "C:");
            CreateDirectoryA(base, nullptr);
            CreateDirectoryA(dir, nullptr);
            std::snprintf(userDir, sizeof(userDir), "%s\\User", dir);
            CreateDirectoryA(userDir, nullptr);
        }
        #else
        {
            const char* home = std::getenv("HOME");
            char base[512];
            std::snprintf(base, sizeof(base), "%s/Library/Application Support/Octofilter", home ? home : "/tmp");
            mkdir(base, 0755);
            mkdir(dir, 0755);
            std::snprintf(userDir, sizeof(userDir), "%s/User", dir);
            mkdir(userDir, 0755);
        }
        #endif

        char path[512];
        #ifdef _WIN32
        std::snprintf(path, sizeof(path), "%s\\%s.octopreset", userDir, name);
        #else
        std::snprintf(path, sizeof(path), "%s/%s.octopreset", userDir, name);
        #endif

        // Serialize current state
        char buf[2048] = {};
        int pos = 0;
        pos += std::snprintf(buf + pos, sizeof(buf) - pos,
            "texture=%.4f\nfeedback=%.4f\npitch=%.4f\nwetdry=%.4f\n"
            "width=%.4f\nharmonic=%.0f\npoints=%d\n",
            fTexture, fFeedback, fPitchShift, fWetDry,
            fStereoCollapse, fHarmonicMode, fPointCount);

        for (int i = 0; i < 8; ++i)
        {
            pos += std::snprintf(buf + pos, sizeof(buf) - pos,
                "co%d=%.4f\nft%d=%.0f\nps%d=%.4f\nfb%d=%.4f\nq%d=%.4f\npn%d=%.4f\nlv%d=%.4f\n",
                i, fPointCutoffOffset[i],
                i, fPointFilterType[i],
                i, fPointPitchShift[i],
                i, fPointFeedback[i],
                i, fPointQ[i],
                i, fPointPan[i],
                i, fPointLevel[i]);
        }

        FILE* f = std::fopen(path, "w");
        if (f)
        {
            std::fwrite(buf, 1, std::strlen(buf), f);
            std::fclose(f);
        }

        scanPresets();

        // Set current to the newly saved preset
        for (int i = 0; i < fPresetCount; ++i)
        {
            if (std::strcmp(fPresetNames[i], name) == 0)
            {
                fCurrentPresetIdx = i;
                break;
            }
        }
    }

    void deletePreset(int idx)
    {
        if (idx < 0 || idx >= fPresetCount) return;

        char dir[512];
        getPresetDir(dir, sizeof(dir));
        char path[512];

        if (fPresetFolders[idx][0] != '\0')
        {
            #ifdef _WIN32
            std::snprintf(path, sizeof(path), "%s\\%s\\%s.octopreset", dir, fPresetFolders[idx], fPresetNames[idx]);
            #else
            std::snprintf(path, sizeof(path), "%s/%s/%s.octopreset", dir, fPresetFolders[idx], fPresetNames[idx]);
            #endif
        }
        else
        {
            #ifdef _WIN32
            std::snprintf(path, sizeof(path), "%s\\%s.octopreset", dir, fPresetNames[idx]);
            #else
            std::snprintf(path, sizeof(path), "%s/%s.octopreset", dir, fPresetNames[idx]);
            #endif
        }

        std::remove(path);

        // If we deleted the current preset, reset
        if (fCurrentPresetIdx == idx)
            fCurrentPresetIdx = -1;
        else if (fCurrentPresetIdx > idx)
            fCurrentPresetIdx--;

        scanPresets();
    }

    void scanPresets()
    {
        fPresetCount = 0;
        fNumFolders = 0;
        char dir[512];
        getPresetDir(dir, sizeof(dir));

        #ifdef _WIN32
        // Scan for subfolders first
        char folderPattern[512];
        std::snprintf(folderPattern, sizeof(folderPattern), "%s\\*", dir);
        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA(folderPattern, &fd);
        if (hFind != INVALID_HANDLE_VALUE)
        {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                if (fd.cFileName[0] == '.') continue;
                if (fNumFolders >= kMaxFolders) break;

                // Store folder name
                std::strncpy(fFolderNames[fNumFolders], fd.cFileName, 63);
                fFolderNames[fNumFolders][63] = '\0';
                fFolderStart[fNumFolders] = fPresetCount;

                // Scan presets in this folder
                char presetPattern[512];
                std::snprintf(presetPattern, sizeof(presetPattern), "%s\\%s\\*.octopreset", dir, fd.cFileName);
                WIN32_FIND_DATAA pfd;
                HANDLE hPresets = FindFirstFileA(presetPattern, &pfd);
                if (hPresets != INVALID_HANDLE_VALUE)
                {
                    do {
                        if (fPresetCount >= kMaxPresets) break;
                        char* dot = std::strrchr(pfd.cFileName, '.');
                        if (dot) *dot = '\0';
                        std::strncpy(fPresetNames[fPresetCount], pfd.cFileName, 63);
                        fPresetNames[fPresetCount][63] = '\0';
                        std::strncpy(fPresetFolders[fPresetCount], fd.cFileName, 63);
                        fPresetFolders[fPresetCount][63] = '\0';
                        fPresetCount++;
                    } while (FindNextFileA(hPresets, &pfd));
                    FindClose(hPresets);
                }

                fFolderCount[fNumFolders] = fPresetCount - fFolderStart[fNumFolders];
                if (fFolderCount[fNumFolders] > 0)
                    fNumFolders++;

            } while (FindNextFileA(hFind, &fd));
            FindClose(hFind);
        }

        // Also scan root-level presets (no folder)
        char rootPattern[512];
        std::snprintf(rootPattern, sizeof(rootPattern), "%s\\*.octopreset", dir);
        HANDLE hRoot = FindFirstFileA(rootPattern, &fd);
        if (hRoot != INVALID_HANDLE_VALUE)
        {
            if (fPresetCount > 0 || true) // always add root as a "folder" if it has presets
            {
                int rootStart = fPresetCount;
                do {
                    if (fPresetCount >= kMaxPresets) break;
                    char* dot = std::strrchr(fd.cFileName, '.');
                    if (dot) *dot = '\0';
                    std::strncpy(fPresetNames[fPresetCount], fd.cFileName, 63);
                    fPresetNames[fPresetCount][63] = '\0';
                    fPresetFolders[fPresetCount][0] = '\0'; // no folder
                    fPresetCount++;
                } while (FindNextFileA(hRoot, &fd));
                FindClose(hRoot);

                int rootCount = fPresetCount - rootStart;
                if (rootCount > 0 && fNumFolders < kMaxFolders)
                {
                    std::strncpy(fFolderNames[fNumFolders], "Unsorted", 63);
                    fFolderStart[fNumFolders] = rootStart;
                    fFolderCount[fNumFolders] = rootCount;
                    fNumFolders++;
                }
            }
        }
        #else
        DIR* d = opendir(dir);
        if (!d) return;
        struct dirent* ent;

        // Scan subfolders
        while ((ent = readdir(d)) != nullptr)
        {
            if (ent->d_name[0] == '.') continue;
            if (ent->d_type != DT_DIR) continue;
            if (fNumFolders >= kMaxFolders) break;

            std::strncpy(fFolderNames[fNumFolders], ent->d_name, 63);
            fFolderNames[fNumFolders][63] = '\0';
            fFolderStart[fNumFolders] = fPresetCount;

            char subdir[512];
            std::snprintf(subdir, sizeof(subdir), "%s/%s", dir, ent->d_name);
            DIR* sd = opendir(subdir);
            if (sd)
            {
                struct dirent* sent;
                while ((sent = readdir(sd)) != nullptr)
                {
                    if (fPresetCount >= kMaxPresets) break;
                    const char* ext = std::strrchr(sent->d_name, '.');
                    if (!ext || std::strcmp(ext, ".octopreset") != 0) continue;

                    size_t nameLen = static_cast<size_t>(ext - sent->d_name);
                    if (nameLen > 63) nameLen = 63;
                    std::memcpy(fPresetNames[fPresetCount], sent->d_name, nameLen);
                    fPresetNames[fPresetCount][nameLen] = '\0';
                    std::strncpy(fPresetFolders[fPresetCount], ent->d_name, 63);
                    fPresetFolders[fPresetCount][63] = '\0';
                    fPresetCount++;
                }
                closedir(sd);
            }

            fFolderCount[fNumFolders] = fPresetCount - fFolderStart[fNumFolders];
            if (fFolderCount[fNumFolders] > 0)
                fNumFolders++;
        }
        closedir(d);

        // Scan root-level presets
        d = opendir(dir);
        if (d)
        {
            int rootStart = fPresetCount;
            while ((ent = readdir(d)) != nullptr)
            {
                if (fPresetCount >= kMaxPresets) break;
                if (ent->d_type == DT_DIR) continue;
                const char* ext = std::strrchr(ent->d_name, '.');
                if (!ext || std::strcmp(ext, ".octopreset") != 0) continue;

                size_t nameLen = static_cast<size_t>(ext - ent->d_name);
                if (nameLen > 63) nameLen = 63;
                std::memcpy(fPresetNames[fPresetCount], ent->d_name, nameLen);
                fPresetNames[fPresetCount][nameLen] = '\0';
                fPresetFolders[fPresetCount][0] = '\0';
                fPresetCount++;
            }
            closedir(d);

            int rootCount = fPresetCount - rootStart;
            if (rootCount > 0 && fNumFolders < kMaxFolders)
            {
                std::strncpy(fFolderNames[fNumFolders], "Unsorted", 63);
                fFolderStart[fNumFolders] = rootStart;
                fFolderCount[fNumFolders] = rootCount;
                fNumFolders++;
            }
        }
        #endif
    }

    void getPresetDir(char* out, size_t len)
    {
        #ifdef _WIN32
        const char* appdata = std::getenv("APPDATA");
        if (appdata)
            std::snprintf(out, len, "%s\\Octofilter\\Presets", appdata);
        else
            std::snprintf(out, len, "C:\\Octofilter\\Presets");
        #else
        const char* home = std::getenv("HOME");
        if (home)
            std::snprintf(out, len, "%s/Library/Application Support/Octofilter/Presets", home);
        else
            std::snprintf(out, len, "/tmp/Octofilter/Presets");
        #endif
    }

    void getPresetPath(const char* name, char* out, size_t len)
    {
        char dir[512];
        getPresetDir(dir, sizeof(dir));
        // If preset has a folder, include it in the path
        if (fCurrentPresetIdx >= 0 && fCurrentPresetIdx < fPresetCount &&
            fPresetFolders[fCurrentPresetIdx][0] != '\0')
        {
            #ifdef _WIN32
            std::snprintf(out, len, "%s\\%s\\%s.octopreset", dir, fPresetFolders[fCurrentPresetIdx], name);
            #else
            std::snprintf(out, len, "%s/%s/%s.octopreset", dir, fPresetFolders[fCurrentPresetIdx], name);
            #endif
        }
        else
        {
            #ifdef _WIN32
            std::snprintf(out, len, "%s\\%s.octopreset", dir, name);
            #else
            std::snprintf(out, len, "%s/%s.octopreset", dir, name);
            #endif
        }
    }

    void getPresetPathInFolder(const char* folder, const char* name, char* out, size_t len)
    {
        char dir[512];
        getPresetDir(dir, sizeof(dir));
        #ifdef _WIN32
        std::snprintf(out, len, "%s\\%s\\%s.octopreset", dir, folder, name);
        #else
        std::snprintf(out, len, "%s/%s/%s.octopreset", dir, folder, name);
        #endif
    }

    void clampPointsToTexture()
    {
        // Display-only clamp: the DSP applies texture clamping internally.
        // This just syncs the per-point knob display if a point is selected.
        if (fSelectedPoint >= 0) syncPerPointKnobs();
    }

    void doUIRandomise()
    {
        // Reseed with time for better entropy on repeated clicks
        fRngState = fRngState * 1103515245u + 12345u;
        fRngState ^= static_cast<uint32_t>(getWidth() * getHeight() + fPointCount * 7919u);
        fRngState = fRngState * 6364136223846793005u + 1442695040888963407u; // better LCG
        auto rndFloat = [&](float mn, float mx) -> float {
            fRngState = fRngState * 6364136223846793005u + 1442695040888963407u;
            const float t = static_cast<float>((fRngState >> 12) & 0xFFFFF) / 1048575.0f;
            return mn + t * (mx - mn);
        };
        auto rndInt = [&](int mn, int mx) -> int {
            fRngState = fRngState * 6364136223846793005u + 1442695040888963407u;
            return mn + static_cast<int>((fRngState >> 16) % static_cast<uint32_t>(mx - mn + 1));
        };

        doRandomiseGlobals(rndFloat);
        for (int i = 0; i < 8; ++i)
            doRandomisePoint(i, rndFloat, rndInt);

        if (fSelectedPoint >= 0) syncPerPointKnobs();
    }

    void doRandomisePointOnly()
    {
        // Randomise ALL points (not just the selected one)
        fRngState = fRngState * 6364136223846793005u + 1442695040888963407u;
        fRngState ^= static_cast<uint32_t>(fPointCount * 104729u);
        auto rndFloat = [&](float mn, float mx) -> float {
            fRngState = fRngState * 6364136223846793005u + 1442695040888963407u;
            const float t = static_cast<float>((fRngState >> 12) & 0xFFFFF) / 1048575.0f;
            return mn + t * (mx - mn);
        };
        auto rndInt = [&](int mn, int mx) -> int {
            fRngState = fRngState * 6364136223846793005u + 1442695040888963407u;
            return mn + static_cast<int>((fRngState >> 16) % static_cast<uint32_t>(mx - mn + 1));
        };
        for (int i = 0; i < 8; ++i)
            doRandomisePoint(i, rndFloat, rndInt);
        if (fSelectedPoint >= 0) syncPerPointKnobs();
    }

    void doRandomiseGlobalsOnly()
    {
        fRngState = fRngState * 6364136223846793005u + 1442695040888963407u;
        auto rndFloat = [&](float mn, float mx) -> float {
            fRngState = fRngState * 6364136223846793005u + 1442695040888963407u;
            const float t = static_cast<float>((fRngState >> 12) & 0xFFFFF) / 1048575.0f;
            return mn + t * (mx - mn);
        };
        doRandomiseGlobals(rndFloat);
    }

    template<typename RndF>
    void doRandomiseGlobals(RndF& rndFloat)
    {
        fTexture = rndFloat(0.2f, 1.0f);
        setParameterValue(kGlobalTexture, fTexture);
        fGlobalKnobs[0].value = fTexture;

        fFeedback = rndFloat(0.3f, 0.9f);
        setParameterValue(kGlobalFeedback, fFeedback);
        fGlobalKnobs[1].value = fFeedback;

        fPitchShift = rndFloat(-7.0f, 7.0f);
        setParameterValue(kGlobalPitchShift, fPitchShift);
        fGlobalKnobs[2].value = fPitchShift;

        fWetDry = rndFloat(0.6f, 1.0f);
        setParameterValue(kGlobalWetDry, fWetDry);
        fGlobalKnobs[3].value = fWetDry;

        fInputGain = 0.0f;  // keep input gain neutral
        setParameterValue(kGlobalInputGain, fInputGain);
        fGlobalKnobs[4].value = fInputGain;

        fOutputGain = 0.0f; // keep output gain neutral
        setParameterValue(kGlobalOutputGain, fOutputGain);
        fGlobalKnobs[5].value = fOutputGain;

        fStereoCollapse = rndFloat(0.4f, 1.0f);
        setParameterValue(kGlobalStereoCollapse, fStereoCollapse);
        fGlobalKnobs[6].value = fStereoCollapse;
    }

    template<typename RndF, typename RndI>
    void doRandomisePoint(int i, RndF& rndFloat, RndI& rndInt)
    {
        const bool harmonic = (fHarmonicMode > 0.5f);

        if (!harmonic)
        {
            const float offset = rndFloat(-24.0f, 24.0f);
            fPointCutoffOffset[i] = offset;
            fPointCutoffZone[i] = (offset >= 0.0f) ? 1 : -1;
            setParameterValue(ppIdx(i, kPPCutoffOffset), offset);
        }

        const float ft = static_cast<float>(rndInt(0, 2));
        fPointFilterType[i] = ft;
        setParameterValue(ppIdx(i, kPPFilterType), ft);

        const float fb = rndFloat(0.4f, 0.9f);
        fPointFeedback[i] = fb;
        setParameterValue(ppIdx(i, kPPFeedback), fb);

        const float ps = rndFloat(-7.0f, 7.0f);
        fPointPitchShift[i] = ps;
        setParameterValue(ppIdx(i, kPPPitchShift), ps);

        const float pan = rndFloat(-1.0f, 1.0f);
        fPointPan[i] = pan;
        setParameterValue(ppIdx(i, kPPPan), pan);

        const float q = rndFloat(2.5f, 12.0f);
        fPointQ[i] = q;
        setParameterValue(ppIdx(i, kPPQ), q);

        const float lv = rndFloat(0.5f, 1.5f);
        fPointLevel[i] = lv;
        setParameterValue(ppIdx(i, kPPLevel), lv);
    }

    void syncKnobToState(const Knob& k)
    {
        // Update mirrored state when a knob changes
        if (k.paramIndex == kGlobalTexture)    { fTexture = k.value; clampPointsToTexture(); }
        if (k.paramIndex == kGlobalFeedback)   fFeedback = k.value;
        if (k.paramIndex == kGlobalPitchShift) fPitchShift = k.value;
        if (k.paramIndex == kGlobalWetDry)     fWetDry = k.value;
        if (k.paramIndex == kGlobalInputGain)  fInputGain = k.value;
        if (k.paramIndex == kGlobalOutputGain) fOutputGain = k.value;
        if (k.paramIndex == kGlobalStereoCollapse) fStereoCollapse = k.value;

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
    float  fScale { 1.0f };
    NanoImage fBgImage;
    int    fPointCount { 2 };
    int    fSelectedPoint { -1 };   // primary selection (shown in panel)
    bool   fPointSelected[8] {};    // multi-select set
    bool   fDraggingPoint { false };
    float  fDragStartY { 0.0f };
    float  fDragStartValue { 0.0f };
    float  fDragStartMX { 0.0f };      // mouse X at drag start
    float  fDragStartMY { 0.0f };      // mouse Y at drag start
    float  fDragStartPan[8] {};        // each point's pan at drag start
    float  fDragStartOffset[8] {};     // each point's cutoff offset at drag start
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

    // Per-point RND button position
    float fPPRndBtnX{0}, fPPRndBtnY{0}, fPPRndBtnW{0}, fPPRndBtnH{0};

    // Global RND button position
    float fGlobalRndBtnX{0}, fGlobalRndBtnY{0}, fGlobalRndBtnW{0}, fGlobalRndBtnH{0};

    // Global params mirrored
    float fTexture{0.5f}, fResonance{0.707f}, fFeedback{0.3f};
    float fPitchShift{0}, fWetDry{1}, fInputGain{0}, fOutputGain{0};
    float fHarmonicMode{0};
    float fStereoCollapse{1.0f};

    // Menu state
    bool  fMenuOpen { false };
    bool  fPresetsOpen { false };
    int   fMenuTab  { 0 };  // 0=Settings, 1=Info
    float fMenuBtnX{0}, fMenuBtnY{0}, fMenuBtnW{0}, fMenuBtnH{0};
    float fPresetsBtnX{0}, fPresetsBtnY{0}, fPresetsBtnW{0}, fPresetsBtnH{0};
    float fPresetSaveBtnX{0}, fPresetSaveBtnY{0}, fPresetSaveBtnW{0}, fPresetSaveBtnH{0};

    // Preset strip (top bar)
    float fPresetStripX{0}, fPresetStripY{0}, fPresetStripW{0}, fPresetStripH{0};
    float fPresetArrowW{0}, fPresetNameW{0};
    int   fCurrentPresetIdx { -1 };

    // Preset file list (with folder info)
    static constexpr int kMaxPresets = 64;
    static constexpr int kMaxFolders = 16;
    char  fPresetNames[kMaxPresets][64] {};
    char  fPresetFolders[kMaxPresets][64] {};  // which folder each preset belongs to
    char  fFolderNames[kMaxFolders][64] {};
    int   fFolderStart[kMaxFolders] {};         // index into presets where folder starts
    int   fFolderCount[kMaxFolders] {};         // number of presets in folder
    int   fPresetCount { 0 };
    int   fNumFolders  { 0 };

    // Preset naming input
    bool  fNamingPreset { false };
    char  fPresetNameInput[64] {};
    int   fPresetNameLen { 0 };
    int   fPresetHoverIdx { -1 };  // which preset row the mouse is over

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
