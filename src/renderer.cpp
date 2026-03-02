#include "renderer.hpp"
#include "terrain.hpp"
#include "routine.hpp"

#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <cstdio>

// ─── Sprite paths ─────────────────────────────────────────────────────────────

static const std::unordered_map<EntityType, std::string> SPRITE_PATHS = {
    { EntityType::Player,   "assets/sprites/player.png"   },
    { EntityType::Goblin,   "assets/sprites/goblin.png"   },
    { EntityType::Mushroom, "assets/sprites/mushroom.png" },
    { EntityType::Poop,     "assets/sprites/poop.png"     },
};

// ─── SpriteCache ─────────────────────────────────────────────────────────────

SpriteCache::SpriteCache(SDL_Renderer* sdl) : sdl(sdl) {}

SpriteCache::~SpriteCache() {
    for (auto& [type, tex] : textures)
        SDL_DestroyTexture(tex);
}

void SpriteCache::loadAll() {
    for (auto& [type, path] : SPRITE_PATHS) {
        SDL_Texture* tex = IMG_LoadTexture(sdl, path.c_str());
        if (!tex) {
            std::fprintf(stderr, "Warning: could not load sprite '%s': %s\n",
                         path.c_str(), IMG_GetError());
            continue;
        }
        textures[type] = tex;
    }
}

SDL_Texture* SpriteCache::get(EntityType type) const {
    auto it = textures.find(type);
    return (it != textures.end()) ? it->second : nullptr;
}

// ─── Renderer ────────────────────────────────────────────────────────────────

Renderer::Renderer() : sprites(nullptr), font_(nullptr) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        throw std::runtime_error(SDL_GetError());

    if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG))
        throw std::runtime_error(IMG_GetError());

    if (TTF_Init() != 0)
        throw std::runtime_error(TTF_GetError());

    window = SDL_CreateWindow(
        "Grid Game",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        VIEWPORT_W,
        VIEWPORT_H,
        SDL_WINDOW_SHOWN
    );
    if (!window) throw std::runtime_error(SDL_GetError());

    sdl = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!sdl) throw std::runtime_error(SDL_GetError());

    sprites = SpriteCache(sdl);
    sprites.loadAll();

    font_ = TTF_OpenFont("assets/fonts/DejaVuSansMono.ttf", 13);
    if (!font_)
        std::fprintf(stderr, "Warning: could not load font: %s\n", TTF_GetError());
}

Renderer::~Renderer() {
    if (font_) TTF_CloseFont(font_);
    TTF_Quit();
    SDL_DestroyRenderer(sdl);
    SDL_DestroyWindow(window);
    IMG_Quit();
    SDL_Quit();
}

void Renderer::setTitle(const std::string& title) {
    SDL_SetWindowTitle(window, title.c_str());
}

void Renderer::beginFrame() {
    SDL_SetRenderDrawColor(sdl, 0, 0, 0, 255);
    SDL_RenderClear(sdl);
}

