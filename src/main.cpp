#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>

#include "types.hpp"
#include "input.hpp"
#include "game.hpp"
#include "entity.hpp"
#include "renderer.hpp"
#include "audio.hpp"
#include "ui.hpp"

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
    UIState ui;
    Input   emptyInput;   // passed to game.tick() while rename is active

    // ── Mouse state ───────────────────────────────────────────────────────────
    TilePos hoveredTile    = {0, 0, 0};
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
                hoveredTile = Renderer::screenToTile(mouseX, mouseY, camera);

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

                    // Click-to-move toward hovered tile + ripple.
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
                        TilePos tile = Renderer::screenToTile(mx, my, camera);
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
        // Hand cursor when hovering over an entity (other than player).
        {
            const Entity* hovered = game.entityAtTile(hoveredTile);
            bool hand = hovered && hovered->type != EntityType::Player;
            renderer.setHandCursor(hand);
        }

        // ── Render ───────────────────────────────────────────────────────────
        renderer.setCamera(camera);
        renderer.setStudioMode(game.inStudio());
        auto [bW, bH] = game.activeGridBounds();
        renderer.setGridBounds(bW, bH);
        renderer.updateEffects(fdt);
        renderer.beginFrame();
        renderer.drawTerrain(game.terrain());

        // Hover highlight (draw before entities so entities appear on top).
        renderer.drawHoverHighlight(hoveredTile);

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
                case VisualEventType::DeployAgent:
                    renderer.spawnBurst(vp, vz, {180, 180, 255, 255}, 6, 2.0f, 0.3f, 4.0f);
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
            SFX::RecordStart, SFX::RecordStop, SFX::DeployAgent, SFX::DeployAgent,
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
        renderer.drawSummonPreview(game.playerSummonPreview());

        if (ui.is(ActivePanel::Recordings))
            renderer.drawRecordingsPanel(game.recordingList(), ui.renamingScript, ui.renameBuffer);
        else if (ui.is(ActivePanel::Controls))
            renderer.drawControlsMenu();
        else if (ui.is(ActivePanel::Rebind))
            renderer.drawRebindPanel(input.getMap(), ui.rebindRow, ui.rebindListening);

        // Entity tooltip (draw after panels so it's always on top).
        {
            const Entity* hovered = game.entityAtTile(hoveredTile);
            if (hovered && hovered->type != EntityType::Player) {
                renderer.drawEntityTooltip(entityTypeName(hovered->type), mouseX, mouseY);
            }
        }

        // Context menu (topmost).
        renderer.drawContextMenu(ui.contextMenu);

        renderer.endFrame();
    }

    return 0;
}
