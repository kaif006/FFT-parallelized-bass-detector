// ============================================================
// fft.h - Cooley-Tukey FFT & Bass Energy Extraction
// ============================================================
#pragma once
#include <vector>
#include <complex>

using namespace std;

using Complex = complex<double>;
using CVector = vector<Complex>;

// In-place Cooley-Tukey FFT
void fft(CVector& signal);

// Apply Hann window to a segment
void apply_hann_window(vector<double>& segment);

// Extract bass energy per segment (20-250 Hz range)
vector<double> extract_bass_energy(
    const vector<CVector>& fft_results,
    int sample_rate, int segment_size);
