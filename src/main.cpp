#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <unordered_set>

#include "types.hpp"
#include "input.hpp"
#include "game.hpp"
#include "entity.hpp"
#include "renderer.hpp"
#include "audio.hpp"
#include "ui.hpp"
#include "studio.hpp"

// ─── StudioState ─────────────────────────────────────────────────────────────
//
// All studio-mode scrubber / instruction-editor state. Owned by main.cpp.
// recompute() rebuilds paths whenever recordings are edited.

struct StudioState {
    int   scrubTick    = 0;
    bool  playing      = false;
    bool  loopMode     = false;
    float speedMult    = 1.0f;
    float scrubAccum   = 0.0f;
    int   instrRow     = 0;      // selected row in instruction panel
    bool  panelFocused = false;  // arrow keys go to panel instead of player
    bool  insertingWait = false;
    bool  insertingMove = false;
    std::string waitBuffer;
    RelDir      insertDir = RelDir::Forward;

    // One path per recording (recomputed on enter / edit).
    std::vector<std::vector<PathStep>> paths;
    std::vector<int>                   conflicts;
    int  pathLen = 0;

    // Fixed spawn used for path simulation (centre of bounded 20x20 studio).
    static constexpr int ORIGIN_X = ROOM_W / 2;
    static constexpr int ORIGIN_Y = ROOM_H / 2;

    void recompute(const Game& game) {
        int n = (int)game.recordingCount();
        paths.resize(n);
        for (int i = 0; i < n; ++i)
            paths[i] = routinePath(game.recording(i),
                                   {ORIGIN_X, ORIGIN_Y, 0}, Direction::S);
        conflicts = studioConflicts(paths);
        pathLen = 0;
        for (const auto& p : paths)
            if ((int)p.size() > pathLen) pathLen = (int)p.size();
        scrubTick = std::min(scrubTick, pathLen > 0 ? pathLen - 1 : 0);
        instrRow  = std::min(instrRow,  std::max(0,
            n > 0 ? (int)game.recording(game.selectedRecordingIdx()).instructions.size() - 1 : 0));
    }

    void reset() {
        scrubTick    = 0;
        playing      = false;
        scrubAccum   = 0.0f;
        insertingWait = false;
        insertingMove = false;
        waitBuffer.clear();
    }
};

// ─── Terrain-aware tile picking ───────────────────────────────────────────────
//
// screenToTile assumes tileZ == cam.z (f == 1). For elevated tiles, f > 1 and
// the tile is shifted on screen. This function does a two-pass correction:
// first pick at cam.z to get an approximate (x,y), then look up the actual
// terrain level there and redo the inverse projection at that z.

// Returns true if screen pixel (px,py) falls inside the top face of tile.
// Used to detect when the cursor is on a cliff face rather than a tile top.
static bool pixelOnTileFace(int px, int py, TilePos tile, const Camera& cam) {
    float lf   = static_cast<float>(tile.z);
    float dz   = lf - cam.z;
    float f    = 1.0f + dz / static_cast<float>(Renderer::Z_PERSP);
    float ts   = Renderer::TILE_SIZE * cam.zoom * f;
    float tsH  = Renderer::TILE_H    * cam.zoom;
    float step = static_cast<float>(Renderer::Z_STEP) * cam.zoom;
    float cx   = Renderer::VIEWPORT_W * 0.5f;
    float cy   = Renderer::VIEWPORT_H * 0.5f;
    float sx0  = cx + (tile.x - cam.pos.x) * ts;
    float sy0  = cy + (tile.y - cam.pos.y) * tsH - dz * step * f;
    return px >= sx0 && px < sx0 + ts && py >= sy0 && py < sy0 + tsH;
}

