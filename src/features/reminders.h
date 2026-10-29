// Déclenchement des rappels. La liste vit dans la page (localStorage) ; elle est
// synchronisée ici pour que l'heure soit tenue même si la page est en veille.
#pragma once

#include <string>

namespace reminders {

// payload : une ligne par rappel à venir, "id\tdueEpochMs\ttexte".
void Sync(const std::string& payload);

// Fin du minuteur de la page (ms epoch), 0 = aucun compte à rebours en cours.
void SetTimerEnd(long long endMs);

// Vérifie les échéances (appelé chaque seconde) : notification Windows, message
// {"t":"fire","id":…} (ou {"t":"timerdone"}) à la page et apparition du dock.
void Check();

} // namespace reminders
