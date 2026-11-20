#include "PluginEditor.h"

using namespace SilverColours;

namespace
{
    juce::Font font (float size, bool bold = false)
    {
        return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
    }

    void drawScreen (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setGradientFill (juce::ColourGradient (screenTop, r.getX(), r.getY(), screenBot, r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.5f);
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.drawRoundedRectangle (r.expanded (1.0f), 9.0f, 1.0f);
    }

    float readParam (SilverPhaseProcessor& p, const char* id)
    {
        return p.apvts.getRawParameterValue (id)->load();
    }

    // indice da banda mais proxima de uma frequencia
    int nearestBand (const std::vector<CorrelationAnalyzer::BandData>& bands, float hz)
    {
        int best = -1; float bestD = 1.0e9f;
        for (int i = 0; i < (int) bands.size(); ++i)
        {
            const float d = std::abs (std::log (bands[(size_t) i].fCentre / hz));
            if (d < bestD) { bestD = d; best = i; }
        }
        return best;
    }
}

//==============================================================================
void FreqAxis::drawFreqGrid (juce::Graphics& g, juce::Rectangle<float> plot, bool withLabels)
{
    const float freqs[] = { 31.5f, 63.0f, 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f };
    const char* names[] = { "31", "63", "125", "250", "500", "1k", "2k", "4k", "8k", "16k" };

    g.setFont (font (11.0f));
    for (int i = 0; i < 10; ++i)
    {
        const float x = toX (freqs[i], plot);
        g.setColour (gridLine.withAlpha (0.8f));
        g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());

        if (withLabels)
        {
            g.setColour (gridText);
            g.drawText (names[i], juce::Rectangle<float> (x - 20.0f, plot.getBottom() + 3.0f, 40.0f, 14.0f),
                        juce::Justification::centred);
        }
    }
}

//==============================================================================
// CorrelationDisplay
//==============================================================================
juce::Rectangle<float> CorrelationDisplay::getPlot() const
{
    auto r = getLocalBounds().toFloat();
    return { r.getX() + FreqAxis::leftMargin, r.getY() + 28.0f,
             r.getWidth() - FreqAxis::leftMargin - FreqAxis::rightMargin, r.getHeight() - 28.0f - 22.0f };
}

float CorrelationDisplay::yFor (float corr, juce::Rectangle<float> plot) const
{
    return plot.getY() + (1.0f - corr) * 0.5f * plot.getHeight();
}

int CorrelationDisplay::bandAt (float x) const
{
    const auto plot = getPlot();
    const auto& bands = proc.analyzer.getBands();
    for (int i = 0; i < (int) bands.size(); ++i)
    {
        const auto& b = bands[(size_t) i];
        if (x >= FreqAxis::toX (b.fLo, plot) && x < FreqAxis::toX (b.fHi, plot))
            return i;
    }
    return -1;
}

void CorrelationDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    drawScreen (g, bounds);

    const auto plot = getPlot();
    const float y0 = yFor (0.0f, plot);
    const float threshold = readParam (proc, ParamIDs::threshold);
    const int listenMode  = (int) readParam (proc, ParamIDs::listen);
    const float listenHz  = readParam (proc, ParamIDs::listenFreq);

    // titulo
    g.setColour (gridText);
    g.setFont (font (12.0f, true));
    g.drawText (utf8 ("CORRELAÇÃO POR BANDA  (L × R)"), bounds.withHeight (26.0f).withTrimmedLeft (14.0f),
                juce::Justification::centredLeft);

    // zona de anti-fase
    g.setColour (red.withAlpha (0.07f));
    g.fillRect (plot.withTop (y0));

    // grade horizontal
    g.setFont (font (11.0f));
    const float levels[] = { 1.0f, 0.5f, 0.0f, -0.5f, -1.0f };
    const char* labels[] = { "+1", "+0.5", "0", "-0.5", "-1" };
    for (int i = 0; i < 5; ++i)
    {
        const float y = yFor (levels[i], plot);
        g.setColour (i == 2 ? gridText.withAlpha (0.9f) : gridLine);
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
        g.setColour (levels[i] < 0.0f ? red.withAlpha (0.85f) : gridText);
        g.drawText (labels[i], juce::Rectangle<float> (bounds.getX() + 4.0f, y - 7.0f, FreqAxis::leftMargin - 10.0f, 14.0f),
                    juce::Justification::centredRight);
    }

    FreqAxis::drawFreqGrid (g, plot, true);

    // legenda na linha do titulo
    {
        auto leg = bounds.withHeight (26.0f).withTrimmedRight (16.0f);
        g.setFont (font (11.0f, true));
        auto item = [&] (juce::Colour c, const juce::String& text, float w)
        {
            auto r = leg.removeFromRight (w);
            g.setColour (c);
            g.fillRect (juce::Rectangle<float> (r.getX(), r.getCentreY() - (c == orange ? 1.0f : 4.0f), 12.0f, c == orange ? 2.0f : 8.0f));
            g.setColour (gridText);
            g.drawText (text, r.withTrimmedLeft (17.0f), juce::Justification::centredLeft);
            leg.removeFromRight (10.0f);
        };
        item (orange, utf8 ("pior valor"), 78.0f);
        item (red,    utf8 ("anti-fase"),  74.0f);
        item (yellow, utf8 ("sem relação"), 90.0f);
        item (teal,   utf8 ("em fase"),    66.0f);
    }

    g.setFont (font (10.0f, true));
    g.setColour (red.withAlpha (0.85f));
    g.drawText ("ANTI-FASE", juce::Rectangle<float> (plot.getX() + 6.0f, plot.getBottom() - 16.0f, 120.0f, 14.0f),
                juce::Justification::centredLeft);

    const auto& bands = proc.analyzer.getBands();
    const int listenBand = listenMode != 0 ? nearestBand (bands, listenHz) : -1;

    // barras
    for (int i = 0; i < (int) bands.size(); ++i)
    {
        const auto& b = bands[(size_t) i];
        const float x1 = FreqAxis::toX (b.fLo, plot) + 1.0f;
        const float x2 = FreqAxis::toX (b.fHi, plot) - 1.0f;
        if (x2 <= x1) continue;
        const float w = x2 - x1;

        const bool flagged = b.everActive && b.minCorr < threshold;
        if (flagged)
        {
            g.setColour (red.withAlpha (0.13f));
            g.fillRect (juce::Rectangle<float> (x1 - 1.0f, plot.getY(), w + 2.0f, plot.getHeight()));
        }

        if (! b.active)
        {
            g.setColour (gridText.withAlpha (0.25f));
            g.fillRect (juce::Rectangle<float> (x1, y0 - 1.0f, w, 2.0f));
        }
        else
        {
            const float y = yFor (b.corr, plot);
            const auto col = forCorrelation (b.corr);
            auto bar = juce::Rectangle<float>::leftTopRightBottom (x1, std::min (y, y0), x2, std::max (y, y0));
            if (bar.getHeight() < 2.0f) bar = bar.withSizeKeepingCentre (w, 2.0f);

            g.setGradientFill (juce::ColourGradient (col.brighter (0.3f), x1, bar.getY(), col.darker (0.35f), x1, bar.getBottom(), false));
            g.fillRect (bar);
        }

        if (b.everActive && b.minCorr < 0.999f)
        {
            g.setColour (orange);
            g.fillRect (juce::Rectangle<float> (x1, yFor (b.minCorr, plot) - 1.0f, w, 2.0f));
        }

        if (i == listenBand)
        {
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.drawRect (juce::Rectangle<float> (x1 - 1.0f, plot.getY(), w + 2.0f, plot.getHeight()), 1.5f);
        }
    }

    // limiar
    {
        const float yT = yFor (threshold, plot);
        juce::Path p;
        p.startNewSubPath (plot.getX(), yT);
        p.lineTo (plot.getRight(), yT);
        const float dashes[] = { 6.0f, 4.0f };
        juce::Path dashed;
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, p, dashes, 2);
        g.setColour (yellow.withAlpha (0.75f));
        g.fillPath (dashed);
        g.setFont (font (10.0f));
        g.drawText ("limiar", juce::Rectangle<float> (plot.getRight() - 50.0f, yT - 14.0f, 46.0f, 12.0f),
                    juce::Justification::centredRight);
    }

    // caixa de leitura do mouse
    if (hoverBand >= 0 && hoverBand < (int) bands.size())
    {
        const auto& b = bands[(size_t) hoverBand];
        juce::StringArray lines;
        lines.add (FreqAxis::format (b.fCentre) + "   (" + FreqAxis::format (b.fLo) + " - " + FreqAxis::format (b.fHi) + ")");
        if (b.active)
        {
            lines.add (utf8 ("Correlação: ") + juce::String (b.corr, 2) + utf8 ("    Mín: ") + juce::String (b.minCorr, 2));
            lines.add ("Mid: " + juce::String (b.midDb, 1) + " dB    Side: " + juce::String (b.sideDb, 1) + " dB");
            lines.add ("Perda em mono: " + juce::String (b.monoLossDb, 1) + " dB");
        }
        else
        {
            lines.add ("Sem sinal nesta banda");
            if (b.everActive) lines.add (utf8 ("Mín desde o Limpar: ") + juce::String (b.minCorr, 2));
        }
        lines.add (listenMode != 0 ? utf8 ("Clique: ouvir  •  duplo clique: sair")
                                  : juce::String ("Clique para ouvir esta banda"));

        const float bw = 250.0f, bh = 16.0f * (float) lines.size() + 10.0f;
        float bx = hoverPos.x + 14.0f, by = hoverPos.y + 10.0f;
        if (bx + bw > bounds.getRight() - 4.0f) bx = hoverPos.x - bw - 10.0f;
        if (by + bh > bounds.getBottom() - 4.0f) by = bounds.getBottom() - bh - 4.0f;

        juce::Rectangle<float> box (bx, by, bw, bh);
        g.setColour (juce::Colour (0xf0e9edf2));
        g.fillRoundedRectangle (box, 6.0f);
        g.setColour (navy);
        g.drawRoundedRectangle (box, 6.0f, 1.0f);

        for (int i = 0; i < lines.size(); ++i)
        {
            g.setFont (font (12.0f, i == 0));
            g.setColour (i == lines.size() - 1 ? navySoft : navy);
            g.drawText (lines[i], box.reduced (8.0f, 5.0f).withHeight (16.0f).translated (0.0f, 16.0f * (float) i),
                        juce::Justification::centredLeft);
        }
    }
}

