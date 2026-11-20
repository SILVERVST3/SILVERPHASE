#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "SilverLookAndFeel.h"

//==============================================================================
// eixo de frequencia logaritmico compartilhado pelos dois graficos
struct FreqAxis
{
    static constexpr float fMin = 20.0f, fMax = 20000.0f;
    static constexpr float leftMargin = 46.0f, rightMargin = 14.0f;

    static float toX (float f, juce::Rectangle<float> plot)
    {
        f = juce::jlimit (fMin, fMax, f);
        return plot.getX() + plot.getWidth() * std::log (f / fMin) / std::log (fMax / fMin);
    }

    static juce::String format (float f)
    {
        if (f < 100.0f)   return juce::String (f, 1) + " Hz";
        if (f < 1000.0f)  return juce::String (juce::roundToInt (f)) + " Hz";
        if (f < 10000.0f) return juce::String (f / 1000.0f, 2) + " kHz";
        return juce::String (f / 1000.0f, 1) + " kHz";
    }

    static void drawFreqGrid (juce::Graphics& g, juce::Rectangle<float> plot, bool withLabels);
};

//==============================================================================
class CorrelationDisplay : public juce::Component
{
public:
    explicit CorrelationDisplay (SilverPhaseProcessor& p) : proc (p) {}

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;

    std::function<void (float)> onBandClicked;

private:
    juce::Rectangle<float> getPlot() const;
    float yFor (float corr, juce::Rectangle<float> plot) const;
    int bandAt (float x) const;

    SilverPhaseProcessor& proc;
    int hoverBand = -1;
    juce::Point<float> hoverPos;
};

//==============================================================================
class SpectrumDisplay : public juce::Component
{
public:
    explicit SpectrumDisplay (SilverPhaseProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;

private:
    SilverPhaseProcessor& proc;
};

//==============================================================================
class GlobalMeter : public juce::Component
{
public:
    explicit GlobalMeter (SilverPhaseProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;

private:
    SilverPhaseProcessor& proc;
};

//==============================================================================
class ProblemList : public juce::Component
{
public:
    explicit ProblemList (SilverPhaseProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    std::function<void (float)> onSelect;

private:
    struct Row { float freq; float minCorr; float corr; };
    std::vector<Row> collectRows() const;
    static constexpr int headerH = 40, rowH = 22, maxRows = 12;

    SilverPhaseProcessor& proc;
};

//==============================================================================
class SilverPhaseEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SilverPhaseEditor (SilverPhaseProcessor&);
    ~SilverPhaseEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void selectListenFrequency (float hz);
    void setupLabel (juce::Label&, const juce::String&);

    SilverPhaseProcessor& proc;
    SilverLookAndFeel lnf;

    CorrelationDisplay corrDisplay;
    SpectrumDisplay    specDisplay;
    GlobalMeter        globalMeter;
    ProblemList        problemList;

    juce::Label resLabel, respLabel, threshLabel, listenLabel, bandLabel;
    juce::ComboBox resolutionBox, listenBox;
    juce::Slider responseSlider, thresholdSlider;
    juce::TextButton freezeButton, clearButton, monoButton;

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<ComboAtt>  resolutionAtt, listenAtt;
    std::unique_ptr<SliderAtt> responseAtt, thresholdAtt;
    std::unique_ptr<ButtonAtt> freezeAtt, monoAtt;

    juce::Rectangle<int> sidebarArea, controlsPanel, meterPanel, listPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SilverPhaseEditor)
};
