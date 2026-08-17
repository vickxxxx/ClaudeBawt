#include "log.h"

#include <windows.h>
#include <cstdio>
#include <cstdarg>

namespace dlog {
namespace {

SRWLOCK   g_lock = SRWLOCK_INIT;
HANDLE    g_file = INVALID_HANDLE_VALUE;
bool      g_tried = false;
char      g_path[MAX_PATH] = {0};

void OpenLocked() {
    if (g_tried)
        return;
    g_tried = true;


    char dir[MAX_PATH] = {0};
    DWORD n = GetTempPathA(MAX_PATH, dir);
    if (n == 0 || n > MAX_PATH)
        lstrcpyA(dir, "C:\\");
    wsprintfA(g_path, "%sclient.log", dir);

    g_file = CreateFileA(g_path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                         OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    char banner[MAX_PATH + 64];
    wsprintfA(banner, "[client] log file: %s\n", g_path);
    OutputDebugStringA(banner);
}

}

void Init() {
    AcquireSRWLockExclusive(&g_lock);
    OpenLocked();
    ReleaseSRWLockExclusive(&g_lock);
    Write("===== client log opened (pid %lu) =====", GetCurrentProcessId());
}

void Shutdown() {
    AcquireSRWLockExclusive(&g_lock);
    if (g_file != INVALID_HANDLE_VALUE) {
        CloseHandle(g_file);
        g_file = INVALID_HANDLE_VALUE;
    }
    ReleaseSRWLockExclusive(&g_lock);
}

void Write(const char* fmt, ...) {
    char body[1024];
    va_list args;
    va_start(args, fmt);
    int bodyLen = vsnprintf(body, sizeof(body), fmt, args);
    va_end(args);
    if (bodyLen < 0)
        body[0] = '\0';
    body[sizeof(body) - 1] = '\0';

    char line[1152];
    _snprintf(line, sizeof(line), "[%08lu][t%lu] %s\n", GetTickCount(),
              GetCurrentThreadId(), body);
    line[sizeof(line) - 1] = '\0';
    int len = lstrlenA(line);

    OutputDebugStringA(line);

    AcquireSRWLockExclusive(&g_lock);
    OpenLocked();
    if (g_file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(g_file, line, (DWORD)len, &written, nullptr);
        FlushFileBuffers(g_file);
    }
    ReleaseSRWLockExclusive(&g_lock);
}

}
