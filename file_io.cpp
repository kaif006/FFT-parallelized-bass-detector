// ============================================================
// file_io.cpp - File I/O via OS System Calls
//
// Linux : open(), read(), write(), close(), mmap(), munmap()
// Windows: CreateFile(), ReadFile(), WriteFile(), CloseHandle(),
//          VirtualAlloc(), VirtualFree()
// ============================================================
#include "file_io.h"
#include <iostream>
#include <cstring>
#include <stdexcept>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <cerrno>
#endif

// ---- Minimal WAV RIFF header (first 12 bytes) ----
#pragma pack(push, 1)
struct RiffHeader
{
    char     riff[4];       // "RIFF"
    uint32_t chunk_size;
    char     wave[4];       // "WAVE"
};

// ---- Generic chunk header ----
struct ChunkHeader
{
    char     id[4];
    uint32_t size;
};

// ---- fmt sub-chunk (standard PCM, 16 bytes) ----
struct FmtChunk
{
    uint16_t audio_format;   // 1 = PCM
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
};
#pragma pack(pop)

// ============================================================
// Helper: scan the mapped buffer for the fmt and data chunks.
// Handles non-standard WAV files that contain extra chunks
// (LIST, INFO, JUNK, bext, etc.) between fmt and data.
// Returns false if either chunk is missing or the buffer is
// too small.
// ============================================================
static bool parse_wav_chunks(
    const uint8_t *buf, size_t file_size,
    FmtChunk &out_fmt,
    const uint8_t *&out_data_ptr,
    uint32_t &out_data_bytes)
{
    if (file_size < sizeof(RiffHeader))
        return false;

    const RiffHeader *riff = reinterpret_cast<const RiffHeader *>(buf);
    if (std::strncmp(riff->riff, "RIFF", 4) != 0 ||
        std::strncmp(riff->wave, "WAVE", 4) != 0)
    {
        std::cerr << "[file_io] Not a valid WAV/RIFF file." << std::endl;
        return false;
    }

    bool found_fmt  = false;
    bool found_data = false;

    // Walk chunks starting right after the 12-byte RIFF header
    size_t offset = sizeof(RiffHeader);
    while (offset + sizeof(ChunkHeader) <= file_size)
    {
        const ChunkHeader *ch =
            reinterpret_cast<const ChunkHeader *>(buf + offset);
        uint32_t ch_size = ch->size;
        size_t   data_offset = offset + sizeof(ChunkHeader);

        if (std::strncmp(ch->id, "fmt ", 4) == 0)
        {
            if (data_offset + sizeof(FmtChunk) > file_size)
            {
                std::cerr << "[file_io] fmt chunk too small." << std::endl;
                return false;
            }
            std::memcpy(&out_fmt, buf + data_offset, sizeof(FmtChunk));
            found_fmt = true;
        }
        else if (std::strncmp(ch->id, "data", 4) == 0)
        {
            out_data_ptr   = buf + data_offset;
            out_data_bytes = ch_size;
            found_data     = true;
            break; // data chunk is always last; stop here
        }

        // Chunks are word-aligned: size is padded to even bytes
        offset = data_offset + ch_size + (ch_size & 1);
    }

    if (!found_fmt)
        std::cerr << "[file_io] fmt chunk not found." << std::endl;
    if (!found_data)
        std::cerr << "[file_io] data chunk not found." << std::endl;

    return found_fmt && found_data;
}

// ============================================================
// Helper: decode raw PCM bytes -> normalized doubles,
// then downmix to mono if stereo.
// ============================================================
static void decode_samples(
    const uint8_t *data_ptr, uint32_t data_bytes,
    const FmtChunk &fmt, WavData &result)
{
    int    bytes_per_sample = fmt.bits_per_sample / 8;
    size_t total_samples    = data_bytes / bytes_per_sample;
    double scale            = (fmt.bits_per_sample == 16) ? 32768.0 : 128.0;

    result.samples.resize(total_samples);
    for (size_t i = 0; i < total_samples; ++i)
    {
        if (fmt.bits_per_sample == 16)
        {
            int16_t s;
            std::memcpy(&s, data_ptr + i * 2, 2);
            result.samples[i] = s / scale;
        }
        else
        {
            result.samples[i] =
                (static_cast<int>(data_ptr[i]) - 128) / scale;
        }
    }

    // Downmix stereo -> mono by averaging channels
    if (fmt.num_channels == 2)
    {
        size_t mono_size = total_samples / 2;
        std::vector<double> mono(mono_size);
        for (size_t i = 0; i < mono_size; ++i)
            mono[i] = (result.samples[i * 2] + result.samples[i * 2 + 1]) * 0.5;
        result.samples = std::move(mono);
    }
}

