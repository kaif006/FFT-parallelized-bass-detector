// ============================================================
// syscall_timing_test.cpp
// CT-353 CCP — Parallelized FFT-Based Bass Frequency Detection
// Benchmarks every OS system call used in the project.
//
// Build (Linux):
//   g++ -O0 -o syscall_timing_test syscall_timing_test.cpp -lpthread
// Build (Windows, MSVC):
//   cl /Od syscall_timing_test.cpp
// ============================================================

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <chrono>

#ifdef _WIN32
  #include <windows.h>
#else
  #include <fcntl.h>
  #include <unistd.h>
  #include <sys/stat.h>
  #include <sys/types.h>
  #include <sys/mman.h>
  #include <sys/ipc.h>
  #include <sys/shm.h>
  #include <sys/wait.h>
  #include <cerrno>
#endif

// ============================================================
// Timer helpers
// ============================================================
using Clock = std::chrono::high_resolution_clock;
using NS    = std::chrono::nanoseconds;

static inline long long now_ns()
{
    return std::chrono::duration_cast<NS>(
               Clock::now().time_since_epoch()).count();
}

// ============================================================
// Result row
// ============================================================
struct Result
{
    std::string category;
    std::string syscall_name;
    double      time_us;   // microseconds
    bool        ok;
    std::string note;
};

static std::vector<Result> results;

static void record(const std::string &cat,
                   const std::string &call,
                   long long          elapsed_ns,
                   bool               ok,
                   const std::string &note = "")
{
    results.push_back({cat, call, elapsed_ns / 1000.0, ok, note});
}

// ============================================================
// Print final table
// ============================================================
static void print_table()
{
    const int W_CAT    = 14;
    const int W_CALL   = 26;
    const int W_TIME   = 14;
    const int W_STATUS =  8;
    const int W_NOTE   = 32;

    auto sep = [&]() {
        std::cout << std::string(W_CAT + W_CALL + W_TIME + W_STATUS + W_NOTE + 5, '-') << "\n";
    };

    std::cout << "\n";
    sep();
    std::cout
        << std::left
        << std::setw(W_CAT)    << "Category"   << " "
        << std::setw(W_CALL)   << "Syscall"     << " "
        << std::setw(W_TIME)   << "Time (us)"   << " "
        << std::setw(W_STATUS) << "Status"      << " "
        << std::setw(W_NOTE)   << "Note"        << "\n";
    sep();

    for (auto &r : results)
    {
        std::cout
            << std::left
            << std::setw(W_CAT)    << r.category     << " "
            << std::setw(W_CALL)   << r.syscall_name  << " "
            << std::fixed << std::setprecision(3)
            << std::setw(W_TIME)   << r.time_us       << " "
            << std::setw(W_STATUS) << (r.ok ? "OK" : "FAIL") << " "
            << std::setw(W_NOTE)   << r.note          << "\n";
    }
    sep();
    std::cout << "\n";
}

// ============================================================
// Temp file helpers
// ============================================================
static const char *TMP_FILE = "syscall_test_tmp.bin";

static void cleanup_tmp() { std::remove(TMP_FILE); }

static void make_tmp_file(size_t bytes = 4096)
{
    // Create the file with some dummy content using stdlib so the
    // individual syscall timings aren't polluted by first-use effects.
    FILE *f = std::fopen(TMP_FILE, "wb");
    if (f) {
        std::vector<char> buf(bytes, 0x5A);
        std::fwrite(buf.data(), 1, bytes, f);
        std::fclose(f);
    }
}

// ============================================================
//  LINUX BENCHMARKS
// ============================================================
#ifndef _WIN32

// ---------- File I/O ----------

static void bench_open()
{
    make_tmp_file();
    long long t0 = now_ns();
    int fd = open(TMP_FILE, O_RDONLY);
    long long t1 = now_ns();
    bool ok = (fd >= 0);
    if (ok) close(fd);
    record("File I/O", "open()", t1 - t0, ok);
}

static void bench_read()
{
    make_tmp_file();
    int fd = open(TMP_FILE, O_RDONLY);
    char buf[4096];
    long long t0 = now_ns();
    ssize_t n = read(fd, buf, sizeof(buf));
    long long t1 = now_ns();
    close(fd);
    record("File I/O", "read()", t1 - t0, n > 0);
}

