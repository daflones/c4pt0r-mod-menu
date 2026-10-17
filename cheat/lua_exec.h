#pragma once
// ============================================================================
//  C4pt0r — Executor de Lua dentro do ScRT do FiveM
//
//  Natives CFX (TriggerServerEvent, GetPlayerServerId, chat, etc.) NAO vivem
//  na tabela de natives do GTA5 — invocar direto crasha (+0x1750). O jeito
//  certo e o mesmo dos menus pagos: executar Lua num lua_State real de um
//  resource do servidor. Ai TriggerServerEvent dispara de verdade, com o
//  contexto de um script legitimo.
//
//  Como: citizen-scripting-lua*.dll exporta funcoes lua_*; hookamos as
//  quentes (lua_resume/lua_pcallk/lua_call) pra capturar um lua_State vivo,
//  e rodamos snippets com luaL_loadbufferx + lua_pcallk.
// ============================================================================

namespace C4pt0r::LuaExec
{
    void        Init();          // lazy — acha a dll, resolve exports, instala hooks
    bool        Ready();         // tem um lua_State capturado
    const char* Status();        // texto curto pro menu ("sem dll", "capturado", ...)
    int         Run(const char* code);   // 0 ok / !=0 erro lua (loga o motivo)
}
