#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "CorrelationAnalyzer.h"
#include "PhaseCorrector.h"

namespace ParamIDs
{
    inline constexpr const char* mono       = "mono";
    inline constexpr const char* listen     = "listen";
    inline constexpr const char* listenFreq = "listenFreq";
    inline constexpr const char* resolution = "resolution";
    inline constexpr const char* response   = "response";
    inline constexpr const char* threshold  = "threshold";
    inline constexpr const char* freeze     = "freeze";

    // secao de correcao
    inline constexpr const char* corrOn       = "corrOn";
    inline constexpr const char* corrMode     = "corrMode";
    inline constexpr const char* corrTarget   = "corrTarget";
    inline constexpr const char* corrStrength = "corrStrength";
    inline constexpr const char* corrMaxRot   = "corrMaxRot";
    inline constexpr const char* corrMaxSide  = "corrMaxSide";
    inline constexpr const char* corrSpeed    = "corrSpeed";
    inline constexpr const char* corrBassMono = "corrBassMono";
    inline constexpr const char* corrBassFreq = "corrBassFreq";
    inline constexpr const char* corrDelta    = "corrDelta";
}

// Biquad simples (TDF-II, double) para o "ouvir banda"
struct SimpleBiquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    void setBandPass (double sampleRate, double freq, double q) noexcept
    {
        const double w0 = juce::MathConstants<double>::twoPi * freq / sampleRate;
        const double alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0 = alpha / a0;  b1 = 0.0;  b2 = -alpha / a0;
        a1 = -2.0 * std::cos (w0) / a0;
        a2 = (1.0 - alpha) / a0;
    }

    void reset() noexcept { z1 = z2 = 0.0; }

    inline float process (float xIn) noexcept
    {
        const double x = xIn;
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }
};

class SilverPhaseProcessor : public juce::AudioProcessor
{
public:
    SilverPhaseProcessor();
    ~SilverPhaseProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SilverPhase"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    CorrelationAnalyzer analyzer;      // entrada (antes)
    CorrelationAnalyzer analyzerOut;   // saida da correcao (depois)
    PhaseCorrector corrector;

    bool isCorrectionActive() const noexcept { return correctionActive.load(); }

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static double qForResolution (int resolutionIndex) noexcept;

private:
    std::atomic<float>* monoParam   = nullptr;
    std::atomic<float>* listenParam = nullptr;
    std::atomic<float>* freqParam   = nullptr;
    std::atomic<float>* resParam    = nullptr;

    SimpleBiquad filt[2][2];   // [canal][estagio]
    double lastFreq = -1.0, lastQ = -1.0;
    int lastListenMode = 0;
    double sr = 44100.0;

    std::atomic<bool> correctionActive { false };
    bool wasCorrecting = false;

    std::atomic<float>* cp[10] {};    // parametros da correcao

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SilverPhaseProcessor)
};
