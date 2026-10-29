#include "FrameProfile.h"
#include "pch.h"
#include <deque>

// NOTE: the engine appears to close all CRT FILE* streams on level load (the log died at
// exactly 'LoadGame' and every later LOGI hung on the mutex). Use a raw Win32 handle.
namespace
{
    std::mutex g_Mutex;
    HANDLE g_File = INVALID_HANDLE_VALUE;
    std::deque<std::string> g_Ring;
    // A tester's log must stay small enough to attach to a message, and a runaway line must not fill the disk over a
    // long session: past this size only a final notice is written (the in-memory ring keeps going for the overlay).
    constexpr unsigned long long kMaxLogBytes = 64ull << 20;
    unsigned long long g_Written = 0; bool g_Capped = false;
}

void Log::Init(const std::wstring& dir)
{
    std::lock_guard lk(g_Mutex);
    std::wstring p = dir + L"\\DishonoredVR.log";
    // Keep the previous session's log: when the game dies and is relaunched (a test script does that by itself),
    // the fault CrashLog wrote is in it, and overwriting it loses the only record of the crash.
    MoveFileExW(p.c_str(), (dir + L"\\DishonoredVR.prev.log").c_str(), MOVEFILE_REPLACE_EXISTING);
    g_File =CreateFileW(p.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (g_File == INVALID_HANDLE_VALUE)
    {   // a game folder we may not write to (installed outside Steam's library, or protected): %LOCALAPPDATA%\DishonoredVR
        wchar_t la[MAX_PATH];
        if (GetEnvironmentVariableW(L"LOCALAPPDATA", la, MAX_PATH))
        {
            const std::wstring alt = std::wstring(la) + L"\\DishonoredVR";
            CreateDirectoryW(alt.c_str(), nullptr);
            MoveFileExW((alt + L"\\DishonoredVR.log").c_str(), (alt + L"\\DishonoredVR.prev.log").c_str(), MOVEFILE_REPLACE_EXISTING);
            g_File = CreateFileW((alt + L"\\DishonoredVR.log").c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        }
    }
}

void Log::Write(const std::string& line)
{ DVR_PROFILE("Log.Log::Write");
    SYSTEMTIME t; GetLocalTime(&t);
    std::string s = fmt::format("[{:02}:{:02}:{:02}.{:03}] {}\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, line);
    OutputDebugStringA(s.c_str());
    std::lock_guard lk(g_Mutex);
    if (g_File != INVALID_HANDLE_VALUE && !g_Capped)
    {
        DWORD written = 0;
        if (g_Written + s.size() > kMaxLogBytes)
        {
            static const char kNote[] = "[log size limit reached: nothing more is written this session]\n";
            WriteFile(g_File, kNote, (DWORD)(sizeof kNote - 1), &written, nullptr);
            g_Capped = true;
        }
        else { WriteFile(g_File, s.data(), (DWORD)s.size(), &written, nullptr); g_Written += written; }
    }
    s.pop_back();
    g_Ring.push_back(std::move(s));
    if (g_Ring.size() > 400) g_Ring.pop_front();
}

std::vector<std::string> Log::Tail(size_t n)
{
    std::lock_guard lk(g_Mutex);
    size_t start = g_Ring.size() > n ? g_Ring.size() - n : 0;
    return { g_Ring.begin() + start, g_Ring.end() };
}
