// Lecteur sous Linux : suit la musique en cours via MPRIS2 (D-Bus), l'équivalent des contrôles
// multimédias de Windows. Même protocole vers la page ({"t":"media",…}) que features/media.cpp.
// Par défaut seule Deezer est suivie (nom, identité ou adresse du morceau contenant « deezer ») ;
// « toutes les sources » prend le premier lecteur qui joue.
#include "features/media.h"

#include "common/util.h"
#include "web/bridge.h"

#include <curl/curl.h>
#include <gio/gio.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iterator>
#include <mutex>
#include <thread>

namespace media {
namespace {

constexpr char kPrefix[] = "org.mpris.MediaPlayer2.";
constexpr char kPlayerPath[] = "/org/mpris/MediaPlayer2";
constexpr char kPlayerIface[] = "org.mpris.MediaPlayer2.Player";
constexpr size_t kMaxArtBytes = 4 * 1024 * 1024;

struct Gv { // propriétaire d'un GVariant
    GVariant* v = nullptr;
    Gv() = default;
    explicit Gv(GVariant* x) : v(x) {}
    Gv(const Gv&) = delete;
    Gv& operator=(const Gv&) = delete;
    Gv(Gv&& o) noexcept : v(o.v) { o.v = nullptr; }
    ~Gv() {
        if (v) g_variant_unref(v);
    }
    explicit operator bool() const { return v != nullptr; }
};

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

// Propriétés d'une interface : dictionnaire a{sv} (vide en cas d'échec ou de lecteur muet).
Gv GetAll(GDBusConnection* bus, const std::string& name, const char* iface) {
    GError* err = nullptr;
    GVariant* res = g_dbus_connection_call_sync(bus, name.c_str(), kPlayerPath, "org.freedesktop.DBus.Properties", "GetAll",
                                                g_variant_new("(s)", iface), G_VARIANT_TYPE("(a{sv})"),
                                                G_DBUS_CALL_FLAGS_NONE, 400, nullptr, &err);
    if (err) g_error_free(err);
    if (!res) return Gv();
    Gv dict(g_variant_get_child_value(res, 0));
    g_variant_unref(res);
    return dict;
}

std::string Str(GVariant* dict, const char* key) {
    if (!dict) return {};
    Gv v(g_variant_lookup_value(dict, key, nullptr));
    if (!v) return {};
    if (g_variant_is_of_type(v.v, G_VARIANT_TYPE_STRING) || g_variant_is_of_type(v.v, G_VARIANT_TYPE_OBJECT_PATH))
        return g_variant_get_string(v.v, nullptr);
    if (g_variant_is_of_type(v.v, G_VARIANT_TYPE("as"))) { // xesam:artist : liste de noms
        std::string out;
        GVariantIter it;
        g_variant_iter_init(&it, v.v);
        const char* s = nullptr;
        while (g_variant_iter_next(&it, "&s", &s)) out += (out.empty() ? "" : ", ") + std::string(s);
        return out;
    }
    return {};
}

long long Num(GVariant* dict, const char* key) {
    if (!dict) return 0;
    Gv v(g_variant_lookup_value(dict, key, nullptr));
    if (!v) return 0;
    switch (g_variant_classify(v.v)) {
        case G_VARIANT_CLASS_INT64: return g_variant_get_int64(v.v);
        case G_VARIANT_CLASS_UINT64: return (long long)g_variant_get_uint64(v.v);
        case G_VARIANT_CLASS_INT32: return g_variant_get_int32(v.v);
        case G_VARIANT_CLASS_UINT32: return g_variant_get_uint32(v.v);
        case G_VARIANT_CLASS_DOUBLE: return (long long)g_variant_get_double(v.v);
        default: return 0;
    }
}

bool Bool(GVariant* dict, const char* key) {
    if (!dict) return false;
    Gv v(g_variant_lookup_value(dict, key, nullptr));
    return v && g_variant_is_of_type(v.v, G_VARIANT_TYPE_BOOLEAN) && g_variant_get_boolean(v.v);
}

// ---------------------------------------------------------------------------
// Pochette : data URL (la page en extrait la teinte, donc pas d'adresse distante)
// ---------------------------------------------------------------------------

const char* ImageMime(const std::string& b) {
    if (b.size() < 12) return nullptr;
    if ((unsigned char)b[0] == 0x89 && b.compare(1, 3, "PNG") == 0) return "image/png";
    if ((unsigned char)b[0] == 0xFF && (unsigned char)b[1] == 0xD8) return "image/jpeg";
    if (b.compare(0, 4, "RIFF") == 0 && b.compare(8, 4, "WEBP") == 0) return "image/webp";
    if (b.compare(0, 3, "GIF") == 0) return "image/gif";
    return nullptr;
}

size_t CurlWrite(char* data, size_t size, size_t count, void* user) {
    auto* out = static_cast<std::string*>(user);
    if (out->size() + size * count > kMaxArtBytes) return 0;
    out->append(data, size * count);
    return size * count;
}

std::string ReadArt(const std::string& url) {
    std::string bytes;
    if (url.rfind("file://", 0) == 0) {
        char* path = g_filename_from_uri(url.c_str(), nullptr, nullptr);
        if (!path) return {};
        std::ifstream f(path, std::ios::binary);
        g_free(path);
        if (!f) return {};
        bytes.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        if (bytes.size() > kMaxArtBytes) return {};
    } else if (url.rfind("https://", 0) == 0 || url.rfind("http://", 0) == 0) {
        CURL* curl = curl_easy_init();
        if (!curl) return {};
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 6L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWrite);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &bytes);
        CURLcode rc = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        if (rc != CURLE_OK) return {};
    } else {
        return {};
    }
    // Seules de vraies images sont relayées : un lecteur ne peut pas nous faire lire un autre fichier.
    const char* mime = ImageMime(bytes);
    if (!mime) return {};
    return std::string("data:") + mime + ";base64," + util::Base64(bytes);
}

