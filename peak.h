// ============================================================
// peak.h - Bass Peak Detection
// ============================================================
#pragma once
#include <vector>

// Detect peaks in bass energy array using adaptive thresholding.
// Returns indices of segments that contain a bass hit.
std::vector<int> detect_peaks(const std::vector<double> &bass_energy);
