#include "PluginProcessor.h"
#include "PluginEditor.h"

SilverPhaseProcessor::SilverPhaseProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SilverPhaseState", createLayout())
{
    monoParam   = apvts.getRawParameterValue (ParamIDs::mono);
    listenParam = apvts.getRawParameterValue (ParamIDs::listen);
    freqParam   = apvts.getRawParameterValue (ParamIDs::listenFreq);
    resParam    = apvts.getRawParameterValue (ParamIDs::resolution);

    const char* ids[10] = { ParamIDs::corrOn, ParamIDs::corrMode, ParamIDs::corrTarget, ParamIDs::corrStrength,
                            ParamIDs::corrMaxRot, ParamIDs::corrMaxSide, ParamIDs::corrSpeed,
                            ParamIDs::corrBassMono, ParamIDs::corrBassFreq, ParamIDs::corrDelta };
    for (int i = 0; i < 10; ++i)
        cp[i] = apvts.getRawParameterValue (ids[i]);
}

juce::AudioProcessorValueTreeState::ParameterLayout SilverPhaseProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::mono, 1 }, "Mono", false));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::listen, 1 }, "Listen",
                    StringArray { "Off", "Band L/R", "Band Side", "Band Mid" }, 0));

    NormalisableRange<float> freqRange (20.0f, 20000.0f);
    freqRange.setSkewForCentre (630.0f);
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::listenFreq, 1 }, "Listen Freq",
                    freqRange, 1000.0f, AudioParameterFloatAttributes().withLabel ("Hz")));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::resolution, 1 }, "Resolution",
                    StringArray { "1/3 oct", "1/6 oct" }, 0));

    NormalisableRange<float> respRange (50.0f, 3000.0f);
    respRange.setSkewForCentre (400.0f);
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::response, 1 }, "Response",
                    respRange, 300.0f, AudioParameterFloatAttributes().withLabel ("ms")));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::threshold, 1 }, "Threshold",
                    NormalisableRange<float> (-1.0f, 0.5f, 0.01f), 0.0f));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::freeze, 1 }, "Freeze", false));

    // ---- correcao ----
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::corrOn, 1 }, "Correction", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::corrMode, 1 }, "Correction Mode",
                    StringArray { "Auto", "Phase only", "Side only" }, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::corrTarget, 1 }, "Target",
                    NormalisableRange<float> (-0.5f, 0.8f, 0.01f), 0.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::corrStrength, 1 }, "Strength",
                    NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f, AudioParameterFloatAttributes().withLabel ("%")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::corrMaxRot, 1 }, "Max Rotation",
                    NormalisableRange<float> (0.0f, 180.0f, 1.0f), 90.0f, AudioParameterFloatAttributes().withLabel ("deg")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::corrMaxSide, 1 }, "Max Side Cut",
                    NormalisableRange<float> (0.0f, 12.0f, 0.1f), 4.0f, AudioParameterFloatAttributes().withLabel ("dB")));
    NormalisableRange<float> speedRange (20.0f, 2000.0f);
    speedRange.setSkewForCentre (250.0f);
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::corrSpeed, 1 }, "Speed",
                    speedRange, 200.0f, AudioParameterFloatAttributes().withLabel ("ms")));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::corrBassMono, 1 }, "Bass Mono", false));
    NormalisableRange<float> bassRange (40.0f, 300.0f, 1.0f);
    bassRange.setSkewForCentre (120.0f);
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::corrBassFreq, 1 }, "Bass Mono Freq",
                    bassRange, 120.0f, AudioParameterFloatAttributes().withLabel ("Hz")));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::corrDelta, 1 }, "Delta", false));

    return layout;
}

double SilverPhaseProcessor::qForResolution (int resolutionIndex) noexcept
{
    // Q de um passa-banda com largura de N oitavas: sqrt(2^N) / (2^N - 1)
    const double n = (resolutionIndex == 1) ? (1.0 / 6.0) : (1.0 / 3.0);
    const double p = std::pow (2.0, n);
    return std::sqrt (p) / (p - 1.0);
}

void SilverPhaseProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    juce::ignoreUnused (samplesPerBlock);
    analyzer.prepare (sampleRate);
    analyzerOut.prepare (sampleRate);
    corrector.prepare (sampleRate);

    wasCorrecting = cp[0]->load() > 0.5f;
    correctionActive.store (wasCorrecting);
    setLatencySamples (wasCorrecting ? PhaseCorrector::getLatencySamples() : 0);

    for (auto& ch : filt)
        for (auto& f : ch)
            f.reset();
    lastFreq = lastQ = -1.0;
}

bool SilverPhaseProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SilverPhaseProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();

    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);

    if (numSamples == 0 || numIn == 0)
        return;

    float* L = buffer.getWritePointer (0);
    float* R = (numOut > 1 && numIn > 1) ? buffer.getWritePointer (1) : L;

    // 1) analise do sinal de entrada (antes de qualquer processamento)
    analyzer.pushSamples (L, R, numSamples);

    // 2) correcao de anti-fase (latencia de 4096 amostras, informada ao host)
    const bool correcting = cp[0]->load() > 0.5f;
    if (correcting != wasCorrecting)
    {
        wasCorrecting = correcting;
        correctionActive.store (correcting);
        if (correcting) corrector.reset();
        setLatencySamples (correcting ? PhaseCorrector::getLatencySamples() : 0);
    }

    if (correcting)
    {
        PhaseCorrector::Settings st;
        st.mode           = (int) cp[1]->load();
        st.target         = cp[2]->load();
        st.strength       = cp[3]->load() / 100.0f;
        st.maxRotationDeg = cp[4]->load();
        st.maxSideDb      = cp[5]->load();
        st.speedMs        = cp[6]->load();
        st.bassMono       = cp[7]->load() > 0.5f;
        st.bassFreq       = cp[8]->load();
        st.delta          = cp[9]->load() > 0.5f;
        st.bandsPerOctave = ((int) resParam->load() == 1) ? 6 : 3;

        corrector.process (L, R, numSamples, st);
        analyzerOut.pushSamples (L, R, numSamples);
    }

    // 3) escuta da banda selecionada
    const int listenMode = (int) listenParam->load();
    if (listenMode != 0)
    {
        const double freq = juce::jlimit (20.0, sr * 0.45, (double) freqParam->load());
        const double q = qForResolution ((int) resParam->load());

        if (listenMode != lastListenMode)
            for (auto& ch : filt) for (auto& f : ch) f.reset();

        if (std::abs (freq - lastFreq) > 1.0e-3 || std::abs (q - lastQ) > 1.0e-6)
        {
            for (auto& ch : filt)
                for (auto& f : ch)
                    f.setBandPass (sr, freq, q);
            lastFreq = freq; lastQ = q;
        }

        // ganho de compensacao (2 estagios em cascata ~ mesmo ganho no centro = 0 dB)
        if (listenMode == 1)            // L/R separados -> mantem a imagem estereo da banda
        {
            for (int i = 0; i < numSamples; ++i)
            {
                L[i] = filt[0][1].process (filt[0][0].process (L[i]));
                if (R != L) R[i] = filt[1][1].process (filt[1][0].process (R[i]));
            }
        }
        else                            // Side ou Mid da banda, nos dois lados
        {
            const float sign = (listenMode == 2) ? -1.0f : 1.0f;
            for (int i = 0; i < numSamples; ++i)
            {
                const float x = 0.5f * (L[i] + sign * R[i]);
                const float y = filt[0][1].process (filt[0][0].process (x));
                L[i] = y;
                if (R != L) R[i] = y;
            }
        }
    }
    lastListenMode = listenMode;

    // 4) soma em mono (teste de compatibilidade)
    if (monoParam->load() > 0.5f && R != L)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float m = 0.5f * (L[i] + R[i]);
            L[i] = m; R[i] = m;
        }
    }
}

juce::AudioProcessorEditor* SilverPhaseProcessor::createEditor()
{
    return new SilverPhaseEditor (*this);
}

void SilverPhaseProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SilverPhaseProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SilverPhaseProcessor();
}
