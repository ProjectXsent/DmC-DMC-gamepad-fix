#include "test_common.h"
#include "../src/config.h"
using namespace dmc;

static void test_defaults() {
    Config c;
    CHECK(c.mode == Mode::Hybrid);
    CHECK(!c.prefer_sdl);
    CHECK(c.ignore_xbox_pads);
    CHECK(c.hide_dinput_pads);
    CHECK(!c.log);
    CHECK_EQ(c.mapping.rumble_scale_pct, 100);
    CHECK_EQ(ParseConfigText("", c), 0);
    CHECK_EQ(ParseConfigText(nullptr, c), 0);
}

static void test_parse() {
    const char* ini =
        "; comment\r\n"
        "[General]\r\n"
        "Mode = sdl   ; inline comment\r\n"
        "prefer_sdl=TRUE\r\n"
        "ignore_xbox_pads = no\r\n"
        "hide_dinput_pads = off\r\n"
        "log=1\r\n"
        "mappings_file = my db.txt\r\n"
        "[Input]\r\n"
        "swap_ab=on\r\n"
        "left_deadzone=15\r\n"
        "right_deadzone=500\r\n"      // clamped to 99
        "trigger_threshold=10\r\n"
        "rumble_scale=150\r\n"
        "bogus=1\r\n"
        "[Hints]\r\n"
        "SDL_JOYSTICK_HIDAPI_PS5_RUMBLE = 1\r\n"
        "SDL_HINT_B=a;b\r\n";         // ';' kept inside hint values
    Config c;
    int n = ParseConfigText(ini, c);
    CHECK_EQ(n, 13);
    CHECK(c.mode == Mode::Sdl);
    CHECK(c.prefer_sdl);
    CHECK(!c.ignore_xbox_pads);
    CHECK(!c.hide_dinput_pads);
    CHECK(c.log);
    CHECK(c.mappings_file == "my db.txt");
    CHECK(c.mapping.swap_ab);
    CHECK(!c.mapping.swap_xy);
    CHECK_EQ(c.mapping.left_deadzone_pct, 15);
    CHECK_EQ(c.mapping.right_deadzone_pct, 99);
    CHECK_EQ(c.mapping.trigger_threshold_pct, 10);
    CHECK_EQ(c.mapping.rumble_scale_pct, 150);
    CHECK_EQ((long long)c.hints.size(), 2);
    CHECK(c.hints[0].first == "SDL_JOYSTICK_HIDAPI_PS5_RUMBLE" && c.hints[0].second == "1");
    CHECK(c.hints[1].second == "a;b");
}

static void test_bad_values_ignored() {
    Config c;
    ParseConfigText("[General]\nmode=banana\nprefer_sdl=maybe\n[Input]\nleft_deadzone=abc\n", c);
    CHECK(c.mode == Mode::Hybrid);
    CHECK(!c.prefer_sdl);
    CHECK_EQ(c.mapping.left_deadzone_pct, 0);
    // keys outside a known section are ignored
    Config d;
    CHECK_EQ(ParseConfigText("mode=sdl\n", d), 0);
    CHECK(d.mode == Mode::Hybrid);
}

int main() {
    test_defaults();
    test_parse();
    test_bad_values_ignored();
    return TEST_SUMMARY();
}
