// SDL3 gamepad backend: device discovery, hotplug, slot assignment, polling.
// NOT thread-safe by itself: the caller (xinput_shim.cpp) serialises access.
#pragma once
#include <cstdint>
#include <string>
#include "config.h"
#include "mapping.h"

namespace dmc { namespace sdlb {

// Optional probe so a new SDL pad avoids slots a real XInput pad occupies.
typedef bool (*RealConnectedFn)(int user);

bool Init(const Config& cfg, const std::string& mappingsPathUtf8, RealConnectedFn probe);
void Pump();                                   // throttled; handles hotplug
bool SlotActive(int user);
bool ReadSlot(int user, PadSnapshot& out);     // false if no pad in slot
bool Rumble(int user, uint16_t low, uint16_t high);
void StopAllRumble();

}}  // namespace dmc::sdlb
