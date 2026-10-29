// Parties de util indépendantes du système (Windows et Linux).
#include "common/util.h"

#include <chrono>
#include <cstdio>

namespace util {

std::string Json(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) {
                    char b[8];
                    std::snprintf(b, sizeof b, "\\u%04x", c);
                    o += b;
                } else {
                    o += (char)c;
                }
        }
    }
    return o + "\"";
}

std::vector<std::string> Split(const std::string& s, char sep, size_t maxParts) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        if (maxParts && parts.size() + 1 == maxParts) {
            parts.push_back(s.substr(start));
            break;
        }
        size_t p = s.find(sep, start);
        if (p == std::string::npos) {
            parts.push_back(s.substr(start));
            break;
        }
        parts.push_back(s.substr(start, p - start));
        start = p + 1;
    }
    return parts;
}

std::string Tail(const std::string& s, size_t n) { return s.size() > n ? s.substr(s.size() - n) : s; }

std::string Base64(const std::string& bytes) {
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        unsigned v = (unsigned char)bytes[i] << 16 | (unsigned char)bytes[i + 1] << 8 | (unsigned char)bytes[i + 2];
        out += table[v >> 18 & 63], out += table[v >> 12 & 63], out += table[v >> 6 & 63], out += table[v & 63];
    }
    if (i < bytes.size()) {
        unsigned v = (unsigned char)bytes[i] << 16 | (i + 1 < bytes.size() ? (unsigned char)bytes[i + 1] << 8 : 0);
        out += table[v >> 18 & 63], out += table[v >> 12 & 63];
        out += i + 1 < bytes.size() ? table[v >> 6 & 63] : '=';
        out += '=';
    }
    return out;
}

long long NowEpochMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

} // namespace util
