#include "PhaseCorrector.h"
#include <cmath>

namespace
{
    constexpr double pi    = 3.14159265358979323846;
    constexpr double twoPi = 2.0 * pi;
    constexpr double tiny  = 1.0e-20;

    // janela sqrt-Hann (analise e sintese): soma da janela = N * 2/pi
    // seno de amplitude A em fundo de escala -> |X| = A * N / pi
    constexpr double refMag = PhaseCorrector::fftSize / pi;
    constexpr double refPow = refMag * refMag;
    constexpr double gateDb = -75.0;

    // overlap-add de Hann (sqrt * sqrt) com hop N/4 soma 2 -> escala 0.5
    constexpr float olaScale = 0.5f;
}

PhaseCorrector::PhaseCorrector()
{
    window.resize (fftSize);
    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = std::sqrt (0.5f - 0.5f * std::cos ((float) twoPi * (float) i / (float) fftSize));

    for (auto* v : { &inL, &inR, &outL, &outR })
        v->assign (fftSize, 0.0f);
    for (auto* v : { &specL, &specR, &origL, &origR })
        v->assign (fftSize * 2, 0.0f);
    bassGain.assign (numBins, 1.0f);
    for (auto* v : { &avgRe, &avgIm, &avgXX, &avgYY, &binTheta })
        v->assign (numBins, 0.0);
    for (auto* v : { &uRe, &uIm, &gSide, &tmpA, &tmpB })
        v->assign (numBins, 0.0f);

    for (auto& m : meterRotationDeg) m.store (0.0f);
    for (auto& m : meterSideDb)      m.store (0.0f);
}

//==============================================================================
void PhaseCorrector::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    buildTable (tables[0], 3);
    buildTable (tables[1], 6);
    lastBassFreq = -1.0f;
    reset();
}

void PhaseCorrector::buildTable (Table& t, int bpo)
{
    t.bandsPerOctave = bpo;
    t.bands.clear();

    const double binHz = sampleRate / fftSize;
    for (const auto& e : makeBandLayout (sampleRate, bpo))
    {
        if ((int) t.bands.size() >= maxBands) break;

        BandState b;
        b.fCentre = e.fCentre;
        b.binLo = (int) std::ceil  (e.fLo / binHz);
        b.binHi = (int) std::floor (e.fHi / binHz);
        if (b.binHi < b.binLo)
            b.binLo = b.binHi = (int) std::lround (e.fCentre / binHz);
        b.binLo = juce::jlimit (1, numBins - 1, b.binLo);
        b.binHi = juce::jlimit (b.binLo, numBins - 1, b.binHi);
        t.bands.push_back (b);
    }

    // banda "dona" de cada bin
    t.binOwner.assign (numBins, -1);
    for (int bi = 0; bi < (int) t.bands.size(); ++bi)
    {
        const auto& b = t.bands[(size_t) bi];
        for (int k = b.binLo; k <= b.binHi; ++k)
            if (t.binOwner[(size_t) k] < 0)
                t.binOwner[(size_t) k] = bi;
    }
}

void PhaseCorrector::reset() noexcept
{
    for (auto* v : { &inL, &inR, &outL, &outR })
        std::fill (v->begin(), v->end(), 0.0f);
    pos = count = 0;

    for (auto& t : tables)
        for (auto& b : t.bands)
        {
            b.correct = b.rotate = false;
            b.keep = b.sideDb = b.feedbackDb = 0.0;
            b.outXY = b.outXX = b.outYY = 0.0;
            b.sign = 1;
        }
    for (auto* v : { &avgRe, &avgIm, &avgXX, &avgYY, &binTheta })
        std::fill (v->begin(), v->end(), 0.0);

    for (auto& m : meterRotationDeg) m.store (0.0f);
    for (auto& m : meterSideDb)      m.store (0.0f);
}

