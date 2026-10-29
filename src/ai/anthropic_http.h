// Requêtes HTTPS (un thread par requête) : mode « Clé API » vers api.anthropic.com, et mode
// « Gratuit » vers Groq ou Gemini (API compatibles OpenAI). Le flux SSE est relayé brut à la
// page par lignes complètes ("chunk"), puis "done" ou "error". La page construit les corps
// JSON ; ici on ajoute la clé, qui ne quitte jamais le natif.
#pragma once

#include <string>

namespace anthropic_http {

void Start(const std::string& id, const std::string& method, const std::string& path, const std::string& betas,
           const std::string& body);

// service : "groq" ou "gemini" ; path doit commencer par le préfixe de l'API du service.
void StartFree(const std::string& id, const std::string& service, const std::string& method, const std::string& path,
               const std::string& body);
bool Cancel(const std::string& id); // false si l'id est inconnu

} // namespace anthropic_http
