// ============================================================
// fft.cpp - Cooley-Tukey FFT Implementation
// Reference: [1] FFT Algorithm - Cooley Tukey
// ============================================================
#include "fft.h"
#include <cmath>
#include <iostream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace std;

// ---- In-place iterative Cooley-Tukey FFT (radix-2 DIT) ----
void fft(CVector &a)
{
    int n = static_cast<int>(a.size());
    if (n <= 1)
        return;

    // Bit-reversal permutation
    for (int i = 1, j = 0; i < n; ++i)
    {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            swap(a[i], a[j]);
    }

    // Butterfly stages
    for (int len = 2; len <= n; len <<= 1)
    {
        double angle = -2.0 * M_PI / len;
        Complex wlen(cos(angle), sin(angle));
        for (int i = 0; i < n; i += len)
        {
            Complex w(1.0, 0.0);
            for (int j = 0; j < len / 2; ++j)
            {
                Complex u = a[i + j];
                Complex v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

// ---- Hann windowing to reduce spectral leakage ----
void apply_hann_window(vector<double> &seg)
{
    int n = static_cast<int>(seg.size());
    for (int i = 0; i < n; ++i)
    {
        double w = 0.5 * (1.0 - cos(2.0 * M_PI * i / (n - 1)));
        seg[i] *= w;
    }
}

// ---- Extract average bass energy (20-250 Hz) per segment ----
vector<double> extract_bass_energy(
    const vector<CVector> &fft_results,
    int sample_rate, int segment_size)
{
    vector<double> energies(fft_results.size(), 0.0);
    double freq_resolution = static_cast<double>(sample_rate) / segment_size;
    int bin_low = static_cast<int>(ceil(20.0 / freq_resolution));
    int bin_high = static_cast<int>(floor(250.0 / freq_resolution));
    bin_high = min(bin_high, segment_size / 2);

    for (size_t s = 0; s < fft_results.size(); ++s)
    {
        double energy = 0.0;
        for (int b = bin_low; b <= bin_high; ++b)
            energy += std::norm(fft_results[s][b]); // |X[b]|^2
        energies[s] = energy / (bin_high - bin_low + 1);
    }
    return energies;
}
