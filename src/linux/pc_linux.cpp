// Actions sur le PC sous Linux. Pas d'index système : la recherche parcourt le dossier
// personnel (sans les dossiers cachés ni node_modules) et ne compare que les noms de fichiers.
// Mêmes règles que features/pc.cpp : seul un fichier renvoyé par Search peut être ouvert, et un
// exécutable ou un script n'est jamais lancé (on montre son dossier).
#include "features/pc.h"

#include "common/util.h"
#include "features/media.h"
#include "linux/host.h"
#include "web/bridge.h"

#include <gio/gdesktopappinfo.h>
#include <gio/gio.h>
#include <glib.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace pc {
namespace {

constexpr size_t kMaxResults = 15;
constexpr size_t kMaxWords = 6;
constexpr long kMaxVisited = 500000;                       // garde-fou sur de très gros dossiers
constexpr std::chrono::seconds kMaxDuration{5};

std::set<std::string> g_found; // chemins déjà renvoyés à la page
std::mutex g_foundMutex;

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

void Reply(const std::string& id, const std::string& fields) {
    bridge::PostToPage("{\"t\":\"reply\",\"id\":" + util::Json(id) + "," + fields + "}");
}

void ReplyError(const std::string& id, const std::string& message) {
    Reply(id, "\"ok\":false,\"error\":" + util::Json(message));
}

// Les mots viennent de Claude : lettres, chiffres, - et _ seulement.
std::vector<std::string> Words(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&] {
        if (!cur.empty() && out.size() < kMaxWords) out.push_back(Lower(cur));
        cur.clear();
    };
    for (unsigned char c : text) {
        if (std::isalnum(c) || c >= 0x80 || c == '-' || c == '_') cur += (char)c; // octets UTF-8 conservés
        else flush();
    }
    flush();
    return out;
}

// AAAA-MM-JJ -> minuit local ; false si la date est impossible.
bool ParseDate(const std::string& s, std::time_t& out) {
    if (s.size() != 10 || s[4] != '-' || s[7] != '-') return false;
    for (int i : {0, 1, 2, 3, 5, 6, 8, 9})
        if (!std::isdigit((unsigned char)s[i])) return false;
    std::tm tm{};
    tm.tm_year = std::atoi(s.substr(0, 4).c_str()) - 1900;
    tm.tm_mon = std::atoi(s.substr(5, 2).c_str()) - 1;
    tm.tm_mday = std::atoi(s.substr(8, 2).c_str());
    tm.tm_isdst = -1;
    std::tm check = tm;
    out = std::mktime(&check);
    return out != (std::time_t)-1 && check.tm_year == tm.tm_year && check.tm_mon == tm.tm_mon && check.tm_mday == tm.tm_mday;
}

struct Query {
    std::vector<std::string> words;
    std::string ext; // sans point, en minuscules
    bool hasFrom = false, hasTo = false;
    std::time_t from = 0, toExclusive = 0;
};

struct Hit {
    std::string path, name;
    long long modified = 0, size = 0;
};

bool Matches(const Query& q, const std::string& name) {
    std::string low = Lower(name);
    for (const auto& w : q.words)
        if (low.find(w) == std::string::npos) return false;
    if (!q.ext.empty()) {
        size_t dot = low.rfind('.');
        if (dot == std::string::npos || low.substr(dot + 1) != q.ext) return false;
    }
    return true;
}

std::vector<Hit> Walk(const Query& q) {
    std::vector<Hit> hits;
    const auto deadline = std::chrono::steady_clock::now() + kMaxDuration;
    long visited = 0;
    std::error_code ec;
    fs::recursive_directory_iterator it(util::HomeDir(), fs::directory_options::skip_permission_denied, ec), end;
    while (!ec && it != end) {
        const fs::directory_entry& entry = *it;
        std::string name = entry.path().filename().string();
        if (++visited > kMaxVisited || ((visited & 0x3FF) == 0 && std::chrono::steady_clock::now() > deadline)) break;

        std::error_code e2;
        if (entry.is_symlink(e2)) { // on ne suit jamais les liens
            it.increment(ec);
            continue;
        }
        if (entry.is_directory(e2)) {
            if ((!name.empty() && name[0] == '.') || name == "node_modules") it.disable_recursion_pending();
        } else if (entry.is_regular_file(e2) && !(name[0] == '.') && Matches(q, name)) {
            auto mtime = entry.last_write_time(e2);
            std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::clock_cast<std::chrono::system_clock>(mtime));
            if ((!q.hasFrom || t >= q.from) && (!q.hasTo || t < q.toExclusive)) {
                Hit h;
                h.path = entry.path().string();
                h.name = name;
                h.modified = (long long)t * 1000;
                h.size = (long long)entry.file_size(e2);
                hits.push_back(std::move(h));
            }
        }
        it.increment(ec);
    }
    std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.modified > b.modified; });
    if (hits.size() > kMaxResults) hits.resize(kMaxResults);
    return hits;
}

