cmake_minimum_required(VERSION 3.22)
project(Neonpuls VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# SDL3 for the window, the renderer, the pads and the audio stream. Same tag
# as Chordtroller; if Chordtroller has already fetched it, use that checkout
# instead of cloning again.
include(FetchContent)

set(_ct_sdl "${CMAKE_CURRENT_SOURCE_DIR}/../chordtroller/build/_deps/sdl3-src")
if(NOT FETCHCONTENT_SOURCE_DIR_SDL3 AND EXISTS "${_ct_sdl}/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_SDL3 "${_ct_sdl}" CACHE PATH "" FORCE)
endif()

set(SDL_SHARED          OFF CACHE BOOL "" FORCE)
set(SDL_STATIC          ON  CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY    OFF CACHE BOOL "" FORCE)
set(SDL_TESTS           OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES        OFF CACHE BOOL "" FORCE)
set(SDL_DISABLE_INSTALL ON  CACHE BOOL "" FORCE)
set(SDL_CAMERA          OFF CACHE BOOL "" FORCE)
set(SDL_DIALOG          OFF CACHE BOOL "" FORCE)

FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.4.16
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(SDL3)

add_executable(Neonpuls WIN32
    Source/main.cpp
    Source/game/Neon.cpp
    Source/game/World.cpp
    Source/game/Input.cpp)

target_include_directories(Neonpuls PRIVATE Source)
target_link_libraries(Neonpuls PRIVATE SDL3::SDL3-static)

# The game is called Rise of the Arpeggiator (2026-10-09); the target and the
# folder keep the working name.
set_target_properties(Neonpuls PROPERTIES OUTPUT_NAME "RiseOfTheArpeggiator")

if(MSVC)
    target_compile_options(Neonpuls PRIVATE /utf-8 /W3 /permissive-)
    target_compile_definitions(Neonpuls PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()
