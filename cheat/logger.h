#pragma once
// ============================================================================
//  C4pt0r Cheats — File Logger
//  Writes to %TEMP%\c4pt0r_log.txt for easy debugging
// ============================================================================

#include <Windows.h>
#include <cstdio>
#include <cstdarg>

namespace C4pt0r::Log
{
    inline FILE* g_File = nullptr;
    inline char  g_Path[MAX_PATH] = {};

    inline void Init() {
        GetTempPathA(MAX_PATH, g_Path);
        strcat_s(g_Path, "c4pt0r_log.txt");
        fopen_s(&g_File, g_Path, "w");
        if (g_File) {
            fprintf(g_File, "=== C4pt0r Cheats Log ===\n");
            fflush(g_File);
        }
    }

    inline void Write(const char* fmt, ...) {
        char buf[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);

        // File
        if (g_File) {
            SYSTEMTIME st;
            GetLocalTime(&st);
            fprintf(g_File, "[%02d:%02d:%02d] %s\n", st.wHour, st.wMinute, st.wSecond, buf);
            fflush(g_File);
        }

        // OutputDebugString
        char dbg[1100];
        snprintf(dbg, sizeof(dbg), "[C4pt0r] %s\n", buf);
        OutputDebugStringA(dbg);
    }

    inline void Close() {
        if (g_File) {
            fprintf(g_File, "=== Log closed ===\n");
            fclose(g_File);
            g_File = nullptr;
        }
    }
}
