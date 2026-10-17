#include "lua_exec.h"
#include "logger.h"
#include "deps/minhook/include/MinHook.h"
#include <Windows.h>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <csetjmp>

namespace C4pt0r::LuaExec
{
    struct FxArgType {
        uint32_t Size : 29;
        uint32_t IsIsolated : 1;
        uint32_t IsString : 1;
        uint32_t IsPointer : 1;
    };

    struct FxIsoBuf {
        size_t Size;
        uint8_t* SafeBuffer;
        uint8_t* NativeBuffer;
    };

    struct FxCtx {
        const void* info;
        uintptr_t   arguments[32];
        FxArgType   types[32];
        size_t      numArguments;
        bool        trivial;
        uint8_t*    isolatedBuffer;
        uint8_t*    isolatedBufferEnd;
        uintptr_t   results[4];
        void*       retvals[16];
        uint8_t     rettypes[16];
        size_t      numReturnValues;
        FxIsoBuf    isolatedBuffers[8];
        size_t      numIsolatedBuffers;
    };

    using FromHashFn     = const void* (__cdecl*)(uint64_t hash);
    using InvokeFn       = void (__fastcall*)(FxCtx* ctx);
    using ScriptErrorFn  = void (__fastcall*)(FxCtx* ctx, const char* msg);

    static FromHashFn s_FromHash = nullptr;
    static InvokeFn   s_Invoke   = nullptr;
    static bool       s_Ok       = false;
    static int        s_Tries    = 0;
    static char       s_Status[96] = "invoker: procurando exports";

    static ScriptErrorFn s_OrigScriptError = nullptr;
    static FARPROC       s_ScriptErrorAddr = nullptr;
    static bool          s_ErrHooked = false;
    static jmp_buf       s_Jb;
    static __declspec(thread) int s_OurCall = 0;

    static void __fastcall hk_ScriptError(FxCtx* ctx, const char* msg)
    {
        if (s_OurCall) {
            Log::Write("LuaExec ScriptError: %s", msg ? msg : "?");
            longjmp(s_Jb, 1);
        }
        s_OrigScriptError(ctx, msg);
    }

    static uint64_t JoaatHash(const char* s)
    {
        uint32_t h = 0;
        while (*s) {
            char c = *s++;
            if (c >= 'A' && c <= 'Z') c += 32;
            h = (h + (uint8_t)c) & 0xFFFFFFFF;
            h = (h + (h << 10)) & 0xFFFFFFFF;
            h = h ^ (h >> 6);
        }
        h = (h + (h << 3)) & 0xFFFFFFFF;
        h = h ^ (h >> 11);
        h = (h + (h << 15)) & 0xFFFFFFFF;
        return h & 0xFFFFFFFFull;
    }

    void Init()
    {
        if (s_Ok || ++s_Tries > 50) return;

        HMODULE core = GetModuleHandleW(L"citizen-scripting-core.dll");
        if (!core) {
            snprintf(s_Status, sizeof(s_Status), "invoker: core dll ausente");
            return;
        }
        s_FromHash = (FromHashFn)GetProcAddress(core,
            "?FromHash@ScriptNativeHandler@invoker@fx@@SAAEBV123@_K@Z");
        s_Invoke = (InvokeFn)GetProcAddress(core,
            "?Invoke@ScriptNativeContext@invoker@fx@@QEAAXXZ");
        if (!s_FromHash || !s_Invoke) {
            snprintf(s_Status, sizeof(s_Status), "invoker: exports ausentes");
            Log::Write("LuaExec: FromHash/Invoke ausentes em citizen-scripting-core.");
            s_FromHash = nullptr; s_Invoke = nullptr;
            return;
        }

        s_ScriptErrorAddr = GetProcAddress(core,
            "?ScriptError@ScriptNativeContext@invoker@fx@@QEBAXPEBD@Z");
        s_Ok = true;
        snprintf(s_Status, sizeof(s_Status), "server events: pronto");
        Log::Write("LuaExec: invoker CFX ok (FromHash=0x%p Invoke=0x%p).",
                   (void*)s_FromHash, (void*)s_Invoke);
    }

    bool Ready() { return s_Ok; }
    const char* Status() { return s_Status; }