void CorrelationDisplay::mouseMove (const juce::MouseEvent& e)
{
    hoverPos = e.position;
    hoverBand = getPlot().contains (e.position) ? bandAt (e.position.x) : -1;
    repaint();
}

void CorrelationDisplay::mouseExit (const juce::MouseEvent&)
{
    hoverBand = -1;
    repaint();
}

void CorrelationDisplay::mouseDown (const juce::MouseEvent& e)
{
    if (e.getNumberOfClicks() > 1)          // o segundo clique e tratado no mouseDoubleClick
        return;

    const int b = bandAt (e.position.x);
    const auto& bands = proc.analyzer.getBands();
    if (b >= 0 && b < (int) bands.size() && onBandClicked)
        onBandClicked (bands[(size_t) b].fCentre);
}

void CorrelationDisplay::mouseDoubleClick (const juce::MouseEvent&)
{
    if (onDoubleClicked)
        onDoubleClicked();
}

//==============================================================================
// SpectrumDisplay (Mid x Side)
//==============================================================================
void SpectrumDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    drawScreen (g, bounds);

    const juce::Rectangle<float> plot (bounds.getX() + FreqAxis::leftMargin, bounds.getY() + 26.0f,
                                       bounds.getWidth() - FreqAxis::leftMargin - FreqAxis::rightMargin,
                                       bounds.getHeight() - 26.0f - 22.0f);

    const float dbTop = 0.0f, dbBottom = -96.0f;
    auto yFor = [&] (float db)
    {
        db = juce::jlimit (dbBottom, dbTop, db);
        return plot.getY() + (dbTop - db) / (dbTop - dbBottom) * plot.getHeight();
    };

    g.setColour (gridText);
    g.setFont (font (12.0f, true));
    g.drawText ("ESPECTRO MID x SIDE", bounds.withHeight (24.0f).withTrimmedLeft (14.0f), juce::Justification::centredLeft);

    // legenda
    {
        auto leg = bounds.withHeight (24.0f).withTrimmedRight (16.0f);
        g.setFont (font (11.0f, true));
        g.setColour (red.withAlpha (0.9f));
        g.drawText (utf8 ("Side > Mid = anti-fase"), leg, juce::Justification::centredRight);
        leg.removeFromRight (150.0f);
        g.setColour (sideOrange);
        g.drawText ("SIDE", leg, juce::Justification::centredRight);
        leg.removeFromRight (44.0f);
        g.setColour (midBlue);
        g.drawText ("MID", leg, juce::Justification::centredRight);
    }

    g.setFont (font (11.0f));
    for (float db = 0.0f; db >= dbBottom; db -= 24.0f)
    {
        const float y = yFor (db);
        g.setColour (gridLine);
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
        g.setColour (gridText);
        g.drawText (juce::String (juce::roundToInt (db)), juce::Rectangle<float> (bounds.getX() + 4.0f, y - 7.0f, FreqAxis::leftMargin - 10.0f, 14.0f),
                    juce::Justification::centredRight);
    }
    FreqAxis::drawFreqGrid (g, plot, true);

    const auto& bands = proc.analyzer.getBands();
    if (bands.empty()) return;

    // preenchimento vermelho onde Side > Mid
    for (const auto& b : bands)
    {
        if (! b.active || b.sideDb <= b.midDb) continue;
        const float x1 = FreqAxis::toX (b.fLo, plot), x2 = FreqAxis::toX (b.fHi, plot);
        g.setColour (red.withAlpha (0.38f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x1, yFor (b.sideDb), x2, yFor (b.midDb)));
    }

    juce::Path midPath, sidePath;
    bool started = false;
    for (const auto& b : bands)
    {
        const float x = FreqAxis::toX (b.fCentre, plot);
        const float ym = yFor (b.active ? b.midDb : dbBottom);
        const float ys = yFor (b.active ? b.sideDb : dbBottom);
        if (! started) { midPath.startNewSubPath (x, ym); sidePath.startNewSubPath (x, ys); started = true; }
        else           { midPath.lineTo (x, ym);          sidePath.lineTo (x, ys); }
    }

    g.saveState();
    g.reduceClipRegion (plot.toNearestInt());
    g.setColour (midBlue);
    g.strokePath (midPath, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved));
    g.setColour (sideOrange);
    g.strokePath (sidePath, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved));
    g.restoreState();
}

