#include "features/pc.h"

#include "common/util.h"
#include "features/media.h"
#include "web/bridge.h"

#include <windows.h>
#include <commdlg.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <msdasc.h>
#include <oledb.h>
#include <shellapi.h>

#include <algorithm>
#include <cstddef>
#include <cwctype>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

namespace pc {
namespace {

std::set<std::wstring> g_found; // chemins (en minuscules) déjà renvoyés à la page
std::mutex g_foundMutex;

std::wstring Lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return (wchar_t)std::towlower(c); });
    return s;
}

void Reply(const std::string& id, const std::string& fields) {
    bridge::PostToPage("{\"t\":\"reply\",\"id\":" + util::Json(id) + "," + fields + "}");
}

void ReplyError(const std::string& id, const std::string& message) {
    Reply(id, "\"ok\":false,\"error\":" + util::Json(message));
}

// ------------------------------------------------------------------ requête SQL

// Les mots viennent de Claude : on ne garde que lettres, chiffres, - et _ (rien qui puisse
// sortir des guillemets de la requête).
std::vector<std::wstring> Words(const std::wstring& text) {
    std::vector<std::wstring> out;
    std::wstring cur;
    auto flush = [&] {
        if (!cur.empty() && out.size() < 6) out.push_back(cur);
        cur.clear();
    };
    for (wchar_t c : text) {
        if (std::iswalnum(c) || c == L'-' || c == L'_') cur += c;
        else flush();
    }
    flush();
    return out;
}

bool ParseDate(const std::string& s, SYSTEMTIME& st) {
    if (s.size() != 10 || s[4] != '-' || s[7] != '-') return false;
    for (int i : {0, 1, 2, 3, 5, 6, 8, 9})
        if (!std::isdigit((unsigned char)s[i])) return false;
    st = {};
    st.wYear = (WORD)std::atoi(s.substr(0, 4).c_str());
    st.wMonth = (WORD)std::atoi(s.substr(5, 2).c_str());
    st.wDay = (WORD)std::atoi(s.substr(8, 2).c_str());
    FILETIME ft;
    return SystemTimeToFileTime(&st, &ft); // refuse les dates impossibles
}

std::wstring DateText(const SYSTEMTIME& st) {
    wchar_t buf[16];
    swprintf(buf, 16, L"%04u-%02u-%02u", st.wYear, st.wMonth, st.wDay);
    return buf;
}

SYSTEMTIME NextDay(const SYSTEMTIME& st) {
    FILETIME ft;
    SystemTimeToFileTime(&st, &ft);
    ULARGE_INTEGER u{{ft.dwLowDateTime, ft.dwHighDateTime}};
    u.QuadPart += 864000000000ULL; // 24 h en unités de 100 ns
    ft = {u.LowPart, u.HighPart};
    SYSTEMTIME out;
    FileTimeToSystemTime(&ft, &out);
    return out;
}

// Retourne une requête vide si aucun critère n'est utilisable.
std::wstring BuildQuery(const std::string& words, const std::string& ext, const std::string& from,
                        const std::string& to) {
    std::wstring where;
    for (const auto& w : Words(util::Widen(words)))
        where += L" AND (CONTAINS(System.FileName, '\"" + w + L"*\"') OR CONTAINS(*, '\"" + w + L"*\"'))";
    std::wstring e;
    for (char c : ext)
        if (std::isalnum((unsigned char)c) && e.size() < 8) e += (wchar_t)std::tolower((unsigned char)c);
    if (!e.empty()) where += L" AND System.FileExtension = '." + e + L"'";
    SYSTEMTIME st;
    if (ParseDate(from, st)) where += L" AND System.DateModified >= '" + DateText(st) + L"'";
    if (ParseDate(to, st)) where += L" AND System.DateModified < '" + DateText(NextDay(st)) + L"'";
    if (where.empty()) return {};

    std::wstring scope = util::KnownFolder(FOLDERID_Profile);
    std::replace(scope.begin(), scope.end(), L'\\', L'/');
    size_t q = 0;
    while ((q = scope.find(L'\'', q)) != std::wstring::npos) scope.insert(q, 1, L'\''), q += 2;
    return L"SELECT TOP 15 System.ItemPathDisplay, System.DateModified, System.Size FROM SystemIndex"
           L" WHERE SCOPE='file:" + scope + L"' AND System.ItemType <> 'Directory'"
           L" AND NOT System.ItemPathDisplay LIKE '%\\AppData\\%'" + where +
           L" ORDER BY System.DateModified DESC";
}

// ------------------------------------------------------------------ OLE DB

// DBGUID_DEFAULT (dialecte SQL par défaut) : absent des bibliothèques de MinGW.
constexpr GUID kDialectDefault = {0xc8b521fb, 0x5cf3, 0x11ce, {0xad, 0xe5, 0x00, 0xaa, 0x00, 0x44, 0x77, 0x3d}};

template <class T>
struct Com {
    T* p = nullptr;
    ~Com() { if (p) p->Release(); }
    T** put() { return &p; }
    T* operator->() const { return p; }
};

