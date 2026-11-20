#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <vector>
#include "BandLayout.h"

/*  Corretor de anti-fase por bandas (STFT, overlap-add 75%).

    Diagnostico por banda, a partir do espectro cruzado medio Sxy = sum L * conj(R):
        correlacao  r = Re{Sxy} / sqrt(Sxx Syy)
        coerencia   c = |Sxy|   / sqrt(Sxx Syy)    (fase estavel entre L e R?)
        fase        phi = arg(Sxy)                 (quanto L esta adiantado em relacao a R)

    Correcao (so nas bandas com r < alvo, e so o minimo para chegar ao alvo):
      1) Rotacao de fase: L *= e^{-i theta/2}, R *= e^{+i theta/2}.
         O modulo de L e de R nao muda -> pan e nivel de cada lado preservados.
         Depois da rotacao: r' = c cos(phi - theta). Escolhe-se o menor theta com r' = alvo.
         Usada quando a coerencia e alta (mesmo som defasado entre L e R).
      2) Reducao de Side da banda (limitada): para som difuso (coerencia baixa),
         ou para completar o que a rotacao maxima nao alcancou.
      3) Mono grave opcional (Side = 0 abaixo da frequencia escolhida).

    Latencia: fftSize amostras.
*/
class PhaseCorrector
{
public:
    static constexpr int fftOrder = 12;
    static constexpr int fftSize  = 1 << fftOrder;   // 4096
    static constexpr int hopSize  = fftSize / 4;
    static constexpr int numBins  = fftSize / 2 + 1;
    static constexpr int maxBands = 128;

    enum Mode { Auto = 0, PhaseOnly = 1, SideOnly = 2 };

    struct Settings
    {
        int   mode           = Auto;
        float target         = 0.0f;   // correlacao alvo (-0.5 .. 0.8)
        float strength       = 1.0f;   // 0 .. 1
        float maxRotationDeg = 90.0f;
        float maxSideDb      = 4.0f;
        float speedMs        = 200.0f;
        bool  bassMono       = false;
        float bassFreq       = 120.0f;
        bool  delta          = false;  // ouvir so o que foi removido
        int   bandsPerOctave = 3;
    };

    PhaseCorrector();

    void prepare (double sampleRate);     // aloca (fora do audio thread ou em prepareToPlay)
    void reset() noexcept;                // zera buffers e estados (sem alocar)
    void process (float* left, float* right, int numSamples, const Settings& s) noexcept;

    static constexpr int getLatencySamples() noexcept { return fftSize; }

    // ---- medidores para a interface (escritos pelo audio thread) ----
    std::array<std::atomic<float>, maxBands> meterRotationDeg {};
    std::array<std::atomic<float>, maxBands> meterSideDb {};
    std::atomic<int> meterNumBands { 0 };
    std::atomic<int> meterBandsPerOctave { 3 };

private:
    struct BandState
    {
        int   binLo = 1, binHi = 1;
        float fCentre = 1000.0f;
        bool   correct = false;    // banda abaixo do alvo neste quadro
        bool   rotate  = false;    // usar rotacao de fase (coerencia alta)
        double keep    = 0.0;      // angulo residual permitido (rad) para chegar ao alvo
        double sideDb  = 0.0;      // reducao de Side aplicada (dB), suavizada
        double feedbackDb = 0.0;   // ajuste fino em malha fechada (dB, <= 0)
        double outXY = 0, outXX = 0, outYY = 0;   // estatisticas da saida
        int    sign = 1;           // lado da rotacao quando a fase esta perto de 180 graus
    };

    struct Table
    {
        int bandsPerOctave = 3;
        std::vector<BandState> bands;
        std::vector<int> binOwner;           // banda de cada bin (-1 = fora)
    };

    void buildTable (Table& t, int bpo);
    void processFrame (const Settings& s) noexcept;

    double sampleRate = 48000.0;
    Table tables[2];
    int   current = 0;

    juce::dsp::FFT fft { fftOrder };
    std::vector<float> window;
    std::vector<float> inL, inR, outL, outR;
    std::vector<float> specL, specR, origL, origR;
    std::vector<float> bassGain;
    float lastBassFreq = -1.0f;
    bool  lastBassMono = false;

    // estatisticas por bin (medias no tempo) e rotacao por bin
    std::vector<double> avgRe, avgIm, avgXX, avgYY, binTheta;
    std::vector<float>  uRe, uIm, gSide, tmpA, tmpB;

    int pos = 0, count = 0;
};