//==============================================================================
// GlobalMeter
//==============================================================================
void GlobalMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const auto& an = proc.analyzer;

    g.setColour (navy);
    g.setFont (font (12.0f, true));
    g.drawText (utf8 ("CORRELAÇÃO GLOBAL"), r.removeFromTop (18.0f), juce::Justification::centredLeft);

    auto bar = r.removeFromTop (22.0f).reduced (0.0f, 2.0f);
    {
        juce::ColourGradient grad (red, bar.getX(), 0.0f, teal, bar.getRight(), 0.0f, false);
        grad.addColour (0.5, yellow);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (bar, 4.0f);
        g.setColour (navy.withAlpha (0.6f));
        g.drawRoundedRectangle (bar, 4.0f, 1.0f);
        g.drawVerticalLine (juce::roundToInt (bar.getCentreX()), bar.getY(), bar.getBottom());
    }

    auto xFor = [&] (float c) { return bar.getX() + (c + 1.0f) * 0.5f * bar.getWidth(); };

    if (an.getGlobalMinimum() < 0.999f)
    {
        const float xm = xFor (an.getGlobalMinimum());
        g.setColour (orange.darker (0.3f));
        g.fillRect (juce::Rectangle<float> (xm - 1.5f, bar.getY() - 3.0f, 3.0f, bar.getHeight() + 6.0f));
    }

    {
        const float xc = xFor (an.getGlobalCorrelation());
        juce::Path tri;
        tri.addTriangle (xc - 6.0f, bar.getBottom() + 7.0f, xc + 6.0f, bar.getBottom() + 7.0f, xc, bar.getBottom() - 2.0f);
        g.setColour (navy);
        g.fillPath (tri);
        g.setColour (juce::Colours::white);
        g.fillRect (juce::Rectangle<float> (xc - 1.0f, bar.getY(), 2.0f, bar.getHeight()));
    }

    r.removeFromTop (8.0f);
    g.setFont (font (10.0f));
    g.setColour (navySoft);
    auto scale = r.removeFromTop (12.0f);
    g.drawText ("-1", scale, juce::Justification::centredLeft);
    g.drawText ("0", scale, juce::Justification::centred);
    g.drawText ("+1", scale, juce::Justification::centredRight);

    r.removeFromTop (4.0f);
    g.setFont (font (13.0f, true));
    g.setColour (navy);
    auto row = r.removeFromTop (18.0f);
    const auto sign = [] (float v) { return (v >= 0.0f ? "+" : "") + juce::String (v, 2); };
    g.drawText ("Atual: " + (an.isGlobalActive() ? sign (an.getGlobalCorrelation()) : juce::String ("--")),
                row, juce::Justification::centredLeft);
    g.setColour (orange.darker (0.4f));
    g.drawText (utf8 ("Mín: ") + sign (an.getGlobalMinimum()), row, juce::Justification::centredRight);

    g.setFont (font (12.0f));
    g.setColour (navy);
    g.drawText ("Perda em mono: " + juce::String (an.getGlobalMonoLossDb(), 1) + " dB",
                r.removeFromTop (18.0f), juce::Justification::centredLeft);
}

