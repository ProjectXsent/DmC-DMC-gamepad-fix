// Build-time configuration injected by CMake (-DDMC_XINPUT_BASENAME=xinput1_4).
// Which XInput DLL a game imports varies: this 32-bit title uses xinput1_3, many
// 64-bit titles use xinput1_4 (or xinput9_1_0). The proxy must carry that name.
#pragma once

#ifndef DMC_XINPUT_BASENAME
#define DMC_XINPUT_BASENAME xinput1_3
#endif

#define DMC_WIDEN3(s) L##s
#define DMC_WIDEN2(x) DMC_WIDEN3(#x)
#define DMC_WIDEN(x)  DMC_WIDEN2(x)

// e.g. L"xinput1_3.dll"
#define DMC_XINPUT_FILE_W DMC_WIDEN(DMC_XINPUT_BASENAME) L".dll"