void Renderer::drawTerrain(const Terrain& terrain) {
    float tsW = TILE_SIZE * camera_.zoom;
    float tsH = TILE_H    * camera_.zoom;
    float tsZ = Z_STEP    * camera_.zoom;
    int   iW  = static_cast<int>(std::ceil(tsW));
    int   iH  = static_cast<int>(std::ceil(tsH));

    // Minimum face width for east/west cliff strips (at least 1px, scales with zoom).
    int faceW = std::max(1, static_cast<int>(camera_.zoom));

    // Compute visible tile range. Extra headroom at top for elevated terrain.
    float halfW = (VIEWPORT_W / 2.0f) / tsW;
    float halfH = (VIEWPORT_H / 2.0f) / tsH;
    int minX = static_cast<int>(std::floor(camera_.pos.x - halfW)) - 1;
    int maxX = static_cast<int>(std::ceil (camera_.pos.x + halfW)) + 1;
    int minY = static_cast<int>(std::floor(camera_.pos.y - halfH)) - 6;  // headroom for tall terrain
    int maxY = static_cast<int>(std::ceil (camera_.pos.y + halfH)) + 1;

    bool bounded = (gridW_ > 0 && gridH_ > 0);

    // Bounded rooms are flat (entity z never changes there); return 0 for all tiles.
    auto levelOf = [&](int tx, int ty) -> int {
        if (bounded) return 0;
        return terrain.levelAt({tx, ty, 0});
    };

    auto darken = [](SDL_Color c, float f) -> SDL_Color {
        return { static_cast<uint8_t>(c.r * f),
                 static_cast<uint8_t>(c.g * f),
                 static_cast<uint8_t>(c.b * f),
                 c.a };
    };

    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            TilePos p = {x, y};

            bool thisVoid = bounded && (x < 0 || x >= gridW_ || y < 0 || y >= gridH_);

            // ── Void tile ─────────────────────────────────────────────────────
            if (thisVoid) {
                SDL_Rect rect = { toPixelX(x), toPixelY(y), iW, iH };
                SDL_SetRenderDrawColor(sdl, 18, 18, 18, 255);
                SDL_RenderFillRect(sdl, &rect);
                continue;
            }

            int       level = levelOf(x, y);
            float     h     = terrain.heightAt(p);
            TileType  ttype = terrain.typeAt(p);
            SDL_Color color = tileColor(h, p, ttype);
            SDL_Color south = darken(color, 0.55f);
            SDL_Color side  = darken(color, 0.45f);

            int sx = toPixelX(x);
            int sy = toPixelY(y, static_cast<float>(level));

            // ── South cliff face ──────────────────────────────────────────────
            // Fills the gap between this tile's bottom and the tile to the south.
            // Height = diff * Z_STEP (always positive when this tile is higher).
            {
                int levelS = levelOf(x, y + 1);
                int diff   = level - levelS;
                if (diff > 0) {
                    int fh = static_cast<int>(std::round(diff * tsZ));
                    SDL_Rect face = { sx, sy + iH, iW, fh };
                    SDL_SetRenderDrawColor(sdl, south.r, south.g, south.b, south.a);
                    SDL_RenderFillRect(sdl, &face);
                }
            }

            // ── East cliff face ───────────────────────────────────────────────
            // Visible only when height diff is large enough that the tiles don't
            // overlap (diff * Z_STEP > TILE_H). Drawn as a thin strip at the
            // right edge of the elevated tile.
            {
                int levelE = levelOf(x + 1, y);
                int fh     = static_cast<int>(std::round((level - levelE) * tsZ - tsH));
                if (fh > 0) {
                    SDL_Rect face = { sx + iW, sy + iH, faceW, fh };
                    SDL_SetRenderDrawColor(sdl, side.r, side.g, side.b, side.a);
                    SDL_RenderFillRect(sdl, &face);
                }
            }

            // ── West cliff face ───────────────────────────────────────────────
            {
                int levelW = levelOf(x - 1, y);
                int fh     = static_cast<int>(std::round((level - levelW) * tsZ - tsH));
                if (fh > 0) {
                    SDL_Rect face = { sx - faceW, sy + iH, faceW, fh };
                    SDL_SetRenderDrawColor(sdl, side.r, side.g, side.b, side.a);
                    SDL_RenderFillRect(sdl, &face);
                }
            }

            // ── Tile top ──────────────────────────────────────────────────────
            SDL_Rect rect = { sx, sy, iW, iH };
            SDL_SetRenderDrawColor(sdl, color.r, color.g, color.b, color.a);
            SDL_RenderFillRect(sdl, &rect);

            // ── Cliff edge lines ──────────────────────────────────────────────
            // Dark line drawn on the tile surface along any edge where the
            // terrain drops away — gives the cliff a crisp defined rim.
            int       lt   = std::max(1, static_cast<int>(camera_.zoom));
            SDL_Color edge = darken(color, 0.4f);
            SDL_SetRenderDrawColor(sdl, edge.r, edge.g, edge.b, edge.a);

            if (levelOf(x, y - 1) < level) {  // north edge
                SDL_Rect r = { sx,           sy, iW, lt };
                SDL_RenderFillRect(sdl, &r);
            }
            if (levelOf(x + 1, y) < level) {  // east edge
                SDL_Rect r = { sx + iW - lt, sy, lt, iH };
                SDL_RenderFillRect(sdl, &r);
            }
            if (levelOf(x - 1, y) < level) {  // west edge
                SDL_Rect r = { sx,           sy, lt, iH };
                SDL_RenderFillRect(sdl, &r);
            }
        }
    }
}

