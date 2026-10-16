// ============================================================================
//  C4pt0r Spoofer — spoof.h : operacoes de spoof (user-mode, requer admin)
// ============================================================================
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <winsock2.h>
#include <iphlpapi.h>
#include <winioctl.h>
#include <ShlObj.h>
#include <string>
#include <vector>
#include <functional>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ole32.lib")

static std::wstring ToWideS(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}
static std::string NarrowS(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}

struct SpoofResult {
    std::string name;
    bool ok;
    std::string detail;
};

// ---- RNG / ids -----------------------------------------------------------------
static std::string RandHex(int len) {
    static const char* h = "0123456789ABCDEF";
    std::string s;
    BYTE b[64];
    HCRYPTPROV prov = 0;
    if (CryptAcquireContextW(&prov, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        CryptGenRandom(prov, (DWORD)sizeof(b), b);
        CryptReleaseContext(prov, 0);
    }
    for (int i = 0; i < len; i++) {
        s += h[b[i % 64] & 0x0F];
    }
    return s;
}
static std::string RandGuid() {
    return "{" + RandHex(8) + "-" + RandHex(4) + "-" + RandHex(4) + "-" + RandHex(4)
        + "-" + RandHex(12) + "}";
}
// MAC localmente administrado (2o nibble = 2/6/A/E)
static std::string RandMac() {
    std::string m = "02" + RandHex(10);
    return m;
}

// ---- Registro ------------------------------------------------------------------
static bool SetRegStr(HKEY root, const wchar_t* key, const wchar_t* val, const std::wstring& data) {
    HKEY h;
    if (RegOpenKeyExW(root, key, 0, KEY_SET_VALUE, &h) != ERROR_SUCCESS) return false;
    LONG rc = RegSetValueExW(h, val, 0, REG_SZ, (const BYTE*)data.c_str(),
        (DWORD)((data.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(h);
    return rc == ERROR_SUCCESS;
}
static bool SetRegDword(HKEY root, const wchar_t* key, const wchar_t* val, DWORD data) {
    HKEY h;
    if (RegOpenKeyExW(root, key, 0, KEY_SET_VALUE, &h) != ERROR_SUCCESS) return false;
    LONG rc = RegSetValueExW(h, val, 0, REG_DWORD, (const BYTE*)&data, sizeof(data));
    RegCloseKey(h);
    return rc == ERROR_SUCCESS;
}
static bool SetRegQword(HKEY root, const wchar_t* key, const wchar_t* val, ULONGLONG data) {
    HKEY h;
    if (RegOpenKeyExW(root, key, 0, KEY_SET_VALUE, &h) != ERROR_SUCCESS) return false;
    LONG rc = RegSetValueExW(h, val, 0, REG_QWORD, (const BYTE*)&data, sizeof(data));
    RegCloseKey(h);
    return rc == ERROR_SUCCESS;
}
static std::wstring GetRegStr(HKEY root, const wchar_t* key, const wchar_t* val) {
    HKEY h;
    if (RegOpenKeyExW(root, key, 0, KEY_QUERY_VALUE, &h) != ERROR_SUCCESS) return L"";
    wchar_t buf[1024]; DWORD sz = sizeof(buf), type = 0;
    std::wstring out;
    if (RegQueryValueExW(h, val, nullptr, &type, (BYTE*)buf, &sz) == ERROR_SUCCESS && type == REG_SZ)
        out = buf;
    RegCloseKey(h);
    return out;
}

// ---- Comando silencioso ---------------------------------------------------------
static int RunCmd(const std::wstring& cmd) {
    STARTUPINFOW si{}; si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::wstring c = L"cmd.exe /c " + cmd;
    if (!CreateProcessW(nullptr, c.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return -1;
    WaitForSingleObject(pi.hProcess, 15000);
    DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return (int)code;
}

// ---- Leitura dos IDs atuais -------------------------------------------------------
struct CurrentIds {
    std::wstring machineGuid, hwProfile, computerName;
    std::vector<std::pair<std::wstring, std::wstring>> macs;   // nome, mac
};

static CurrentIds ReadCurrentIds() {
    CurrentIds ids;
    ids.machineGuid = GetRegStr(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Cryptography", L"MachineGuid");
    ids.hwProfile = GetRegStr(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\IDConfigDB\\Hardware Profiles\\0001",
        L"HwProfileGuid");
    wchar_t cn[256] = {}; DWORD sz = 256;
    GetComputerNameW(cn, &sz);
    ids.computerName = cn;

    ULONG bufLen = 64 * 1024;
    std::vector<BYTE> buf(bufLen);
    IP_ADAPTER_ADDRESSES* addrs = (IP_ADAPTER_ADDRESSES*)buf.data();
    if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
        GAA_FLAG_SKIP_DNS_SERVER, nullptr, addrs, &bufLen) == NO_ERROR) {
        for (auto* a = addrs; a; a = a->Next) {
            if (a->IfType != IF_TYPE_ETHERNET_CSMACD &&
                a->IfType != IF_TYPE_IEEE80211) continue;
            if (a->PhysicalAddressLength < 6) continue;
            wchar_t mac[32];
            swprintf_s(mac, L"%02X:%02X:%02X:%02X:%02X:%02X",
                a->PhysicalAddress[0], a->PhysicalAddress[1], a->PhysicalAddress[2],
                a->PhysicalAddress[3], a->PhysicalAddress[4], a->PhysicalAddress[5]);
            ids.macs.push_back({ a->FriendlyName, mac });
        }
    }
    return ids;
}

// ---- Backup / restore --------------------------------------------------------------
struct BackupEntry { std::wstring key, val, data; DWORD type; ULONGLONG qv; };
static std::wstring BackupPath() {
    wchar_t* app = nullptr;
    SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &app);
    std::wstring dir = app; CoTaskMemFree(app);
    dir += L"\\C4pt0r";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\spoofer-backup.txt";
}
static bool ReadRegEntry(HKEY root, const wchar_t* key, const wchar_t* val,
    std::wstring& outStr, DWORD& outType, ULONGLONG& outQ) {
    HKEY h;
    if (RegOpenKeyExW(root, key, 0, KEY_QUERY_VALUE, &h) != ERROR_SUCCESS) return false;
    outType = 0; outQ = 0;
    BYTE buf[2048]; DWORD sz = sizeof(buf);
    if (RegQueryValueExW(h, val, nullptr, &outType, buf, &sz) == ERROR_SUCCESS) {
        if (outType == REG_SZ) outStr = (wchar_t*)buf;
        else if (outType == REG_DWORD) outQ = *(DWORD*)buf;
        else if (outType == REG_QWORD) outQ = *(ULONGLONG*)buf;
    }
    RegCloseKey(h);
    return true;
}
// backup simples em texto: root|key|val|type|data
static void BackupReg(std::vector<std::wstring>& lines, HKEY root, const wchar_t* rootName,
    const wchar_t* key, const wchar_t* val) {
    std::wstring s; DWORD t; ULONGLONG q;
    if (!ReadRegEntry(root, key, val, s, t, q)) return;
    std::wstring line = rootName; line += L"|"; line += key; line += L"|"; line += val;
    line += L"|" + std::to_wstring(t) + L"|";
    if (t == REG_SZ) line += s;
    else line += std::to_wstring(q);
    lines.push_back(line);
}
static void WriteBackup(const std::vector<std::wstring>& lines) {
    HANDLE f = CreateFileW(BackupPath().c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    for (auto& l : lines) {
        std::string u; int n = WideCharToMultiByte(CP_UTF8, 0, l.c_str(), -1, nullptr, 0, nullptr, nullptr);
        u.resize(n - 1);
        WideCharToMultiByte(CP_UTF8, 0, l.c_str(), -1, u.data(), n, nullptr, nullptr);
        DWORD w; WriteFile(f, u.data(), (DWORD)u.size(), &w, nullptr);
        WriteFile(f, "\n", 1, &w, nullptr);
    }
    CloseHandle(f);
}
static bool HasBackup() {
    return GetFileAttributesW(BackupPath().c_str()) != INVALID_FILE_ATTRIBUTES;
}
static std::vector<std::wstring> ReadBackup() {
    std::vector<std::wstring> out;
    HANDLE f = CreateFileW(BackupPath().c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return out;
    DWORD sz = GetFileSize(f, nullptr);
    if (!sz || sz > 512 * 1024) { CloseHandle(f); return out; }
    std::string buf(sz, 0);
    DWORD r; ReadFile(f, buf.data(), sz, &r, nullptr); CloseHandle(f);
    // utf8 -> wide por linha
    size_t pos = 0;
    while (pos < buf.size()) {
        size_t eol = buf.find('\n', pos);
        if (eol == std::string::npos) eol = buf.size();
        std::string line = buf.substr(pos, eol - pos);
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (!line.empty()) {
            int n = MultiByteToWideChar(CP_UTF8, 0, line.c_str(), -1, nullptr, 0);
            std::wstring w(n - 1, 0);
            MultiByteToWideChar(CP_UTF8, 0, line.c_str(), -1, w.data(), n);
            out.push_back(w);
        }
        pos = eol + 1;
    }
    return out;
}

// ---- Operacoes de spoof -------------------------------------------------------------

// MachineGuid + SQM + HwProfile + ProductId/Install
static std::vector<SpoofResult> SpoofRegistry(bool& rebootNeeded, std::vector<std::wstring>* backup) {
    std::vector<SpoofResult> res;
    rebootNeeded = false;

    std::wstring newGuid = ToWideS(RandHex(8) + "-" + RandHex(4) + "-" + RandHex(4)
        + "-" + RandHex(4) + "-" + RandHex(12));
    std::wstring newGuidB = L"{" + newGuid + L"}";

    struct K { HKEY root; const wchar_t* rootN; const wchar_t* key; const wchar_t* val;
               std::wstring data; const char* name; };
    std::vector<K> ops = {
        {HKEY_LOCAL_MACHINE, L"HKLM", L"SOFTWARE\\Microsoft\\Cryptography",
            L"MachineGuid", newGuid, "MachineGuid"},
        {HKEY_LOCAL_MACHINE, L"HKLM", L"SOFTWARE\\Microsoft\\SQMClient",
            L"MachineId", newGuidB, "SQM MachineId (HKLM)"},
        {HKEY_CURRENT_USER, L"HKCU", L"SOFTWARE\\Microsoft\\SQMClient",
            L"MachineId", newGuidB, "SQM MachineId (HKCU)"},
        {HKEY_LOCAL_MACHINE, L"HKLM",
            L"SYSTEM\\CurrentControlSet\\Control\\IDConfigDB\\Hardware Profiles\\0001",
            L"HwProfileGuid", newGuidB, "HwProfileGuid"},
        {HKEY_LOCAL_MACHINE, L"HKLM",
            L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
            L"ProductId", ToWideS(RandHex(5) + "-" + RandHex(5) + "-" + RandHex(5)
                + "-" + RandHex(5)), "ProductId"},
        {HKEY_LOCAL_MACHINE, L"HKLM",
            L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
            L"BuildGUID", newGuid, "BuildGUID"},
    };
    for (auto& o : ops) {
        if (backup) BackupReg(*backup, o.root, o.rootN, o.key, o.val);
        bool ok = SetRegStr(o.root, o.key, o.val, o.data);
        res.push_back({ o.name, ok, ok ? "" : "falha (permissao?)" });
    }

    // InstallDate (dword) + InstallTime (qword) — timestamps aleatorios passados
    DWORD install = 0x5F000000 + (RandHex(6)[0] & 0x0F) * 0x10000 + 0x12345;
    if (backup) BackupReg(*backup, HKEY_LOCAL_MACHINE, L"HKLM",
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"InstallDate");
    bool ok = SetRegDword(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"InstallDate", install);
    res.push_back({ "InstallDate", ok, ok ? "" : "falha" });

    ULONGLONG ft = 132000000000000000ULL + (ULONGLONG)(RandHex(8)[0]);
    if (backup) BackupReg(*backup, HKEY_LOCAL_MACHINE, L"HKLM",
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"InstallTime");
    ok = SetRegQword(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"InstallTime", ft);
    res.push_back({ "InstallTime", ok, ok ? "" : "falha" });

    return res;
}

// MAC de todos os adaptadores fisicos + restart
static std::vector<SpoofResult> SpoofMac() {
    std::vector<SpoofResult> res;
    ULONG bufLen = 64 * 1024;
    std::vector<BYTE> buf(bufLen);
    IP_ADAPTER_ADDRESSES* addrs = (IP_ADAPTER_ADDRESSES*)buf.data();
    if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
        GAA_FLAG_SKIP_DNS_SERVER, nullptr, addrs, &bufLen) != NO_ERROR) {
        res.push_back({ "MAC", false, "sem acesso aos adaptadores" });
        return res;
    }
    const wchar_t* classKey =
        L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4D36E972-E325-11CE-BFC1-08002BE10318}";

    for (auto* a = addrs; a; a = a->Next) {
        if (a->IfType != IF_TYPE_ETHERNET_CSMACD && a->IfType != IF_TYPE_IEEE80211) continue;
        if (a->PhysicalAddressLength < 6) continue;

        // acha a subchave 000N cujo NetCfgInstanceId == AdapterName (guid)
        std::wstring regPath;
        for (int i = 0; i < 64; i++) {
            wchar_t sub[16]; swprintf_s(sub, L"%04d", i);
            std::wstring p = std::wstring(classKey) + L"\\" + sub;
            std::wstring inst = GetRegStr(HKEY_LOCAL_MACHINE, p.c_str(), L"NetCfgInstanceId");
            if (inst.empty()) { if (i > 8) break; continue; }
            std::string instA(inst.begin(), inst.end());
            if (_stricmp(instA.c_str(), a->AdapterName) == 0) { regPath = p; break; }
        }
        if (regPath.empty()) {
            std::string nm(a->FriendlyName, a->FriendlyName + wcslen(a->FriendlyName));
            res.push_back({ "MAC " + nm, false, "chave nao encontrada" });
            continue;
        }
        std::string nm(a->FriendlyName, a->FriendlyName + wcslen(a->FriendlyName));
        std::wstring newMac = ToWideS(RandMac());
        bool ok = SetRegStr(HKEY_LOCAL_MACHINE, regPath.c_str(), L"NetworkAddress", newMac);
        if (ok) {
            // reinicia adaptador pra aplicar
            std::wstring dev = L"netsh interface set interface \"";
            dev += a->FriendlyName;
            RunCmd(dev + L"\" admin=disable");
            Sleep(1200);
            RunCmd(dev + L"\" admin=enable");
            res.push_back({ "MAC " + nm, true,
                NarrowS(newMac).substr(0, 2) + ":" + NarrowS(newMac).substr(2, 2) + ":" +
                NarrowS(newMac).substr(4, 2) + ":" + NarrowS(newMac).substr(6, 2) + ":" +
                NarrowS(newMac).substr(8, 2) + ":" + NarrowS(newMac).substr(10, 2) });
        } else {
            res.push_back({ "MAC " + nm, false, "falha ao gravar" });
        }
    }
    if (res.empty()) res.push_back({ "MAC", false, "nenhum adaptador fisico" });
    return res;
}

// Serial do volume do drive de sistema (boot sector NTFS, offset 0x48)
static SpoofResult SpoofVolumeSerial(const wchar_t* drive) {
    SpoofResult r{ "Serial do volume", false, "" };
    std::wstring path = std::wstring(L"\\\\.\\") + drive;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) { r.detail = "sem acesso ao volume"; return r; }

    BYTE sec[512];
    DWORD br = 0;
    if (!ReadFile(h, sec, sizeof(sec), &br, nullptr) || br < 512) {
        r.detail = "leitura do boot sector falhou"; CloseHandle(h); return r;
    }
    if (memcmp(sec + 3, "NTFS", 4) != 0) {
        r.detail = "drive nao e NTFS"; CloseHandle(h); return r;
    }
    // novo serial de 8 bytes em 0x48
    std::string hex = RandHex(16);
    for (int i = 0; i < 8; i++)
        sec[0x48 + i] = (BYTE)strtoul(hex.substr(i * 2, 2).c_str(), nullptr, 16);

    DWORD ret = 0;
    DeviceIoControl(h, FSCTL_LOCK_VOLUME, nullptr, 0, nullptr, 0, &ret, nullptr);
    DWORD bw = 0;
    SetFilePointer(h, 0, nullptr, FILE_BEGIN);
    r.ok = WriteFile(h, sec, sizeof(sec), &bw, nullptr) && bw == 512;
    DeviceIoControl(h, FSCTL_UNLOCK_VOLUME, nullptr, 0, nullptr, 0, &ret, nullptr);
    CloseHandle(h);
    if (r.ok) r.detail = "serial alterado (reboot p/ refletir)";
    else r.detail = "escrita bloqueada (AV/EDR?)";
    return r;
}

// Nome do computador
static SpoofResult SpoofHostname(std::vector<std::wstring>* backup) {
    SpoofResult r{ "Nome do PC", false, "" };
    std::wstring newName = L"DESKTOP-" + ToWideS(RandHex(7));
    const wchar_t* keys[] = {
        L"SYSTEM\\CurrentControlSet\\Control\\ComputerName\\ComputerName",
        L"SYSTEM\\CurrentControlSet\\Control\\ComputerName\\ActiveComputerName",
    };
    bool ok = true;
    for (auto k : keys) {
        if (backup) BackupReg(*backup, HKEY_LOCAL_MACHINE, L"HKLM", k, L"ComputerName");
        if (!SetRegStr(HKEY_LOCAL_MACHINE, k, L"ComputerName", newName)) ok = false;
    }
    if (backup) BackupReg(*backup, HKEY_LOCAL_MACHINE, L"HKLM",
        L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters", L"Hostname");
    if (!SetRegStr(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters", L"Hostname", newName))
        ok = false;
    if (!SetRegStr(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters", L"NV Hostname", newName))
        ok = false;
    r.ok = ok;
    if (ok) r.detail = NarrowS(newName) + " (reboot p/ aplicar)";
    else r.detail = "falha (permissao?)";
    return r;
}

// Limpa rastros locais do FiveM/launchers
static void DeleteTree(const std::wstring& dir) {
    std::wstring pat = dir + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) { RemoveDirectoryW(dir.c_str()); return; }
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        std::wstring p = dir + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) DeleteTree(p);
        else { SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL); DeleteFileW(p.c_str()); }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    RemoveDirectoryW(dir.c_str());
}
static SpoofResult CleanFiveMTraces() {
    SpoofResult r{ "Limpeza FiveM", true, "" };
    wchar_t* la = nullptr;
    SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &la);
    std::wstring base = la ? la : L"";
    CoTaskMemFree(la);
    int n = 0;
    const wchar_t* rel[] = {
        L"\\Digital Entitlements",
        L"\\FiveM\\FiveM.app\\cache\\browser",
        L"\\FiveM\\FiveM.app\\cache\\priv",
        L"\\FiveM\\FiveM.app\\data\\server-cache",
        L"\\FiveM\\FiveM.app\\data\\server-cache-priv",
        L"\\FiveM\\FiveM.app\\data\\nui-storage",
        L"\\FiveM\\FiveM.app\\logs",
        L"\\FiveM\\FiveM.app\\crashes",
    };
    for (auto p : rel) {
        std::wstring full = base + p;
        if (GetFileAttributesW(full.c_str()) != INVALID_FILE_ATTRIBUTES) {
            DeleteTree(full);
            n++;
        }
    }
    // registros do CitizenFX
    RegDeleteTreeW(HKEY_CURRENT_USER, L"SOFTWARE\\CitizenFX");
    RunCmd(L"ipconfig /flushdns >nul 2>&1");
    char d[32]; snprintf(d, 32, "%d locais limpos + DNS flush", n);
    r.detail = d;
    return r;
}

// ---- Restore ------------------------------------------------------------------------
static std::vector<SpoofResult> RestoreBackup() {
    std::vector<SpoofResult> res;
    auto lines = ReadBackup();
    if (lines.empty()) { res.push_back({ "Restore", false, "sem backup salvo" }); return res; }
    int ok = 0, fail = 0;
    for (auto& l : lines) {
        // root|key|val|type|data
        std::vector<std::wstring> parts;
        size_t pos = 0;
        while (true) {
            size_t p = l.find(L'|', pos);
            if (p == std::wstring::npos) { parts.push_back(l.substr(pos)); break; }
            parts.push_back(l.substr(pos, p - pos)); pos = p + 1;
        }
        if (parts.size() < 5) continue;
        HKEY root = parts[0] == L"HKCU" ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
        DWORD type = (DWORD)_wtoi(parts[3].c_str());
        bool ok1 = false;
        if (type == REG_SZ) ok1 = SetRegStr(root, parts[1].c_str(), parts[2].c_str(), parts[4]);
        else if (type == REG_DWORD) ok1 = SetRegDword(root, parts[1].c_str(), parts[2].c_str(),
            (DWORD)_wcstoui64(parts[4].c_str(), nullptr, 10));
        else if (type == REG_QWORD) ok1 = SetRegQword(root, parts[1].c_str(), parts[2].c_str(),
            _wcstoui64(parts[4].c_str(), nullptr, 10));
        ok1 ? ok++ : fail++;
    }
    char d[64]; snprintf(d, 64, "%d restaurados, %d falhas", ok, fail);
    res.push_back({ "Restore", fail == 0, d });
    return res;
}