//==============================================================================
// ProblemList
//==============================================================================
ProblemList::ProblemList (SilverPhaseProcessor& p) : proc (p)
{
    viewport.setViewedComponent (&rowsComp, false);
    viewport.setScrollBarsShown (true, false);       // so rolagem vertical
    viewport.setScrollBarThickness (10);
    viewport.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, navySoft);
    viewport.getVerticalScrollBar().setColour (juce::ScrollBar::trackColourId, silverDark.withAlpha (0.35f));
    addAndMakeVisible (viewport);
}

std::vector<ProblemList::Row> ProblemList::collectRows() const
{
    std::vector<Row> rows;
    const float threshold = readParam (proc, ParamIDs::threshold);

    for (const auto& b : proc.analyzer.getBands())
        if (b.everActive && b.minCorr < threshold)
            rows.push_back ({ b.fCentre, b.minCorr, b.active ? b.corr : 1.0f });

    std::sort (rows.begin(), rows.end(), [] (const Row& a, const Row& b) { return a.minCorr < b.minCorr; });
    return rows;
}

void ProblemList::resized()
{
    viewport.setBounds (getLocalBounds().withTrimmedTop (headerH));
    updateRowsSize();
}

void ProblemList::updateRowsSize()
{
    const int contentH = (int) rowsComp.rows.size() * rowH;
    const bool needsScroll = contentH > viewport.getHeight();
    const int w = viewport.getWidth() - (needsScroll ? viewport.getScrollBarThickness() + 2 : 0);
    rowsComp.setSize (juce::jmax (1, w), juce::jmax (1, contentH));
}

void ProblemList::refresh()
{
    rowsComp.rows = collectRows();
    updateRowsSize();
    rowsComp.repaint();
    repaint();
}

void ProblemList::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();

    g.setColour (navy);
    g.setFont (font (12.0f, true));
    auto title = r.removeFromTop (18.0f);
    g.drawText (utf8 ("FREQUÊNCIAS EM ANTI-FASE"), title, juce::Justification::centredLeft);
    if (! rowsComp.rows.empty())
    {
        g.setColour (navySoft);
        g.drawText (juce::String ((int) rowsComp.rows.size()), title, juce::Justification::centredRight);
    }
    g.setFont (font (11.0f));
    g.setColour (navySoft);
    g.drawText (utf8 ("clique: ouvir  •  duplo clique: sair"), r.removeFromTop (16.0f), juce::Justification::centredLeft);
    r.removeFromTop ((float) headerH - 34.0f);

    if (rowsComp.rows.empty())
    {
        g.setColour (navySoft);
        g.setFont (font (12.0f));
        g.drawText (utf8 ("Nenhuma banda abaixo do limiar"), r.removeFromTop (24.0f), juce::Justification::centredLeft);
    }
}

