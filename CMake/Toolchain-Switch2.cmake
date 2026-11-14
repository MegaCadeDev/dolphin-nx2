if(NOT DEFINED ENV{DEVKITPRO})
  message(FATAL_ERROR "DEVKITPRO must point to the devkitPro installation")
endif()

# The Switch 2 build is the Switch frontend linked against libounce and
# OunceVK from ounce-devkit (github.com/ouncebrew/ounce-devkit) in place of
# libnx and the Switch Mesa SDK.
set(DOLPHIN_SWITCH ON CACHE BOOL "Build the standalone Nintendo Switch frontend" FORCE)
set(DOLPHIN_SWITCH2 ON CACHE BOOL "Build the frontend for Switch 2 with libounce" FORCE)
include("$ENV{DEVKITPRO}/libounce/cmake/Switch2.cmake")

add_compile_options(
  "-ffile-prefix-map=${CMAKE_SOURCE_DIR}=."
  "-fmacro-prefix-map=${CMAKE_SOURCE_DIR}=."
  -fno-ident
)
