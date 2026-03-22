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
find_package(Vulkan REQUIRED)

# ─── Shader compilation (glslc → SPIR-V) ─────────────────────────────────────

find_program(GLSLC glslc HINTS "${CMAKE_PREFIX_PATH}/bin" REQUIRED)
file(GLOB SHADER_SOURCES "${CMAKE_SOURCE_DIR}/shaders/*.comp")
set(SHADER_SPV_DIR "${CMAKE_SOURCE_DIR}/shaders/spv")
file(MAKE_DIRECTORY "${SHADER_SPV_DIR}")

set(SHADER_SPV_FILES "")
foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME "${SHADER}" NAME)
    set(SPV "${SHADER_SPV_DIR}/${SHADER_NAME}.spv")
    add_custom_command(
        OUTPUT  "${SPV}"
        COMMAND "${GLSLC}" "${SHADER}" -o "${SPV}"
        DEPENDS "${SHADER}"
        COMMENT "Compiling shader ${SHADER_NAME}"
    )
    list(APPEND SHADER_SPV_FILES "${SPV}")
endforeach()

add_custom_target(shaders ALL DEPENDS ${SHADER_SPV_FILES})

# ─── GPU compute sources (Vulkan) ─────────────────────────────────────────────

set(GPU_SOURCES
    src/vk/vk_context.cpp
    src/vk/gpu_runner.cpp
    src/vk/compute_pipeline.cpp
)

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
    src/effectSpread.cpp
    src/movement.cpp
    src/fluid.cpp
    src/alchemy.cpp
    src/game.cpp
    src/studio.cpp
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
    ${GPU_SOURCES}
)

add_dependencies(grid_game shaders)
target_include_directories(grid_game PRIVATE src vendor)
target_link_libraries(grid_game PRIVATE
    SDL2::SDL2 SDL2_image::SDL2_image PkgConfig::SDL2_TTF PkgConfig::SDL2_MIXER
    Vulkan::Vulkan
)

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
    tests/test_phase15.cpp
    tests/test_phase16.cpp
    tests/test_phase17.cpp
    tests/test_phase18.cpp
    tests/test_alchemy.cpp
    tests/test_extra.cpp
    ${LOGIC_SOURCES}
)

target_include_directories(tests PRIVATE src vendor)
# input.cpp uses SDL types (SDL_Keycode, SDL_Event) but does not call SDL_Init,
# so it is safe to link without a display.
target_link_libraries(tests PRIVATE SDL2::SDL2)

add_test(NAME phase1 COMMAND tests)

# GPU smoke test — requires Vulkan device and VK_LAYER_PATH set for validation layers.
# Run with: ctest -R gpu_smoke or VK_LAYER_PATH=... ctest -R gpu_smoke
add_test(NAME gpu_smoke
    COMMAND gpu_smoke_test
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
set_tests_properties(gpu_smoke PROPERTIES
    ENVIRONMENT "VK_LAYER_PATH=${CMAKE_PREFIX_PATH}/bin"
    LABELS "gpu"
)

# ─── gpu_smoke_test: Phase 25a GPU pipeline verification ─────────────────────

add_executable(gpu_smoke_test src/vk/smoke_test_main.cpp ${GPU_SOURCES})
add_dependencies(gpu_smoke_test shaders)
target_include_directories(gpu_smoke_test PRIVATE src src/vk vendor)
target_link_libraries(gpu_smoke_test PRIVATE Vulkan::Vulkan)
set_target_properties(gpu_smoke_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

# ─── play: turn-by-turn CLI driver for AI play sessions ──────────────────────

add_executable(play play.cpp ${LOGIC_SOURCES})
target_include_directories(play PRIVATE src vendor)
target_link_libraries(play PRIVATE SDL2::SDL2)
set_target_properties(play PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
