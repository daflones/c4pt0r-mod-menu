#pragma once

namespace C4pt0r::LuaExec
{
    void        Init();
    bool        Ready();
    const char* Status();
    int         Run(const char* code);
}
