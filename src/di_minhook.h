// In-process interception of DirectInput8Create via MinHook (inline detour).
// Nothing extra to install in the game folder: the XInput proxy does it all in-process.
#pragma once

namespace dmc { namespace dimh {

// Called from DllMain. The exe imports DINPUT8.dll statically, so the module is
// already mapped by now and no LoadLibrary is needed. If it is not (delay load),
// RetryIfNeeded() tries again at the first XInput call.
void InstallEarly();
void RetryIfNeeded();
const char* Status();   // human-readable result, for the log banner

}}  // namespace dmc::dimh
