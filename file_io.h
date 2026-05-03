// ============================================================
// file_io.h - File I/O via System Calls (Linux & Windows)
// ============================================================
#pragma once
#include <vector>
#include <string>
#include <cstdint>

using namespace std;

struct WavData
{
    int sample_rate;
    int num_channels;
    int bits_per_sample;
    vector<double> samples; // mono, normalized [-1.0, 1.0]
};

WavData read_wav_file(const std::string &path);
void write_text_file(const std::string &path, const std::string &content);
std::vector<std::vector<double>> segment_signal(const std::vector<double> &samples, int seg_size);
