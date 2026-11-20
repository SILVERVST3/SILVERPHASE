#pragma once

#include <vector>
#include <cmath>
#include <algorithm>

// Bandas fracionarias de oitava (centros referenciados a 1 kHz, padrao ISO).
// Usado pelo analisador e pelo corretor, para que os indices das bandas coincidam.
struct BandEdges
{
    float fLo, fCentre, fHi;
};

inline std::vector<BandEdges> makeBandLayout (double sampleRate, int bandsPerOctave)
{
    std::vector<BandEdges> out;
    const double nyquist  = sampleRate * 0.5;
    const double topF     = std::min (20000.0, nyquist * 0.95);
    const int    bpo      = std::max (1, bandsPerOctave);
    const double halfStep = std::pow (2.0, 1.0 / (2.0 * bpo));

    const int nMin = (int) std::ceil  (bpo * std::log2 (20.0 / 1000.0));
    const int nMax = (int) std::floor (bpo * std::log2 (topF / 1000.0));

    for (int n = nMin; n <= nMax; ++n)
    {
        const double fc = 1000.0 * std::pow (2.0, (double) n / bpo);
        out.push_back ({ (float) (fc / halfStep), (float) fc, (float) std::min (fc * halfStep, nyquist) });
    }
    return out;
}
