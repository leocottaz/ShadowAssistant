// Actions de la fenêtre du dock sous Linux, implémentées dans main_linux.cpp et appelées par le pont.
#pragma once

#include <functional>
#include <string>

namespace host {

// Fenêtre « Ouvrir » devant le dock ; done reçoit le chemin choisi ("" si annulé). Thread UI.
void ChooseFile(std::function<void(const std::string&)> done);

void Quit();
void OpenUrl(const std::string& url);
void EvalJson(const std::string& json); // remet un JSON à la page — thread UI uniquement

} // namespace host
