// INI configuration. Parsing is locale-independent (integers only) because the
// host game may call setlocale(). Pure code; no Windows dependency.
#pragma once
#include <string>
#include <vector>
#include <utility>
#include "mapping.h"

namespace dmc {

enum class Mode { Hybrid, Sdl, Passthrough };

struct Config {
    Mode mode = Mode::Hybrid;
    bool prefer_sdl = false;        // hybrid: an SDL pad shadows a real XInput pad
    bool ignore_xbox_pads = true;   // SDL pads typed Xbox360/XboxOne are left to real XInput
    bool hide_dinput_pads = true;   // dinput8 proxy hides gamepads from the game's DirectInput
    bool log = false;
    std::string mappings_file = "gamecontrollerdb.txt";
    MappingConfig mapping;
    std::vector<std::pair<std::string, std::string>> hints;  // raw SDL hints
};

// Parses INI text on top of `cfg` (unknown keys are ignored). Returns the
// number of recognised settings applied.
int ParseConfigText(const char* text, Config& cfg);

}  // namespace dmc
