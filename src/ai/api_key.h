// Clés API, chiffrées avec DPAPI (liées à la session Windows) dans %APPDATA%\ShadowAssistant :
// Anthropic (apikey.bin) et celles des modèles gratuits, Groq et Gemini (<service>key.bin).
// Elles ne sont jamais envoyées à la page.
#pragma once

#include <string>

namespace api_key {

// Service : "anthropic" (par défaut), "groq" ou "gemini". Tout autre nom est refusé.
bool IsKnown(const std::string& slot);

void Load(); // toutes les clés
bool Save(const std::string& key, const std::string& slot = "anthropic");
void Remove(const std::string& slot = "anthropic");
std::string Get(const std::string& slot = "anthropic"); // copie, sûre entre threads

// Fragment JSON "hasKey":…,"keyHint":"…" (4 derniers caractères).
std::string StatusJson(const std::string& slot = "anthropic");

// Fragment JSON "freeKeys":{"groq":{"hasKey":…,"keyHint":…},"gemini":{…}}.
std::string FreeStatusJson();

} // namespace api_key
