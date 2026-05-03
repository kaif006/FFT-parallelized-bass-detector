// ============================================================
// peak.cpp - Adaptive Threshold Bass Peak Detection
// ============================================================
#include "peak.h"
#include <numeric>
#include <cmath>
#include <iostream>

std::vector<int> detect_peaks(const std::vector<double> &energy)
{
    if (energy.empty())
        return {};

    // Compute mean and standard deviation
    double mean = std::accumulate(energy.begin(), energy.end(), 0.0) / energy.size();
    double sq_sum = 0.0;
    for (double e : energy)
        sq_sum += (e - mean) * (e - mean);
    double stddev = std::sqrt(sq_sum / energy.size());

    // Threshold = mean + 1.5 * stddev (adaptive)
    double threshold = mean + 1.5 * stddev;

    std::vector<int> peaks;
    int n = static_cast<int>(energy.size());

    // Simple local maxima detection above threshold
    for (int i = 1; i < n - 1; ++i)
    {
        if (energy[i] > threshold &&
            energy[i] > energy[i - 1] &&
            energy[i] > energy[i + 1])
        {
            peaks.push_back(i);
        }
    }

    std::cout << "[peak] Threshold=" << threshold
              << " Peaks found=" << peaks.size() << std::endl;
    return peaks;
}
