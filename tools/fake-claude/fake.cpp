// Faux claude.exe pour tester le dock : imite `claude -p --output-format stream-json`.
#include <windows.h>
#include <cstdio>
#include <iostream>
#include <string>

static std::string esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (c == '\n') o += "\\n";
        else if (c == '\r') {}
        else o += c;
    }
    return o;
}

int wmain(int argc, wchar_t** argv) {
    std::string args;
    bool hasApiKey = GetEnvironmentVariableA("ANTHROPIC_API_KEY", nullptr, 0) > 0;
    for (int i = 1; i < argc; ++i) {
        char b[4096];
        WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, b, sizeof b, nullptr, nullptr);
        std::string a = b;
        if (a == "--version") { std::printf("9.9.9 (Claude Code)\n"); return 0; }
        if (a.size() > 60) a = a.substr(0, 60) + "...";
        args += "<" + a + "> ";
    }
    std::string input((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
    if (input.find("ERREUR") != std::string::npos) {
        std::printf("{\"type\":\"result\",\"subtype\":\"error\",\"is_error\":true,\"result\":\"Not logged in · Please run /login\"}\n");
        return 1;
    }
    std::printf("{\"type\":\"system\",\"subtype\":\"init\",\"session_id\":\"11111111-2222-3333-4444-555555555555\"}\n");
    std::printf("{\"type\":\"stream_event\",\"event\":{\"type\":\"message_start\",\"message\":{}}}\n");
    std::printf("{\"type\":\"stream_event\",\"event\":{\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"text\",\"text\":\"\"}}}\n");
    std::string parts[] = {"C'est noté, je te le rappelle à 18h ✓\\n\\n",
                           "Args reçus : " + esc(args) + "\\n\\nAPI key héritée : " + (hasApiKey ? "OUI" : "non") + "\\n\\n",
                           "[[rappel|2099-01-01T18:00|none|Appeler Paul]]\\n[[note|Idée de test]]"};
    for (auto& p : parts) {
        std::printf("{\"type\":\"stream_event\",\"event\":{\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\",\"text\":\"%s\"}}}\n", p.c_str());
        std::fflush(stdout);
        Sleep(400);
    }
    std::printf("{\"type\":\"stream_event\",\"event\":{\"type\":\"content_block_stop\",\"index\":0}}\n");
    std::printf("{\"type\":\"result\",\"subtype\":\"success\",\"is_error\":false,\"result\":\"ok\",\"session_id\":\"11111111-2222-3333-4444-555555555555\"}\n");
    return 0;
}
