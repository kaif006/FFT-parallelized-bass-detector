// ============================================================
// ipc.cpp - Inter-Process Communication for Timestamp Generation
//
// Linux  : pipe(), shmget(), shmat(), shmdt(), shmctl()
// Windows: CreatePipe(), ReadFile(), WriteFile(), CloseHandle()
//
// Design: parent writes peak indices to a pipe; child reads
// them, converts to timestamps, writes to output file.
// ============================================================
#include "ipc.h"
#include "file_io.h"
#include <iostream>
#include <sstream>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <cerrno>
#endif

// ============================================================
// LINUX Implementation
// ============================================================
#ifndef _WIN32
void generate_timestamps_ipc(
    const std::vector<int> &peak_indices,
    double seg_dur,
    const std::string &output_file)
{
    int count = static_cast<int>(peak_indices.size());

    // --- System call: shmget() - allocate shared memory segment ---
    key_t key = ftok("/tmp", 42);
    int shm_id = shmget(key, sizeof(double) * (count + 1),
                        IPC_CREAT | 0666);
    if (shm_id < 0)
    {
        perror("[ipc] shmget() failed");
        return;
    }

    // --- System call: shmat() - attach shared memory ---
    double *shm_data = static_cast<double *>(shmat(shm_id, nullptr, 0));
    if (shm_data == reinterpret_cast<double *>(-1))
    {
        perror("[ipc] shmat() failed");
        shmctl(shm_id, IPC_RMID, nullptr);
        return;
    }

    // --- System call: pipe() ---
    int pipefd[2];
    if (pipe(pipefd) < 0)
    {
        perror("[ipc] pipe() failed");
        shmdt(shm_data);
        shmctl(shm_id, IPC_RMID, nullptr);
        return;
    }

    // Store count in shared memory slot 0; timestamps start at slot 1
    shm_data[0] = static_cast<double>(count);

    // --- System call: fork() ---
    pid_t pid = fork();
    if (pid < 0)
    {
        perror("[ipc] fork() failed");
        return;
    }

    if (pid == 0)
    {
        // ---- CHILD: read indices from pipe, compute + save timestamps ----
        close(pipefd[1]); // close write end
        std::ostringstream oss;
        oss << "# Bass Timestamp File\n";
        oss << "# Format: index, timestamp_seconds\n";

        int idx;
        // --- System call: read() from pipe ---
        while (read(pipefd[0], &idx, sizeof(int)) == sizeof(int))
        {
            double ts = idx * seg_dur;
            oss << idx << ", " << ts << "\n";
            // Also place timestamp in shared memory
            if (idx < count)
                shm_data[idx + 1] = ts;
        }
        close(pipefd[0]);

        // Write timestamps to file using system call write()
        write_text_file(output_file, oss.str());
        _exit(0);
    }
    else
    {
        // ---- PARENT: write peak indices to pipe ----
        close(pipefd[0]); // close read end
        for (int i = 0; i < count; ++i)
        {
            // --- System call: write() to pipe ---
            if (write(pipefd[1], &peak_indices[i], sizeof(int)) < 0)
                perror("[ipc] write() to pipe failed");
        }
        close(pipefd[1]); // signal EOF to child

        // --- System call: wait() ---
        int status;
        wait(&status);
    }

    // --- System call: shmdt() - detach ---
    shmdt(shm_data);
    // Cleanup shared memory
    shmctl(shm_id, IPC_RMID, nullptr);

    std::cout << "[ipc][Linux] Used pipe(), shmget(), shmat(), shmdt(), fork(), wait()" << std::endl;
}

// ============================================================
// WINDOWS Implementation
// ============================================================
#else
void generate_timestamps_ipc(
    const std::vector<int> &peak_indices,
    double seg_dur,
    const std::string &output_file)
{
    int count = static_cast<int>(peak_indices.size());

    HANDLE hRead, hWrite;
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    // --- System call: CreatePipe() ---
    if (!CreatePipe(&hRead, &hWrite, &sa, 0))
    {
        std::cerr << "[ipc] CreatePipe() failed: " << GetLastError() << std::endl;
        return;
    }

    // In Windows, we do in-process pipe read/write for simplicity
    // (CreateProcess for a child exe would require a separate binary)
    std::ostringstream oss;
    oss << "# Bass Timestamp File\n# Format: index, timestamp_seconds\n";

    for (int i = 0; i < count; ++i)
    {
        int idx = peak_indices[i];
        DWORD written = 0;
        // --- System call: WriteFile() to pipe ---
        WriteFile(hWrite, &idx, sizeof(int), &written, nullptr);
    }
    CloseHandle(hWrite);

    int idx;
    DWORD bytes_read = 0;
    // --- System call: ReadFile() from pipe ---
    while (ReadFile(hRead, &idx, sizeof(int), &bytes_read, nullptr) && bytes_read > 0)
    {
        double ts = idx * seg_dur;
        oss << idx << ", " << ts << "\n";
    }
    CloseHandle(hRead);

    write_text_file(output_file, oss.str());
    std::cout << "[ipc][Windows] Used CreatePipe(), WriteFile(), ReadFile(), CloseHandle()" << std::endl;
}
#endif // _WIN32