void Renderer::drawShadow(Vec2f renderPos, float renderZ) {
    float ts  = TILE_SIZE * camera_.zoom;
    float tsH = TILE_H    * camera_.zoom;
    float cx  = toPixelX(renderPos.x) + ts  * 0.5f;
    float cy  = toPixelY(renderPos.y, renderZ) + tsH * 0.5f;
    float rx  = ts  * 0.35f;
    float ry  = tsH * 0.22f;

    constexpr int N = 24;
    SDL_Vertex verts[N + 1];
    int        idx[N * 3];

    SDL_Color col = { 0, 0, 0, 90 };
    verts[0] = { { cx, cy }, col, { 0, 0 } };
    for (int i = 0; i < N; ++i) {
        float angle = 2.0f * static_cast<float>(M_PI) * i / N;
        verts[i + 1] = {
            { cx + rx * std::cos(angle), cy + ry * std::sin(angle) },
            col, { 0, 0 }
        };
        int next = (i + 1) % N;
        idx[i*3 + 0] = 0;
        idx[i*3 + 1] = i + 1;
        idx[i*3 + 2] = next + 1;
    }

    SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(sdl, nullptr, verts, N + 1, idx, N * 3);
}

void Renderer::drawSprite(Vec2f renderPos, float renderZ, EntityType type) {
    SDL_Texture* tex = sprites.get(type);
    if (!tex) return;

    int iTs = static_cast<int>(std::ceil(TILE_SIZE * camera_.zoom));
    int iH  = static_cast<int>(std::ceil(TILE_H    * camera_.zoom));
    SDL_Rect dst = {
        toPixelX(renderPos.x),
        toPixelY(renderPos.y, renderZ) + iH / 2 - iTs,
        iTs,
        iTs
    };
    SDL_RenderCopy(sdl, tex, nullptr, &dst);
}

void Renderer::endFrame() {
    SDL_RenderPresent(sdl);
}

SDL_Color Renderer::tileColor(float height, TilePos pos, TileType type) const {
    if (type == TileType::BareEarth)
        return { 139, 90, 43, 255 };

    if (type == TileType::Portal)
        return { 160, 60, 220, 255 };

    if (studioMode_) {
        // Muted blue-grey studio floor.
        int v = static_cast<int>(height * 18.0f);
        int r = std::clamp(90  + v, 72,  115);
        int g = std::clamp(105 + v, 88,  128);
        int b = std::clamp(160 + v, 140, 185);
        if ((pos.x + pos.y) % 2 == 0) { r += 4; g += 4; b += 5; }
        return { static_cast<uint8_t>(r), static_cast<uint8_t>(g),
                 static_cast<uint8_t>(b), 255 };
    }

    // World: flat green with a subtle checkerboard to show tile boundaries.
    int g = ((pos.x + pos.y) % 2 == 0) ? 134 : 120;
    return { 0, static_cast<uint8_t>(g), 0, 255 };
}

int Renderer::toPixelX(float tileX) const {
    float ts = TILE_SIZE * camera_.zoom;
    return static_cast<int>(std::round(VIEWPORT_W / 2.0f + (tileX - camera_.pos.x) * ts));
}

int Renderer::toPixelY(float tileY, float tileZ) const {
    float tsH  = TILE_H  * camera_.zoom;
    float step = Z_STEP  * camera_.zoom;
    return static_cast<int>(std::round(
        VIEWPORT_H / 2.0f
        + (tileY - camera_.pos.y) * tsH
        - (tileZ - camera_.z)     * step
    ));
}

// ─── HUD & Text ──────────────────────────────────────────────────────────────

void Renderer::drawFacingIndicator(Vec2f renderPos, float renderZ, Direction facing) {
    float ts  = TILE_SIZE * camera_.zoom;
    float tsH = TILE_H    * camera_.zoom;
    float cx  = toPixelX(renderPos.x) + ts  * 0.5f;
    float cy  = toPixelY(renderPos.y, renderZ) + tsH * 0.5f - ts * 0.5f;

    // Unit vector for each direction
    float dx = 0.0f, dy = 0.0f;
    switch (facing) {
        case Direction::N:  dx =  0.000f; dy = -1.000f; break;
        case Direction::NE: dx =  0.707f; dy = -0.707f; break;
        case Direction::E:  dx =  1.000f; dy =  0.000f; break;
        case Direction::SE: dx =  0.707f; dy =  0.707f; break;
        case Direction::S:  dx =  0.000f; dy =  1.000f; break;
        case Direction::SW: dx = -0.707f; dy =  0.707f; break;
        case Direction::W:  dx = -1.000f; dy =  0.000f; break;
        case Direction::NW: dx = -0.707f; dy = -0.707f; break;
    }
    float px = -dy, py = dx;   // perpendicular

    float tipDist  = ts * 0.38f;
    float baseDist = ts * 0.20f;
    float halfW    = ts * 0.13f;

    SDL_FPoint tip   = { cx + dx * tipDist,  cy + dy * tipDist  };
    SDL_FPoint baseL = { cx + dx * baseDist + px * halfW,
                         cy + dy * baseDist + py * halfW };
    SDL_FPoint baseR = { cx + dx * baseDist - px * halfW,
                         cy + dy * baseDist - py * halfW };

    SDL_Color col = {255, 255, 255, 210};
    SDL_Vertex verts[3] = {
        { tip,   col, {0, 0} },
        { baseL, col, {0, 0} },
        { baseR, col, {0, 0} },
    };
    SDL_RenderGeometry(sdl, nullptr, verts, 3, nullptr, 0);
}

