#include "features/reminders.h"

#include "app/dock.h"
#include "app/tray.h"
#include "common/util.h"
#include "web/bridge.h"

#ifdef _WIN32
#include "web/webview_host.h"
#endif

#include <cstdlib>
#include <vector>

namespace reminders {
namespace {

// Windows suspend la page quand le dock est caché : on la réveille. Sous Linux, elle ne dort jamais.
void WakePage() {
#ifdef _WIN32
    webview::Wake();
#endif
}

struct Reminder {
    std::string id;
    long long due = 0; // ms epoch
    std::string text;
    bool fired = false;
};

std::vector<Reminder> g_list;
long long g_timerEnd = 0; // ms epoch, 0 = pas de minuteur

} // namespace

void SetTimerEnd(long long endMs) { g_timerEnd = endMs; }

void Sync(const std::string& payload) {
    std::vector<Reminder> next;
    for (const auto& line : util::Split(payload, '\n')) {
        auto f = util::Split(line, '\t', 3);
        if (f.size() < 3 || f[0].empty()) continue;
        Reminder r{f[0], std::atoll(f[1].c_str()), f[2], false};
        for (const auto& old : g_list)
            if (old.id == r.id && old.due == r.due) r.fired = old.fired;
        next.push_back(std::move(r));
    }
    g_list = std::move(next);
}

void Check() {
    long long now = util::NowEpochMs();
    for (auto& r : g_list) {
        if (r.fired || r.due > now) continue;
        r.fired = true;
        tray::Notify("Rappel", r.text, dock::Cfg().sound);
        // Devant une appli plein écran, le dock ne descend pas : la page peut être en veille.
        // On la réveille pour qu'elle compte le rappel et demande la cloche du mini-dock.
        WakePage();
        bridge::PostToPage("{\"t\":\"fire\",\"id\":" + util::Json(r.id) + "}");
        dock::RevealBriefly(9000);
    }
    if (g_timerEnd && g_timerEnd <= now) {
        g_timerEnd = 0;
        tray::Notify("Minuteur", "Temps écoulé", dock::Cfg().sound);
        WakePage();
        bridge::PostToPage("{\"t\":\"timerdone\"}");
        dock::RevealBriefly(9000);
    }
}

} // namespace reminders
