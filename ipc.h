// ============================================================
// ipc.h - IPC: Pipe-based timestamp generation
// Linux  : pipe(), shmget(), shmat()
// Windows: CreatePipe()
// ============================================================
#pragma once
#include <vector>
#include <string>

void generate_timestamps_ipc(
    const std::vector<int> &peak_indices,
    double segment_duration_sec,
    const std::string &output_file);