static void bench_write()
{
    int fd = open(TMP_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    const char *data = "timing_test_data";
    long long t0 = now_ns();
    ssize_t n = write(fd, data, strlen(data));
    long long t1 = now_ns();
    close(fd);
    record("File I/O", "write()", t1 - t0, n > 0);
}

static void bench_close()
{
    make_tmp_file();
    int fd = open(TMP_FILE, O_RDONLY);
    long long t0 = now_ns();
    int rc = close(fd);
    long long t1 = now_ns();
    record("File I/O", "close()", t1 - t0, rc == 0);
}

// ---------- Memory ----------

static void bench_mmap()
{
    make_tmp_file(4096);
    int fd = open(TMP_FILE, O_RDONLY);
    struct stat st; fstat(fd, &st);
    long long t0 = now_ns();
    void *m = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    long long t1 = now_ns();
    bool ok = (m != MAP_FAILED);
    if (ok) munmap(m, st.st_size);
    close(fd);
    record("Memory", "mmap()", t1 - t0, ok);
}

static void bench_munmap()
{
    make_tmp_file(4096);
    int fd = open(TMP_FILE, O_RDONLY);
    struct stat st; fstat(fd, &st);
    void *m = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    long long t0 = now_ns();
    int rc = munmap(m, st.st_size);
    long long t1 = now_ns();
    record("Memory", "munmap()", t1 - t0, rc == 0);
}

// ---------- Process ----------

static void bench_fork_wait()
{
    long long t0 = now_ns();
    pid_t pid = fork();
    if (pid == 0) { _exit(0); }   // child exits immediately
    long long t1 = now_ns();

    int status;
    long long t2 = now_ns();
    waitpid(pid, &status, 0);
    long long t3 = now_ns();

    record("Process", "fork()",  t1 - t0, pid > 0);
    record("Process", "wait()",  t3 - t2, WIFEXITED(status));
}

static void bench_exec()
{
    // Time execve() itself: fork a child that immediately execs /bin/true
    long long t_exec_start = 0, t_exec_end = 0;

    pid_t pid = fork();
    if (pid == 0)
    {
        t_exec_start = now_ns();
        execl("/bin/true", "true", nullptr);
        _exit(1); // only reached if exec fails
    }
    int status;
    waitpid(pid, &status, 0);

    // We can't measure exec inside the child easily without IPC,
    // so we time the round-trip and note it covers fork+exec+exit+wait.
    // For a tighter exec-only estimate: use vfork().
    pid_t pid2 = fork();
    if (pid2 == 0)
    {
        long long t0c = now_ns();
        execl("/bin/true", "true", nullptr);
        _exit(1);
    }
    long long tw0 = now_ns();
    waitpid(pid2, &status, 0);
    long long tw1 = now_ns();

    record("Process", "exec() [via execl]", tw1 - tw0,
           WIFEXITED(status), "fork+exec+exit round-trip");
}

// ---------- IPC ----------

static void bench_pipe()
{
    int fds[2];
    long long t0 = now_ns();
    int rc = pipe(fds);
    long long t1 = now_ns();
    if (rc == 0) { close(fds[0]); close(fds[1]); }
    record("IPC", "pipe()", t1 - t0, rc == 0);
}

static void bench_shmget()
{
    key_t key = ftok("/tmp", 99);
    long long t0 = now_ns();
    int id = shmget(key, 4096, IPC_CREAT | 0666);
    long long t1 = now_ns();
    bool ok = (id >= 0);
    if (ok) shmctl(id, IPC_RMID, nullptr);
    record("IPC", "shmget()", t1 - t0, ok);
}

static void bench_shmat()
{
    key_t key = ftok("/tmp", 100);
    int id = shmget(key, 4096, IPC_CREAT | 0666);
    long long t0 = now_ns();
    void *p = shmat(id, nullptr, 0);
    long long t1 = now_ns();
    bool ok = (p != reinterpret_cast<void *>(-1));
    if (ok) shmdt(p);
    shmctl(id, IPC_RMID, nullptr);
    record("IPC", "shmat()", t1 - t0, ok);
}

// ---------- Permissions ----------

static void bench_chmod()
{
    make_tmp_file();
    long long t0 = now_ns();
    int rc = chmod(TMP_FILE, 0644);
    long long t1 = now_ns();
    record("Permissions", "chmod()", t1 - t0, rc == 0);
}

static void bench_chown()
{
    make_tmp_file();
    struct stat st; stat(TMP_FILE, &st);
    long long t0 = now_ns();
    // chown to current owner (no-op — still exercises the syscall)
    int rc = chown(TMP_FILE, st.st_uid, st.st_gid);
    long long t1 = now_ns();
    record("Permissions", "chown()", t1 - t0, rc == 0);
}

static void bench_umask()
{
    long long t0 = now_ns();
    mode_t old = umask(0022);
    long long t1 = now_ns();
    umask(old); // restore
    record("Permissions", "umask()", t1 - t0, true);
}

// ============================================================
// LINUX main
// ============================================================
int main()
{
    std::cout << "=== CT-353 CCP — Syscall Timing Benchmark (Linux) ===\n";
    std::cout << "Each syscall is timed once using clock_gettime()\n"
                 "via std::chrono::high_resolution_clock.\n";

    bench_open();
    bench_read();
    bench_write();
    bench_close();
    bench_mmap();
    bench_munmap();
    bench_fork_wait();   // records fork() and wait()
    bench_exec();        // records exec()
    bench_pipe();
    bench_shmget();
    bench_shmat();
    bench_chmod();
    bench_chown();
    bench_umask();

    print_table();
    cleanup_tmp();
    return 0;
}

// ============================================================
//  WINDOWS BENCHMARKS
// ============================================================
#else

static void bench_CreateFile()
{
    make_tmp_file();
    long long t0 = now_ns();
    HANDLE h = CreateFileA(TMP_FILE, GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    long long t1 = now_ns();
    bool ok = (h != INVALID_HANDLE_VALUE);
    if (ok) CloseHandle(h);
    record("File I/O", "CreateFile()", t1 - t0, ok);
}

static void bench_ReadFile()
{
    make_tmp_file();
    HANDLE h = CreateFileA(TMP_FILE, GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    char buf[4096]; DWORD nr = 0;
    long long t0 = now_ns();
    BOOL ok = ReadFile(h, buf, sizeof(buf), &nr, nullptr);
    long long t1 = now_ns();
    CloseHandle(h);
    record("File I/O", "ReadFile()", t1 - t0, ok && nr > 0);
}

static void bench_WriteFile()
{
    HANDLE h = CreateFileA(TMP_FILE, GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    const char *data = "timing_test_data"; DWORD nw = 0;
    long long t0 = now_ns();
    BOOL ok = WriteFile(h, data, (DWORD)strlen(data), &nw, nullptr);
    long long t1 = now_ns();
    CloseHandle(h);
    record("File I/O", "WriteFile()", t1 - t0, ok && nw > 0);
}

static void bench_CloseHandle()
{
    make_tmp_file();
    HANDLE h = CreateFileA(TMP_FILE, GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    long long t0 = now_ns();
    BOOL ok = CloseHandle(h);
    long long t1 = now_ns();
    record("File I/O", "CloseHandle()", t1 - t0, ok);
}

static void bench_VirtualAlloc()
{
    long long t0 = now_ns();
    void *p = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    long long t1 = now_ns();
    bool ok = (p != nullptr);
    if (ok) VirtualFree(p, 0, MEM_RELEASE);
    record("Memory", "VirtualAlloc()", t1 - t0, ok);
}

static void bench_VirtualFree()
{
    void *p = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    long long t0 = now_ns();
    BOOL ok = VirtualFree(p, 0, MEM_RELEASE);
    long long t1 = now_ns();
    record("Memory", "VirtualFree()", t1 - t0, ok);
}

static void bench_CreateProcess()
{
    STARTUPINFOA si = {}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    char cmd[] = "cmd.exe /c exit";
    long long t0 = now_ns();
    BOOL ok = CreateProcessA(nullptr, cmd, nullptr, nullptr,
                             FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    long long t1 = now_ns();
    if (ok) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    record("Process", "CreateProcess()", t1 - t0, ok);
}

static void bench_WaitForSingleObject()
{
    STARTUPINFOA si = {}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    char cmd[] = "cmd.exe /c exit";
    CreateProcessA(nullptr, cmd, nullptr, nullptr,
                   FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    long long t0 = now_ns();
    DWORD rc = WaitForSingleObject(pi.hProcess, INFINITE);
    long long t1 = now_ns();
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    record("Process", "WaitForSingleObject()", t1 - t0, rc == WAIT_OBJECT_0);
}

static void bench_CreatePipe()
{
    HANDLE hr, hw;
    SECURITY_ATTRIBUTES sa = {}; sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE;
    long long t0 = now_ns();
    BOOL ok = CreatePipe(&hr, &hw, &sa, 0);
    long long t1 = now_ns();
    if (ok) { CloseHandle(hr); CloseHandle(hw); }
    record("IPC", "CreatePipe()", t1 - t0, ok);
}

static void bench_CreateFileMapping()
{
    long long t0 = now_ns();
    HANDLE h = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr,
                                  PAGE_READWRITE, 0, 4096, "TimingTestMap");
    long long t1 = now_ns();
    bool ok = (h != nullptr);
    if (ok) CloseHandle(h);
    record("IPC", "CreateFileMapping()", t1 - t0, ok);
}

static void bench_MapViewOfFile()
{
    HANDLE h = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr,
                                  PAGE_READWRITE, 0, 4096, "TimingTestMap2");
    long long t0 = now_ns();
    void *p = MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, 4096);
    long long t1 = now_ns();
    bool ok = (p != nullptr);
    if (ok) UnmapViewOfFile(p);
    CloseHandle(h);
    record("IPC", "MapViewOfFile()", t1 - t0, ok);
}

static void bench_SetFileSecurity_chmod()
{
    make_tmp_file();
    // Build a simple DACL granting full control to Everyone
    SECURITY_DESCRIPTOR sd;
    InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
    SetSecurityDescriptorDacl(&sd, TRUE, nullptr, FALSE); // null DACL = full access
    long long t0 = now_ns();
    BOOL ok = SetFileSecurityA(TMP_FILE, DACL_SECURITY_INFORMATION, &sd);
    long long t1 = now_ns();
    record("Permissions", "SetFileSecurity() [chmod]", t1 - t0, ok,
           "null DACL = full access");
}

static void bench_SetFileSecurity_chown()
{
    make_tmp_file();
    SECURITY_DESCRIPTOR sd;
    InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
    // Get current process SID to set as owner
    HANDLE hToken;
    OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken);
    DWORD len = 0;
    GetTokenInformation(hToken, TokenUser, nullptr, 0, &len);
    std::vector<BYTE> buf(len);
    GetTokenInformation(hToken, TokenUser, buf.data(), len, &len);
    PSID sid = reinterpret_cast<TOKEN_USER *>(buf.data())->User.Sid;
    SetSecurityDescriptorOwner(&sd, sid, FALSE);
    long long t0 = now_ns();
    BOOL ok = SetFileSecurityA(TMP_FILE, OWNER_SECURITY_INFORMATION, &sd);
    long long t1 = now_ns();
    CloseHandle(hToken);
    record("Permissions", "SetFileSecurity() [chown]", t1 - t0, ok,
           "set owner to current user");
}

static void bench_SetFileSecurity_umask()
{
    make_tmp_file();
    // Windows has no umask; closest analog is setting default DACL on process token
    HANDLE hToken;
    OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_ADJUST_DEFAULT, &hToken);
    DWORD len = 0;
    GetTokenInformation(hToken, TokenDefaultDacl, nullptr, 0, &len);
    std::vector<BYTE> buf(len);
    GetTokenInformation(hToken, TokenDefaultDacl, buf.data(), len, &len);
    TOKEN_DEFAULT_DACL *tdd = reinterpret_cast<TOKEN_DEFAULT_DACL *>(buf.data());
    long long t0 = now_ns();
    BOOL ok = SetTokenInformation(hToken, TokenDefaultDacl, tdd, len);
    long long t1 = now_ns();
    CloseHandle(hToken);
    record("Permissions", "SetFileSecurity() [umask]", t1 - t0, ok,
           "set process default DACL");
}

// ============================================================
// WINDOWS main
// ============================================================
int main()
{
    std::cout << "=== CT-353 CCP — Syscall Timing Benchmark (Windows) ===\n";
    std::cout << "Each syscall is timed once using QueryPerformanceCounter()\n"
                 "via std::chrono::high_resolution_clock.\n";

    bench_CreateFile();
    bench_ReadFile();
    bench_WriteFile();
    bench_CloseHandle();
    bench_VirtualAlloc();
    bench_VirtualFree();
    bench_CreateProcess();
    bench_WaitForSingleObject();
    bench_CreatePipe();
    bench_CreateFileMapping();
    bench_MapViewOfFile();
    bench_SetFileSecurity_chmod();
    bench_SetFileSecurity_chown();
    bench_SetFileSecurity_umask();

    print_table();
    cleanup_tmp();
    return 0;
}

#endif // _WIN32