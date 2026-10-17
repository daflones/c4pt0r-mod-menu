#pragma once
// ============================================================================
//  C4pt0r Cheats — File Logger (non-blocking)
//  Writes to %TEMP%\c4pt0r_log.txt
//
//  POR QUE NAO PODE BLOQUEAR (o que matou o cheat):
//  - fprintf/fflush tomam um lock interno do CRT no FILE. Se a thread
//    trava dentro (WriteFile segurado por scan de AV/EDR), TODO thread que
//    logar depois trava junto — inclusive o supervisor, que nunca escreve
//    o "SUPERVISOR". Log em silencio = cheat morto sem rastro.
//  - OutputDebugStringA pega o DBWinMutex (mutex GLOBAL do sistema) —
//    pode bloquear por outros processos.
//
//  Agora: WriteFile overlapped (retorna na hora, kernel escreve async) +
//  flag atomica — se uma thread ja esta dentro, as outras pulam o write
//  em vez de esperar. Buffers persistentes (o write async le depois de
//  voltar), rotacionados entre 32 slots.
// ============================================================================

#include <Windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <atomic>

namespace C4pt0r::Log
{
    inline HANDLE            g_File   = INVALID_HANDLE_VALUE;
    inline char              g_Path[MAX_PATH] = {};
    inline std::atomic<bool> g_Busy{false};       // false = livre
    inline LARGE_INTEGER     g_Offset{};          // posicao do append async
    inline char              g_Slots[32][1200] = {}; // buffers p/ writes async
    inline int               g_Slot = 0;

    inline void RawWrite(const char* text, int len) {
        if (g_File == INVALID_HANDLE_VALUE || len <= 0) return;
        char* line = g_Slots[g_Slot];
        g_Slot = (g_Slot + 1) % 32;
        int n = snprintf(line, 1200, "%s", text);
        if (n <= 0) return;
        if (n >= 1200) n = 1199;
        OVERLAPPED ov{};
        ov.Offset     = g_Offset.LowPart;
        ov.OffsetHigh = g_Offset.HighPart;
        g_Offset.QuadPart += n;
        DWORD w = 0;
        WriteFile(g_File, line, n, &w, &ov);  // overlapped: nunca bloqueia
    }

    inline void Init() {
        GetTempPathA(MAX_PATH, g_Path);
        strcat_s(g_Path, "c4pt0r_log.txt");
        g_Offset.QuadPart = 0;
        g_File = CreateFileA(g_Path, GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
        RawWrite("=== C4pt0r Cheats Log ===\r\n", 28);
    }

    inline void Write(const char* fmt, ...) {
        char buf[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);

        // outra thread no meio de um write? pula — log nunca pode parar
        // o cheat. (feature/supervisor seguem via atomics p/ diagnostico)
        if (g_Busy.exchange(true)) return;

        SYSTEMTIME st;
        GetLocalTime(&st);
        char line[1200];
        snprintf(line, sizeof(line), "[%02d:%02d:%02d] %s\r\n",
                 st.wHour, st.wMinute, st.wSecond, buf);
        RawWrite(line, static_cast<int>(strlen(line)));

        g_Busy = false;
    }

    inline void Close() {
        Write("=== Log closed ===");
        if (g_File != INVALID_HANDLE_VALUE) {
            CancelIoEx(g_File, nullptr);   // descarta writes pendentes
            CloseHandle(g_File);
            g_File = INVALID_HANDLE_VALUE;
        }
    }
}