void ProblemList::Rows::paint (juce::Graphics& g)
{
    const float listenHz = readParam (owner.proc, ParamIDs::listenFreq);
    const bool listening = readParam (owner.proc, ParamIDs::listen) > 0.5f;

    // desenha so as linhas visiveis
    const auto clip = g.getClipBounds();
    const int first = juce::jmax (0, clip.getY() / rowH);
    const int last  = juce::jmin ((int) rows.size() - 1, clip.getBottom() / rowH);

    for (int i = first; i <= last; ++i)
    {
        const auto& row = rows[(size_t) i];
        auto line = juce::Rectangle<float> (0.0f, (float) (i * rowH), (float) getWidth(), (float) rowH).reduced (0.0f, 1.5f);

        const bool sel = listening && std::abs (std::log (row.freq / listenHz)) < 0.02f;
        g.setColour (sel ? buttonOn.withAlpha (0.18f) : juce::Colours::white.withAlpha (0.45f));
        g.fillRoundedRectangle (line, 4.0f);
        if (sel)
        {
            g.setColour (buttonOn.withAlpha (0.6f));
            g.drawRoundedRectangle (line.reduced (0.5f), 4.0f, 1.0f);
        }

        auto txt = line.reduced (6.0f, 0.0f);
        g.setColour (navy);
        g.setFont (font (12.0f, true));
        g.drawText (FreqAxis::format (row.freq), txt.removeFromLeft (78.0f), juce::Justification::centredLeft);

        auto barArea = txt.removeFromLeft (txt.getWidth() - 46.0f).reduced (4.0f, 5.0f);
        g.setColour (silverDark.withAlpha (0.5f));
        g.fillRoundedRectangle (barArea, 2.0f);
        const float amount = juce::jlimit (0.0f, 1.0f, (1.0f - row.minCorr) * 0.5f);
        g.setColour (forCorrelation (row.minCorr));
        g.fillRoundedRectangle (barArea.withWidth (barArea.getWidth() * amount), 2.0f);

        g.setColour (row.minCorr < 0.0f ? red.darker (0.3f) : navy);
        g.drawText (juce::String (row.minCorr, 2), txt, juce::Justification::centredRight);
    }
}

void ProblemList::Rows::mouseDown (const juce::MouseEvent& e)
{
    if (e.getNumberOfClicks() > 1)          // o segundo clique e tratado no mouseDoubleClick
        return;

    const int idx = rowAt (e.position.y);
    if (idx >= 0 && idx < (int) rows.size() && owner.onSelect)
        owner.onSelect (rows[(size_t) idx].freq);
}

void ProblemList::Rows::mouseDoubleClick (const juce::MouseEvent&)
{
    if (owner.onDeselect)
        owner.onDeselect();
}

