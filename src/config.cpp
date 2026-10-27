#include "config.h"
#include <cstring>
#include <cctype>

namespace dmc {

static std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

static std::string Lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

static bool ParseBool(const std::string& v, bool& out) {
    std::string l = Lower(v);
    if (l == "1" || l == "true" || l == "yes" || l == "on") { out = true; return true; }
    if (l == "0" || l == "false" || l == "no" || l == "off") { out = false; return true; }
    return false;
}

static bool ParseInt(const std::string& v, int lo, int hi, int& out) {
    if (v.empty()) return false;
    size_t i = 0; bool neg = false;
    if (v[0] == '-') { neg = true; i = 1; }
    if (i >= v.size()) return false;
    long long n = 0;
    for (; i < v.size(); ++i) {
        if (!std::isdigit((unsigned char)v[i])) return false;
        n = n * 10 + (v[i] - '0');
        if (n > 1000000) n = 1000000;
    }
    if (neg) n = -n;
    if (n < lo) n = lo;
    if (n > hi) n = hi;
    out = (int)n;
    return true;
}

int ParseConfigText(const char* text, Config& cfg) {
    int applied = 0;
    std::string section;
    const char* p = text ? text : "";
    while (*p) {
        const char* eol = std::strchr(p, '\n');
        std::string line = Trim(eol ? std::string(p, eol - p) : std::string(p));
        p = eol ? eol + 1 : p + std::strlen(p);

        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[') {
            size_t close = line.find(']');
            if (close != std::string::npos) section = Lower(Trim(line.substr(1, close - 1)));
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = Trim(line.substr(0, eq));
        std::string val = Trim(line.substr(eq + 1));
        if (section != "hints") {  // inline "; comment" allowed outside [Hints]
            size_t sc = val.find(';');
            if (sc != std::string::npos) val = Trim(val.substr(0, sc));
        }
        std::string k = Lower(key);
        bool b = false; int n = 0;

        if (section == "hints") {
            if (!key.empty()) { cfg.hints.emplace_back(key, val); ++applied; }
        } else if (section == "general") {
            if (k == "mode") {
                std::string v = Lower(val);
                if (v == "hybrid") { cfg.mode = Mode::Hybrid; ++applied; }
                else if (v == "sdl") { cfg.mode = Mode::Sdl; ++applied; }
                else if (v == "passthrough") { cfg.mode = Mode::Passthrough; ++applied; }
            }
            else if (k == "prefer_sdl" && ParseBool(val, b)) { cfg.prefer_sdl = b; ++applied; }
            else if (k == "ignore_xbox_pads" && ParseBool(val, b)) { cfg.ignore_xbox_pads = b; ++applied; }
            else if (k == "hide_dinput_pads" && ParseBool(val, b)) { cfg.hide_dinput_pads = b; ++applied; }
            else if (k == "log" && ParseBool(val, b)) { cfg.log = b; ++applied; }
            else if (k == "mappings_file" && !val.empty()) { cfg.mappings_file = val; ++applied; }
        } else if (section == "input") {
            if (k == "swap_ab" && ParseBool(val, b)) { cfg.mapping.swap_ab = b; ++applied; }
            else if (k == "swap_xy" && ParseBool(val, b)) { cfg.mapping.swap_xy = b; ++applied; }
            else if (k == "left_deadzone" && ParseInt(val, 0, 99, n)) { cfg.mapping.left_deadzone_pct = n; ++applied; }
            else if (k == "right_deadzone" && ParseInt(val, 0, 99, n)) { cfg.mapping.right_deadzone_pct = n; ++applied; }
            else if (k == "trigger_threshold" && ParseInt(val, 0, 99, n)) { cfg.mapping.trigger_threshold_pct = n; ++applied; }
            else if (k == "rumble_scale" && ParseInt(val, 0, 200, n)) { cfg.mapping.rumble_scale_pct = n; ++applied; }
        }
    }
    return applied;
}

}  // namespace dmc
