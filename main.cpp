// ============================================================
// main.cpp - Entry Point
// CT-353 Operating Systems: CCP Project
// Parallelized FFT-Based Bass Frequency Detection System
// ============================================================
#include <iostream>
#include <string>
#include "file_io.h"
#include "fft.h"
#include "parallel.h"
#include "ipc.h"
#include "peak.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/time.h>
#endif

using namespace std;

// ---- Cross-platform timer ----
double get_time_seconds()
{
#ifdef _WIN32
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart / freq.QuadPart;
#else
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return tv.tv_sec + tv.tv_usec * 1e-6;
#endif
}

int main(int argc, char *argv[])
{
    std::cout << "=== Parallelized FFT-Based Bass Frequency Detection ===" << endl;
    const char *input_file = (argc > 1) ? argv[1] : "input.wav";
    const char *output_file = (argc > 2) ? argv[2] : "timestamps.txt";

    // ---- Step 1: Read WAV file via system calls ----
    cout << "[1] Reading WAV file: " << input_file << endl;
    WavData wav = read_wav_file(input_file);
    if (wav.samples.empty())
    {
        cerr << "ERROR: Failed to read WAV file." << endl;
        return 1;
    }
    cout << " Sample rate : " << wav.sample_rate << " Hz" << endl;
    cout << " Total samples: " << wav.samples.size() << endl;

    // ---- Step 2: Segment signal ----
    const int SEGMENT_SIZE = 4096;
    cout << "[2] Segmenting signal (segment size = " << SEGMENT_SIZE << ")" << endl;
    vector<std::vector<double>> segments = segment_signal(wav.samples, SEGMENT_SIZE);
    cout << " Segments: " << segments.size() << endl;

    // ---- Step 3 & 4: Apply windowing + Parallel FFT ----
    cout << "[3] Applying Hann window to each segment..." << endl;
    for (auto &seg : segments)
        apply_hann_window(seg);

    cout << "[4] Running parallel FFT on all segments..." << endl;
    double t_start = get_time_seconds();
    auto fft_results = parallel_fft(segments, wav.sample_rate);
    double t_end = get_time_seconds();
    cout << " FFT completed in " << (t_end - t_start) << " seconds." << endl;

    // ---- Step 5 & 6: Extract bass frequencies + Detect peaks ----
    cout << "[5] Extracting bass energy (20-250 Hz)..." << endl;
    auto bass_energy = extract_bass_energy(fft_results, wav.sample_rate, SEGMENT_SIZE);
    std::cout << "[6] Detecting peaks..." << endl;
    auto peaks = detect_peaks(bass_energy);
    cout << " Bass peaks detected: " << peaks.size() << endl;

    // ---- Step 7: Generate timestamps via IPC ----
    cout << "[7] Generating timestamps using IPC..." << endl;
    double seg_duration = (double)SEGMENT_SIZE / wav.sample_rate;
    generate_timestamps_ipc(peaks, seg_duration, output_file);

    cout << "\n=== Done. Timestamps written to: " << output_file << " ===" << endl;
    return 0;
}
