cmake_minimum_required(VERSION 3.16)
project(GridGame CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ─── Dependencies ─────────────────────────────────────────────────────────────

find_package(SDL2       REQUIRED)
find_package(SDL2_image REQUIRED)
find_package(PkgConfig  REQUIRED)
pkg_check_modules(SDL2_TTF   REQUIRED IMPORTED_TARGET SDL2_ttf)
pkg_check_modules(SDL2_MIXER REQUIRED IMPORTED_TARGET SDL2_mixer)

# ─── Logic sources: no display required, safe to link into tests ─────────────

set(LOGIC_SOURCES
    src/terrain.cpp
    src/entity.cpp
    src/input.cpp
    src/spatial.cpp
    src/scheduler.cpp
    src/events.cpp
    src/recorder.cpp
    src/routine_vm.cpp
    src/game.cpp
    src/terminal_renderer.cpp
)

# ─── Render sources: requires SDL display, not linked into tests ──────────────

set(RENDER_SOURCES
    src/renderer.cpp
    src/audio.cpp
)

# ─── Main executable ──────────────────────────────────────────────────────────

add_executable(grid_game
    src/main.cpp
    ${LOGIC_SOURCES}
    ${RENDER_SOURCES}
)

target_include_directories(grid_game PRIVATE src vendor)
target_link_libraries(grid_game PRIVATE SDL2::SDL2 SDL2_image::SDL2_image PkgConfig::SDL2_TTF PkgConfig::SDL2_MIXER)

# Run from the project root so asset paths resolve correctly
set_target_properties(grid_game PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

# ─── Tests ────────────────────────────────────────────────────────────────────

enable_testing()

add_executable(tests
    tests/test_phase1.cpp
    tests/test_phase2.cpp
    tests/test_phase3.cpp
    tests/test_phase4.cpp
    tests/test_phase5.cpp
    tests/test_phase6.cpp
    tests/test_phase7.cpp
    tests/test_phase8.cpp
    tests/test_phase9.cpp
    tests/test_terminal_renderer.cpp
    tests/test_fire_voltage.cpp
    tests/test_phase10.cpp
    tests/test_input_map.cpp
    tests/test_phase11.cpp
    tests/test_phase12.cpp
    tests/test_phase13.cpp
    tests/test_phase14.cpp
    ${LOGIC_SOURCES}
)

target_include_directories(tests PRIVATE src vendor)
# input.cpp uses SDL types (SDL_Keycode, SDL_Event) but does not call SDL_Init,
# so it is safe to link without a display.
target_link_libraries(tests PRIVATE SDL2::SDL2)

add_test(NAME phase1 COMMAND tests)