    static bool DoInvoke(FxCtx* ctx, uintptr_t* ret);

    static bool CallFx(uint64_t hash, uintptr_t* args, FxArgType* types,
                       int nargs, uintptr_t* ret)
    {
        if (!s_ErrHooked && s_ScriptErrorAddr) {
            s_ErrHooked = true;
            MH_Initialize();
            if (MH_CreateHook((void*)s_ScriptErrorAddr, (void*)hk_ScriptError,
                              (void**)&s_OrigScriptError) == MH_OK &&
                MH_EnableHook((void*)s_ScriptErrorAddr) == MH_OK) {
                Log::Write("LuaExec: ScriptError neutered.");
            }
        }

        const void* info = nullptr;
        __try { info = s_FromHash(hash); }
        __except (EXCEPTION_EXECUTE_HANDLER) { info = nullptr; }
        if (!info) return false;

        const uint8_t* h = (const uint8_t*)info;
        __try {
            if (*(const uint64_t*)h != hash) return false;
            void* scr = *(void**)(h + 24);
            void* fx  = *(void**)(h + 32);
            if (scr) {
                Log::Write("LuaExec: hash 0x%llX colidiu na registry GTA5 — ignorado.", hash);
                return false;
            }
            if (!fx) {
                Log::Write("LuaExec: hash 0x%llX sem handler CFX.", hash);
                return false;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }

        FxCtx ctx;
        memset(&ctx, 0, sizeof(ctx));
        ctx.info = info;
        ctx.numArguments = nargs;
        ctx.trivial = true;
        for (int i = 0; i < nargs; ++i) {
            ctx.arguments[i] = args[i];
            ctx.types[i] = types ? types[i] : FxArgType{};
            if (ctx.types[i].IsPointer) ctx.trivial = false;
        }
        ctx.retvals[0] = ctx.results;
        ctx.rettypes[0] = 9;
        ctx.numReturnValues = 1;

        s_OurCall = 1;
        if (setjmp(s_Jb) != 0) {
            s_OurCall = 0;
            return false;
        }
        bool ok = DoInvoke(&ctx, ret);
        s_OurCall = 0;
        return ok;
    }

    static bool DoInvoke(FxCtx* ctx, uintptr_t* ret)
    {
        __try {
            s_Invoke(ctx);
            if (ret) ret[0] = ctx->results[0];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    bool TriggerServer(const char* eventName, const void* payload, int payloadLen)
    {
        if (!s_Ok) return false;

        static char s_NameBuf[256];
        static uint8_t* s_Blobs[16] = {};
        static int s_BlobIdx = 0;

        strncpy_s(s_NameBuf, eventName, sizeof(s_NameBuf) - 1);

        uint8_t* blob = (uint8_t*)malloc(payloadLen);
        if (!blob) return false;
        memcpy(blob, payload, payloadLen);
        int idx = s_BlobIdx++ % 16;
        if (s_Blobs[idx]) free(s_Blobs[idx]);
        s_Blobs[idx] = blob;

        uintptr_t args[3];
        FxArgType types[3] = {};
        args[0] = (uintptr_t)s_NameBuf;
        types[0].IsString = 1; types[0].IsPointer = 1;
        types[0].Size = (uint32_t)strlen(s_NameBuf) + 1;
        args[1] = (uintptr_t)blob;
        types[1].IsPointer = 1;
        types[1].Size = (uint32_t)payloadLen;
        args[2] = (uintptr_t)payloadLen;

        bool ok = CallFx(0x7FDD1128 /*TRIGGER_SERVER_EVENT_INTERNAL*/,
                         args, types, 3, nullptr);
        Log::Write("LuaExec: TriggerServer '%s' -> %s", eventName,
                   ok ? "OK" : "FALHOU");
        return ok;
    }

    int PlayerServerId(int localPlayerId)
    {
        if (!s_Ok) return -1;
        uintptr_t args[1] = { (uintptr_t)localPlayerId };
        uintptr_t ret = 0;
        if (!CallFx(JoaatHash("GET_PLAYER_SERVER_ID"), args, nullptr, 1, &ret))
            return -1;
        return (int)ret;
    }
}