// Two-pass terrain-aware picking. Returns the tile whose top face contains
// the pixel, accounting for perspective and elevation. Sets *valid=false
// when the pixel is on a cliff face between tiles.
static TilePos screenToTileAccurate(int px, int py, const Camera& cam,
                                    const Terrain& terrain, bool* valid) {
    // Pass 1: assume z = cam.z
    TilePos t0 = Renderer::screenToTile(px, py, cam);
    int z0 = terrain.levelAt(t0);

    // Pass 2: re-solve using z0
    float ts   = Renderer::TILE_SIZE * cam.zoom;
    float tsH  = Renderer::TILE_H    * cam.zoom;
    float step = static_cast<float>(Renderer::Z_STEP) * cam.zoom;
    float dz   = static_cast<float>(z0) - cam.z;
    float f    = 1.0f + dz / static_cast<float>(Renderer::Z_PERSP);
    float tileX = cam.pos.x + (px - Renderer::VIEWPORT_W * 0.5f) / (ts * f);
    float tileY = cam.pos.y + (py - Renderer::VIEWPORT_H * 0.5f + dz * step * f) / tsH;

    int rx = static_cast<int>(std::floor(tileX));
    int ry = static_cast<int>(std::floor(tileY));
    // Use the actual terrain level at the pass-2 position — NOT z0 from pass 1.
    // Without this, pixelOnTileFace computes the wrong screen bounds for the tile.
    int rz = terrain.levelAt({rx, ry, 0});
    TilePos result = { rx, ry, rz };
    if (valid) *valid = pixelOnTileFace(px, py, result, cam);
    return result;
}

// ─── Entity display name ──────────────────────────────────────────────────────

static const char* entityTypeName(EntityType t) {
    switch (t) {
        case EntityType::Player:      return "Player";
        case EntityType::Goblin:      return "Goblin";
        case EntityType::Mushroom:    return "Mushroom";
        case EntityType::Poop:        return "Agent";
        case EntityType::Campfire:    return "Campfire";
        case EntityType::TreeStump:   return "Tree Stump";
        case EntityType::Log:         return "Log";
        case EntityType::Battery:     return "Battery";
        case EntityType::Lightbulb:   return "Lightbulb";
        case EntityType::Tree:        return "Tree";
        case EntityType::Rock:        return "Rock";
        case EntityType::Chest:       return "Chest";
        case EntityType::MudGolem:    return "Mud Golem";
        case EntityType::StoneGolem:  return "Stone Golem";
        case EntityType::ClayGolem:   return "Clay Golem";
        case EntityType::WaterGolem:  return "Water Golem";
        case EntityType::BushGolem:   return "Bush Golem";
        case EntityType::WoodGolem:   return "Wood Golem";
        case EntityType::IronGolem:   return "Iron Golem";
        case EntityType::CopperGolem: return "Copper Golem";
    }
    return "Unknown";
}

// ─── Context menu helpers ─────────────────────────────────────────────────────

static void openContextMenu(UIState& ui, TilePos tile, const Entity* ent,
                             TileType ttype, int screenX, int screenY) {
    constexpr int ITEM_H = 20;
    constexpr int PAD    = 8;
    constexpr int W      = 148;

    ui.contextMenu.items.clear();
    if (ent && ent->type != EntityType::Player)
        ui.contextMenu.items.push_back(entityTypeName(ent->type));
    ui.contextMenu.items.push_back("Move here");
    if (ttype == TileType::Grass || ttype == TileType::BareEarth)
        ui.contextMenu.items.push_back("Dig");

    int H = PAD + (int)ui.contextMenu.items.size() * ITEM_H + PAD;
    int X = std::clamp(screenX, 0, Renderer::VIEWPORT_W - W);
    int Y = std::clamp(screenY, 0, Renderer::VIEWPORT_H - H);

    ui.contextMenu.bounds  = { X, Y, W, H };
    ui.contextMenu.active  = true;
    ui.contextMenu.hovered = -1;
    (void)tile; // tile stored implicitly via game.queueClickMove called on selection
}