struct Row {
    DBSTATUS status[3];
    VARIANT value[3];
};

long long VariantEpochMs(const VARIANT& v) {
    FILETIME ft{};
    if (v.vt == VT_FILETIME) { // pas un membre de VARIANT : les 8 octets sont dans ullVal
        ft = {(DWORD)(v.ullVal & 0xFFFFFFFF), (DWORD)(v.ullVal >> 32)};
    } else if (v.vt == VT_DATE) {
        SYSTEMTIME st;
        if (!VariantTimeToSystemTime(v.date, &st) || !SystemTimeToFileTime(&st, &ft)) return 0;
    } else {
        return 0;
    }
    ULARGE_INTEGER u{{ft.dwLowDateTime, ft.dwHighDateTime}};
    return u.QuadPart > 116444736000000000ULL ? (long long)((u.QuadPart - 116444736000000000ULL) / 10000) : 0;
}

long long VariantNumber(const VARIANT& v) {
    VARIANT tmp;
    VariantInit(&tmp);
    long long n = 0;
    if (SUCCEEDED(VariantChangeType(&tmp, const_cast<VARIANT*>(&v), 0, VT_R8))) n = (long long)tmp.dblVal;
    VariantClear(&tmp);
    return n;
}

// Exécute la requête ; remplit json (tableau) ou retourne un message d'erreur.
std::string RunQuery(const std::wstring& sql, std::string& json) {
    Com<IDataInitialize> init;
    if (FAILED(CoCreateInstance(CLSID_MSDAINITIALIZE, nullptr, CLSCTX_INPROC_SERVER, IID_IDataInitialize,
                                (void**)init.put())))
        return "OLE DB est indisponible.";
    Com<IDBInitialize> db;
    wchar_t conn[] = L"Provider=Search.CollatorDSO;Extended Properties='Application=Windows';";
    if (FAILED(init->GetDataSource(nullptr, CLSCTX_INPROC_SERVER, conn, IID_IDBInitialize, (IUnknown**)db.put())) ||
        FAILED(db->Initialize()))
        return "La recherche Windows est indisponible (service Windows Search arrêté ?).";
    Com<IDBCreateSession> sessionFactory;
    Com<IDBCreateCommand> commandFactory;
    Com<ICommandText> command;
    Com<IRowset> rows;
    if (FAILED(db->QueryInterface(IID_IDBCreateSession, (void**)sessionFactory.put())) ||
        FAILED(sessionFactory->CreateSession(nullptr, IID_IDBCreateCommand, (IUnknown**)commandFactory.put())) ||
        FAILED(commandFactory->CreateCommand(nullptr, IID_ICommandText, (IUnknown**)command.put())) ||
        FAILED(command->SetCommandText(kDialectDefault, sql.c_str())) ||
        FAILED(command->Execute(nullptr, IID_IRowset, nullptr, nullptr, (IUnknown**)rows.put())) || !rows.p)
        return "La recherche a échoué.";

    DBBINDING bind[3]{};
    for (int i = 0; i < 3; ++i) {
        bind[i].iOrdinal = i + 1;
        bind[i].obValue = offsetof(Row, value) + i * sizeof(VARIANT);
        bind[i].obStatus = offsetof(Row, status) + i * sizeof(DBSTATUS);
        bind[i].dwPart = DBPART_VALUE | DBPART_STATUS;
        bind[i].dwMemOwner = DBMEMOWNER_CLIENTOWNED;
        bind[i].eParamIO = DBPARAMIO_NOTPARAM;
        bind[i].cbMaxLen = sizeof(VARIANT);
        bind[i].wType = DBTYPE_VARIANT;
    }
    Com<IAccessor> accessor;
    HACCESSOR hAcc = 0;
    if (FAILED(rows->QueryInterface(IID_IAccessor, (void**)accessor.put())) ||
        FAILED(accessor->CreateAccessor(DBACCESSOR_ROWDATA, 3, bind, sizeof(Row), &hAcc, nullptr)))
        return "Lecture des résultats impossible.";

    json = "[";
    bool first = true;
    std::vector<std::wstring> found;
    for (;;) {
        HROW handles[10];
        HROW* ph = handles;
        DBCOUNTITEM got = 0;
        HRESULT hr = rows->GetNextRows(DB_NULL_HCHAPTER, 0, 10, &got, &ph);
        for (DBCOUNTITEM i = 0; i < got; ++i) {
            Row r{};
            for (auto& v : r.value) VariantInit(&v);
            if (SUCCEEDED(rows->GetData(handles[i], hAcc, &r)) && r.status[0] == DBSTATUS_S_OK &&
                r.value[0].vt == VT_BSTR) {
                std::wstring path = r.value[0].bstrVal;
                const wchar_t* slash = wcsrchr(path.c_str(), L'\\');
                json += std::string(first ? "" : ",") + "{\"path\":" + util::Json(util::Narrow(path)) +
                        ",\"name\":" + util::Json(util::Narrow(slash ? slash + 1 : path.c_str())) +
                        ",\"modified\":" + std::to_string(VariantEpochMs(r.value[1])) +
                        ",\"size\":" + std::to_string(VariantNumber(r.value[2])) + "}";
                first = false;
                found.push_back(Lower(path));
            }
            for (auto& v : r.value) VariantClear(&v);
        }
        if (got) rows->ReleaseRows(got, handles, nullptr, nullptr, nullptr);
        if (hr != S_OK || !got) break; // DB_S_ENDOFROWSET
    }
    accessor->ReleaseAccessor(hAcc, nullptr);
    json += "]";
    std::lock_guard lock(g_foundMutex);
    if (g_found.size() > 2000) g_found.clear();
    g_found.insert(found.begin(), found.end());
    return {};
}

