# Provides ${DMC_SDL_TARGET}. By default SDL3 is fetched from source and linked
# statically, so the release ships a single self-contained xinput1_3.dll.
#
#   -DDMC_USE_SYSTEM_SDL=ON   use an installed SDL3 (find_package) instead
#   -DDMC_SDL_TAG=release-3.x.y   bump the pinned SDL version
option(DMC_USE_SYSTEM_SDL "Use an installed SDL3 instead of fetching it" OFF)
set(DMC_SDL_TAG "release-3.4.16" CACHE STRING "SDL3 git tag to fetch (pinned for reproducible builds)")

if(DMC_USE_SYSTEM_SDL)
  find_package(SDL3 REQUIRED CONFIG)
else()
  include(FetchContent)
  set(SDL_SHARED  OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC  ON  CACHE BOOL "" FORCE)
  set(SDL_TESTS   OFF CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
  # We only need the joystick/gamepad subsystem. Trim the rest to keep the DLL
  # small. If a future SDL release rejects one of these, just delete the line.
  set(SDL_AUDIO   OFF CACHE BOOL "" FORCE)
  set(SDL_RENDER  OFF CACHE BOOL "" FORCE)
  set(SDL_CAMERA  OFF CACHE BOOL "" FORCE)
  set(SDL_GPU     OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        ${DMC_SDL_TAG}
    GIT_SHALLOW    TRUE)
  FetchContent_MakeAvailable(SDL3)
endif()

if(TARGET SDL3::SDL3-static)
  set(DMC_SDL_TARGET SDL3::SDL3-static)
elseif(TARGET SDL3::SDL3)
  set(DMC_SDL_TARGET SDL3::SDL3)
  message(WARNING "Only shared SDL3 found; you must ship SDL3.dll next to xinput1_3.dll.")
else()
  message(FATAL_ERROR "No SDL3 target available")
endif()