//==============================================================================
// Editor
//==============================================================================
SilverPhaseEditor::SilverPhaseEditor (SilverPhaseProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      corrDisplay (p), specDisplay (p), globalMeter (p), problemList (p)
{
    setLookAndFeel (&lnf);

    addAndMakeVisible (corrDisplay);
    addAndMakeVisible (specDisplay);
    addAndMakeVisible (globalMeter);
    addAndMakeVisible (problemList);

    corrDisplay.onBandClicked = [this] (float hz) { selectListenFrequency (hz); };
    problemList.onSelect      = [this] (float hz) { selectListenFrequency (hz); };
    corrDisplay.onDoubleClicked = [this] { stopListening(); };
    problemList.onDeselect      = [this] { stopListening(); };

    setupLabel (resLabel,    utf8 ("Resolução"));
    setupLabel (respLabel,   "Resposta");
    setupLabel (threshLabel, "Limiar");
    setupLabel (listenLabel, "Ouvir");
    setupLabel (bandLabel,   "");
    bandLabel.setFont (font (12.0f));

    resolutionBox.addItemList ({ "1/3 oitava", "1/6 oitava" }, 1);
    listenBox.addItemList ({ "Desligado", "Banda L/R", "Banda Side", "Banda Mid" }, 1);
    addAndMakeVisible (resolutionBox);
    addAndMakeVisible (listenBox);

    for (auto* s : { &responseSlider, &thresholdSlider })
    {
        s->setSliderStyle (juce::Slider::LinearHorizontal);
        s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 20);
        s->setColour (juce::Slider::textBoxTextColourId,       navy);
        s->setColour (juce::Slider::textBoxBackgroundColourId, field);
        s->setColour (juce::Slider::textBoxOutlineColourId,    silverDark);
        addAndMakeVisible (*s);
    }

    freezeButton.setButtonText ("Congelar");
    clearButton.setButtonText ("Limpar");
    monoButton.setButtonText ("Mono");
    freezeButton.setClickingTogglesState (true);
    monoButton.setClickingTogglesState (true);
    clearButton.onClick = [this] { proc.analyzer.clearHold(); repaint(); };
    addAndMakeVisible (freezeButton);
    addAndMakeVisible (clearButton);
    addAndMakeVisible (monoButton);

    auto& s = proc.apvts;
    resolutionAtt = std::make_unique<ComboAtt>  (s, ParamIDs::resolution, resolutionBox);
    listenAtt     = std::make_unique<ComboAtt>  (s, ParamIDs::listen,     listenBox);
    responseAtt   = std::make_unique<SliderAtt> (s, ParamIDs::response,   responseSlider);
    thresholdAtt  = std::make_unique<SliderAtt> (s, ParamIDs::threshold,  thresholdSlider);
    freezeAtt     = std::make_unique<ButtonAtt> (s, ParamIDs::freeze,     freezeButton);
    monoAtt       = std::make_unique<ButtonAtt> (s, ParamIDs::mono,       monoButton);

    responseSlider.textFromValueFunction  = [] (double v) { return juce::String (juce::roundToInt (v)) + " ms"; };
    responseSlider.valueFromTextFunction  = [] (const juce::String& t) { return t.getDoubleValue(); };
    thresholdSlider.textFromValueFunction = [] (double v) { return (v >= 0.0 ? "+" : "") + juce::String (v, 2); };
    thresholdSlider.valueFromTextFunction = [] (const juce::String& t) { return t.getDoubleValue(); };
    responseSlider.updateText();
    thresholdSlider.updateText();

    setResizable (true, true);
    setResizeLimits (900, 580, 1800, 1100);
    setSize (1080, 680);

    startTimerHz (30);
}

SilverPhaseEditor::~SilverPhaseEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SilverPhaseEditor::setupLabel (juce::Label& l, const juce::String& text)
{
    l.setText (text, juce::dontSendNotification);
    l.setFont (font (12.0f, true));
    l.setColour (juce::Label::textColourId, navy);
    l.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (l);
}

void SilverPhaseEditor::selectListenFrequency (float hz)
{
    if (auto* fp = proc.apvts.getParameter (ParamIDs::listenFreq))
    {
        fp->beginChangeGesture();
        fp->setValueNotifyingHost (fp->convertTo0to1 (hz));
        fp->endChangeGesture();
    }

    if (auto* lp = proc.apvts.getParameter (ParamIDs::listen))
        if (lp->getValue() < 0.01f)          // estava desligado -> liga "Banda L/R"
        {
            lp->beginChangeGesture();
            lp->setValueNotifyingHost (lp->convertTo0to1 (1.0f));
            lp->endChangeGesture();
        }
}

void SilverPhaseEditor::stopListening()
{
    if (auto* lp = proc.apvts.getParameter (ParamIDs::listen))
    {
        lp->beginChangeGesture();
        lp->setValueNotifyingHost (0.0f);   // "Desligado"
        lp->endChangeGesture();
    }
}

void SilverPhaseEditor::timerCallback()
{
    auto& an = proc.analyzer;
    an.setBandsPerOctave ((int) readParam (proc, ParamIDs::resolution) == 1 ? 6 : 3);
    an.setResponseMs (readParam (proc, ParamIDs::response));
    an.setFreeze (readParam (proc, ParamIDs::freeze) > 0.5f);
    an.process();

    const int listenMode = (int) readParam (proc, ParamIDs::listen);
    const juce::String bandText = listenMode == 0
        ? utf8 ("Clique numa banda do gráfico para ouvi-la")
        : utf8 ("Ouvindo: ") + FreqAxis::format (readParam (proc, ParamIDs::listenFreq))
              + utf8 ("  (duplo clique para sair)");
    if (bandLabel.getText() != bandText)
        bandLabel.setText (bandText, juce::dontSendNotification);

    corrDisplay.repaint();
    specDisplay.repaint();
    globalMeter.repaint();
    problemList.refresh();
}