//==============================================================================
void PhaseCorrector::process (float* left, float* right, int numSamples, const Settings& s) noexcept
{
    const int wanted = (s.bandsPerOctave >= 6) ? 1 : 0;
    if (wanted != current)
    {
        current = wanted;
        for (auto& b : tables[current].bands)
        {
            b.correct = b.rotate = false;
            b.keep = b.sideDb = b.feedbackDb = 0.0;
            b.outXY = b.outXX = b.outYY = 0.0;
        }
    }

    if (s.bassMono != lastBassMono || std::abs (s.bassFreq - lastBassFreq) > 0.01f)
    {
        lastBassMono = s.bassMono;
        lastBassFreq = s.bassFreq;
        const double binHz = sampleRate / fftSize;
        const double f0 = s.bassFreq, f1 = s.bassFreq * 1.26;   // transicao de 1/3 de oitava
        for (int k = 0; k < numBins; ++k)
        {
            const double f = k * binHz;
            float g = 1.0f;
            if (s.bassMono)
                g = f <= f0 ? 0.0f : (f >= f1 ? 1.0f : (float) ((f - f0) / (f1 - f0)));
            bassGain[(size_t) k] = g;
        }
    }

    const int mask = fftSize - 1;
    for (int i = 0; i < numSamples; ++i)
    {
        const float xl = left[i], xr = right[i];   // le os dois antes de escrever (L e R podem ser o mesmo buffer)

        inL[(size_t) pos] = xl;
        inR[(size_t) pos] = xr;

        left[i]  = outL[(size_t) pos];
        right[i] = outR[(size_t) pos];
        outL[(size_t) pos] = 0.0f;
        outR[(size_t) pos] = 0.0f;

        pos = (pos + 1) & mask;

        if (++count >= hopSize)
        {
            count = 0;
            processFrame (s);
        }
    }
}

//==============================================================================
namespace
{
    // suavizacao 1/4-1/2-1/4 entre bins vizinhos
    inline void smooth3 (const std::vector<float>& in, std::vector<float>& out, int n) noexcept
    {
        out[0] = in[0];
        out[(size_t) n - 1] = in[(size_t) n - 1];
        for (int k = 1; k < n - 1; ++k)
            out[(size_t) k] = 0.25f * in[(size_t) k - 1] + 0.5f * in[(size_t) k] + 0.25f * in[(size_t) k + 1];
    }
}