// ---------------------------------------------------------------------------
// État et thread
// ---------------------------------------------------------------------------

struct Snapshot {
    bool active = false;
    std::string bus, trackId, app, title, artist, album, status, artUrl;
    long long pos = 0, dur = 0, at = 0;
    bool canNext = false, canPrev = false, canSeek = false;

    std::string TrackKey() const { return title + "\x1f" + artist + "\x1f" + album; }
    // Tout sauf la position : elle avance seule et est extrapolée par la page.
    bool SameStateAs(const Snapshot& o) const {
        return active == o.active && app == o.app && title == o.title && artist == o.artist && album == o.album &&
               status == o.status && dur == o.dur && canNext == o.canNext && canPrev == o.canPrev && canSeek == o.canSeek;
    }
};

std::thread g_thread;
std::atomic<bool> g_running{false};
std::atomic<bool> g_playing{false};
std::mutex g_mutex;
std::condition_variable g_wake;
std::deque<std::string> g_commands;
bool g_allSources = false; // protégé par g_mutex

long long NowMs() { return util::NowEpochMs(); }

bool ReadPlayer(GDBusConnection* bus, const std::string& name, bool allSources, Snapshot& out) {
    Gv player = GetAll(bus, name, kPlayerIface);
    if (!player) return false;
    Gv root = GetAll(bus, name, "org.mpris.MediaPlayer2");
    Gv meta(g_variant_lookup_value(player.v, "Metadata", G_VARIANT_TYPE("a{sv}")));

    Snapshot s;
    s.bus = name;
    s.app = Str(root.v, "Identity");
    if (s.app.empty()) s.app = name.substr(sizeof(kPrefix) - 1);
    s.title = Str(meta.v, "xesam:title");
    s.artist = Str(meta.v, "xesam:artist");
    s.album = Str(meta.v, "xesam:album");
    s.trackId = Str(meta.v, "mpris:trackid");
    s.artUrl = Str(meta.v, "mpris:artUrl");
    s.dur = Num(meta.v, "mpris:length") / 1000;
    s.pos = Num(player.v, "Position") / 1000;
    s.at = NowMs();
    std::string status = Str(player.v, "PlaybackStatus");
    s.status = status == "Playing" ? "playing" : status == "Paused" ? "paused" : status == "Stopped" ? "stopped" : "other";
    s.canNext = Bool(player.v, "CanGoNext");
    s.canPrev = Bool(player.v, "CanGoPrevious");
    s.canSeek = Bool(player.v, "CanSeek");
    s.active = !s.title.empty() || !s.artist.empty(); // sans métadonnées : session vide

    if (!allSources) {
        std::string hay = Lower(name + " " + s.app + " " + Str(meta.v, "xesam:url"));
        if (hay.find("deezer") == std::string::npos) return false;
    }
    out = std::move(s);
    return true;
}

