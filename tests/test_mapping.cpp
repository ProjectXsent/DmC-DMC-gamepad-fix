#include "test_common.h"
#include "../src/mapping.h"
using namespace dmc;

static void test_buttons() {
    MappingConfig cfg;
    PadSnapshot s;
    s.buttons = PB_SOUTH | PB_NORTH | PB_START | PB_DLEFT | PB_LSHOULDER;
    XInputGamepad g = MapToXInput(s, cfg);
    CHECK_EQ(g.wButtons, XBTN_A | XBTN_Y | XBTN_START | XBTN_DPAD_LEFT | XBTN_LEFT_SHOULDER);

    s.buttons = PB_EAST | PB_WEST | PB_BACK | PB_LSTICK | PB_RSTICK | PB_RSHOULDER |
                PB_DUP | PB_DDOWN | PB_DRIGHT;
    g = MapToXInput(s, cfg);
    CHECK_EQ(g.wButtons, XBTN_B | XBTN_X | XBTN_BACK | XBTN_LEFT_THUMB | XBTN_RIGHT_THUMB |
                         XBTN_RIGHT_SHOULDER | XBTN_DPAD_UP | XBTN_DPAD_DOWN | XBTN_DPAD_RIGHT);

    s.buttons = 0;
    CHECK_EQ(MapToXInput(s, cfg).wButtons, 0);
}

static void test_swaps() {
    MappingConfig cfg; cfg.swap_ab = true;
    PadSnapshot s; s.buttons = PB_SOUTH;
    CHECK_EQ(MapToXInput(s, cfg).wButtons, XBTN_B);
    s.buttons = PB_EAST;
    CHECK_EQ(MapToXInput(s, cfg).wButtons, XBTN_A);
    s.buttons = PB_SOUTH | PB_EAST;
    CHECK_EQ(MapToXInput(s, cfg).wButtons, XBTN_A | XBTN_B);

    MappingConfig cfg2; cfg2.swap_xy = true;
    s.buttons = PB_WEST;
    CHECK_EQ(MapToXInput(s, cfg2).wButtons, XBTN_Y);
    s.buttons = PB_NORTH;
    CHECK_EQ(MapToXInput(s, cfg2).wButtons, XBTN_X);
}

static void test_y_axis() {
    CHECK_EQ(FlipY(0), 0);
    CHECK_EQ(FlipY(32767), -32767);
    CHECK_EQ(FlipY(-32767), 32767);
    CHECK_EQ(FlipY(-32768), 32767);  // must not overflow

    MappingConfig cfg; PadSnapshot s;
    s.ly = -20000;  // SDL: stick pushed UP
    CHECK_EQ(MapToXInput(s, cfg).sThumbLY, 20000);  // XInput: positive = up
    s.lx = 1234;
    CHECK_EQ(MapToXInput(s, cfg).sThumbLX, 1234);   // X is not flipped
}

static void test_triggers() {
    CHECK_EQ(ScaleTrigger(0, 0), 0);
    CHECK_EQ(ScaleTrigger(32767, 0), 255);
    CHECK_EQ(ScaleTrigger(16384, 0), 128);
    CHECK_EQ(ScaleTrigger(-5, 0), 0);          // clamped
    CHECK_EQ(ScaleTrigger(5000, 20), 0);       // below 20% threshold
    CHECK_EQ(ScaleTrigger(32767, 20), 255);    // rescaled to full
    int mid = ScaleTrigger(20000, 20);
    CHECK(mid > 0 && mid < 255);
}

static void test_deadzone() {
    int16_t x = 1000, y = 1000;
    ApplyRadialDeadzone(x, y, 0);
    CHECK_EQ(x, 1000); CHECK_EQ(y, 1000);      // disabled = untouched

    x = 3000; y = 3000;                        // magnitude ~4243 < 20% (6553)
    ApplyRadialDeadzone(x, y, 20);
    CHECK_EQ(x, 0); CHECK_EQ(y, 0);

    x = 32767; y = 0;                          // full deflection stays full
    ApplyRadialDeadzone(x, y, 20);
    CHECK_EQ(x, 32767); CHECK_EQ(y, 0);

    x = 0; y = -32768;                         // extreme negative must not wrap
    ApplyRadialDeadzone(x, y, 20);
    CHECK(y < -32000);

    x = 20000; y = 0;                          // monotonic + rescaled
    ApplyRadialDeadzone(x, y, 20);
    CHECK(x > 0 && x < 20000);
}

static void test_rumble() {
    CHECK_EQ(ScaleRumbleMotor(40000, 100), 40000);
    CHECK_EQ(ScaleRumbleMotor(40000, 50), 20000);
    CHECK_EQ(ScaleRumbleMotor(40000, 0), 0);
    CHECK_EQ(ScaleRumbleMotor(60000, 200), 65535);  // clamped
}

int main() {
    test_buttons();
    test_swaps();
    test_y_axis();
    test_triggers();
    test_deadzone();
    test_rumble();
    return TEST_SUMMARY();
}
