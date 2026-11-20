#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <vector>

/*  Analisador de correlacao estereo por bandas.

    Para cada banda (1/3 ou 1/6 de oitava) calcula, a partir de uma STFT:
        Sxy = soma Re{ L(k) * conj(R(k)) }
        Sxx = soma |L(k)|^2 ,  Syy = soma |R(k)|^2
    com media exponencial no tempo, e a correlacao da banda:
        r = Sxy / sqrt(Sxx * Syy)      (+1 = mono, 0 = sem relacao, -1 = anti-fase)

    Isso equivale (Parseval) ao correlacionador classico aplicado ao sinal
    filtrado naquela banda. Mid/Side saem dos mesmos termos:
        |M|^2 = (Sxx + Syy + 2Sxy)/4 ,  |S|^2 = (Sxx + Syy - 2Sxy)/4

    Threading: pushSamples() e chamado pelo audio thread (FIFO lock-free).
    Todo o resto (process, getters, ajustes) roda no thread da interface.
*/
class CorrelationAnalyzer
{
public:
    static constexpr int fftOrder = 13;              // 8192 pontos
    static constexpr int fftSize  = 1 << fftOrder;
    static constexpr int hopSize  = fftSize / 4;
    static constexpr int numBins  = fftSize / 2 + 1;

    struct BandData
    {
        float fLo = 0, fCentre = 0, fHi = 0;
        int   binLo = 0, binHi = 0;

        float corr       = 1.0f;     // correlacao atual
        float minCorr    = 1.0f;     // pior valor (peak hold)
        float levelDb    = -120.0f;  // nivel (L+R)/2
        float midDb      = -120.0f;
        float sideDb     = -120.0f;
        float monoLossDb = 0.0f;     // quanto a banda perde ao somar em mono
        bool  active     = false;    // acima do gate agora
        bool  everActive = false;    // ja teve sinal desde o ultimo Clear
        int   activeFrames = 0;      // quadros seguidos com sinal (para estabilizar)

        double avgXY = 0, avgXX = 0, avgYY = 0;
    };

    CorrelationAnalyzer();

    // ---- audio thread ----
    void prepare (double sampleRate) noexcept;
    void pushSamples (const float* left, const float* right, int numSamples) noexcept;

    // ---- thread da interface ----
    bool process();                       // true se houve quadro novo
    void setBandsPerOctave (int bpo);     // 3 ou 6
    void setResponseMs (float ms);
    void setFreeze (bool shouldFreeze)    { frozen = shouldFreeze; }
    void setGateDb (float db)             { gateDb = db; }
    void clearHold();

    const std::vector<BandData>& getBands() const noexcept { return bands; }
    float getGlobalCorrelation() const noexcept  { return globalCorr; }
    float getGlobalMinimum() const noexcept      { return globalMin; }
    float getGlobalMonoLossDb() const noexcept   { return globalMonoLossDb; }
    bool  isGlobalActive() const noexcept        { return globalActive; }
    double getSampleRate() const noexcept        { return currentSampleRate; }
    int   getBandsPerOctave() const noexcept     { return bandsPerOctave; }

private:
    void rebuildIfNeeded();
    void rebuildBands();
    void updateAlpha();
    void consume (const float* l, const float* r, int n, bool& newFrame);
    void analyseFrame();

    // FIFO audio -> interface
    static constexpr int fifoCapacity = 1 << 16;
    juce::AbstractFifo fifo { fifoCapacity };
    std::vector<float> fifoL, fifoR;
    std::atomic<double> pendingSampleRate { 44100.0 };

    // historico circular para a STFT
    std::vector<float> histL, histR;
    int writePos = 0, samplesSinceHop = 0;

    juce::dsp::FFT fft { fftOrder };
    std::vector<float> window, bufL, bufR;

    std::vector<BandData> bands;
    double currentSampleRate = 0.0;
    int    bandsPerOctave = 3, builtBandsPerOctave = 0;
    float  responseMs = 300.0f;
    double alpha = 0.9;
    float  gateDb = -80.0f;
    bool   frozen = false;

    double gXY = 0, gXX = 0, gYY = 0;
    float  globalCorr = 1.0f, globalMin = 1.0f, globalMonoLossDb = 0.0f;
    bool   globalActive = false, globalEverActive = false;
    int    globalActiveFrames = 0;
    int    settleFrames = 8;     // quadros ate a leitura entrar no peak hold
};
