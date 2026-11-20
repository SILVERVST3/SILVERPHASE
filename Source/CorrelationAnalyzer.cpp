#include "CorrelationAnalyzer.h"
#include <cmath>
#include <algorithm>

namespace
{
    constexpr double tiny = 1.0e-20;

    // Janela Hann: seno de amplitude A em fundo de escala -> |X| ~ A * N/4
    constexpr double refMag = CorrelationAnalyzer::fftSize / 4.0;
    constexpr double refPow = refMag * refMag;

    inline float powToDb (double p)
    {
        return (float) (10.0 * std::log10 (p / refPow + tiny));
    }
}

CorrelationAnalyzer::CorrelationAnalyzer()
{
    fifoL.assign (fifoCapacity, 0.0f);
    fifoR.assign (fifoCapacity, 0.0f);
    histL.assign (fftSize, 0.0f);
    histR.assign (fftSize, 0.0f);
    bufL.assign (fftSize * 2, 0.0f);
    bufR.assign (fftSize * 2, 0.0f);

    window.resize (fftSize);
    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) fftSize);
}

//==============================================================================
void CorrelationAnalyzer::prepare (double sampleRate) noexcept
{
    if (sampleRate > 0.0)
        pendingSampleRate.store (sampleRate);
}

void CorrelationAnalyzer::pushSamples (const float* left, const float* right, int numSamples) noexcept
{
    int s1, n1, s2, n2;
    fifo.prepareToWrite (numSamples, s1, n1, s2, n2);

    if (n1 > 0)
    {
        std::copy (left,  left  + n1, fifoL.begin() + s1);
        std::copy (right, right + n1, fifoR.begin() + s1);
    }
    if (n2 > 0)
    {
        std::copy (left  + n1, left  + n1 + n2, fifoL.begin() + s2);
        std::copy (right + n1, right + n1 + n2, fifoR.begin() + s2);
    }

    fifo.finishedWrite (n1 + n2);   // se a FIFO encher, o excesso e descartado
}

//==============================================================================
void CorrelationAnalyzer::setBandsPerOctave (int bpo)
{
    bandsPerOctave = juce::jlimit (1, 12, bpo);
}

void CorrelationAnalyzer::setResponseMs (float ms)
{
    ms = juce::jlimit (20.0f, 10000.0f, ms);
    if (std::abs (ms - responseMs) > 0.01f)
    {
        responseMs = ms;
        updateAlpha();
    }
}

void CorrelationAnalyzer::updateAlpha()
{
    if (currentSampleRate <= 0.0)
        return;

    const double hopSeconds = hopSize / currentSampleRate;
    const double tau = responseMs / 1000.0;
    alpha = std::exp (-hopSeconds / std::max (tau, 1.0e-3));

    // o quadro da FFT cobre 4 hops + o tempo de resposta da media:
    // so depois disso a leitura entra no "pior valor" (evita falsos negativos
    // quando um som comeca de repente e o transiente vaza para outras bandas)
    settleFrames = (fftSize / hopSize) + (int) std::ceil (3.0 * tau / hopSeconds);
}

void CorrelationAnalyzer::clearHold()
{
    for (auto& b : bands)
    {
        const bool settled = b.active && b.activeFrames >= settleFrames;
        b.minCorr = settled ? b.corr : 1.0f;
        b.everActive = settled;
    }
    const bool gSettled = globalActive && globalActiveFrames >= settleFrames;
    globalMin = gSettled ? globalCorr : 1.0f;
    globalEverActive = gSettled;
}

void CorrelationAnalyzer::rebuildIfNeeded()
{
    const double sr = pendingSampleRate.load();
    if (std::abs (sr - currentSampleRate) > 0.5 || bandsPerOctave != builtBandsPerOctave)
    {
        currentSampleRate = sr;
        builtBandsPerOctave = bandsPerOctave;
        rebuildBands();
        updateAlpha();
    }
}

void CorrelationAnalyzer::rebuildBands()
{
    bands.clear();
    gXY = gXX = gYY = 0.0;
    globalCorr = globalMin = 1.0f;
    globalActive = globalEverActive = false;

    const double binHz   = currentSampleRate / fftSize;
    const double nyquist = currentSampleRate * 0.5;
    const double topF    = std::min (20000.0, nyquist * 0.95);
    const int    bpo     = builtBandsPerOctave;
    const double halfStep = std::pow (2.0, 1.0 / (2.0 * bpo));

    // centros referenciados a 1 kHz (padrao ISO para 1/3 de oitava)
    const int nMin = (int) std::ceil  (bpo * std::log2 (20.0 / 1000.0));
    const int nMax = (int) std::floor (bpo * std::log2 (topF / 1000.0));

    for (int n = nMin; n <= nMax; ++n)
    {
        BandData b;
        const double fc = 1000.0 * std::pow (2.0, (double) n / bpo);
        b.fCentre = (float) fc;
        b.fLo     = (float) (fc / halfStep);
        b.fHi     = (float) std::min (fc * halfStep, nyquist);

        b.binLo = (int) std::ceil  (b.fLo / binHz);
        b.binHi = (int) std::floor (b.fHi / binHz);
        if (b.binHi < b.binLo)                  // banda mais estreita que um bin
            b.binLo = b.binHi = (int) std::lround (fc / binHz);

        b.binLo = juce::jlimit (1, numBins - 1, b.binLo);
        b.binHi = juce::jlimit (b.binLo, numBins - 1, b.binHi);
        bands.push_back (b);
    }
}