// ------------------------------------------------------------------ ouverture

bool IsRunnable(const std::wstring& lowerPath) {
    static const wchar_t* kExt[] = {L".exe", L".com", L".bat", L".cmd", L".ps1", L".psm1", L".vbs", L".vbe",
                                    L".js",  L".jse", L".wsf", L".wsh", L".msi", L".msp",  L".scr", L".lnk",
                                    L".url", L".hta", L".cpl", L".jar", L".reg", L".pif",  L".appref-ms"};
    const wchar_t* dot = wcsrchr(lowerPath.c_str(), L'.');
    if (!dot) return false;
    for (auto e : kExt)
        if (!wcscmp(dot, e)) return true;
    return false;
}

} // namespace

void Search(const std::string& payload) {
    auto f = util::Split(payload, '\n', 5);
    if (f.empty() || f[0].empty()) return;
    f.resize(5);
    std::wstring sql = BuildQuery(f[1], f[2], f[3], f[4]);
    if (sql.empty()) {
        ReplyError(f[0], "Donne au moins un mot, une extension ou une date.");
        return;
    }
    std::thread([id = f[0], sql] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        std::string json;
        std::string err = RunQuery(sql, json);
        if (err.empty()) Reply(id, "\"ok\":true,\"results\":" + json);
        else ReplyError(id, err);
        CoUninitialize();
    }).detach();
}

void OpenFound(const std::string& path) {
    std::wstring p = util::Widen(path), low = Lower(p);
    {
        std::lock_guard lock(g_foundMutex);
        if (!g_found.count(low)) return;
    }
    if (IsRunnable(low)) {
        std::wstring args = L"/select,\"" + p + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    } else {
        ShellExecuteW(nullptr, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}

void RunRoutine(const std::string& steps) {
    std::thread([lines = util::Split(steps, '\n')] {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); // ShellExecute et le volume utilisent COM
        for (const auto& line : lines) {
            auto f = util::Split(line, '\t', 2);
            if (f.size() != 2) continue;
            const std::string &type = f[0], &value = f[1];
            if (type == "open" && !value.empty()) {
                ShellExecuteW(nullptr, L"open", util::Widen(value).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            } else if (type == "volume") {
                SetVolume(std::atoi(value.c_str()));
            } else if (type == "music") {
                bool playing = media::IsPlaying();
                if ((value == "play" && !playing) || (value == "pause" && playing)) media::Command("toggle");
                else if (value == "next") media::Command("next");
            } else if (type == "wait") {
                Sleep((DWORD)std::clamp(std::atoi(value.c_str()), 0, 60) * 1000);
            }
        }
        CoUninitialize();
    }).detach();
}

void SetVolume(int percent) {
    percent = std::clamp(percent, 0, 100);
    Com<IMMDeviceEnumerator> devices;
    Com<IMMDevice> device;
    Com<IAudioEndpointVolume> volume;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                (void**)devices.put())) ||
        FAILED(devices->GetDefaultAudioEndpoint(eRender, eConsole, device.put())) ||
        FAILED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, (void**)volume.put())))
        return;
    volume->SetMasterVolumeLevelScalar(percent / 100.0f, nullptr);
    volume->SetMute(percent == 0, nullptr);
}

void PickFile(const std::string& id, HWND owner) {
    // Fenêtre modale : dans son propre thread, pour ne pas bloquer l'évènement WebView2 en cours.
    std::thread([id, owner] {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        wchar_t file[MAX_PATH] = L"";
        OPENFILENAMEW ofn{sizeof ofn};
        ofn.hwndOwner = owner;
        ofn.lpstrFilter = L"Applications et raccourcis\0*.exe;*.lnk;*.url\0Tous les fichiers\0*.*\0";
        ofn.lpstrFile = file;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrTitle = L"Choisir ce que la routine ouvre";
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_NODEREFERENCELINKS | OFN_NOCHANGEDIR;
        bool ok = GetOpenFileNameW(&ofn);
        Reply(id, std::string("\"ok\":true,\"path\":") + util::Json(ok ? util::Narrow(file) : ""));
        CoUninitialize();
    }).detach();
}

} // namespace pc
