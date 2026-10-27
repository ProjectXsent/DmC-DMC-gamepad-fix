#include "mapping.h"
#include <cmath>
#include <algorithm>

namespace dmc {

static int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

uint8_t ScaleTrigger(int16_t v, int threshold_pct) {
    int in = Clamp(v, 0, 32767);
    int thr = Clamp(threshold_pct, 0, 99) * 32767 / 100;
    if (in <= thr) return 0;
    if (thr > 0) {  // rescale so output starts at 0 right above the threshold
        in = (int)((int64_t)(in - thr) * 32767 / (32767 - thr));
    }
    return (uint8_t)((in * 255 + 16383) / 32767);
}

int16_t FlipY(int16_t y) {
    return y == INT16_MIN ? INT16_MAX : (int16_t)(-y);
}

void ApplyRadialDeadzone(int16_t& x, int16_t& y, int deadzone_pct) {
    int pct = Clamp(deadzone_pct, 0, 99);
    if (pct == 0) return;
    const double dz = 32767.0 * pct / 100.0;
    const double fx = x, fy = y;
    const double mag = std::sqrt(fx * fx + fy * fy);
    if (mag <= dz) { x = 0; y = 0; return; }
    double scaled = (std::min(mag, 32767.0) - dz) / (32767.0 - dz);  // 0..1
    double k = scaled * 32767.0 / mag;
    x = (int16_t)Clamp((int)std::lround(fx * k), -32768, 32767);
    y = (int16_t)Clamp((int)std::lround(fy * k), -32768, 32767);
}

uint16_t ScaleRumbleMotor(uint16_t value, int scale_pct) {
    int64_t v = (int64_t)value * Clamp(scale_pct, 0, 200) / 100;
    return (uint16_t)(v > 0xFFFF ? 0xFFFF : v);
}

XInputGamepad MapToXInput(const PadSnapshot& in, const MappingConfig& cfg) {
    uint32_t b = in.buttons;
    if (cfg.swap_ab) {
        bool s = (b & PB_SOUTH) != 0, e = (b & PB_EAST) != 0;
        b = (b & ~(uint32_t)(PB_SOUTH | PB_EAST)) | (e ? PB_SOUTH : 0u) | (s ? PB_EAST : 0u);
    }
    if (cfg.swap_xy) {
        bool w = (b & PB_WEST) != 0, n = (b & PB_NORTH) != 0;
        b = (b & ~(uint32_t)(PB_WEST | PB_NORTH)) | (n ? PB_WEST : 0u) | (w ? PB_NORTH : 0u);
    }

    static const struct { uint32_t pb; uint16_t xb; } table[] = {
        {PB_SOUTH, XBTN_A}, {PB_EAST, XBTN_B}, {PB_WEST, XBTN_X}, {PB_NORTH, XBTN_Y},
        {PB_BACK, XBTN_BACK}, {PB_START, XBTN_START},
        {PB_LSTICK, XBTN_LEFT_THUMB}, {PB_RSTICK, XBTN_RIGHT_THUMB},
        {PB_LSHOULDER, XBTN_LEFT_SHOULDER}, {PB_RSHOULDER, XBTN_RIGHT_SHOULDER},
        {PB_DUP, XBTN_DPAD_UP}, {PB_DDOWN, XBTN_DPAD_DOWN},
        {PB_DLEFT, XBTN_DPAD_LEFT}, {PB_DRIGHT, XBTN_DPAD_RIGHT},
    };

    XInputGamepad g{};
    for (const auto& e : table)
        if (b & e.pb) g.wButtons = (uint16_t)(g.wButtons | e.xb);

    int16_t lx = in.lx, ly = FlipY(in.ly), rx = in.rx, ry = FlipY(in.ry);
    ApplyRadialDeadzone(lx, ly, cfg.left_deadzone_pct);
    ApplyRadialDeadzone(rx, ry, cfg.right_deadzone_pct);
    g.sThumbLX = lx; g.sThumbLY = ly; g.sThumbRX = rx; g.sThumbRY = ry;

    g.bLeftTrigger  = ScaleTrigger(in.lt, cfg.trigger_threshold_pct);
    g.bRightTrigger = ScaleTrigger(in.rt, cfg.trigger_threshold_pct);
    return g;
}

}  // namespace dmc
