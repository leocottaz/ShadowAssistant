#include "features/media.h"

#include "common/util.h"
#include "web/bridge.h"

#include <windows.h>
#include <inspectable.h>
#include <roapi.h>
#include <shcore.h>
#include <winstring.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <thread>

// ---------------------------------------------------------------------------
// Interfaces WinRT (ABI). Les en-têtes de MinGW ne fournissent pas
// Windows.Media.Control : on déclare les vtables à la main, dans l'ordre exact des
// métadonnées Windows (C:\Windows\System32\WinMetadata\Windows.Media.winmd).
// Seules les méthodes jusqu'à la dernière utilisée sont nécessaires.
//
// Elles doivent rester dans un namespace NOMMÉ : dans un namespace anonyme, GCC
// sait qu'aucune classe du programme ne les implémente et « optimise » les appels
// virtuels en sauts vers une adresse invalide (plantage en Release uniquement).
// ---------------------------------------------------------------------------

namespace media_abi {

using boolean_t = unsigned char;

// IAsyncOperation<T> : put_Completed, get_Completed, GetResults.
template <class R>
struct IAsyncOp : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE put_Completed(void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Completed(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResults(R* results) = 0;
};

struct IAsyncInfoAbi : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Id(UINT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Status(int*) = 0; // 0 en cours, 1 terminé, 2 annulé, 3 erreur
    virtual HRESULT STDMETHODCALLTYPE get_ErrorCode(HRESULT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE Cancel() = 0;
    virtual HRESULT STDMETHODCALLTYPE Close() = 0;
};
const IID IID_IAsyncInfoAbi = {0x00000036, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

template <class T>
struct IVectorViewAbi : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetAt(UINT32 index, T* item) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Size(UINT32* size) = 0;
};

struct IStreamReferenceAbi : IInspectable { // IRandomAccessStreamReference
    virtual HRESULT STDMETHODCALLTYPE OpenReadAsync(IInspectable** operation) = 0;
};

struct IPlaybackControls : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_IsPlayEnabled(boolean_t*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsPauseEnabled(boolean_t*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsStopEnabled(boolean_t*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsRecordEnabled(boolean_t*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsFastForwardEnabled(boolean_t*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsRewindEnabled(boolean_t*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsNextEnabled(boolean_t*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsPreviousEnabled(boolean_t*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsChannelUpEnabled(boolean_t*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsChannelDownEnabled(boolean_t*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsPlayPauseToggleEnabled(boolean_t*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsShuffleEnabled(boolean_t*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsRepeatEnabled(boolean_t*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsPlaybackRateEnabled(boolean_t*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsPlaybackPositionEnabled(boolean_t*) = 0;
};

struct IPlaybackInfo : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Controls(IPlaybackControls**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_PlaybackStatus(int*) = 0; // 3 arrêté, 4 lecture, 5 pause
};

struct ITimelineProperties : IInspectable { // durées en unités de 100 ns
    virtual HRESULT STDMETHODCALLTYPE get_StartTime(INT64*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_EndTime(INT64*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_MinSeekTime(INT64*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_MaxSeekTime(INT64*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Position(INT64*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_LastUpdatedTime(INT64*) = 0; // DateTime : 100 ns depuis 1601
};

struct IMediaProperties : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Title(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Subtitle(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AlbumArtist(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Artist(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AlbumTitle(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TrackNumber(INT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Genres(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AlbumTrackCount(INT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_PlaybackType(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Thumbnail(IStreamReferenceAbi**) = 0;
};

struct ISession : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_SourceAppUserModelId(HSTRING*) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryGetMediaPropertiesAsync(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTimelineProperties(ITimelineProperties**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPlaybackInfo(IPlaybackInfo**) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryPlayAsync(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryPauseAsync(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryStopAsync(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryRecordAsync(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryFastForwardAsync(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryRewindAsync(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE TrySkipNextAsync(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE TrySkipPreviousAsync(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryChangeChannelUpAsync(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryChangeChannelDownAsync(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryTogglePlayPauseAsync(IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryChangeAutoRepeatModeAsync(int, IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryChangePlaybackRateAsync(double, IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryChangeShuffleActiveAsync(boolean_t, IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryChangePlaybackPositionAsync(INT64, IInspectable**) = 0;
};

struct ISessionManager : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetCurrentSession(ISession**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSessions(IVectorViewAbi<ISession*>**) = 0;
};

struct ISessionManagerStatics : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE RequestAsync(IInspectable**) = 0;
};
const IID IID_ISessionManagerStatics = {0x2050c4ee, 0x11a0, 0x57de, {0xae, 0xd7, 0xc9, 0x7c, 0x70, 0x33, 0x82, 0x45}};

} // namespace media_abi

namespace media {
namespace {

using namespace media_abi;

// ---------------------------------------------------------------------------
// Petits utilitaires COM / WinRT
// ---------------------------------------------------------------------------

template <class T>
struct Ptr { // pointeur COM minimal
    T* p = nullptr;
    Ptr() = default;
    Ptr(const Ptr&) = delete;
    Ptr& operator=(const Ptr&) = delete;
    ~Ptr() { reset(); }
    void reset() {
        if (p) p->Release();
        p = nullptr;
    }
    T** put() {
        reset();
        return &p;
    }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

// Attend la fin d'une opération asynchrone WinRT (on est sur un thread de fond).
template <class R>
bool Await(IInspectable* op, R* result, DWORD timeoutMs = 3000) {
    if (!op) return false;
    Ptr<IAsyncInfoAbi> info;
    if (FAILED(op->QueryInterface(IID_IAsyncInfoAbi, (void**)info.put()))) return false;
    int status = 0;
    ULONGLONG deadline = GetTickCount64() + timeoutMs;
    while (SUCCEEDED(info->get_Status(&status)) && status == 0) {
        if (GetTickCount64() > deadline) {
            info->Cancel();
            return false;
        }
        Sleep(3);
    }
    if (status != 1) return false;
    return SUCCEEDED(static_cast<IAsyncOp<R>*>(static_cast<void*>(op))->GetResults(result));
}

// Lance une commande Try…Async et attend sa fin (le résultat booléen est ignoré).
template <class Fn>
void RunCommand(Fn start) {
    Ptr<IInspectable> op;
    if (SUCCEEDED(start(op.put()))) {
        boolean_t ok = 0;
        Await(op.p, &ok);
    }
}

std::string FromHString(HSTRING h) {
    if (!h) return {};
    UINT32 len = 0;
    const wchar_t* raw = WindowsGetStringRawBuffer(h, &len);
    std::string s = util::Narrow(std::wstring(raw, len));
    WindowsDeleteString(h);
    return s;
}

std::string ReadHString(IMediaProperties* props, HRESULT (STDMETHODCALLTYPE IMediaProperties::*getter)(HSTRING*)) {
    HSTRING h = nullptr;
    return SUCCEEDED((props->*getter)(&h)) ? FromHString(h) : std::string();
}

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

// 100 ns depuis 1601 -> ms depuis 1970.
long long FileTimeToEpochMs(INT64 t) { return t ? (t - 116444736000000000LL) / 10000 : 0; }

// Lit la pochette et la renvoie en data URL (vide si indisponible).
std::string ReadThumbnail(IMediaProperties* props) {
    Ptr<IStreamReferenceAbi> ref;
    if (FAILED(props->get_Thumbnail(ref.put())) || !ref) return {};
    Ptr<IInspectable> op;
    if (FAILED(ref->OpenReadAsync(op.put()))) return {};
    Ptr<IInspectable> winrtStream;
    if (!Await(op.p, winrtStream.put()) || !winrtStream) return {};
    Ptr<IStream> stream;
    if (FAILED(CreateStreamOverRandomAccessStream(winrtStream.p, IID_IStream, (void**)stream.put()))) return {};
    std::string bytes;
    char buf[65536];
    ULONG read = 0;
    while (SUCCEEDED(stream->Read(buf, sizeof buf, &read)) && read > 0) {
        bytes.append(buf, read);
        if (bytes.size() > 4 * 1024 * 1024) return {}; // garde-fou
    }
    if (bytes.size() < 8) return {};
    const char* mime = (unsigned char)bytes[0] == 0x89 && bytes[1] == 'P' ? "image/png" : "image/jpeg";
    return std::string("data:") + mime + ";base64," + util::Base64(bytes);
}

// ---------------------------------------------------------------------------
// État et thread
// ---------------------------------------------------------------------------

struct Snapshot {
    bool active = false;
    std::string app, title, artist, album, status;
    long long pos = 0, dur = 0, at = 0;
    bool canNext = false, canPrev = false, canSeek = false;

    std::string TrackKey() const { return title + "\x1f" + artist + "\x1f" + album; }
    bool SameAs(const Snapshot& o) const {
        return active == o.active && app == o.app && title == o.title && artist == o.artist && album == o.album &&
               status == o.status && pos == o.pos && dur == o.dur && at == o.at && canNext == o.canNext &&
               canPrev == o.canPrev && canSeek == o.canSeek;
    }
};

std::thread g_thread;
std::atomic<bool> g_running{false};
std::atomic<bool> g_playing{false};
std::mutex g_mutex;
std::condition_variable g_wake;
std::deque<std::string> g_commands;
bool g_allSources = false; // protégé par g_mutex

const char* StatusName(int s) {
    switch (s) {
        case 4: return "playing";
        case 5: return "paused";
        case 3: return "stopped";
        default: return "other";
    }
}

int PlaybackStatus(ISession* s) {
    Ptr<IPlaybackInfo> info;
    int status = 0;
    if (SUCCEEDED(s->GetPlaybackInfo(info.put())) && info) info->get_PlaybackStatus(&status);
    return status;
}

// Choisit la session suivie, en préférant celle qui joue :
//  - mode Deezer : une session dont l'identifiant d'appli contient « deezer » ;
//  - toutes les sources : la session courante de Windows si elle joue, sinon la première qui joue.
bool PickSession(ISessionManager* mgr, bool allSources, Ptr<ISession>& out) {
    Ptr<ISession> current;
    if (allSources && SUCCEEDED(mgr->GetCurrentSession(current.put())) && current && PlaybackStatus(current.p) == 4) {
        std::swap(out.p, current.p);
        return true;
    }
    Ptr<IVectorViewAbi<ISession*>> list;
    if (SUCCEEDED(mgr->GetSessions(list.put())) && list) {
        UINT32 n = 0;
        list->get_Size(&n);
        for (UINT32 i = 0; i < n; ++i) {
            Ptr<ISession> s;
            if (FAILED(list->GetAt(i, s.put())) || !s) continue;
            if (!allSources) {
                HSTRING id = nullptr;
                s->get_SourceAppUserModelId(&id);
                if (Lower(FromHString(id)).find("deezer") == std::string::npos) continue;
            }
            bool playing = PlaybackStatus(s.p) == 4;
            if (!out || playing) std::swap(out.p, s.p);
            if (playing) return true;
        }
    }
    if (!out && current) std::swap(out.p, current.p);
    return (bool)out;
}

Snapshot ReadSession(ISession* s, IMediaProperties** propsOut) {
    Snapshot snap;
    snap.active = true;
    HSTRING id = nullptr;
    s->get_SourceAppUserModelId(&id);
    snap.app = FromHString(id);

    Ptr<IInspectable> op;
    if (SUCCEEDED(s->TryGetMediaPropertiesAsync(op.put()))) {
        IMediaProperties* props = nullptr;
        if (Await(op.p, &props) && props) {
            snap.title = ReadHString(props, &IMediaProperties::get_Title);
            snap.artist = ReadHString(props, &IMediaProperties::get_Artist);
            if (snap.artist.empty()) snap.artist = ReadHString(props, &IMediaProperties::get_AlbumArtist);
            snap.album = ReadHString(props, &IMediaProperties::get_AlbumTitle);
            *propsOut = props; // l'appelant libère
        }
    }

    Ptr<IPlaybackInfo> info;
    if (SUCCEEDED(s->GetPlaybackInfo(info.put())) && info) {
        int status = 0;
        info->get_PlaybackStatus(&status);
        snap.status = StatusName(status);
        Ptr<IPlaybackControls> c;
        if (SUCCEEDED(info->get_Controls(c.put())) && c) {
            boolean_t v = 0;
            c->get_IsNextEnabled(&v), snap.canNext = v;
            c->get_IsPreviousEnabled(&v), snap.canPrev = v;
            c->get_IsPlaybackPositionEnabled(&v), snap.canSeek = v;
        }
    }

    Ptr<ITimelineProperties> tl;
    if (SUCCEEDED(s->GetTimelineProperties(tl.put())) && tl) {
        INT64 start = 0, end = 0, pos = 0, updated = 0;
        tl->get_StartTime(&start);
        tl->get_EndTime(&end);
        tl->get_Position(&pos);
        tl->get_LastUpdatedTime(&updated);
        snap.dur = (end - start) / 10000;
        snap.pos = (pos - start) / 10000;
        snap.at = FileTimeToEpochMs(updated);
    }
    return snap;
}

std::string ToJson(const Snapshot& s, const std::string* art) {
    if (!s.active) return "{\"t\":\"media\",\"active\":false}";
    std::string j = "{\"t\":\"media\",\"active\":true,\"app\":" + util::Json(s.app) + ",\"title\":" + util::Json(s.title) +
                    ",\"artist\":" + util::Json(s.artist) + ",\"album\":" + util::Json(s.album) +
                    ",\"status\":" + util::Json(s.status) + ",\"pos\":" + std::to_string(s.pos) +
                    ",\"dur\":" + std::to_string(s.dur) + ",\"at\":" + std::to_string(s.at) +
                    ",\"canNext\":" + (s.canNext ? "true" : "false") + ",\"canPrev\":" + (s.canPrev ? "true" : "false") +
                    ",\"canSeek\":" + (s.canSeek ? "true" : "false");
    if (art) j += ",\"art\":" + util::Json(*art);
    return j + "}";
}

void ExecuteCommand(ISession* s, const std::string& cmd) {
    auto f = util::Split(cmd, '\n', 2);
    const std::string& name = f[0];
    if (!s) return;
    if (name == "toggle") RunCommand([&](IInspectable** op) { return s->TryTogglePlayPauseAsync(op); });
    else if (name == "next") RunCommand([&](IInspectable** op) { return s->TrySkipNextAsync(op); });
    else if (name == "prev") RunCommand([&](IInspectable** op) { return s->TrySkipPreviousAsync(op); });
    else if (name == "seek" && f.size() == 2) {
        INT64 ticks = std::max(0LL, std::atoll(f[1].c_str())) * 10000;
        RunCommand([&](IInspectable** op) { return s->TryChangePlaybackPositionAsync(ticks, op); });
    }
}

void Worker() {
    RoInitialize(RO_INIT_MULTITHREADED);
    Ptr<ISessionManager> mgr;
    {
        HSTRING cls = nullptr;
        const wchar_t name[] = L"Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager";
        Ptr<ISessionManagerStatics> statics;
        if (SUCCEEDED(WindowsCreateString(name, (UINT32)wcslen(name), &cls)) &&
            SUCCEEDED(RoGetActivationFactory(cls, IID_ISessionManagerStatics, (void**)statics.put()))) {
            Ptr<IInspectable> op;
            if (SUCCEEDED(statics->RequestAsync(op.put()))) Await(op.p, mgr.put(), 10000);
        }
        if (cls) WindowsDeleteString(cls);
    }

    Snapshot last;
    std::string lastTrack, lastArt;
    bool sentOnce = false;
    while (g_running) {
        bool allSources, forceFull = false;
        std::deque<std::string> commands;
        {
            std::unique_lock lock(g_mutex);
            g_wake.wait_for(lock, std::chrono::milliseconds(400), [] { return !g_commands.empty() || !g_running; });
            commands.swap(g_commands);
            allSources = g_allSources;
        }
        if (!g_running) break;
        if (!mgr) continue;

        Ptr<ISession> session;
        PickSession(mgr.p, allSources, session);
        for (const auto& c : commands) {
            if (c == "refresh") forceFull = true;
            else ExecuteCommand(session.p, c);
        }
        if (!commands.empty() && session) Sleep(120); // laisser l'appli appliquer la commande

        Snapshot snap;
        Ptr<IMediaProperties> props;
        if (session) snap = ReadSession(session.p, props.put());
        if (snap.active && snap.title.empty() && snap.artist.empty()) snap = Snapshot(); // session vide
        g_playing = snap.active && snap.status == "playing";

        bool trackChanged = snap.TrackKey() != lastTrack;
        if (snap.active && trackChanged) {
            lastArt = props ? ReadThumbnail(props.p) : std::string();
            lastTrack = snap.TrackKey();
        }
        if (!snap.active) lastTrack.clear();
        if (forceFull || !sentOnce || trackChanged || !snap.SameAs(last)) {
            bool withArt = snap.active && (forceFull || trackChanged || !sentOnce);
            bridge::PostToPage(ToJson(snap, withArt ? &lastArt : nullptr));
            last = snap;
            sentOnce = true;
        }
    }
    mgr.reset();
    RoUninitialize();
}

} // namespace

void Start() {
    if (g_running.exchange(true)) return;
    g_thread = std::thread(Worker);
}

void Stop() {
    if (!g_running.exchange(false)) return;
    g_wake.notify_all();
    if (g_thread.joinable()) g_thread.join();
}

void Command(const std::string& payload) {
    std::lock_guard lock(g_mutex);
    auto f = util::Split(payload, '\n', 2);
    if (f[0] == "sources") {
        g_allSources = f.size() == 2 && f[1] == "all";
        g_commands.push_back("refresh");
    } else {
        g_commands.push_back(payload);
    }
    g_wake.notify_all();
}

bool IsPlaying() { return g_playing; }

} // namespace media