// ===========================================================
// LINUX IMPLEMENTATION
// ===========================================================
#ifndef _WIN32
WavData read_wav_file(const std::string &path)
{
    WavData result;

    // --- System call: open() ---
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0)
    {
        perror("[file_io] open() failed");
        return result;
    }

    // Get file size for mmap
    struct stat st;
    if (fstat(fd, &st) < 0)
    {
        perror("[file_io] fstat() failed");
        close(fd);
        return result;
    }
    size_t file_size = static_cast<size_t>(st.st_size);

    // --- System call: mmap() ---
    void *mapped = mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapped == MAP_FAILED)
    {
        perror("[file_io] mmap() failed");
        close(fd);
        return result;
    }

    const uint8_t *buf = static_cast<const uint8_t *>(mapped);

    // --- Parse chunks (handles extra metadata chunks) ---
    FmtChunk        fmt{};
    const uint8_t  *data_ptr   = nullptr;
    uint32_t        data_bytes  = 0;

    if (!parse_wav_chunks(buf, file_size, fmt, data_ptr, data_bytes))
    {
        munmap(mapped, file_size);
        close(fd);
        return result;
    }

    result.sample_rate    = fmt.sample_rate;
    result.num_channels   = fmt.num_channels;
    result.bits_per_sample = fmt.bits_per_sample;

    decode_samples(data_ptr, data_bytes, fmt, result);

    // --- System call: munmap() ---
    munmap(mapped, file_size);
    // --- System call: close() ---
    close(fd);

    std::cout << "[file_io][Linux] read_wav_file() used open(), mmap(), munmap(), close()" << std::endl;
    return result;
}

void write_text_file(const std::string &path, const std::string &content)
{
    // --- System call: open() with write flags ---
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
    {
        perror("[file_io] open() for write failed");
        return;
    }

    // --- System call: write() ---
    ssize_t written = write(fd, content.c_str(), content.size());
    if (written < 0)
        perror("[file_io] write() failed");

    // --- System call: close() ---
    close(fd);
    std::cout << "[file_io][Linux] write_text_file() used open(), write(), close()" << std::endl;
}

// ===========================================================
// WINDOWS IMPLEMENTATION
// ===========================================================
#else
WavData read_wav_file(const std::string &path)
{
    WavData result;

    // --- System call: CreateFile() ---
    HANDLE hFile = CreateFileA(
        path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        std::cerr << "[file_io] CreateFile() failed: " << GetLastError() << std::endl;
        return result;
    }

    DWORD file_size = GetFileSize(hFile, nullptr);

    // --- System call: VirtualAlloc() ---
    uint8_t *buf = static_cast<uint8_t *>(
        VirtualAlloc(nullptr, file_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!buf)
    {
        std::cerr << "[file_io] VirtualAlloc() failed." << std::endl;
        CloseHandle(hFile);
        return result;
    }

    DWORD bytes_read = 0;
    // --- System call: ReadFile() ---
    if (!ReadFile(hFile, buf, file_size, &bytes_read, nullptr))
    {
        std::cerr << "[file_io] ReadFile() failed: " << GetLastError() << std::endl;
        VirtualFree(buf, 0, MEM_RELEASE);
        CloseHandle(hFile);
        return result;
    }

    // --- System call: CloseHandle() ---
    CloseHandle(hFile);

    // --- Parse chunks (handles extra metadata chunks) ---
    FmtChunk        fmt{};
    const uint8_t  *data_ptr  = nullptr;
    uint32_t        data_bytes = 0;

    if (!parse_wav_chunks(buf, file_size, fmt, data_ptr, data_bytes))
    {
        VirtualFree(buf, 0, MEM_RELEASE);
        return result;
    }

    result.sample_rate     = fmt.sample_rate;
    result.num_channels    = fmt.num_channels;
    result.bits_per_sample = fmt.bits_per_sample;

    decode_samples(data_ptr, data_bytes, fmt, result);

    // --- System call: VirtualFree() ---
    VirtualFree(buf, 0, MEM_RELEASE);
    std::cout << "[file_io][Windows] CreateFile(), ReadFile(), VirtualAlloc(), VirtualFree(), CloseHandle()" << std::endl;
    return result;
}

void write_text_file(const std::string &path, const std::string &content)
{
    // --- System call: CreateFile() ---
    HANDLE hFile = CreateFileA(
        path.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        std::cerr << "[file_io] CreateFile() for write failed: " << GetLastError() << std::endl;
        return;
    }
    DWORD written = 0;
    // --- System call: WriteFile() ---
    WriteFile(hFile, content.c_str(), static_cast<DWORD>(content.size()), &written, nullptr);
    // --- System call: CloseHandle() ---
    CloseHandle(hFile);
    std::cout << "[file_io][Windows] CreateFile(), WriteFile(), CloseHandle()" << std::endl;
}
#endif // _WIN32

// ---- Platform-independent helpers ----
std::vector<std::vector<double>> segment_signal(
    const std::vector<double> &samples, int seg_size)
{
    std::vector<std::vector<double>> segments;
    size_t n = samples.size();
    for (size_t i = 0; i + seg_size <= n; i += seg_size)
        segments.emplace_back(samples.begin() + i, samples.begin() + i + seg_size);
    return segments;
}