void PhaseCorrector::processFrame (const Settings& s) noexcept
{
    auto& t = tables[current];
    const int nb = (int) t.bands.size();
    const int mask = fftSize - 1;

    // ---- quadro com janela (mais antigo em pos) ----
    for (int j = 0; j < fftSize; ++j)
    {
        const size_t idx = (size_t) ((pos + j) & mask);
        specL[(size_t) j] = inL[idx] * window[(size_t) j];
        specR[(size_t) j] = inR[idx] * window[(size_t) j];
    }
    std::fill (specL.begin() + fftSize, specL.end(), 0.0f);
    std::fill (specR.begin() + fftSize, specR.end(), 0.0f);

    fft.performRealOnlyForwardTransform (specL.data(), true);
    fft.performRealOnlyForwardTransform (specR.data(), true);

    if (s.delta)
    {
        std::copy (specL.begin(), specL.begin() + 2 * numBins, origL.begin());
        std::copy (specR.begin(), specR.begin() + 2 * numBins, origR.begin());
    }

    const double hopSec  = hopSize / sampleRate;
    const double detectA = std::exp (-hopSec / std::max (0.02, s.speedMs / 1000.0));
    const double smoothA = std::exp (-hopSec / std::max (0.01, 0.5 * s.speedMs / 1000.0));
    const double target  = juce::jlimit (-0.9, 0.95, (double) s.target);
    const double maxRot  = juce::degreesToRadians ((double) s.maxRotationDeg);

    // ---- 1) espectro cruzado medio por bin ----
    for (int k = 1; k < numBins; ++k)
    {
        const double lr = specL[(size_t) (2 * k)], li = specL[(size_t) (2 * k + 1)];
        const double rr = specR[(size_t) (2 * k)], ri = specR[(size_t) (2 * k + 1)];
        const size_t kk = (size_t) k;
        avgRe[kk] = detectA * avgRe[kk] + (1.0 - detectA) * (lr * rr + li * ri);   // L * conj(R)
        avgIm[kk] = detectA * avgIm[kk] + (1.0 - detectA) * (li * rr - lr * ri);
        avgXX[kk] = detectA * avgXX[kk] + (1.0 - detectA) * (lr * lr + li * li);
        avgYY[kk] = detectA * avgYY[kk] + (1.0 - detectA) * (rr * rr + ri * ri);
    }

    // ---- 2) diagnostico por banda ----
    for (auto& b : t.bands)
    {
        double xyRe = 0, xyIm = 0, xx = 0, yy = 0;
        for (int k = b.binLo; k <= b.binHi; ++k)
        {
            xyRe += avgRe[(size_t) k]; xyIm += avgIm[(size_t) k];
            xx   += avgXX[(size_t) k]; yy   += avgYY[(size_t) k];
        }

        b.correct = false; b.rotate = false; b.keep = 0.0;

        const double level = 10.0 * std::log10 (0.5 * (xx + yy) / refPow + tiny);
        if (level <= gateDb) continue;

        const double norm = std::sqrt (xx * yy) + tiny;
        const double r = xyRe / norm;
        const double c = std::sqrt (xyRe * xyRe + xyIm * xyIm) / norm;

        // lado preferido da rotacao (com histerese perto de 180 graus)
        const double phiB = std::atan2 (xyIm, xyRe);
        if (std::abs (phiB) < 0.85 * pi)
            b.sign = phiB >= 0.0 ? 1 : -1;

        if (r < target)
        {
            b.correct = true;
            b.rotate  = (s.mode == PhaseOnly) || (s.mode == Auto && c >= 0.5);
            // angulo residual que ainda deixa a banda no alvo: c cos(keep) = alvo
            b.keep    = (c > target) ? std::acos (juce::jlimit (-1.0, 1.0, target / c)) : 0.0;
        }
    }

    // ---- 3) rotacao por bin (fase medida em cada bin, decisao da banda) ----
    for (int k = 1; k < numBins; ++k)
    {
        const int owner = t.binOwner[(size_t) k];
        double thetaT = 0.0;
        double& th = binTheta[(size_t) k];

        if (owner >= 0)
        {
            const auto& b = t.bands[(size_t) owner];
            if (b.correct && b.rotate)
            {
                const int k0 = std::max (1, k - 1), k1 = std::min (numBins - 1, k + 1);
                const double cr = 0.25 * avgRe[(size_t) k0] + 0.5 * avgRe[(size_t) k] + 0.25 * avgRe[(size_t) k1];
                const double ci = 0.25 * avgIm[(size_t) k0] + 0.5 * avgIm[(size_t) k] + 0.25 * avgIm[(size_t) k1];
                double phi = std::atan2 (ci, cr);

                // perto de 180 graus o sinal de phi oscila de bin para bin:
                // todos os bins da banda giram para o mesmo lado
                if (std::abs (phi) > 0.85 * pi && ((phi > 0.0) != (b.sign > 0)))
                    phi += (phi > 0.0 ? -twoPi : twoPi);

                const double need = std::max (0.0, std::abs (phi) - b.keep);
                thetaT = (phi >= 0.0 ? 1.0 : -1.0) * std::min (need * s.strength, maxRot);
            }
        }

        if (thetaT - th >  pi) thetaT -= twoPi;
        if (thetaT - th < -pi) thetaT += twoPi;
        th = smoothA * th + (1.0 - smoothA) * thetaT;
        if (th >  pi) th -= twoPi;
        if (th < -pi) th += twoPi;

        uRe[(size_t) k] = (float) std::cos (0.5 * th);
        uIm[(size_t) k] = (float) std::sin (0.5 * th);
    }
    uRe[0] = 1.0f; uIm[0] = 0.0f;

    // ---- 4) reducao de Side por banda (o que a rotacao nao resolveu) ----
    for (int bi = 0; bi < nb; ++bi)
    {
        auto& b = t.bands[(size_t) bi];
        double sideT = 0.0, rotW = 0.0, powW = 0.0;

        double re2 = 0.0, sumP = 0.0;
        for (int k = b.binLo; k <= b.binHi; ++k)
        {
            const size_t kk = (size_t) k;
            const double mag = std::sqrt (avgRe[kk] * avgRe[kk] + avgIm[kk] * avgIm[kk]);
            const double phi = std::atan2 (avgIm[kk], avgRe[kk]);
            re2  += mag * std::cos (phi - binTheta[kk]);
            const double p = avgXX[kk] + avgYY[kk];
            sumP += p;
            rotW += std::abs (binTheta[kk]) * p;
            powW += p;
        }

        if (b.correct && s.mode != PhaseOnly)
        {
            const double m2 = std::max (0.0, (sumP + 2.0 * re2) * 0.25);
            const double s2 = std::max (0.0, (sumP - 2.0 * re2) * 0.25);
            const double r2 = (m2 - s2) / (m2 + s2 + tiny);
            if (r2 < target && s2 > tiny)
            {
                const double g2 = m2 * (1.0 - target) / ((1.0 + target) * s2);
                const double g  = std::sqrt (juce::jlimit (1.0e-8, 1.0, g2));
                sideT = 20.0 * std::log10 (g) * s.strength;
            }

            // ajuste fino em malha fechada: mede a correlacao da saida desta banda
            const double rOut = b.outXY / (std::sqrt (b.outXX * b.outYY) + tiny);
            const double step = 0.15 * s.strength;              // dB por quadro
            if (rOut < target - 0.02)      b.feedbackDb -= step;
            else if (rOut > target + 0.08) b.feedbackDb += step;
            b.feedbackDb = juce::jlimit (-(double) s.maxSideDb, 0.0, b.feedbackDb);
        }
        else
        {
            b.feedbackDb = std::min (0.0, b.feedbackDb + 0.3);  // solta devagar
        }

        sideT = std::max (-(double) s.maxSideDb, sideT + b.feedbackDb);
        b.sideDb = smoothA * b.sideDb + (1.0 - smoothA) * sideT;

        meterRotationDeg[(size_t) bi].store ((float) juce::radiansToDegrees (powW > tiny ? rotW / powW : 0.0));
        meterSideDb[(size_t) bi].store ((float) b.sideDb);
    }
    meterNumBands.store (nb);
    meterBandsPerOctave.store (t.bandsPerOctave);

    // ganho de Side por bin (da banda dona), suavizado entre bins, vezes o mono grave
    for (int k = 0; k < numBins; ++k)
    {
        const int owner = t.binOwner[(size_t) k];
        gSide[(size_t) k] = owner >= 0 ? (float) std::pow (10.0, t.bands[(size_t) owner].sideDb / 20.0) : 1.0f;
    }
    smooth3 (gSide, tmpA, numBins);
    smooth3 (uRe, tmpB, numBins);  std::swap (uRe, tmpB);
    smooth3 (uIm, tmpB, numBins);  std::swap (uIm, tmpB);

    // ---- 5) aplica ----
    for (int k = 0; k < numBins; ++k)
    {
        float ur = uRe[(size_t) k], ui = uIm[(size_t) k];
        const float un = std::sqrt (ur * ur + ui * ui);
        if (un > 1.0e-4f) { ur /= un; ui /= un; } else { ur = 1.0f; ui = 0.0f; }

        const float gS = tmpA[(size_t) k] * bassGain[(size_t) k];

        const size_t re = (size_t) (2 * k), im = re + 1;
        const float lr = specL[re], li = specL[im], rr = specR[re], ri = specR[im];

        // L' = L * conj(u) ;  R' = R * u     (|L| e |R| nao mudam)
        const float l2r = lr * ur + li * ui, l2i = li * ur - lr * ui;
        const float r2r = rr * ur - ri * ui, r2i = ri * ur + rr * ui;

        const float mr = 0.5f * (l2r + r2r), mi = 0.5f * (l2i + r2i);
        const float sr = 0.5f * (l2r - r2r) * gS, si = 0.5f * (l2i - r2i) * gS;

        specL[re] = mr + sr;  specL[im] = mi + si;
        specR[re] = mr - sr;  specR[im] = mi - si;
    }

    // estatisticas da saida por banda (para o ajuste fino)
    for (auto& b : t.bands)
    {
        double xy = 0, xx = 0, yy = 0;
        for (int k = b.binLo; k <= b.binHi; ++k)
        {
            const double lr = specL[(size_t) (2 * k)], li = specL[(size_t) (2 * k + 1)];
            const double rr = specR[(size_t) (2 * k)], ri = specR[(size_t) (2 * k + 1)];
            xy += lr * rr + li * ri;  xx += lr * lr + li * li;  yy += rr * rr + ri * ri;
        }
        b.outXY = detectA * b.outXY + (1.0 - detectA) * xy;
        b.outXX = detectA * b.outXX + (1.0 - detectA) * xx;
        b.outYY = detectA * b.outYY + (1.0 - detectA) * yy;
    }

    if (s.delta)
    {
        for (int k = 0; k < 2 * numBins; ++k)
        {
            specL[(size_t) k] = origL[(size_t) k] - specL[(size_t) k];
            specR[(size_t) k] = origR[(size_t) k] - specR[(size_t) k];
        }
    }

    fft.performRealOnlyInverseTransform (specL.data());
    fft.performRealOnlyInverseTransform (specR.data());

    for (int j = 0; j < fftSize; ++j)
    {
        const size_t idx = (size_t) ((pos + j) & mask);
        const float w = window[(size_t) j] * olaScale;
        outL[idx] += specL[(size_t) j] * w;
        outR[idx] += specR[(size_t) j] * w;
    }
}
