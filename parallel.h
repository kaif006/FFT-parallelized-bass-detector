// ============================================================
// parallel.h - Parallel FFT using OS Process Primitives
// Linux  : fork(), exec(), wait()
// Windows: CreateProcess(), WaitForSingleObject()
// ============================================================
#pragma once
#include "fft.h"
#include <vector>

std::vector<CVector> parallel_fft(
    const std::vector<std::vector<double>> &segments,
    int sample_rate);
