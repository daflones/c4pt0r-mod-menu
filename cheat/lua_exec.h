#pragma once

namespace C4pt0r::LuaExec
{
    void        Init();
    bool        Ready();
    const char* Status();
    bool        TriggerServer(const char* eventName,
                              const void* payload, int payloadLen);
    int         PlayerServerId(int localPlayerId);
}
