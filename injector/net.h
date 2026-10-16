// ============================================================================
//  C4pt0r — net.h : HTTPS via WinHTTP + parser JSON minimo
//  Sem dependencias externas.
// ============================================================================
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <winhttp.h>
#include <string>
#include <vector>
#include <map>
#include <memory>

#pragma comment(lib, "winhttp.lib")

// ---- JSON minimo -----------------------------------------------------------
struct Json {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> a;
    std::map<std::string, Json> o;

    const Json& operator[](const char* k) const {
        static Json nil;
        auto it = o.find(k);
        return it != o.end() ? it->second : nil;
    }
    std::string str() const {
        if (type == Str) return s;
        if (type == Num) {
            char buf[32]; snprintf(buf, 32, "%g", n); return buf;
        }
        if (type == Bool) return b ? "true" : "false";
        return "";
    }
    bool truthy() const { return type == Bool ? b : type == Num ? n != 0 : !str().empty() || type == Arr || type == Obj; }
};

static Json ParseJson(const char*& p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    Json j;
    switch (*p) {
    case '{':
        j.type = Json::Obj; p++;
        while (*p && *p != '}') {
            while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ',') p++;
            if (*p == '}') break;
            Json key = ParseJson(p);
            if (*p == ':') p++;
            j.o[key.s] = ParseJson(p);
        }
        if (*p == '}') p++;
        return j;
    case '[':
        j.type = Json::Arr; p++;
        while (*p && *p != ']') {
            while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ',') p++;
            if (*p == ']') break;
            j.a.push_back(ParseJson(p));
        }
        if (*p == ']') p++;
        return j;
    case '"': {
        j.type = Json::Str; p++;
        while (*p && *p != '"') {
            if (*p == '\\' && p[1]) {
                p++;
                switch (*p) {
                case 'n': j.s += '\n'; break;
                case 't': j.s += '\t'; break;
                case 'r': j.s += '\r'; break;
                case 'u': {
                    // \uXXXX — pega so ASCII (suficiente aqui)
                    int cp = (int)strtol(std::string(p + 1, 4).c_str(), nullptr, 16);
                    j.s += (cp < 128) ? (char)cp : '?';
                    p += 4;
                    break;
                }
                default: j.s += *p;
                }
                p++;
            } else j.s += *p++;
        }
        if (*p == '"') p++;
        return j;
    }
    case 't': j.type = Json::Bool; j.b = true; p += 4; return j;
    case 'f': j.type = Json::Bool; j.b = false; p += 5; return j;
    case 'n': j.type = Json::Null; p += 4; return j;
    default:
        j.type = Json::Num; j.n = strtod(p, (char**)&p); return j;
    }
}

static Json JsonParse(const std::string& s) {
    const char* p = s.c_str();
    return ParseJson(p);
}

// ---- HTTPS -----------------------------------------------------------------
struct HttpRes {
    long status = 0;
    std::string body;
};

static std::wstring ToWide(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

// host sem https:// — ex "bzthdklqvjtutkeiimpq.supabase.co"
static HttpRes Http(const std::string& host, const std::string& method,
                     const std::string& path,
                     const std::vector<std::pair<std::string, std::string>>& headers = {},
                     const std::string& body = "") {
    HttpRes out;
    HINTERNET hSes = WinHttpOpen(L"C4pt0rLoader/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, nullptr, nullptr, 0);
    if (!hSes) return out;
    HINTERNET hCon = WinHttpConnect(hSes, ToWide(host).c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hCon) { WinHttpCloseHandle(hSes); return out; }
    HINTERNET hReq = WinHttpOpenRequest(hCon, ToWide(method).c_str(), ToWide(path).c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hReq) { WinHttpCloseHandle(hCon); WinHttpCloseHandle(hSes); return out; }

    DWORD secs = 25000;
    WinHttpSetOption(hReq, WINHTTP_OPTION_RECEIVE_TIMEOUT, &secs, sizeof(DWORD));
    WinHttpSetOption(hReq, WINHTTP_OPTION_SEND_TIMEOUT, &secs, sizeof(DWORD));
    WinHttpSetOption(hReq, WINHTTP_OPTION_CONNECT_TIMEOUT, &secs, sizeof(DWORD));

    std::wstring allHdr;
    for (auto& [k, v] : headers) allHdr += ToWide(k + ": " + v + "\r\n");

    BOOL ok = WinHttpSendRequest(hReq, allHdr.c_str(), (DWORD)-1,
        (LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0);
    if (ok) ok = WinHttpReceiveResponse(hReq, nullptr);

    if (ok) {
        DWORD code = 0, sz = sizeof(code);
        WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            nullptr, &code, &sz, nullptr);
        out.status = (long)code;
        DWORD avail = 0;
        while (WinHttpQueryDataAvailable(hReq, &avail) && avail) {
            std::string buf(avail, 0);
            DWORD read = 0;
            WinHttpReadData(hReq, buf.data(), avail, &read);
            out.body += buf.substr(0, read);
        }
    }

    WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hCon);
    WinHttpCloseHandle(hSes);
    return out;
}

// URL assinada: baixa de qualquer host (Supabase storage)
static HttpRes HttpDownload(const std::string& url) {
    // url completa https://host/path
    std::string u = url;
    if (u.rfind("https://", 0) == 0) u = u.substr(8);
    else if (u.rfind("http://", 0) == 0) u = u.substr(7);
    auto slash = u.find('/');
    std::string host = u.substr(0, slash);
    std::string path = slash == std::string::npos ? "/" : u.substr(slash);
    return Http(host, "GET", path);
}
