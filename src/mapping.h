// Pure translation layer: neutral gamepad snapshot -> XINPUT_GAMEPAD.
// No SDL and no Windows dependencies, so it is fully unit-testable.
#pragma once
#include <cstdint>
#include "xinput_types.h"

namespace dmc {

// Bits for PadSnapshot::buttons. Names are *positional* (SDL3 style):
// SOUTH is the bottom face button, which XInput calls A.
enum PadButton : uint32_t {
    PB_SOUTH = 1u << 0,  PB_EAST  = 1u << 1,  PB_WEST  = 1u << 2,  PB_NORTH = 1u << 3,
    PB_BACK  = 1u << 4,  PB_START = 1u << 5,
    PB_LSTICK = 1u << 6, PB_RSTICK = 1u << 7,
    PB_LSHOULDER = 1u << 8, PB_RSHOULDER = 1u << 9,
    PB_DUP = 1u << 10, PB_DDOWN = 1u << 11, PB_DLEFT = 1u << 12, PB_DRIGHT = 1u << 13,
};

struct PadSnapshot {
    uint32_t buttons = 0;
    // Stick axes in SDL convention: -32768..32767, Y positive = DOWN.
    int16_t lx = 0, ly = 0, rx = 0, ry = 0;
    // Triggers in SDL convention: 0..32767.
    int16_t lt = 0, rt = 0;
};

struct MappingConfig {
    bool swap_ab = false;            // swap A<->B (label-matching on Nintendo pads)
    bool swap_xy = false;            // swap X<->Y
    int  left_deadzone_pct = 0;      // 0..99, radial, rescaled
    int  right_deadzone_pct = 0;
    int  trigger_threshold_pct = 0;  // 0..99
    int  rumble_scale_pct = 100;     // 0..200 (result clamped to 16 bits)
};

// Individual helpers (exposed for tests).
uint8_t  ScaleTrigger(int16_t sdl_value, int threshold_pct);
int16_t  FlipY(int16_t sdl_y);  // SDL Y-down -> XInput Y-up, safe for -32768
void     ApplyRadialDeadzone(int16_t& x, int16_t& y, int deadzone_pct);
uint16_t ScaleRumbleMotor(uint16_t value, int scale_pct);

XInputGamepad MapToXInput(const PadSnapshot& in, const MappingConfig& cfg);

}  // namespace dmc
