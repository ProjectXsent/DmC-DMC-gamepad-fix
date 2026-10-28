// Hides gamepad-class devices from a game's DirectInput (see di_filter.h for why).
// di_minhook.cpp intercepts DirectInput8Create and hands the created interface to
// PatchInterface(). The hiding is an in-place vtable patch of IDirectInput8A/W::EnumDevices
// and ::EnumDevicesBySemantics.
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace dmc { namespace dihide {

// True when the calling thread is inside SDL, so SDL's own DirectInput
// enumeration is never filtered. May be null.
typedef bool (*CallerIsSdlFn)();

void Attach();                                   // DllMain-safe: just sets up a lock
void Configure(bool hide, CallerIsSdlFn guard);
// Patches the vtable if `riid` is IID_IDirectInput8A or W (no-op otherwise).
void PatchInterface(REFIID riid, void* iface);

}}  // namespace dmc::dihide
