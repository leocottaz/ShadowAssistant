// Lecteur : suit la musique en cours via les contrôles multimédias de Windows
// (GlobalSystemMediaTransportControls, ceux du panneau « en cours de lecture »).
// Par défaut seule Deezer est suivie ; « toutes les sources » prend la session courante.
// Un thread dédié interroge Windows et poste {"t":"media",…} à la page quand ça change.
#pragma once

#include <string>

namespace media {

void Start();
void Stop();

// Commande venant de la page : "toggle", "next", "prev", "seek\n<ms>",
// "sources\ndeezer|all", "refresh" (renvoie l'état complet, pochette comprise).
void Command(const std::string& payload);

// Vrai si la musique suivie est en lecture (lisible depuis n'importe quel thread).
bool IsPlaying();

} // namespace media
