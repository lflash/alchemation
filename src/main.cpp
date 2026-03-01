#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>

#include "types.hpp"
#include "input.hpp"
#include "game.hpp"
#include "renderer.hpp"

int main() {
    std::srand(static_cast<unsigned>(std::time(nullptr)));

    Renderer renderer;
    Input    input;
    Game     game;

    // ── UI state ─────────────────────────────────────────────────────────────
    bool showControls   = false;
    bool showRecordings = false;
    bool renamingScript = false;
    std::string renameBuffer;
    Input emptyInput;   // passed to game.tick() while rename is active

    // ── Camera state ─────────────────────────────────────────────────────────
    Camera camera;                        // starts at (0,0), zoom 1.0
    Vec2f  camOffset = {0.0f, 0.0f};      // arrow-key pan offset (in tile units)

    const float CAM_LERP  = 8.0f;   // exponential smoothing rate (units/sec)
    const float PAN_SPEED = 8.0f;   // tile units per second when panning
    const float ZOOM_STEP = 1.15f;  // per scroll notch
    const float ZOOM_MIN  = 0.25f;
    const float ZOOM_MAX  = 4.0f;

    // ── Timing ───────────────────────────────────────────────────────────────
    const double TICK_DT     = 1.0 / 50.0;
    double       accumulator = 0.0;
    uint64_t     lastTime    = SDL_GetTicks64();
    Tick         currentTick = 0;
    bool         quit        = false;

    while (!quit) {
        input.beginFrame();

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) quit = true;

            // Text input events for rename mode
            if (renamingScript) {
                if (e.type == SDL_TEXTINPUT) {
                    renameBuffer += e.text.text;
                    continue;
                }
                if (e.type == SDL_KEYDOWN) {
                    SDL_Keycode k = e.key.keysym.sym;
                    if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
                        // Confirm rename
                        auto list = game.recordingList();
                        for (const auto& r : list)
                            if (r.selected) { game.renameRecording(r.index, renameBuffer); break; }
                        SDL_StopTextInput();
                        renamingScript = false;
                    } else if (k == SDLK_ESCAPE) {
                        SDL_StopTextInput();
                        renamingScript = false;
                    } else if (k == SDLK_BACKSPACE && !renameBuffer.empty()) {
                        renameBuffer.pop_back();
                    }
                    continue;
                }
            }

            input.handleEvent(e);
        }

        if (input.pressed(Key::Escape)) quit = true;

        // Panel toggles — mutually exclusive
        if (input.pressed(Key::H)) {
            showControls   = !showControls;
            showRecordings = false;
        }
        if (input.pressed(Key::I)) {
            showRecordings = !showRecordings;
            showControls   = false;
        }

        // Start rename when recordings panel is open and Enter pressed
        if (showRecordings && !renamingScript && input.pressed(Key::Enter)) {
            auto list = game.recordingList();
            for (const auto& r : list) {
                if (r.selected) { renameBuffer = r.name; break; }
            }
            SDL_StartTextInput();
            renamingScript = true;
        }

        uint64_t now = SDL_GetTicks64();
        double   dt  = (now - lastTime) / 1000.0;
        lastTime = now;
        accumulator += std::min(dt, 0.1);

        const Input& tickInput = renamingScript ? emptyInput : input;
        while (accumulator >= TICK_DT) {
            game.tick(tickInput, currentTick);
            accumulator -= TICK_DT;
            ++currentTick;
        }

        // Snap camera instantly on grid switch (no lerp artefact).
        if (game.consumeGridSwitch()) {
            Vec2f snap = toVec(game.playerPos());
            camera.pos    = snap;
            camera.target = snap;
            camOffset     = {0.0f, 0.0f};
        }

        // ── Camera update (per render frame, uses real dt) ────────────────
        float fdt = static_cast<float>(dt);

        // Arrow keys pan the offset; Backspace re-centres.
        if (input.held(Key::ArrowLeft))  camOffset.x -= PAN_SPEED * fdt;
        if (input.held(Key::ArrowRight)) camOffset.x += PAN_SPEED * fdt;
        if (input.held(Key::ArrowUp))    camOffset.y -= PAN_SPEED * fdt;
        if (input.held(Key::ArrowDown))  camOffset.y += PAN_SPEED * fdt;
        if (!renamingScript && input.pressed(Key::Backspace)) camOffset = {0.0f, 0.0f};

        // Ctrl + scroll wheel → zoom around screen centre.
        int scroll = input.scroll();
        if (input.held(Key::Ctrl) && scroll != 0) {
            camera.zoom *= std::pow(ZOOM_STEP, static_cast<float>(scroll));
            camera.zoom  = std::clamp(camera.zoom, ZOOM_MIN, ZOOM_MAX);
        }

        // Camera target = player's current render position + manual offset.
        Vec2f playerRenderPos = lerp(
            toVec(game.playerPos()),
            toVec(game.playerDestination()),
            game.playerMoveT()
        );
        camera.target = { playerRenderPos.x + camOffset.x,
                          playerRenderPos.y + camOffset.y };

        // Exponential lerp toward target.
        float factor = 1.0f - std::exp(-CAM_LERP * fdt);
        camera.pos.x += (camera.target.x - camera.pos.x) * factor;
        camera.pos.y += (camera.target.y - camera.pos.y) * factor;

        // ── Render ───────────────────────────────────────────────────────
        renderer.setCamera(camera);
        renderer.beginFrame();
        renderer.drawTerrain(game.terrain());

        for (const Entity* ent : game.drawOrder()) {
            Vec2f renderPos = lerp(toVec(ent->pos), toVec(ent->destination), ent->moveT);
            renderer.drawSprite(renderPos, ent->type);
        }

        renderer.drawHUD(game.playerMana(), game.isRecording());
        if (showRecordings)
            renderer.drawRecordingsPanel(game.recordingList(), renamingScript, renameBuffer);
        else if (showControls)
            renderer.drawControlsMenu();

        renderer.endFrame();
    }

    return 0;
}
