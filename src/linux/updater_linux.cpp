// Mise à jour sous Linux : pas d'installation automatique. Le .deb s'installe dans /opt (root requis) et
// les autres distributions utilisent leur gestionnaire de paquets ; la page affiche la nouvelle version
// et renvoie vers la page de la release (commande « openurl »).
#include "features/updater.h"

#include "web/bridge.h"

namespace updater {

bool CanSelfUpdate() { return false; }

void Install(const std::string&) {
    bridge::PostToPage("{\"t\":\"update\",\"state\":\"error\",\"msg\":\"La mise à jour automatique n'est pas disponible sous Linux : "
                       "installe la nouvelle version avec ton gestionnaire de paquets ou depuis la page de la release.\"}");
}

} // namespace updater
