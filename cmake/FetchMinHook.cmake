# Provides the static library target dmc_minhook (MinHook, BSD-2-Clause).
#
# We fetch the sources but do NOT use MinHook's own CMake project: compiling the few
# files ourselves makes them inherit our static-CRT setting and avoids depending on
# MinHook's target names/options, which differ between releases.
#   -DDMC_MINHOOK_TAG=<tag>   bump the pinned release
enable_language(C)
include(FetchContent)
set(DMC_MINHOOK_TAG "v1.3.3" CACHE STRING "MinHook git tag (pinned for reproducible builds)")

FetchContent_Declare(minhook
  GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
  GIT_TAG        ${DMC_MINHOOK_TAG}
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  _dmc_no_cmake)   # nonexistent dir => download only, skip its CMakeLists
FetchContent_MakeAvailable(minhook)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(_dmc_hde "${minhook_SOURCE_DIR}/src/hde/hde64.c")
else()
  set(_dmc_hde "${minhook_SOURCE_DIR}/src/hde/hde32.c")
endif()

add_library(dmc_minhook STATIC
  "${minhook_SOURCE_DIR}/src/buffer.c"
  "${minhook_SOURCE_DIR}/src/hook.c"
  "${minhook_SOURCE_DIR}/src/trampoline.c"
  "${_dmc_hde}")
target_include_directories(dmc_minhook PUBLIC "${minhook_SOURCE_DIR}/include")
if(MSVC)
  target_compile_options(dmc_minhook PRIVATE /W0)   # third-party C; silence our /W4
endif()
