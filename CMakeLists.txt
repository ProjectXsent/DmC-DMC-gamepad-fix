cmake_minimum_required(VERSION 3.21)
project(dmc_sdl_controller VERSION 0.1.0 LANGUAGES CXX)

option(DMC_BUILD_SHIM    "Build the xinput1_3.dll proxy (Windows, 32-bit only)" ${WIN32})
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
  if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR
      "DmC: Devil May Cry is a 32-bit executable, so the shim must be 32-bit.\n"
      "Configure with:  cmake -S . -B build -A Win32   (or use the 'win32' preset)")
  endif()

  include(cmake/FetchSDL.cmake)

  # Target name == DLL name. exports.def pins the ordinals the game imports by.
  add_library(xinput1_3 SHARED
    src/dllmain.cpp
    src/xinput_shim.cpp
    src/real_xinput.cpp
    src/sdl_backend.cpp
    src/winutil.cpp
    src/log.cpp
    src/exports.def)
  set_target_properties(xinput1_3 PROPERTIES PREFIX "" OUTPUT_NAME "xinput1_3")
  target_link_libraries(xinput1_3 PRIVATE dmc_core ${DMC_SDL_TARGET})
  target_compile_definitions(xinput1_3 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)

  # Second proxy: hides gamepads from the game's DirectInput so the physical pad is
  # not seen twice (generic "Button N" prompts vs Xbox glyphs). No SDL dependency.
  add_library(dinput8 SHARED
    src/dinput_proxy.cpp
    src/winutil.cpp
    src/log.cpp
    src/dinput8.def)
  set_target_properties(dinput8 PROPERTIES PREFIX "" OUTPUT_NAME "dinput8")
  target_link_libraries(dinput8 PRIVATE dmc_core)
  target_compile_definitions(dinput8 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)

  # Dev convenience: drop the default config + mapping DB next to the built DLL.
  add_custom_command(TARGET xinput1_3 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/config/dmc_sdl.ini" "$<TARGET_FILE_DIR:xinput1_3>/dmc_sdl.ini")

  install(TARGETS xinput1_3 dinput8 RUNTIME DESTINATION .)
  install(FILES config/dmc_sdl.ini tools/install.ps1 README.md LICENSE DESTINATION .)
endif()

if(DMC_BUILD_HARNESS)
  add_executable(xinput_harness tests/harness/harness.cpp)
  if(DMC_BUILD_SHIM)
    add_dependencies(xinput_harness xinput1_3)
    add_custom_command(TARGET xinput_harness POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "$<TARGET_FILE:xinput1_3>" "$<TARGET_FILE_DIR:xinput_harness>/xinput1_3.dll")
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
