// ============================================================================
//  C4pt0r — Executor de Lua dentro do ScRT do FiveM (ver lua_exec.h)
// ============================================================================
#include "lua_exec.h"
#include "logger.h"
#include "deps/minhook/include/MinHook.h"
#include <Windows.h>
#include <cstring>
#include <cstdint>
#include <atomic>

namespace C4pt0r::LuaExec
{
    // ---- Tipos Lua (ABI C, x64 — args extras sao ignorados com seguranca) --
    typedef void* lua_State;
    typedef int (*lua_CFunction)(lua_State*);
    typedef int (*lua_resume_fn)(lua_State*, lua_State*, int, int*);
    typedef int (*lua_pcallk_fn)(lua_State*, int, int, int, intptr_t, lua_CFunction);
    typedef int (*lua_call_fn)(lua_State*, int, int);
    typedef int (*luaL_loadbufferx_fn)(lua_State*, const char*, size_t,
                                     const char*, const char*);
    typedef int (*luaL_loadstring_fn)(lua_State*, const char*);
    typedef const char* (*lua_tostring_fn)(lua_State*, int);
    typedef void (*lua_settop_fn)(lua_State*, int);

    // ---- Estado resolvido --------------------------------------------------
    static HMODULE            s_Dll       = nullptr;
    static std::atomic<lua_State*> s_State{nullptr};
    static luaL_loadbufferx_fn s_LoadBuf  = nullptr;
    static luaL_loadstring_fn  s_LoadStr  = nullptr;
    static lua_pcallk_fn       s_Pcallk   = nullptr;
    static lua_tostring_fn     s_ToString = nullptr;
    static lua_settop_fn       s_SetTop   = nullptr;
    static char                s_Status[96] = "lua: procurando dll";
    static bool                s_InitDone = false;
    static int                 s_InitTries = 0;

    // ---- Hooks de captura --------------------------------------------------
    static lua_resume_fn s_OrigResume = nullptr;
    static lua_pcallk_fn s_OrigPcallk = nullptr;
    static lua_call_fn   s_OrigCall   = nullptr;

    static int hk_resume(lua_State* L, lua_State* from, int nargs, int* nres)
    {
        if (L) s_State.store(L, std::memory_order_relaxed);
        return s_OrigResume(L, from, nargs, nres);
    }
    static int hk_pcallk(lua_State* L, int na, int nr, int ef,
                         intptr_t ctx, lua_CFunction k)
    {
        if (L) s_State.store(L, std::memory_order_relaxed);
        return s_OrigPcallk(L, na, nr, ef, ctx, k);
    }
    static int hk_call(lua_State* L, int nargs, int nresults)
    {
        if (L) s_State.store(L, std::memory_order_relaxed);
        return s_OrigCall(L, nargs, nresults);
    }

    static void HookSym(HMODULE dll, const char* name, void* hk, void** orig)
    {
        FARPROC fp = GetProcAddress(dll, name);
        if (!fp) return;
        if (MH_CreateHook(reinterpret_cast<void*>(fp), hk, orig) == MH_OK &&
            MH_EnableHook(reinterpret_cast<void*>(fp)) == MH_OK) {
            Log::Write("LuaExec: hook %s ok.", name);
        }
    }

    void Init()
    {
        if (s_State.load()) return;                 // ja temos um state
        if (s_InitDone && s_Dll) return;            // hooks postos, esperando
        if (++s_InitTries > 200) return;            // desiste depois de ~100s

        if (!s_Dll) {
            static const wchar_t* kDlls[] = {
                L"citizen-scripting-lua.dll",
                L"citizen-scripting-lua54.dll",
                L"citizen-scripting-lua53.dll",
                L"citizen-scripting-lua52.dll",
                L"citizen-scripting-luajit.dll",
                L"citizen-scripting-lua5x.dll",
            };
            for (auto* n : kDlls) {
                s_Dll = GetModuleHandleW(n);
                if (s_Dll) break;
            }
            if (!s_Dll) {
                snprintf(s_Status, sizeof(s_Status), "lua: dll nao carregada");
                return;
            }
            Log::Write("LuaExec: dll achada.");

            // funcoes de execucao
            s_LoadBuf  = (luaL_loadbufferx_fn)GetProcAddress(s_Dll, "luaL_loadbufferx");
            s_LoadStr  = (luaL_loadstring_fn )GetProcAddress(s_Dll, "luaL_loadstring");
            s_Pcallk   = (lua_pcallk_fn      )GetProcAddress(s_Dll, "lua_pcallk");
            s_ToString = (lua_tostring_fn    )GetProcAddress(s_Dll, "lua_tostring");
            s_SetTop   = (lua_settop_fn      )GetProcAddress(s_Dll, "lua_settop");
            if ((!s_LoadBuf && !s_LoadStr) || !s_Pcallk) {
                snprintf(s_Status, sizeof(s_Status), "lua: exports ausentes");
                Log::Write("LuaExec: luaL_loadbuffer/lua_pcallk ausentes — sem executor.");
                s_Dll = nullptr;
                return;
            }

            // hooks de captura de lua_State — qualquer um que disparar serve
            MH_Initialize();
            HookSym(s_Dll, "lua_resume", (void*)hk_resume, (void**)&s_OrigResume);
            HookSym(s_Dll, "lua_pcallk", (void*)hk_pcallk, (void**)&s_OrigPcallk);
            HookSym(s_Dll, "lua_call",   (void*)hk_call,   (void**)&s_OrigCall);
            s_InitDone = true;
            snprintf(s_Status, sizeof(s_Status), "lua: esperando um state");
            return;
        }
    }

    bool Ready() { return s_State.load() != nullptr; }

    const char* Status()
    {
        if (s_State.load()) {
            snprintf(s_Status, sizeof(s_Status), "lua: state capturado");
        }
        return s_Status;
    }

    int Run(const char* code)
    {
        lua_State* L = s_State.load();
        if (!L) {
            Log::Write("LuaExec: sem lua_State ainda (joga um tempo e tenta de novo).");
            return -1;
        }
        if (s_SetTop) s_SetTop(L, 0);

        int st;
        if (s_LoadBuf)
            st = s_LoadBuf(L, code, strlen(code), "c4pt0r", "t");
        else
            st = s_LoadStr(L, code);
        if (st != 0) {
            const char* e = s_ToString ? s_ToString(L, -1) : "?";
            Log::Write("LuaExec: erro de sintaxe: %s", e ? e : "?");
            if (s_SetTop) s_SetTop(L, 0);
            return st;
        }
        st = s_Pcallk(L, 0, -1 /*LUA_MULTRET*/, 0, 0, nullptr);
        if (st != 0) {
            const char* e = s_ToString ? s_ToString(L, -1) : "?";
            Log::Write("LuaExec: erro runtime: %s", e ? e : "?");
            if (s_SetTop) s_SetTop(L, 0);
            return st;
        }
        Log::Write("LuaExec: script rodou ok.");
        return 0;
    }
}
