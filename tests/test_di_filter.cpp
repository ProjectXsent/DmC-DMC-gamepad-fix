#include "test_common.h"
#include "../src/di_filter.h"
using namespace dmc;

int main() {
    // controller classes are hidden
    CHECK(IsPadDevType(DI8DEVTYPE_JOYSTICK));
    CHECK(IsPadDevType(DI8DEVTYPE_GAMEPAD));
    CHECK(IsPadDevType(DI8DEVTYPE_DRIVING));
    CHECK(IsPadDevType(DI8DEVTYPE_FLIGHT));
    CHECK(IsPadDevType(DI8DEVTYPE_1STPERSON));
    // subtype bits in the high bytes must not matter
    CHECK(IsPadDevType(0x00000415));
    CHECK(IsPadDevType(0x00010114));
    // keyboard / mouse / generic device must stay visible
    CHECK(!IsPadDevType(DI8DEVTYPE_KEYBOARD));
    CHECK(!IsPadDevType(DI8DEVTYPE_MOUSE));
    CHECK(!IsPadDevType(DI8DEVTYPE_DEVICE));
    CHECK(!IsPadDevType(0x00000413));
    // supplemental (0x1C), devicectrl (0x19), screen pointer, remote, and junk
    CHECK(!IsPadDevType(0x19));
    CHECK(!IsPadDevType(0x1C));
    CHECK(!IsPadDevType(0x00));
    CHECK(!IsPadDevType(0x13 | 0x1500));  // low byte is what counts
    return TEST_SUMMARY();
}
