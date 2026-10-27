// Minimal, platform-independent copies of the XInput structures and constants
// the shim needs. Kept separate from <xinput.h> so the mapping/config code can
// be unit-tested on any host, and so we never collide with the SDK header.
#pragma once
#include <cstdint>

namespace dmc {

#pragma pack(push, 4)
struct XInputGamepad {
    uint16_t wButtons;
    uint8_t  bLeftTrigger;
    uint8_t  bRightTrigger;
    int16_t  sThumbLX;
    int16_t  sThumbLY;
    int16_t  sThumbRX;
    int16_t  sThumbRY;
};

struct XInputState {
    uint32_t      dwPacketNumber;
    XInputGamepad Gamepad;
};

struct XInputVibration {
    uint16_t wLeftMotorSpeed;   // low-frequency (large) motor
    uint16_t wRightMotorSpeed;  // high-frequency (small) motor
};

struct XInputCapabilities {
    uint8_t         Type;
    uint8_t         SubType;
    uint16_t        Flags;
    XInputGamepad   Gamepad;
    XInputVibration Vibration;
};

struct XInputBatteryInformation {
    uint8_t BatteryType;
    uint8_t BatteryLevel;
};
#pragma pack(pop)

static_assert(sizeof(XInputGamepad) == 12, "XINPUT_GAMEPAD layout");
static_assert(sizeof(XInputState) == 16, "XINPUT_STATE layout");
static_assert(sizeof(XInputVibration) == 4, "XINPUT_VIBRATION layout");
static_assert(sizeof(XInputCapabilities) == 20, "XINPUT_CAPABILITIES layout");

// wButtons flags
enum : uint16_t {
    XBTN_DPAD_UP        = 0x0001,
    XBTN_DPAD_DOWN      = 0x0002,
    XBTN_DPAD_LEFT      = 0x0004,
    XBTN_DPAD_RIGHT     = 0x0008,
    XBTN_START          = 0x0010,
    XBTN_BACK           = 0x0020,
    XBTN_LEFT_THUMB     = 0x0040,
    XBTN_RIGHT_THUMB    = 0x0080,
    XBTN_LEFT_SHOULDER  = 0x0100,
    XBTN_RIGHT_SHOULDER = 0x0200,
    XBTN_A              = 0x1000,
    XBTN_B              = 0x2000,
    XBTN_X              = 0x4000,
    XBTN_Y              = 0x8000,
};

// Win32 error codes used by the XInput API.
constexpr uint32_t XERR_SUCCESS              = 0;
constexpr uint32_t XERR_BAD_ARGUMENTS        = 160;
constexpr uint32_t XERR_DEVICE_NOT_CONNECTED = 1167;
constexpr uint32_t XERR_EMPTY                = 4306;

constexpr int kMaxUsers = 4;

}  // namespace dmc