void Renderer::drawHUD(int mana, bool isRecording) {
    constexpr int PAD = 8;
    constexpr int H   = 30;
    constexpr int X   = 10;
    constexpr int Y   = 10;

    // Build strings
    std::string manaStr = "\xe2\x99\xa6 " + std::to_string(mana); // ♦
    std::string recStr  = " \xe2\x97\x8f REC";                    // ●

    // Measure text widths to size the panel
    int manaW = 0, manaH = 0, recW = 0, recH = 0;
    if (font_) {
        TTF_SizeUTF8(font_, manaStr.c_str(), &manaW, &manaH);
        if (isRecording) TTF_SizeUTF8(font_, recStr.c_str(), &recW, &recH);
    }
    int panelW = PAD + manaW + recW + PAD;

    // Background
    SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl, 10, 10, 10, 195);
    SDL_Rect panel = {X, Y, panelW, H};
    SDL_RenderFillRect(sdl, &panel);
    SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_NONE);

    SDL_SetRenderDrawColor(sdl, 90, 90, 90, 255);
    SDL_RenderDrawRect(sdl, &panel);

    // Mana — gold
    int ty = Y + (H - manaH) / 2;
    drawText(manaStr, X + PAD, ty, {220, 185, 50, 255});

    // Recording indicator — red
    if (isRecording)
        drawText(recStr, X + PAD + manaW, ty, {210, 60, 60, 255});
}

void Renderer::drawText(const std::string& text, int x, int y, SDL_Color col) const {
    if (!font_ || text.empty()) return;
    SDL_Surface* surf = TTF_RenderUTF8_Blended(font_, text.c_str(), col);
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(sdl, surf);
    SDL_FreeSurface(surf);
    if (!tex) return;
    int w, h;
    SDL_QueryTexture(tex, nullptr, nullptr, &w, &h);
    SDL_Rect dst = {x, y, w, h};
    SDL_RenderCopy(sdl, tex, nullptr, &dst);
    SDL_DestroyTexture(tex);
}

