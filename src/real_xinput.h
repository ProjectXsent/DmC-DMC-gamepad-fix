// Forwarding to the genuine system xinput1_3.dll (SysWOW64 for 32-bit).
// Loaded lazily by full path so we never load ourselves.
#pragma once
#include <cstdint>
#include "xinput_types.h"

namespace dmc { namespace real {

bool Load();       // idempotent; call outside DllMain
bool Available();

uint32_t GetState(uint32_t user, XInputState* state);
uint32_t SetState(uint32_t user, XInputVibration* vib);
uint32_t GetCapabilities(uint32_t user, uint32_t flags, XInputCapabilities* caps);
void     Enable(int enable);
uint32_t GetBatteryInformation(uint32_t user, uint8_t devType, XInputBatteryInformation* info);
uint32_t GetKeystroke(uint32_t user, uint32_t reserved, void* keystroke);
uint32_t GetDSoundAudioDeviceGuids(uint32_t user, void* render, void* capture);

}}  // namespace dmc::real
