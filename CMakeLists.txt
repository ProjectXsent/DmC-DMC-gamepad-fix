cmake_minimum_required(VERSION 3.21)
project(dmc_sdl_controller VERSION 0.1.0 LANGUAGES CXX)

option(DMC_BUILD_SHIM    "Build the XInput proxy DLL (Windows)" ${WIN32})

# The game also reads gamepads through DirectInput, so a physical pad would be seen twice
# (generic "Button N" prompts vs Xbox glyphs). With DMC_USE_MINHOOK=ON (default) the XInput
# proxy detours DirectInput8Create in-process with MinHook and hides gamepad-class
# devices; OFF builds without that hook (and without fetching MinHook).
option(DMC_USE_MINHOOK "Hide DirectInput gamepads via an in-process MinHook detour" ON)

# Which XInput DLL the target game imports (check with tools/inspect_imports.py):
#   32-bit DmC: Devil May Cry -> xinput1_3 (default)
#   many 64-bit titles        -> xinput1_4 (Windows 8+) or xinput9_1_0
set(DMC_XINPUT_DLL "xinput1_3" CACHE STRING "Base name of the XInput proxy DLL (no .dll)")
# The ordinal numbering 2..8 is the expected xinput1_3 layout (recalled from memory;
# CI and tools/inspect_imports.py check that 2 and 3 are exported). For other names
# it defaults to OFF (names-only export). Turn ON only if the game imports that DLL
# BY ORDINAL, and confirm the numbering with `inspect_imports.py --exports`.
if(DMC_XINPUT_DLL STREQUAL "xinput1_3")
  option(DMC_EXPORT_ORDINALS "Also export the XInput ordinals (2..8)" ON)
else()
  option(DMC_EXPORT_ORDINALS "Also export the XInput ordinals (2..8)" OFF)
endif()
option(DMC_BUILD_TESTS   "Build unit tests" ON)
option(DMC_BUILD_HARNESS "Build the 32-bit ordinal-import test harness (Windows)" ${WIN32})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static CRT: the shim lives inside a 2012-era game process; don't depend on
# (or collide with) whatever MSVC runtime the game ships.
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  add_compile_options(/W4 /permissive-)
endif()

# Platform-independent logic: mapping + config. Fully unit-tested on any host.
add_library(dmc_core STATIC src/mapping.cpp src/config.cpp)
target_include_directories(dmc_core PUBLIC src)

if(DMC_BUILD_SHIM)
  if(NOT WIN32)
    message(FATAL_ERROR "The shim is a Windows DLL. Use -DDMC_BUILD_SHIM=OFF to build tests only.")
  endif()
  # The proxy's bitness MUST match the game's (see tools/inspect_imports.py output).
  if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(DMC_ARCH "x86")
  else()
    set(DMC_ARCH "x64")
  endif()
  message(STATUS "dmc-sdl-controller: building ${DMC_ARCH} proxy ${DMC_XINPUT_DLL}.dll "
                 "(ordinals: ${DMC_EXPORT_ORDINALS})")
  if(DMC_ARCH STREQUAL "x86" AND NOT DMC_XINPUT_DLL STREQUAL "xinput1_3")
    message(WARNING "A 32-bit build named ${DMC_XINPUT_DLL}: DmC: Devil May Cry itself imports xinput1_3.")
  endif()

  include(cmake/FetchSDL.cmake)

  # Generate the export table: names always, ordinals optionally.
  foreach(n 2 3 4 5 6 7 8)
    if(DMC_EXPORT_ORDINALS)
      set(DMC_ORD_${n} "@${n}")
    else()
      set(DMC_ORD_${n} "")
    endif()
  endforeach()
  configure_file(src/exports.def.in "${CMAKE_BINARY_DIR}/generated/xinput_exports.def")

  set(DMC_PROXY_DEFS WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS
                     DMC_XINPUT_BASENAME=${DMC_XINPUT_DLL})

  if(DMC_USE_MINHOOK)
    include(cmake/FetchMinHook.cmake)
  endif()

  add_library(dmc_xinput SHARED
    src/dllmain.cpp
    src/xinput_shim.cpp
    src/real_xinput.cpp
    src/sdl_backend.cpp
    src/winutil.cpp
    src/log.cpp
    "${CMAKE_BINARY_DIR}/generated/xinput_exports.def")
  set_target_properties(dmc_xinput PROPERTIES PREFIX "" OUTPUT_NAME "${DMC_XINPUT_DLL}")
  target_link_libraries(dmc_xinput PRIVATE dmc_core ${DMC_SDL_TARGET})
  target_compile_definitions(dmc_xinput PRIVATE ${DMC_PROXY_DEFS})
  if(DMC_USE_MINHOOK)
    target_sources(dmc_xinput PRIVATE src/di_hide.cpp src/di_minhook.cpp)
    target_link_libraries(dmc_xinput PRIVATE dmc_minhook)
    target_compile_definitions(dmc_xinput PRIVATE DMC_DI_MINHOOK)
  endif()

  # Dev convenience: drop the default config next to the built DLLs.
  add_custom_command(TARGET dmc_xinput POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/config/dmc_sdl.ini" "$<TARGET_FILE_DIR:dmc_xinput>/dmc_sdl.ini")

  install(TARGETS dmc_xinput RUNTIME DESTINATION .)
  install(FILES config/dmc_sdl.ini tools/install.ps1 README.md LICENSE DESTINATION .)
endif()

if(DMC_BUILD_HARNESS)
  add_executable(xinput_harness tests/harness/harness.cpp)
  target_compile_definitions(xinput_harness PRIVATE DMC_XINPUT_BASENAME=${DMC_XINPUT_DLL})
  if(DMC_BUILD_SHIM)
    add_dependencies(xinput_harness dmc_xinput)
    add_custom_command(TARGET xinput_harness POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "$<TARGET_FILE:dmc_xinput>" "$<TARGET_FILE_DIR:xinput_harness>")
  endif()
endif()

if(DMC_BUILD_TESTS)
  enable_testing()
  add_executable(test_mapping tests/test_mapping.cpp)
  target_link_libraries(test_mapping PRIVATE dmc_core)
  add_test(NAME mapping COMMAND test_mapping)

  add_executable(test_di_filter tests/test_di_filter.cpp)
  target_link_libraries(test_di_filter PRIVATE dmc_core)
  add_test(NAME di_filter COMMAND test_di_filter)

  add_executable(test_config tests/test_config.cpp)
  target_link_libraries(test_config PRIVATE dmc_core)
  add_test(NAME config COMMAND test_config)

  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_FOUND)
    add_test(NAME inspect_imports
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_inspect_imports.py)
  endif()
endif()
