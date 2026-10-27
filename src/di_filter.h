// DirectInput device-type filtering. Pure; no Windows dependency, unit-tested.
//
// Why: the game reads gamepads through BOTH XInput and DirectInput. A physical
// pad therefore shows up twice, and the on-screen prompts flip between generic
// "Button N" labels (DirectInput) and Xbox glyphs (XInput). The dinput8.dll proxy
// hides gamepad-class devices from the game's DirectInput enumeration so only
// the XInput path (real or SDL-backed) remains.
#pragma once
#include <cstddef>
#include <cstdint>

namespace dmc {

// DI8DEVTYPE_* occupy the low byte of DIDEVICEINSTANCE::dwDevType; the high
// bytes carry a subtype.
constexpr uint32_t DI8DEVTYPE_DEVICE   = 0x11;
constexpr uint32_t DI8DEVTYPE_MOUSE    = 0x12;
constexpr uint32_t DI8DEVTYPE_KEYBOARD = 0x13;
constexpr uint32_t DI8DEVTYPE_JOYSTICK = 0x14;
constexpr uint32_t DI8DEVTYPE_GAMEPAD  = 0x15;
constexpr uint32_t DI8DEVTYPE_DRIVING  = 0x16;
constexpr uint32_t DI8DEVTYPE_FLIGHT   = 0x17;
constexpr uint32_t DI8DEVTYPE_1STPERSON = 0x18;

// True for controller-class devices that should be hidden. Keyboards, mice and
// the catch-all/supplemental/remote classes are deliberately left visible.
inline bool IsPadDevType(uint32_t devType) {
    uint32_t t = devType & 0xFFu;
    return t >= DI8DEVTYPE_JOYSTICK && t <= DI8DEVTYPE_1STPERSON;
}

// Leading fields of DIDEVICEINSTANCEA/W (identical in both). We only read
// dwDevType, so we avoid depending on <dinput.h>.
struct DIDevHead {
    uint32_t dwSize;
    uint8_t  guidInstance[16];
    uint8_t  guidProduct[16];
    uint32_t dwDevType;
};
static_assert(offsetof(DIDevHead, dwDevType) == 36, "DIDEVICEINSTANCE layout");

}  // namespace dmc