int main() {
    std::srand(static_cast<unsigned>(std::time(nullptr)));

    Renderer    renderer;
    Input       input;
    Game        game;
    AudioSystem audio;

    constexpr const char* SAVE_PATH     = "save.dat";
    constexpr const char* SETTINGS_PATH = "settings.dat";
    game.load(SAVE_PATH);
    input.setMap(InputMap::load(SETTINGS_PATH));

    // ── UI state ─────────────────────────────────────────────────────────────
    UIState     ui;
    StudioState studio;
    Input       emptyInput;   // passed to game.tick() while rename is active
    bool        wasInStudio = false;

    // ── Mouse state ───────────────────────────────────────────────────────────
    TilePos hoveredTile    = {0, 0, 0};
    bool    hoveredValid   = false;
    int     mouseX         = 0, mouseY = 0;
    bool    middleDragging = false;
    int     dragLastX      = 0, dragLastY = 0;
    // Tile under right-click (remembered for context menu actions).
    TilePos contextTile    = {0, 0, 0};

    // ── Camera state ─────────────────────────────────────────────────────────
    Camera camera;
    Vec2f  camOffset = {0.0f, 0.0f};

    const float CAM_LERP  = 8.0f;
    const float PAN_SPEED = 8.0f;
    const float ZOOM_STEP = 1.15f;
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

            // ── Mouse motion ─────────────────────────────────────────────────
            if (e.type == SDL_MOUSEMOTION) {
                mouseX = e.motion.x;
                mouseY = e.motion.y;
                hoveredTile = screenToTileAccurate(mouseX, mouseY, camera, game.terrain(), &hoveredValid);

                // Update context menu hovered item.
                if (ui.contextMenu.active) {
                    ContextMenu& cm = ui.contextMenu;
                    int relY = mouseY - cm.bounds.y - 8; // 8 = PAD
                    int idx  = relY / 20;                // 20 = ITEM_H
                    cm.hovered = (cm.bounds.contains(mouseX, mouseY) &&
                                  idx >= 0 && idx < (int)cm.items.size()) ? idx : -1;
                }

                // Middle-drag pan.
                if (middleDragging) {
                    int dx = e.motion.x - dragLastX;
                    int dy = e.motion.y - dragLastY;
                    dragLastX = e.motion.x;
                    dragLastY = e.motion.y;
                    float ts  = Renderer::TILE_SIZE * camera.zoom;
                    float tsH = Renderer::TILE_H    * camera.zoom;
                    camOffset.x -= static_cast<float>(dx) / ts;
                    camOffset.y -= static_cast<float>(dy) / tsH;
                }
            }

            // ── Mouse button down ─────────────────────────────────────────────
            if (e.type == SDL_MOUSEBUTTONDOWN) {
                int mx = e.button.x;
                int my = e.button.y;

                if (e.button.button == SDL_BUTTON_LEFT) {
                    // Close context menu on any left-click.
                    if (ui.contextMenu.active) {
                        if (ui.contextMenu.bounds.contains(mx, my)) {
                            // Select item.
                            int relY = my - ui.contextMenu.bounds.y - 8;
                            int idx  = relY / 20;
                            if (idx >= 0 && idx < (int)ui.contextMenu.items.size()) {
                                const std::string& item = ui.contextMenu.items[idx];
                                if (item == "Move here")
                                    game.queueClickMove(contextTile);
                                else if (item == "Dig")
                                    game.queueClickMove(contextTile); // face + next tick digs
                                // Entity name item → no-op (just labels)
                            }
                        }
                        ui.contextMenu.active = false;
                        continue;
                    }

                    // Ignore clicks on active panels.
                    if (ui.isOpen()) { input.handleEvent(e); continue; }

                    // Click-to-move toward hovered tile + ripple (only on valid tile face).
                    if (!hoveredValid) { input.handleEvent(e); continue; }
                    game.queueClickMove(hoveredTile);
                    renderer.spawnBurst(
                        { static_cast<float>(hoveredTile.x) + 0.5f,
                          static_cast<float>(hoveredTile.y) + 0.5f },
                        static_cast<float>(hoveredTile.z),
                        {255, 255, 200, 180}, 6, 1.5f, 0.25f, 2.0f);
                }

                if (e.button.button == SDL_BUTTON_RIGHT) {
                    if (!ui.isOpen()) {
                        ui.contextMenu.active = false;
                        bool    _v;
                        TilePos tile = screenToTileAccurate(mx, my, camera, game.terrain(), &_v);
                        contextTile  = tile;
                        const Entity*  ent   = game.entityAtTile(tile);
                        TileType       ttype = game.terrain().typeAt(tile);
                        openContextMenu(ui, tile, ent, ttype, mx, my);
                    }
                }

                if (e.button.button == SDL_BUTTON_MIDDLE) {
                    middleDragging = true;
                    dragLastX = mx;
                    dragLastY = my;
                }
            }

            // ── Mouse button up ───────────────────────────────────────────────
            if (e.type == SDL_MOUSEBUTTONUP) {
                if (e.button.button == SDL_BUTTON_MIDDLE)
                    middleDragging = false;
            }

            // ── Text input for rename mode ────────────────────────────────────
            if (ui.renamingScript) {
                if (e.type == SDL_TEXTINPUT) {
                    ui.renameBuffer += e.text.text;
                    continue;
                }
                if (e.type == SDL_KEYDOWN) {
                    SDL_Keycode k = e.key.keysym.sym;
                    if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
                        auto list = game.recordingList();
                        for (const auto& r : list)
                            if (r.selected) { game.renameRecording(r.index, ui.renameBuffer); break; }
                        SDL_StopTextInput();
                        ui.renamingScript = false;
                    } else if (k == SDLK_ESCAPE) {
                        SDL_StopTextInput();
                        ui.renamingScript = false;
                    } else if (k == SDLK_BACKSPACE && !ui.renameBuffer.empty()) {
                        ui.renameBuffer.pop_back();
                    }
                    continue;
                }
            }

            // ── Delete script from recordings panel ───────────────────────────
            if (ui.is(ActivePanel::Recordings) && !ui.renamingScript &&
                e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_DELETE) {
                size_t idx = game.selectedRecordingIdx();
                game.deleteRecording(idx);
                studio.recompute(game);
                continue;
            }

            // ── Rebind capture ────────────────────────────────────────────────
            if (ui.rebindListening && e.type == SDL_KEYDOWN && e.key.repeat == 0) {
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_ESCAPE) {
                    ui.rebindListening = false;
                } else {
                    InputMap m = input.getMap();
                    m.set(static_cast<Action>(ui.rebindRow), k);
                    input.setMap(m);
                    ui.rebindListening = false;
                }
                continue;
            }

            // Close context menu on any key press.
            if (e.type == SDL_KEYDOWN && ui.contextMenu.active) {
                ui.contextMenu.active = false;
            }

            // ── Studio insert-WAIT mode ───────────────────────────────────────
            if (game.inStudio() && studio.insertingWait && e.type == SDL_KEYDOWN) {
                SDL_Keycode k = e.key.keysym.sym;
                if (k >= SDLK_0 && k <= SDLK_9) {
                    if (studio.waitBuffer.size() < 4)
                        studio.waitBuffer += static_cast<char>('0' + (k - SDLK_0));
                } else if (k == SDLK_BACKSPACE && !studio.waitBuffer.empty()) {
                    studio.waitBuffer.pop_back();
                } else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
                    uint16_t ticks = studio.waitBuffer.empty()
                                     ? 1 : static_cast<uint16_t>(std::stoi(studio.waitBuffer));
                    size_t selRec = game.selectedRecordingIdx();
                    game.insertWait(selRec, (size_t)studio.instrRow, ticks);
                    studio.instrRow++;
                    studio.insertingWait = false;
                    studio.waitBuffer.clear();
                    studio.recompute(game);
                } else if (k == SDLK_ESCAPE) {
                    studio.insertingWait = false;
                    studio.waitBuffer.clear();
                }
                continue;
            }

            // ── Studio insert-MOVE mode ───────────────────────────────────────
            if (game.inStudio() && studio.insertingMove && e.type == SDL_KEYDOWN) {
                SDL_Keycode k = e.key.keysym.sym;
                if      (k == SDLK_UP)    studio.insertDir = RelDir::Forward;
                else if (k == SDLK_DOWN)  studio.insertDir = RelDir::Back;
                else if (k == SDLK_LEFT)  studio.insertDir = RelDir::Left;
                else if (k == SDLK_RIGHT) studio.insertDir = RelDir::Right;
                else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
                    size_t selRec = game.selectedRecordingIdx();
                    game.insertMoveRel(selRec, (size_t)studio.instrRow, studio.insertDir);
                    studio.instrRow++;
                    studio.insertingMove = false;
                    studio.recompute(game);
                } else if (k == SDLK_ESCAPE) {
                    studio.insertingMove = false;
                }
                continue;
            }

            // ── Studio panel-focused key nav ──────────────────────────────────
            if (game.inStudio() && studio.panelFocused && e.type == SDL_KEYDOWN) {
                SDL_Keycode k  = e.key.keysym.sym;
                size_t selRec  = game.selectedRecordingIdx();
                int    instrN  = (selRec < game.recordingCount())
                                 ? (int)game.recording(selRec).instructions.size() : 0;
                if (k == SDLK_UP && (SDL_GetModState() & KMOD_SHIFT)) {
                    if (studio.instrRow > 0) {
                        game.reorderInstruction(selRec, studio.instrRow, studio.instrRow - 1);
                        studio.instrRow--;
                        studio.recompute(game);
                    }
                    continue;
                }
                if (k == SDLK_DOWN && (SDL_GetModState() & KMOD_SHIFT)) {
                    if (studio.instrRow + 1 < instrN) {
                        game.reorderInstruction(selRec, studio.instrRow, studio.instrRow + 1);
                        studio.instrRow++;
                        studio.recompute(game);
                    }
                    continue;
                }
                if (k == SDLK_UP)   { studio.instrRow = std::max(0, studio.instrRow - 1); continue; }
                if (k == SDLK_DOWN) { studio.instrRow = std::min(instrN - 1, studio.instrRow + 1); continue; }
                if (k == SDLK_BACKSPACE && instrN > 0) {
                    game.deleteInstruction(selRec, (size_t)studio.instrRow);
                    instrN = (int)game.recording(selRec).instructions.size();
                    if (studio.instrRow >= instrN) studio.instrRow = std::max(0, instrN - 1);
                    studio.recompute(game);
                    continue;
                }
                if (k == SDLK_w) { studio.insertingWait = true; studio.waitBuffer.clear(); continue; }
                if (k == SDLK_m) { studio.insertingMove = true; studio.insertDir = RelDir::Forward; continue; }
            }

            // ── Studio raw keys (always active in studio) ─────────────────────
            if (game.inStudio() && e.type == SDL_KEYDOWN) {
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_LEFTBRACKET) {
                    studio.scrubTick = std::max(0, studio.scrubTick - 1);
                    studio.playing = false;
                } else if (k == SDLK_RIGHTBRACKET) {
                    if (studio.pathLen > 0)
                        studio.scrubTick = std::min(studio.pathLen - 1, studio.scrubTick + 1);
                    studio.playing = false;
                } else if (k == SDLK_SPACE && !studio.panelFocused) {
                    studio.playing = !studio.playing;
                } else if (k == SDLK_l) {
                    studio.loopMode = !studio.loopMode;
                } else if (k == SDLK_EQUALS || k == SDLK_PLUS || k == SDLK_KP_PLUS) {
                    static const float speeds[] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f};
                    for (int si = 0; si < 4; ++si)
                        if (studio.speedMult < speeds[si + 1] - 0.01f)
                            { studio.speedMult = speeds[si + 1]; break; }
                } else if (k == SDLK_MINUS || k == SDLK_KP_MINUS) {
                    static const float speeds[] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f};
                    for (int si = 4; si > 0; --si)
                        if (studio.speedMult > speeds[si - 1] + 0.01f)
                            { studio.speedMult = speeds[si - 1]; break; }
                } else if (k == SDLK_0) {
                    studio.reset();
                } else if (k == SDLK_p) {
                    studio.panelFocused = !studio.panelFocused;
                }
            }

            input.handleEvent(e);
        }

        if (input.pressed(Action::Quit)) {
            game.save(SAVE_PATH);
            input.getMap().save(SETTINGS_PATH);
            quit = true;
        }

        // ── Panel toggles — mutually exclusive ────────────────────────────────
        if (input.pressed(Action::ToggleControls)) {
            ui.is(ActivePanel::Controls) ? ui.close() : ui.open(ActivePanel::Controls);
        }
        if (input.pressed(Action::ToggleRecordings)) {
            ui.is(ActivePanel::Recordings) ? ui.close() : ui.open(ActivePanel::Recordings);
        }
        if (input.pressed(Action::ToggleRebind)) {
            if (ui.is(ActivePanel::Rebind)) {
                input.getMap().save(SETTINGS_PATH);
                ui.close();
            } else {
                ui.open(ActivePanel::Rebind);
            }
        }

        if (ui.is(ActivePanel::Rebind) && !ui.rebindListening) {
            if (input.pressed(Action::PanUp))
                ui.rebindRow = (ui.rebindRow - 1 + INPUT_ACTION_COUNT) % INPUT_ACTION_COUNT;
            if (input.pressed(Action::PanDown))
                ui.rebindRow = (ui.rebindRow + 1) % INPUT_ACTION_COUNT;
            if (input.pressed(Action::Confirm))
                ui.rebindListening = true;
        }

        // Start rename when recordings panel is open and Enter pressed.
        if (ui.is(ActivePanel::Recordings) && !ui.renamingScript &&
            input.pressed(Action::Confirm)) {
            auto list = game.recordingList();
            for (const auto& r : list) {
                if (r.selected) { ui.renameBuffer = r.name; break; }
            }
            SDL_StartTextInput();
            ui.renamingScript = true;
        }

        uint64_t now = SDL_GetTicks64();
        double   dt  = (now - lastTime) / 1000.0;
        lastTime = now;
        accumulator += std::min(dt, 0.1);

        const Input& tickInput = ui.renamingScript ? emptyInput : input;
        while (accumulator >= TICK_DT) {
            game.tick(tickInput, currentTick);
            accumulator -= TICK_DT;
            ++currentTick;
        }

        // ── Studio state update ───────────────────────────────────────────────
        bool nowInStudio = game.inStudio();
        if (nowInStudio && !wasInStudio) {
            // Player just entered the studio — compute paths fresh.
            studio.reset();
            studio.recompute(game);
        }
        wasInStudio = nowInStudio;

        if (nowInStudio && studio.playing && studio.pathLen > 0) {
            studio.scrubAccum += studio.speedMult;
            while (studio.scrubAccum >= 1.0f) {
                studio.scrubTick++;
                studio.scrubAccum -= 1.0f;
                if (studio.scrubTick >= studio.pathLen) {
                    if (studio.loopMode) {
                        studio.scrubTick = 0;
                    } else {
                        studio.scrubTick = studio.pathLen - 1;
                        studio.playing   = false;
                    }
                }
            }
        }

        // Snap camera instantly on grid switch.
        if (game.consumeGridSwitch()) {
            Vec2f snap = toVec(game.playerPos());
            camera.pos     = snap;
            camera.target  = snap;
            camera.z       = static_cast<float>(game.playerPos().z);
            camera.targetZ = camera.z;
            camOffset      = {0.0f, 0.0f};
        }

        // ── Camera update ─────────────────────────────────────────────────────
        float fdt = static_cast<float>(dt);

        if (input.held(Action::PanLeft))  camOffset.x -= PAN_SPEED * fdt;
        if (input.held(Action::PanRight)) camOffset.x += PAN_SPEED * fdt;
        if (input.held(Action::PanUp))    camOffset.y -= PAN_SPEED * fdt;
        if (input.held(Action::PanDown))  camOffset.y += PAN_SPEED * fdt;
        if (!ui.renamingScript && input.pressed(Action::ResetCamera)) camOffset = {0.0f, 0.0f};

        int scroll = input.scroll();
        if (input.held(Action::ZoomModifier) && scroll != 0) {
            camera.zoom *= std::pow(ZOOM_STEP, static_cast<float>(scroll));
            camera.zoom  = std::clamp(camera.zoom, ZOOM_MIN, ZOOM_MAX);
        }

        Vec2f playerRenderPos = lerp(toVec(game.playerPos()),
                                     toVec(game.playerDestination()),
                                     game.playerMoveT());
        float playerRenderZ = lerp(static_cast<float>(game.playerPos().z),
                                   static_cast<float>(game.playerDestination().z),
                                   game.playerMoveT());
        camera.target  = { playerRenderPos.x + camOffset.x,
                           playerRenderPos.y + camOffset.y };
        camera.targetZ = playerRenderZ;

        float factor = 1.0f - std::exp(-CAM_LERP * fdt);
        camera.pos.x += (camera.target.x - camera.pos.x) * factor;
        camera.pos.y += (camera.target.y - camera.pos.y) * factor;
        camera.z     += (camera.targetZ  - camera.z)     * factor;

        // ── Cursor ───────────────────────────────────────────────────────────
        // Hand cursor when hovering over a non-player entity on a valid tile.
        {
            const Entity* hovered = hoveredValid ? game.entityAtTile(hoveredTile) : nullptr;
            renderer.setHandCursor(hovered && hovered->type != EntityType::Player);
        }

        // ── Render ───────────────────────────────────────────────────────────
        renderer.setCamera(camera);
        renderer.setStudioMode(game.inStudio());
        auto [bW, bH] = game.activeGridBounds();
        renderer.setGridBounds(bW, bH);
        renderer.updateEffects(fdt);
        renderer.beginFrame();
        renderer.drawTerrain(game.terrain());

        // Fluid overlay (draw after terrain, before hover and entities).
        renderer.drawFluidOverlay(game.fluidOverlay());

        // Hover highlight (draw before entities so entities appear on top).
        if (hoveredValid) renderer.drawHoverHighlight(hoveredTile);

        for (const Entity* ent : game.drawOrder()) {
            Vec2f renderPos = lerp(toVec(ent->pos), toVec(ent->destination), ent->moveT);
            float renderZ   = lerp(static_cast<float>(ent->pos.z),
                                   static_cast<float>(ent->destination.z), ent->moveT);
            renderer.drawShadow(renderPos, renderZ);
            renderer.drawSprite(renderPos, renderZ, ent->type, ent->id, ent->moveT, ent->lit);
            renderer.drawEntityEffects(renderPos, renderZ, ent->burning, ent->electrified);
            if (ent->type != EntityType::Mushroom   &&
                ent->type != EntityType::Campfire   &&
                ent->type != EntityType::TreeStump  &&
                ent->type != EntityType::Log        &&
                ent->type != EntityType::Battery    &&
                ent->type != EntityType::Lightbulb  &&
                ent->type != EntityType::Tree       &&
                ent->type != EntityType::Rock       &&
                ent->type != EntityType::Chest      &&
                !isGolem(ent->type))
                renderer.drawFacingIndicator(renderPos, renderZ, ent->facing);
        }

        // ── Studio overlays (drawn before particles so they appear under effects) ─
        if (game.inStudio() && studio.paths.size() > 0) {
            // Build path views for all recordings
            std::vector<Renderer::StudioPathView> views;
            for (int i = 0; i < (int)studio.paths.size(); ++i) {
                views.push_back({ &studio.paths[i], agentPaletteColor(i) });
            }
            int selRec = (int)game.selectedRecordingIdx();
            renderer.drawStudioPaths(views, studio.conflicts,
                                     studio.scrubTick, selRec);

            // Ghost entity at scrub position for selected recording
            if (selRec >= 0 && selRec < (int)studio.paths.size()) {
                const auto& selPath = studio.paths[selRec];
                if (studio.scrubTick < (int)selPath.size()) {
                    const PathStep& gs = selPath[studio.scrubTick];
                    renderer.drawGhostEntity(gs.pos, gs.facing, EntityType::Poop);
                }
            }
        }

        renderer.drawParticles();
        renderer.drawDyingEntities();

        // ── Visual events ─────────────────────────────────────────────────────
        for (const VisualEvent& ve : game.drainVisualEvents()) {
            Vec2f vp = ve.pos;
            float vz = ve.z;
            switch (ve.type) {
                case VisualEventType::Dig:
                    renderer.spawnBurst(vp, vz, {139, 90, 43, 255}, 8, 3.0f, 0.4f, 3.0f);
                    break;
                case VisualEventType::CollectMushroom:
                    renderer.spawnBurst(vp, vz, {255, 220, 50, 255}, 10, 4.0f, 0.5f, 3.0f);
                    break;
                case VisualEventType::GoblinHit:
                    renderer.flashEntity(ve.entityID, {255, 80, 80, 255}, 6);
                    renderer.triggerShake(4.0f);
                    break;
                case VisualEventType::GoblinDie:
                    renderer.addDyingEntity(vp, vz, ve.entityType);
                    renderer.spawnBurst(vp, vz, {200, 50, 50, 255}, 12, 5.0f, 0.6f, 4.0f);
                    renderer.triggerShake(6.0f);
                    break;
                case VisualEventType::PlayerLand:
                    renderer.spawnBurst(vp, vz, {139, 90, 43, 200}, 6, 2.0f, 0.3f, 2.0f);
                    break;
                case VisualEventType::PortalEnter:
                    renderer.triggerFade(0.0f, 2.0f);
                    break;
                case VisualEventType::GridSwitch:
                    renderer.triggerShake(8.0f);
                    break;
                case VisualEventType::Summon:
                    renderer.spawnBurst(vp, vz, {100, 200, 255, 255}, 14, 4.0f, 0.5f, 4.0f);
                    renderer.triggerShake(3.0f);
                    break;
            }
        }

        // ── Audio ─────────────────────────────────────────────────────────────
        static constexpr SFX eventToSFX[] = {
            SFX::Step, SFX::Dig, SFX::Plant, SFX::CollectMushroom,
            SFX::RecordStart, SFX::RecordStop, SFX::DeployAgent,
            SFX::PortalCreate, SFX::PortalEnter, SFX::GridSwitch,
            SFX::GoblinHit, SFX::AgentStep,
        };
        for (AudioEvent ev : game.drainAudioEvents())
            audio.playSFX(eventToSFX[static_cast<int>(ev)]);

        // Proximity-based music layers.
        {
            float ts    = Renderer::TILE_SIZE * camera.zoom;
            float halfW = Renderer::VIEWPORT_W / (2.0f * ts);
            float halfH = Renderer::VIEWPORT_H / (2.0f * ts);
            int goblinCount = 0;
            for (const Entity* ent : game.drawOrder()) {
                if (ent->type != EntityType::Goblin) continue;
                Vec2f rp = lerp(toVec(ent->pos), toVec(ent->destination), ent->moveT);
                if (std::abs(rp.x - camera.pos.x) <= halfW + 2 &&
                    std::abs(rp.y - camera.pos.y) <= halfH + 2)
                    ++goblinCount;
            }
            bool inStudio = game.inStudio();
            auto [gW, gH] = game.activeGridBounds();
            bool inRoom   = (gW > 0 && gH > 0 && !inStudio);

            audio.setLayerTarget(MusicLayer::WorldCalm,
                                 (!inStudio && !inRoom) ? 1.0f : 0.0f);
            audio.setLayerTarget(MusicLayer::GoblinTension,
                                 std::min(1.0f, goblinCount / 3.0f));
            audio.setLayerTarget(MusicLayer::Studio,       inStudio ? 1.0f : 0.0f);
            audio.setLayerTarget(MusicLayer::RoomInterior, inRoom   ? 1.0f : 0.0f);
        }
        audio.update(fdt);

        // ── HUD & overlays ────────────────────────────────────────────────────
        renderer.drawHUD(game.playerMana(), game.isRecording());
        renderer.drawActionBar(playerActionName(game.activePlayerAction()));
        renderer.drawSummonPreview(game.playerSummonPreview());

        if (ui.is(ActivePanel::Recordings))
            renderer.drawRecordingsPanel(game.recordingList(), ui.renamingScript, ui.renameBuffer);
        else if (ui.is(ActivePanel::Controls))
            renderer.drawControlsMenu();
        else if (ui.is(ActivePanel::Rebind))
            renderer.drawRebindPanel(input.getMap(), ui.rebindRow, ui.rebindListening);

        // Studio instruction panel and timeline (hidden when any other menu is open).
        if (game.inStudio() && game.recordingCount() > 0 && !ui.isOpen()) {
            size_t selRec = game.selectedRecordingIdx();
            if (selRec < game.recordingCount()) {
                const Recording& selRecording = game.recording(selRec);

                // Compute instruction index at current scrub position.
                int scrubInstrIdx = -1;
                if (selRec < studio.paths.size() &&
                    studio.scrubTick < (int)studio.paths[selRec].size())
                    scrubInstrIdx = studio.paths[selRec][studio.scrubTick].instrIdx;

                // Build conflict-instruction set (path tick → instrIdx).
                std::vector<int> conflictInstrs;
                {
                    std::unordered_set<int> seen;
                    for (int tick : studio.conflicts) {
                        if (selRec < studio.paths.size() &&
                            tick < (int)studio.paths[selRec].size()) {
                            int ii = studio.paths[selRec][tick].instrIdx;
                            if (seen.insert(ii).second)
                                conflictInstrs.push_back(ii);
                        }
                    }
                }

                renderer.drawInstructionPanel(selRecording, studio.instrRow,
                                              scrubInstrIdx,
                                              studio.insertingWait,
                                              studio.insertingMove,
                                              studio.waitBuffer,
                                              studio.insertDir);
                renderer.drawTimeline(selRecording, scrubInstrIdx, conflictInstrs);
            }
        }

        // Entity tooltip (draw after panels so it's always on top).
        if (hoveredValid) {
            const Entity* hovered = game.entityAtTile(hoveredTile);
            if (hovered && hovered->type != EntityType::Player)
                renderer.drawEntityTooltip(entityTypeName(hovered->type), mouseX, mouseY);
        }

        // Context menu (topmost).
        renderer.drawContextMenu(ui.contextMenu);

        renderer.endFrame();
    }

    return 0;
}