void SilverPhaseEditor::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();

    // fundo prata escovado
    g.setGradientFill (juce::ColourGradient (silverLight, 0.0f, 0.0f, silverMid.darker (0.05f), 0.0f, b.getBottom(), false));
    g.fillAll();
    juce::Random rng (7);
    for (int y = 0; y < getHeight(); y += 2)
    {
        g.setColour (juce::Colours::white.withAlpha (rng.nextFloat() * 0.10f));
        g.drawHorizontalLine (y, 0.0f, b.getRight());
    }

    // cabecalho
    auto header = b.withHeight (56.0f).reduced (18.0f, 0.0f);
    g.setColour (navy);
    g.setFont (font (28.0f, true));
    g.drawText ("SilverPhase", header, juce::Justification::centredLeft);
    g.setFont (font (12.0f, true));
    g.setColour (navySoft);
    g.drawText ("MULTIBAND CORRELATION METER", header.withTrimmedLeft (182.0f), juce::Justification::centredLeft);
    g.setFont (font (14.0f, true));
    g.setColour (navy.withAlpha (0.75f));
    g.drawText ("S I L V E R", header, juce::Justification::centredRight);

    g.setColour (silverDark);
    g.drawHorizontalLine (56, 12.0f, b.getRight() - 12.0f);
    g.setColour (juce::Colours::white.withAlpha (0.8f));
    g.drawHorizontalLine (57, 12.0f, b.getRight() - 12.0f);

    // paineis da lateral
    for (auto panel : { meterPanel, controlsPanel, listPanel })
    {
        auto pr = panel.toFloat();
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.fillRoundedRectangle (pr, 8.0f);
        g.setColour (silverDark);
        g.drawRoundedRectangle (pr.reduced (0.5f), 8.0f, 1.0f);
    }

    // rodape
    g.setColour (navySoft);
    g.setFont (font (11.0f));
    g.drawText (utf8 ("Barra = correlação atual   •   traço laranja = pior valor desde o Limpar   •   "
                      "Mono + Banda L/R: ouça a banda cancelar"),
                b.withTop (b.getBottom() - 24.0f).reduced (18.0f, 0.0f), juce::Justification::centredLeft);
}

void SilverPhaseEditor::resized()
{
    auto area = getLocalBounds().reduced (14);
    area.removeFromTop (50);
    area.removeFromBottom (18);

    sidebarArea = area.removeFromRight (290);
    area.removeFromRight (12);

    const int corrH = juce::roundToInt ((float) area.getHeight() * 0.62f);
    corrDisplay.setBounds (area.removeFromTop (corrH));
    area.removeFromTop (10);
    specDisplay.setBounds (area);

    auto side = sidebarArea;
    meterPanel = side.removeFromTop (128);
    side.removeFromTop (10);
    controlsPanel = side.removeFromTop (232);
    side.removeFromTop (10);
    listPanel = side;

    globalMeter.setBounds (meterPanel.reduced (12, 8));

    auto c = controlsPanel.reduced (12, 10);
    auto rowWithLabel = [&] (juce::Label& label, juce::Component& comp)
    {
        auto row = c.removeFromTop (26);
        label.setBounds (row.removeFromLeft (76));
        comp.setBounds (row);
        c.removeFromTop (8);
    };
    rowWithLabel (resLabel,    resolutionBox);
    rowWithLabel (respLabel,   responseSlider);
    rowWithLabel (threshLabel, thresholdSlider);

    {
        auto row = c.removeFromTop (28);
        const int w = (row.getWidth() - 16) / 3;
        freezeButton.setBounds (row.removeFromLeft (w)); row.removeFromLeft (8);
        clearButton.setBounds  (row.removeFromLeft (w)); row.removeFromLeft (8);
        monoButton.setBounds   (row);
        c.removeFromTop (8);
    }
    rowWithLabel (listenLabel, listenBox);
    bandLabel.setBounds (c.removeFromTop (20));

    problemList.setBounds (listPanel.reduced (12, 8));
}