// Choisit le lecteur suivi, en préférant celui qui joue.
Snapshot PickPlayer(GDBusConnection* bus, bool allSources) {
    Snapshot best;
    GError* err = nullptr;
    GVariant* res = g_dbus_connection_call_sync(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                                                "ListNames", nullptr, G_VARIANT_TYPE("(as)"), G_DBUS_CALL_FLAGS_NONE, 400,
                                                nullptr, &err);
    if (err) g_error_free(err);
    if (!res) return best;
    GVariantIter* it = nullptr;
    g_variant_get(res, "(as)", &it);
    const char* n = nullptr;
    while (g_variant_iter_next(it, "&s", &n)) {
        std::string name = n;
        if (name.rfind(kPrefix, 0) != 0) continue;
        Snapshot s;
        if (!ReadPlayer(bus, name, allSources, s) || !s.active) continue;
        if (!best.active || (s.status == "playing" && best.status != "playing")) best = std::move(s);
        if (best.status == "playing") break;
    }
    g_variant_iter_free(it);
    g_variant_unref(res);
    return best;
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

void CallPlayer(GDBusConnection* bus, const std::string& name, const char* method, GVariant* params) {
    GError* err = nullptr;
    GVariant* res = g_dbus_connection_call_sync(bus, name.c_str(), kPlayerPath, kPlayerIface, method, params, nullptr,
                                                G_DBUS_CALL_FLAGS_NONE, 800, nullptr, &err);
    if (err) g_error_free(err);
    if (res) g_variant_unref(res);
}

void ExecuteCommand(GDBusConnection* bus, const Snapshot& s, const std::string& cmd) {
    if (!s.active) return;
    auto f = util::Split(cmd, '\n', 2);
    const std::string& name = f[0];
    if (name == "toggle") CallPlayer(bus, s.bus, "PlayPause", nullptr);
    else if (name == "next") CallPlayer(bus, s.bus, "Next", nullptr);
    else if (name == "prev") CallPlayer(bus, s.bus, "Previous", nullptr);
    else if (name == "seek" && f.size() == 2 && g_variant_is_object_path(s.trackId.c_str())) {
        long long us = std::max(0LL, std::atoll(f[1].c_str())) * 1000;
        CallPlayer(bus, s.bus, "SetPosition", g_variant_new("(ox)", s.trackId.c_str(), (gint64)us));
    }
}

void Worker() {
    GError* err = nullptr;
    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &err);
    if (err) g_error_free(err);
    static std::once_flag curlInit;
    std::call_once(curlInit, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });

    Snapshot last;
    std::string lastTrack, lastArt;
    bool sentOnce = false;
    while (g_running) {
        bool allSources, forceFull = false;
        std::deque<std::string> commands;
        {
            std::unique_lock lock(g_mutex);
            g_wake.wait_for(lock, std::chrono::milliseconds(500), [] { return !g_commands.empty() || !g_running; });
            commands.swap(g_commands);
            allSources = g_allSources;
        }
        if (!g_running) break;
        if (!bus) continue;

        Snapshot snap = PickPlayer(bus, allSources);
        for (const auto& c : commands) {
            if (c == "refresh") forceFull = true;
            else ExecuteCommand(bus, snap, c);
        }
        if (!commands.empty() && snap.active) {
            std::this_thread::sleep_for(std::chrono::milliseconds(120)); // laisser l'appli appliquer la commande
            snap = PickPlayer(bus, allSources);
        }
        g_playing = snap.active && snap.status == "playing";

        bool trackChanged = snap.TrackKey() != lastTrack;
        if (snap.active && trackChanged) {
            lastArt = ReadArt(snap.artUrl);
            lastTrack = snap.TrackKey();
        }
        if (!snap.active) lastTrack.clear();

        // La page extrapole la position : on ne renvoie que si elle s'écarte (saut dans le morceau).
        bool jumped = false;
        if (snap.active && last.active) {
            long long expected = last.pos + (last.status == "playing" ? snap.at - last.at : 0);
            jumped = std::llabs(snap.pos - expected) > 1500;
        }
        if (forceFull || !sentOnce || trackChanged || !snap.SameStateAs(last) || jumped) {
            bool withArt = snap.active && (forceFull || trackChanged || !sentOnce);
            bridge::PostToPage(ToJson(snap, withArt ? &lastArt : nullptr));
            last = snap;
            sentOnce = true;
        }
    }
    if (bus) g_object_unref(bus);
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