// ------------------------------------------------------------------ ouverture

bool Spawn(const std::vector<std::string>& args) {
    std::vector<gchar*> argv;
    for (const auto& a : args) argv.push_back(const_cast<gchar*>(a.c_str()));
    argv.push_back(nullptr);
    GError* err = nullptr;
    gboolean ok = g_spawn_async(nullptr, argv.data(), nullptr,
                                (GSpawnFlags)(G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL),
                                nullptr, nullptr, nullptr, &err);
    if (err) g_error_free(err);
    return ok;
}

// Pas d'invite : exécute un programme et attend, pour les réglages du volume.
bool RunQuiet(const std::vector<std::string>& args) {
    std::vector<gchar*> argv;
    for (const auto& a : args) argv.push_back(const_cast<gchar*>(a.c_str()));
    argv.push_back(nullptr);
    gint status = 0;
    GError* err = nullptr;
    gboolean ok = g_spawn_sync(nullptr, argv.data(), nullptr,
                               (GSpawnFlags)(G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL),
                               nullptr, nullptr, nullptr, nullptr, &status, &err);
    if (err) g_error_free(err);
    return ok && g_spawn_check_wait_status(status, nullptr);
}

bool IsRunnable(const std::string& path) {
    static const char* kExt[] = {".sh",   ".bash", ".zsh",  ".fish",  ".py",      ".pl",  ".rb",  ".js",
                                 ".desktop", ".appimage", ".run", ".bin", ".jar", ".exe", ".bat", ".cmd",
                                 ".ps1",  ".msi",  ".deb",  ".rpm",   ".flatpakref", ".command"};
    std::string low = Lower(path);
    for (auto e : kExt)
        if (low.size() >= strlen(e) && low.compare(low.size() - strlen(e), strlen(e), e) == 0) return true;
    return access(path.c_str(), X_OK) == 0; // tout fichier exécutable
}

// Montre le fichier dans le gestionnaire de fichiers (FileManager1), à défaut son dossier.
void RevealInFileManager(const std::string& path) {
    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
    char* uri = g_filename_to_uri(path.c_str(), nullptr, nullptr);
    bool done = false;
    if (bus && uri) {
        const gchar* uris[] = {uri, nullptr};
        GError* err = nullptr;
        GVariant* res = g_dbus_connection_call_sync(bus, "org.freedesktop.FileManager1", "/org/freedesktop/FileManager1",
                                                    "org.freedesktop.FileManager1", "ShowItems",
                                                    g_variant_new("(^ass)", uris, ""), nullptr, G_DBUS_CALL_FLAGS_NONE, 2000,
                                                    nullptr, &err);
        if (err) g_error_free(err);
        done = res != nullptr;
        if (res) g_variant_unref(res);
    }
    if (!done) Spawn({"xdg-open", fs::path(path).parent_path().string()});
    g_free(uri);
    if (bus) g_object_unref(bus);
}

