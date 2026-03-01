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
    float ts  = TILE_SIZE * camera_.zoom;
    int   iTs = static_cast<int>(std::ceil(ts));

    // Compute which tile columns/rows are visible.
    float halfW = (VIEWPORT_W / 2.0f) / ts;
    float halfH = (VIEWPORT_H / 2.0f) / ts;
    int minX = static_cast<int>(std::floor(camera_.pos.x - halfW)) - 1;
    int maxX = static_cast<int>(std::ceil (camera_.pos.x + halfW)) + 1;
    int minY = static_cast<int>(std::floor(camera_.pos.y - halfH)) - 1;
    int maxY = static_cast<int>(std::ceil (camera_.pos.y + halfH)) + 1;

    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            TilePos   p     = {x, y};
            float     h     = terrain.heightAt(p);
            TileType  ttype = terrain.typeAt(p);
            SDL_Color color = tileColor(h, p, ttype);

            SDL_SetRenderDrawColor(sdl, color.r, color.g, color.b, color.a);
            SDL_Rect rect = { toPixelX(x), toPixelY(y), iTs, iTs };
            SDL_RenderFillRect(sdl, &rect);
        }
    }
}

void Renderer::drawSprite(Vec2f renderPos, EntityType type) {
    SDL_Texture* tex = sprites.get(type);
    if (!tex) return;

    int iTs = static_cast<int>(std::ceil(TILE_SIZE * camera_.zoom));
    SDL_Rect dst = {
        toPixelX(renderPos.x),
        toPixelY(renderPos.y),
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

    // Map Perlin height [-1,1] → green channel [72, 184]
    auto g = static_cast<int>(128.0f + height * 56.0f);
    g = std::clamp(g, 64, 220);

    // Checkerboard: slightly lighter on even tiles
    if ((pos.x + pos.y) % 2 == 0)
        g = std::min(g + 6, 255);

    return { 0, static_cast<uint8_t>(g), 0, 255 };
}

int Renderer::toPixelX(float tileX) const {
    float ts = TILE_SIZE * camera_.zoom;
    return static_cast<int>(std::round(VIEWPORT_W / 2.0f + (tileX - camera_.pos.x) * ts));
}

int Renderer::toPixelY(float tileY) const {
    float ts = TILE_SIZE * camera_.zoom;
    return static_cast<int>(std::round(VIEWPORT_H / 2.0f + (tileY - camera_.pos.y) * ts));
}

// ─── HUD & Text ──────────────────────────────────────────────────────────────

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
    constexpr int H    = 322;
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