void Renderer::drawRecordingsPanel(const std::vector<RecordingInfo>& list,
                                   bool renaming, const std::string& renameBuffer) {
    constexpr int PAD         = 10;
    constexpr int W           = 310;
    constexpr int ROW         = 22;
    constexpr int MAX_VISIBLE = 8;
    constexpr int X           = VIEWPORT_W - W - 10;
    constexpr int Y           = 10;

    int numVisible = std::min((int)list.size(), MAX_VISIBLE);
    int extraRows  = list.empty() ? 1 : (list.size() > MAX_VISIBLE ? 1 : 0);
    int H = PAD + 20 + 10 + (numVisible + extraRows) * ROW + 10 + 20 + PAD;

    // Background + border
    SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl, 10, 10, 10, 195);
    SDL_Rect panel = {X, Y, W, H};
    SDL_RenderFillRect(sdl, &panel);
    SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(sdl, 90, 90, 90, 255);
    SDL_RenderDrawRect(sdl, &panel);

    SDL_Color titleCol  = {220, 210,  80, 255};
    SDL_Color selCol    = {255, 255, 255, 255};
    SDL_Color normCol   = {150, 150, 150, 255};
    SDL_Color dimCol    = { 80,  80,  80, 255};
    SDL_Color accentCol = { 90, 180,  90, 255};
    SDL_Color hintCol   = {100, 140, 100, 255};

    int ty = Y + PAD;

    // Title
    drawText("R E C O R D I N G S", X + 58, ty, titleCol);
    ty += 20;

    // Separator
    SDL_SetRenderDrawColor(sdl, 70, 70, 70, 255);
    SDL_RenderDrawLine(sdl, X + 5, ty + 4, X + W - 5, ty + 4);
    ty += 10;

    // Rows
    if (list.empty()) {
        drawText("  no recordings yet", X + PAD, ty, dimCol);
        ty += ROW;
    } else {
        for (int i = 0; i < numVisible; ++i) {
            const auto& rec = list[i];

            if (rec.selected) {
                // Row highlight
                SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(sdl, 40, 90, 40, 110);
                SDL_Rect rowRect = {X + 2, ty - 1, W - 4, ROW};
                SDL_RenderFillRect(sdl, &rowRect);
                SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_NONE);

                // Arrow indicator
                drawText("\xe2\x96\xb6", X + PAD, ty, accentCol);   // ▶

                if (renaming) {
                    // Text input field
                    bool blink = (SDL_GetTicks64() / 500) % 2;
                    std::string field = renameBuffer + (blink ? "|" : " ");
                    drawText(field, X + PAD + 16, ty, selCol);
                } else {
                    drawText(rec.name, X + PAD + 16, ty, selCol);
                }
            } else {
                drawText("  " + rec.name, X + PAD, ty, normCol);
            }

            // Step count (right side)
            std::string steps = std::to_string(rec.steps);
            drawText(steps, X + W - PAD - 28, ty,
                     rec.selected ? accentCol : dimCol);

            ty += ROW;
        }

        if ((int)list.size() > MAX_VISIBLE) {
            std::string more = "  \xe2\x80\xa6 " +                               // …
                std::to_string((int)list.size() - MAX_VISIBLE) + " more";
            drawText(more, X + PAD, ty, dimCol);
            ty += ROW;
        }
    }

    // Footer separator
    SDL_SetRenderDrawColor(sdl, 70, 70, 70, 255);
    SDL_RenderDrawLine(sdl, X + 5, ty + 4, X + W - 5, ty + 4);
    ty += 10;

    // Footer hints
    if (renaming)
        drawText("\xe2\x86\xb5 confirm   Esc cancel", X + PAD, ty, hintCol);  // ↵
    else
        drawText("Q cycle   E deploy   \xe2\x86\xb5 rename", X + PAD, ty, hintCol);
}

void Renderer::drawControlsMenu() {
    constexpr int PAD  = 12;
    constexpr int LINE = 18;
    constexpr int W    = 308;
    constexpr int H    = 358;
    constexpr int X    = VIEWPORT_W - W - 10;
    constexpr int Y    = 10;

    // Semi-transparent background
    SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl, 10, 10, 10, 195);
    SDL_Rect panel = {X, Y, W, H};
    SDL_RenderFillRect(sdl, &panel);
    SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_NONE);

    // Border
    SDL_SetRenderDrawColor(sdl, 90, 90, 90, 255);
    SDL_RenderDrawRect(sdl, &panel);

    SDL_Color title  = {220, 210,  80, 255};
    SDL_Color key    = {200, 200, 200, 255};
    SDL_Color action = {140, 200, 140, 255};
    SDL_Color dim    = { 80,  80,  80, 255};

    int tx = X + PAD;
    int ty = Y + PAD;

    // Title — centred
    drawText("C O N T R O L S", X + 62, ty, title);
    ty += LINE + 4;

    auto sep = [&]() {
        SDL_SetRenderDrawColor(sdl, dim.r, dim.g, dim.b, dim.a);
        SDL_RenderDrawLine(sdl, X + 5, ty + 4, X + W - 5, ty + 4);
        ty += 12;
    };

    auto row = [&](const char* k, const char* a) {
        drawText(k, tx,        ty, key);
        drawText(a, tx + 148,  ty, action);
        ty += LINE;
    };

    sep();
    row("WASD",          "Move");
    row("Shift + WASD",  "Strafe");
    row("F",             "Dig ahead");
    row("C",             "Plant mushroom");
    row("R",             "Toggle recording");
    row("Q",             "Cycle recording");
    row("E",             "Deploy agent");
    row("O",             "Create room portal");
    row("Tab",           "Toggle studio");
    sep();
    row("Arrows",        "Pan camera");
    row("Backspace",     "Re-centre");
    row("Ctrl + Scroll", "Zoom");
    sep();
    row("H",             "Toggle this menu");
    row("I",             "Recordings panel");
    row("Esc",           "Quit");
}
