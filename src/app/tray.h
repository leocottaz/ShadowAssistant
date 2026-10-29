// Icône de la zone de notification et notifications (bulles Windows, D-Bus sous Linux).
#pragma once

#ifdef _WIN32
#include <windows.h>
#endif

#include <string>

namespace tray {

void Notify(const std::string& title, const std::string& text, bool sound);

#ifdef _WIN32
constexpr UINT WM_APP_TRAY = WM_APP + 2; // callback des clics sur l'icône

// Crée l'icône (orbe dégradé généré à l'exécution) et l'ajoute à la zone de notification.
void Init(HWND window);
void Readd();  // après un redémarrage de l'Explorateur (message TaskbarCreated)
void Remove();
HICON Icon();
#else
void Init(); // icône StatusNotifierItem (D-Bus) et son menu
#endif

} // namespace tray