//==============================================================================
bool CorrelationAnalyzer::process()
{
    rebuildIfNeeded();

    bool newFrame = false;
    int available = fifo.getNumReady();

    while (available > 0)
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (std::min (available, 4096), s1, n1, s2, n2);

        if (n1 > 0) consume (fifoL.data() + s1, fifoR.data() + s1, n1, newFrame);
        if (n2 > 0) consume (fifoL.data() + s2, fifoR.data() + s2, n2, newFrame);

        fifo.finishedRead (n1 + n2);
        available -= (n1 + n2);

        if (n1 + n2 == 0)
            break;
    }

    return newFrame && ! frozen;
}

void CorrelationAnalyzer::consume (const float* l, const float* r, int n, bool& newFrame)
{
    for (int i = 0; i < n; ++i)
    {
        histL[(size_t) writePos] = l[i];
        histR[(size_t) writePos] = r[i];
        writePos = (writePos + 1) & (fftSize - 1);

        if (++samplesSinceHop >= hopSize)
        {
            samplesSinceHop = 0;
            if (! frozen)
            {
                analyseFrame();
                newFrame = true;
            }
        }
    }
}

void CorrelationAnalyzer::analyseFrame()
{
    if (bands.empty())
        return;

    // monta o quadro (mais antigo -> mais novo) com janela
    for (int i = 0; i < fftSize; ++i)
    {
        const int idx = (writePos + i) & (fftSize - 1);
        bufL[(size_t) i] = histL[(size_t) idx] * window[(size_t) i];
        bufR[(size_t) i] = histR[(size_t) idx] * window[(size_t) i];
    }
    std::fill (bufL.begin() + fftSize, bufL.end(), 0.0f);
    std::fill (bufR.begin() + fftSize, bufR.end(), 0.0f);

    fft.performRealOnlyForwardTransform (bufL.data(), true);
    fft.performRealOnlyForwardTransform (bufR.data(), true);

    const double a = alpha, oneMinusA = 1.0 - alpha;
    double tXY = 0, tXX = 0, tYY = 0;

    for (auto& b : bands)
    {
        double sxy = 0, sxx = 0, syy = 0;

        for (int k = b.binLo; k <= b.binHi; ++k)
        {
            const double lr = bufL[(size_t) (2 * k)], li = bufL[(size_t) (2 * k + 1)];
            const double rr = bufR[(size_t) (2 * k)], ri = bufR[(size_t) (2 * k + 1)];

            sxy += lr * rr + li * ri;
            sxx += lr * lr + li * li;
            syy += rr * rr + ri * ri;
        }

        tXY += sxy; tXX += sxx; tYY += syy;

        b.avgXY = a * b.avgXY + oneMinusA * sxy;
        b.avgXX = a * b.avgXX + oneMinusA * sxx;
        b.avgYY = a * b.avgYY + oneMinusA * syy;

        const double sumPow = b.avgXX + b.avgYY;
        b.levelDb = powToDb (0.5 * sumPow);
        b.midDb   = powToDb (std::max (0.0, (sumPow + 2.0 * b.avgXY) * 0.25));
        b.sideDb  = powToDb (std::max (0.0, (sumPow - 2.0 * b.avgXY) * 0.25));
        b.active  = b.levelDb > gateDb;

        if (b.active)
        {
            b.corr = (float) juce::jlimit (-1.0, 1.0, b.avgXY / std::sqrt (b.avgXX * b.avgYY + tiny));
            const double ratio = (sumPow + 2.0 * b.avgXY) / (2.0 * sumPow + tiny);
            b.monoLossDb = (float) std::max (-60.0, 10.0 * std::log10 (std::max (ratio, 1.0e-6)));

            if (b.activeFrames < settleFrames)
                ++b.activeFrames;
            else if (! b.everActive) { b.minCorr = b.corr; b.everActive = true; }
            else                       b.minCorr = std::min (b.minCorr, b.corr);
        }
        else
        {
            b.activeFrames = 0;
            b.corr = 1.0f;
            b.monoLossDb = 0.0f;
        }
    }

    // correlacao global (20 Hz - 20 kHz)
    gXY = a * gXY + oneMinusA * tXY;
    gXX = a * gXX + oneMinusA * tXX;
    gYY = a * gYY + oneMinusA * tYY;

    globalActive = powToDb (0.5 * (gXX + gYY)) > gateDb;
    if (globalActive)
    {
        globalCorr = (float) juce::jlimit (-1.0, 1.0, gXY / std::sqrt (gXX * gYY + tiny));
        const double ratio = (gXX + gYY + 2.0 * gXY) / (2.0 * (gXX + gYY) + tiny);
        globalMonoLossDb = (float) std::max (-60.0, 10.0 * std::log10 (std::max (ratio, 1.0e-6)));

        if (globalActiveFrames < settleFrames)
            ++globalActiveFrames;
        else if (! globalEverActive) { globalMin = globalCorr; globalEverActive = true; }
        else                           globalMin = std::min (globalMin, globalCorr);
    }
    else
    {
        globalActiveFrames = 0;
        globalCorr = 1.0f;
        globalMonoLossDb = 0.0f;
    }
}