// Étape « open » d'une routine : adresse, fichier, dossier, .desktop, programme ou nom de commande.
void OpenTarget(const std::string& value) {
    if (value.rfind("http://", 0) == 0 || value.rfind("https://", 0) == 0) {
        Spawn({"xdg-open", value});
        return;
    }
    std::error_code ec;
    if (fs::exists(value, ec)) {
        if (value.size() > 8 && Lower(value).compare(value.size() - 8, 8, ".desktop") == 0) {
            if (GDesktopAppInfo* info = g_desktop_app_info_new_from_filename(value.c_str())) {
                g_app_info_launch(G_APP_INFO(info), nullptr, nullptr, nullptr);
                g_object_unref(info);
            }
        } else if (fs::is_regular_file(value, ec) && access(value.c_str(), X_OK) == 0) {
            Spawn({value}); // programme choisi par l'utilisateur dans sa routine
        } else {
            Spawn({"xdg-open", value});
        }
        return;
    }
    if (value.find('/') == std::string::npos) Spawn({value}); // nom de commande dans le PATH (ex. firefox)
}

} // namespace

void Search(const std::string& payload) {
    auto f = util::Split(payload, '\n', 5);
    if (f.empty() || f[0].empty()) return;
    f.resize(5);

    Query q;
    q.words = Words(f[1]);
    for (unsigned char c : f[2])
        if (std::isalnum(c) && q.ext.size() < 8) q.ext += (char)std::tolower(c);
    q.hasFrom = ParseDate(f[3], q.from);
    std::time_t to = 0;
    if (ParseDate(f[4], to)) {
        q.hasTo = true;
        q.toExclusive = to + 24 * 3600; // jour inclus
    }
    if (q.words.empty() && q.ext.empty() && !q.hasFrom && !q.hasTo) {
        ReplyError(f[0], "Donne au moins un mot, une extension ou une date.");
        return;
    }
    std::thread([id = f[0], q] {
        std::vector<Hit> hits = Walk(q);
        std::string json = "[";
        for (size_t i = 0; i < hits.size(); ++i)
            json += std::string(i ? "," : "") + "{\"path\":" + util::Json(hits[i].path) + ",\"name\":" + util::Json(hits[i].name) +
                    ",\"modified\":" + std::to_string(hits[i].modified) + ",\"size\":" + std::to_string(hits[i].size) + "}";
        json += "]";
        {
            std::lock_guard lock(g_foundMutex);
            if (g_found.size() > 2000) g_found.clear();
            for (const auto& h : hits) g_found.insert(h.path);
        }
        Reply(id, "\"ok\":true,\"results\":" + json);
    }).detach();
}

void OpenFound(const std::string& path) {
    {
        std::lock_guard lock(g_foundMutex);
        if (!g_found.count(path)) return;
    }
    if (IsRunnable(path)) RevealInFileManager(path);
    else Spawn({"xdg-open", path});
}

void RunRoutine(const std::string& steps) {
    std::thread([lines = util::Split(steps, '\n')] {
        for (const auto& line : lines) {
            auto f = util::Split(line, '\t', 2);
            if (f.size() != 2) continue;
            const std::string &type = f[0], &value = f[1];
            if (type == "open" && !value.empty()) {
                OpenTarget(value);
            } else if (type == "volume") {
                SetVolume(std::atoi(value.c_str()));
            } else if (type == "music") {
                bool playing = media::IsPlaying();
                if ((value == "play" && !playing) || (value == "pause" && playing)) media::Command("toggle");
                else if (value == "next") media::Command("next");
            } else if (type == "wait") {
                std::this_thread::sleep_for(std::chrono::seconds(std::clamp(std::atoi(value.c_str()), 0, 60)));
            }
        }
    }).detach();
}

// Sortie audio par défaut : PipeWire/WirePlumber (wpctl), à défaut PulseAudio (pactl).
void SetVolume(int percent) {
    percent = std::clamp(percent, 0, 100);
    char frac[16];
    std::snprintf(frac, sizeof frac, "%.2f", percent / 100.0);
    std::string pct = std::to_string(percent) + "%";
    bool mute = percent == 0;
    if (RunQuiet({"wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", frac})) {
        RunQuiet({"wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", mute ? "1" : "0"});
    } else if (RunQuiet({"pactl", "set-sink-volume", "@DEFAULT_SINK@", pct})) {
        RunQuiet({"pactl", "set-sink-mute", "@DEFAULT_SINK@", mute ? "1" : "0"});
    }
}

void PickFile(const std::string& id) {
    host::ChooseFile([id](const std::string& path) { Reply(id, std::string("\"ok\":true,\"path\":") + util::Json(path)); });
}

} // namespace pc
