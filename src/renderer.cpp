#include "renderer.hpp"
#include "terrain.hpp"
#include "routine.hpp"

#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_set>
#include <string>

// ─── Sprite paths ─────────────────────────────────────────────────────────────

static const std::unordered_map<EntityType, std::string> SPRITE_PATHS = {
    { EntityType::Player,      "assets/sprites/player.png"     },
    { EntityType::Goblin,      "assets/sprites/goblin.png"     },
    { EntityType::Mushroom,    "assets/sprites/mushroom.png"   },
    { EntityType::Poop,        "assets/sprites/poop.png"       },
    { EntityType::Campfire,    "assets/sprites/campfire.png"   },
    { EntityType::TreeStump,   "assets/sprites/tree_stump.png" },
    { EntityType::Log,         "assets/sprites/logs.png"       },
    { EntityType::Battery,     "assets/sprites/battery.png"    },
    { EntityType::Lightbulb,   "assets/sprites/lightbulb.png"  },
    // All golem types share a placeholder sprite until per-type art lands
    { EntityType::MudGolem,    "assets/sprites/golem.png"      },
    { EntityType::StoneGolem,  "assets/sprites/golem.png"      },
    { EntityType::ClayGolem,   "assets/sprites/golem.png"      },
    { EntityType::WaterGolem,  "assets/sprites/golem.png"      },
    { EntityType::BushGolem,   "assets/sprites/golem.png"      },
    { EntityType::WoodGolem,   "assets/sprites/golem.png"      },
    { EntityType::IronGolem,   "assets/sprites/golem.png"      },
    { EntityType::CopperGolem, "assets/sprites/golem.png"      },
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

Renderer::Renderer() : sprites(nullptr), font_(nullptr),
                        cursorArrow_(nullptr), cursorHand_(nullptr) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0)
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

    cursorArrow_ = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    cursorHand_  = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
}

Renderer::~Renderer() {
    for (auto& [key, tex] : textCache_) SDL_DestroyTexture(tex);
    textCache_.clear();
    if (cursorArrow_) SDL_FreeCursor(cursorArrow_);
    if (cursorHand_)  SDL_FreeCursor(cursorHand_);
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
    ++rendererTick_;
    SDL_SetRenderDrawColor(sdl, 0, 0, 0, 255);
    SDL_RenderClear(sdl);
}

void Renderer::drawTerrain(const Terrain& terrain) {
    float tsW = TILE_SIZE * camera_.zoom;
    float tsH = TILE_H    * camera_.zoom;
    int   iH  = static_cast<int>(std::ceil(tsH));

    // Visible tile range. Extra vertical headroom for elevated terrain.
    float halfW = (VIEWPORT_W / 2.0f) / tsW;
    float halfH = (VIEWPORT_H / 2.0f) / tsH;
    int minX = static_cast<int>(std::floor(camera_.pos.x - halfW)) - 2;
    int maxX = static_cast<int>(std::ceil (camera_.pos.x + halfW)) + 2;
    int minY = static_cast<int>(std::floor(camera_.pos.y - halfH)) - 6;
    int maxY = static_cast<int>(std::ceil (camera_.pos.y + halfH)) + 1;

    bool bounded = (gridW_ > 0 && gridH_ > 0);

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
                int sx = toPixelX(x, 0.0f);
                int sw = toPixelX(x + 1, 0.0f) - sx;
                SDL_Rect rect = { sx, toPixelY(y, 0.0f), sw, iH };
                SDL_SetRenderDrawColor(sdl, 18, 18, 18, 255);
                SDL_RenderFillRect(sdl, &rect);
                continue;
            }

            int       level = levelOf(x, y);
            float     lf    = static_cast<float>(level);
            float     h     = terrain.heightAt(p);
            TileType  ttype = terrain.typeAt(p);
            SDL_Color color = tileColor(h, p, ttype);
            SDL_Color south = darken(color, 0.55f);
            SDL_Color side  = darken(color, 0.45f);

            int sx = toPixelX(x,     lf);
            int sy = toPixelY(y,     lf);
            int sw = toPixelX(x + 1, lf) - sx;  // apparent width from perspective scaling

            // ── South cliff face ──────────────────────────────────────────────
            // Gap between this tile's bottom edge and the top of the tile to the
            // south — computed directly from the projection, no manual diff*step.
            {
                int levelS = levelOf(x, y + 1);
                int sy_s   = toPixelY(y + 1, static_cast<float>(levelS));
                int fh     = sy_s - (sy + iH);
                if (fh > 0) {
                    SDL_Rect face = { sx, sy + iH, sw, fh };
                    SDL_SetRenderDrawColor(sdl, south.r, south.g, south.b, south.a);
                    SDL_RenderFillRect(sdl, &face);
                }
            }

            // ── East cliff face ───────────────────────────────────────────────
            // With perspective scaling, elevated tiles appear wider (larger f).
            // Tiles left of centre: high tile extends left → gap opens on right.
            // Tiles right of centre: high tile extends right → west face opens instead.
            {
                int levelE = levelOf(x + 1, y);
                int ex     = sx + sw;                              // right edge of this tile
                int ex_e   = toPixelX(x + 1, static_cast<float>(levelE));  // left edge of eastern tile
                int fw     = ex_e - ex;
                if (fw > 0) {
                    SDL_Rect face = { ex, sy, fw, iH };
                    SDL_SetRenderDrawColor(sdl, side.r, side.g, side.b, side.a);
                    SDL_RenderFillRect(sdl, &face);
                }
            }

            // ── West cliff face ───────────────────────────────────────────────
            {
                int levelW = levelOf(x - 1, y);
                int wx_w   = toPixelX(x, static_cast<float>(levelW));  // right edge of western tile
                int fw     = sx - wx_w;
                if (fw > 0) {
                    SDL_Rect face = { wx_w, sy, fw, iH };
                    SDL_SetRenderDrawColor(sdl, side.r, side.g, side.b, side.a);
                    SDL_RenderFillRect(sdl, &face);
                }
            }

            // ── Tile top ──────────────────────────────────────────────────────
            SDL_Rect rect = { sx, sy, sw, iH };
            SDL_SetRenderDrawColor(sdl, color.r, color.g, color.b, color.a);
            SDL_RenderFillRect(sdl, &rect);

            // ── North edge line ───────────────────────────────────────────────
            if (levelOf(x, y - 1) < level) {
                int       lt   = std::max(1, static_cast<int>(camera_.zoom));
                SDL_Color edge = darken(color, 0.4f);
                SDL_SetRenderDrawColor(sdl, edge.r, edge.g, edge.b, edge.a);
                SDL_Rect r = { sx, sy, sw, lt };
                SDL_RenderFillRect(sdl, &r);
            }

            // ── Procedural tile detail ────────────────────────────────────────
            if (ttype == TileType::Grass) {
                // ~12 % of grass tiles get a small flower or stone dot.
                uint32_t h = static_cast<uint32_t>(x) * 2654435761u
                           ^ static_cast<uint32_t>(y) * 2246822519u;
                if ((h & 0xFF) < 30) {
                    int dx = static_cast<int>((h >> 8)  & 0x1F) * sw / 32;
                    int dy = static_cast<int>((h >> 13) & 0x0F) * iH / 16;
                    bool isFlower = (h >> 17) & 1u;
                    SDL_Color det;
                    if (isFlower) {
                        uint32_t hue = (h >> 18) & 0x3u;
                        det = hue == 0 ? SDL_Color{255, 220,  50, 255}
                            : hue == 1 ? SDL_Color{255, 120, 120, 255}
                            :            SDL_Color{255, 255, 255, 255};
                    } else {
                        det = { 100, 90, 80, 255 };   // stone
                    }
                    SDL_Rect dot = { sx + dx, sy + dy, 2, 2 };
                    SDL_SetRenderDrawColor(sdl, det.r, det.g, det.b, det.a);
                    SDL_RenderFillRect(sdl, &dot);
                }
            } else if (ttype == TileType::BareEarth) {
                // ~8 % of bare-earth tiles get a short crack line.
                uint32_t h = static_cast<uint32_t>(x) * 3456789013u
                           ^ static_cast<uint32_t>(y) * 1234567891u;
                if ((h & 0xFF) < 20) {
                    int dx  = static_cast<int>((h >> 8)  & 0x1F) * sw / 32;
                    int dy  = static_cast<int>((h >> 13) & 0x0F) * iH / 16;
                    int dx2 = dx + 3 + static_cast<int>((h >> 18) & 0x3u);
                    SDL_SetRenderDrawColor(sdl, 100, 65, 30, 255);
                    SDL_RenderDrawLine(sdl, sx + dx, sy + dy, sx + dx2, sy + dy + 1);
                }
            }
        }
    }
}

void Renderer::drawShadow(Vec2f renderPos, float renderZ) {
    float ts  = TILE_SIZE * camera_.zoom;
    float tsH = TILE_H    * camera_.zoom;
    float cx  = toPixelX(renderPos.x, renderZ) + ts  * 0.5f;
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

void Renderer::drawSprite(Vec2f renderPos, float renderZ, EntityType type,
                           EntityID eid, float moveT, bool lit) {
    SDL_Texture* tex = sprites.get(type);
    if (!tex) return;

    // Hit flash: tint the sprite with the flash colour.
    auto flashIt = entityFlashes_.find(eid);
    if (flashIt != entityFlashes_.end()) {
        const RGBA& fc = flashIt->second.color;
        SDL_SetTextureColorMod(tex, fc.r, fc.g, fc.b);
    } else if (type == EntityType::Lightbulb && !lit) {
        SDL_SetTextureColorMod(tex, 80, 80, 80);
    } else {
        SDL_SetTextureColorMod(tex, 255, 255, 255);
    }

    // Walk bob: a small vertical hop at the mid-point of each step.
    float bob = std::sin(moveT * static_cast<float>(M_PI)) * 2.0f * camera_.zoom;

    int iTs = static_cast<int>(std::ceil(TILE_SIZE * camera_.zoom));
    int iH  = static_cast<int>(std::ceil(TILE_H    * camera_.zoom));
    SDL_Rect dst = {
        toPixelX(renderPos.x, renderZ),
        toPixelY(renderPos.y, renderZ) + iH / 2 - iTs - static_cast<int>(bob),
        iTs,
        iTs
    };
    SDL_RenderCopy(sdl, tex, nullptr, &dst);
}

void Renderer::endFrame() {
    // Fade overlay (portal entry, grid switch).
    if (fadeAlpha_ > 0.0f) {
        SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(sdl, 0, 0, 0,
            static_cast<uint8_t>(std::clamp(fadeAlpha_, 0.0f, 1.0f) * 255.0f));
        SDL_Rect full = { 0, 0, VIEWPORT_W, VIEWPORT_H };
        SDL_RenderFillRect(sdl, &full);
        SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_NONE);
    }
    SDL_RenderPresent(sdl);
}

SDL_Color Renderer::tileColor(float height, TilePos pos, TileType type) const {
    // Slow day/night cycle: 300-second period, ±15% brightness.
    float dayFactor = 0.85f + 0.15f * std::sin(dayNightT_ / 150.0f * static_cast<float>(M_PI));

    if (type == TileType::BareEarth)
        return { static_cast<uint8_t>(139 * dayFactor),
                 static_cast<uint8_t>(90  * dayFactor),
                 static_cast<uint8_t>(43  * dayFactor), 255 };

    if (type == TileType::Portal) {
        // Shimmering purple pulse.
        float pulse = 0.7f + 0.3f * std::sin(dayNightT_ * 3.0f
                                              + pos.x * 0.7f + pos.y * 0.5f);
        return { static_cast<uint8_t>(160 * pulse),
                 static_cast<uint8_t>(60  * pulse),
                 static_cast<uint8_t>(220 * pulse), 255 };
    }

    if (type == TileType::Fire) {
        // Flickery orange-red: spatial hash + time-based flicker.
        int   v       = (pos.x * 13 + pos.y * 7) & 0x1F;
        float flicker = 0.8f + 0.2f * std::sin(dayNightT_ * 20.0f
                                                + pos.x + pos.y * 1.3f);
        return { static_cast<uint8_t>((220 + v / 4) * flicker),
                 static_cast<uint8_t>((80  + v / 2) * flicker),
                 0, 255 };
    }

    if (type == TileType::Puddle) {
        // Gentle ripple.
        float ripple = 0.85f + 0.15f * std::sin(dayNightT_ * 4.0f
                                                 + pos.x * 0.9f + pos.y * 1.1f);
        return { static_cast<uint8_t>(60  * ripple),
                 static_cast<uint8_t>(100 * ripple),
                 static_cast<uint8_t>(200 * ripple), 255 };
    }

    // Summoning medium tiles — each has a distinct flat colour with a day/night tint.
    if (type == TileType::Mud)
        return { static_cast<uint8_t>(101 * dayFactor),
                 static_cast<uint8_t>( 67 * dayFactor),
                 static_cast<uint8_t>( 33 * dayFactor), 255 };
    if (type == TileType::Stone)
        return { static_cast<uint8_t>(120 * dayFactor),
                 static_cast<uint8_t>(120 * dayFactor),
                 static_cast<uint8_t>(120 * dayFactor), 255 };
    if (type == TileType::Clay)
        return { static_cast<uint8_t>(180 * dayFactor),
                 static_cast<uint8_t>( 90 * dayFactor),
                 static_cast<uint8_t>( 60 * dayFactor), 255 };
    if (type == TileType::Bush)
        return { static_cast<uint8_t>( 30 * dayFactor),
                 static_cast<uint8_t>(100 * dayFactor),
                 static_cast<uint8_t>( 30 * dayFactor), 255 };
    if (type == TileType::Wood)
        return { static_cast<uint8_t>(130 * dayFactor),
                 static_cast<uint8_t>( 80 * dayFactor),
                 static_cast<uint8_t>( 30 * dayFactor), 255 };
    if (type == TileType::Iron)
        return { static_cast<uint8_t>( 80 * dayFactor),
                 static_cast<uint8_t>( 85 * dayFactor),
                 static_cast<uint8_t>( 95 * dayFactor), 255 };
    if (type == TileType::Copper)
        return { static_cast<uint8_t>(184 * dayFactor),
                 static_cast<uint8_t>(115 * dayFactor),
                 static_cast<uint8_t>( 51 * dayFactor), 255 };

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

    // World: flat green with a subtle checkerboard + day/night tint.
    int g = ((pos.x + pos.y) % 2 == 0) ? 134 : 120;
    return { 0, static_cast<uint8_t>(g * dayFactor), 0, 255 };
}

int Renderer::toPixelX(float tileX, float tileZ) const {
    float ts = TILE_SIZE * camera_.zoom;
    float f  = 1.0f + (tileZ - camera_.z) / static_cast<float>(Z_PERSP);
    return static_cast<int>(std::round(
        VIEWPORT_W / 2.0f + (tileX - camera_.pos.x) * ts * f + shakeOffX_
    ));
}

int Renderer::toPixelY(float tileY, float tileZ) const {
    float tsH  = TILE_H * camera_.zoom;
    float step = Z_STEP * camera_.zoom;
    float f    = 1.0f + (tileZ - camera_.z) / static_cast<float>(Z_PERSP);
    return static_cast<int>(std::round(
        VIEWPORT_H / 2.0f
        + (tileY - camera_.pos.y) * tsH
        - (tileZ - camera_.z)     * step * f
        + shakeOffY_
    ));
}

// ─── Visual effects ──────────────────────────────────────────────────────────

void Renderer::updateEffects(float fdt) {
    lastFdt_   = fdt;
    dayNightT_ += fdt;

    // Advance particles; remove dead ones.
    particles_.erase(
        std::remove_if(particles_.begin(), particles_.end(),
                       [fdt](Particle& p) { return !p.tick(fdt); }),
        particles_.end());

    // Screen shake: exponential decay + sinusoidal offset direction.
    shakeAmt_ = shakeDecay(shakeAmt_, fdt);
    shakeOffX_ = shakeAmt_ * std::cos(dayNightT_ * 53.0f);
    shakeOffY_ = shakeAmt_ * std::sin(dayNightT_ * 47.0f);

    // Fade: advance toward target and auto-reverse at full black.
    if (fadeDelta_ != 0.0f) {
        fadeAlpha_ += fadeDelta_ * fdt;
        if (fadeDelta_ > 0.0f && fadeAlpha_ >= 1.0f) {
            fadeAlpha_ = 1.0f;
            fadeDelta_ = -fadeDelta_;   // reverse back to transparent
        } else if (fadeDelta_ < 0.0f && fadeAlpha_ <= 0.0f) {
            fadeAlpha_ = 0.0f;
            fadeDelta_ = 0.0f;
        }
    }

    // Flash timers: decrement and erase when expired.
    std::vector<EntityID> expired;
    for (auto& [eid, flash] : entityFlashes_)
        if (--flash.ticksLeft <= 0) expired.push_back(eid);
    for (EntityID eid : expired) entityFlashes_.erase(eid);

    // Dying entities: fade out over their lifetime.
    dying_.erase(
        std::remove_if(dying_.begin(), dying_.end(),
                       [fdt](DyingEntity& d) { d.life -= fdt; return d.life <= 0.0f; }),
        dying_.end());

    // Ambient dust motes (world only, capped at 150 particles).
    if (!studioMode_ && particles_.size() < 150) {
        dustAccum_ += fdt;
        while (dustAccum_ > 0.15f) {
            dustAccum_ -= 0.15f;
            if (std::rand() % 5 == 0) {
                Particle p;
                p.pos = {
                    camera_.pos.x + ((std::rand() % 220) / 10.0f - 11.0f),
                    camera_.pos.y + ((std::rand() % 220) / 10.0f - 11.0f)
                };
                p.vel = {
                    ((std::rand() % 60) - 30) / 200.0f,
                    ((std::rand() % 60) - 50) / 200.0f   // slight upward drift
                };
                p.z       = camera_.z;
                p.life    = 2.0f + (std::rand() % 200) / 100.0f;
                p.maxLife = p.life;
                p.size    = 1.0f + (std::rand() % 2);
                p.color   = { 210, 230, 180, 60 };   // pale greenish mote
                particles_.push_back(p);
            }
        }
    }
}

void Renderer::spawnBurst(Vec2f pos, float z, RGBA color,
                           int count, float speed, float lifeMax, float size) {
    for (int i = 0; i < count; ++i) {
        float angle = static_cast<float>(i) / count * 2.0f * static_cast<float>(M_PI);
        float s = speed * (0.5f + (std::rand() % 100) / 100.0f);
        Particle p;
        p.pos     = pos;
        p.vel     = { std::cos(angle) * s, std::sin(angle) * s };
        p.z       = z;
        p.life    = lifeMax * (0.5f + (std::rand() % 100) / 200.0f);
        p.maxLife = p.life;
        p.size    = size;
        p.color   = color;
        particles_.push_back(p);
    }
}

void Renderer::triggerShake(float amount) {
    shakeAmt_ = std::max(shakeAmt_, amount);
}

void Renderer::triggerFade(float startAlpha, float delta) {
    fadeAlpha_ = startAlpha;
    fadeDelta_ = delta;
}

void Renderer::flashEntity(EntityID eid, RGBA color, int ticks) {
    entityFlashes_[eid] = { color, ticks };
}

void Renderer::addDyingEntity(Vec2f pos, float z, EntityType type, float lifeMax) {
    dying_.push_back({ pos, z, type, lifeMax, lifeMax });
}

void Renderer::drawEntityEffects(Vec2f pos, float z, bool burning, bool electrified) {
    if (!burning && !electrified) return;

    // Sprite rect — matches the destination rect used in drawSprite.
    int iTs = static_cast<int>(std::ceil(TILE_SIZE * camera_.zoom));
    int iH  = static_cast<int>(std::ceil(TILE_H    * camera_.zoom));
    SDL_Rect rect = {
        toPixelX(pos.x, z),
        toPixelY(pos.y, z) + iH / 2 - iTs,
        iTs, iTs
    };

    SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);

    if (burning) {
        // Orange overlay, subtle pulse.
        float pulse = 0.6f + 0.4f * std::sin(dayNightT_ * 12.0f + pos.x + pos.y);
        SDL_SetRenderDrawColor(sdl, 255, 100, 0,
                               static_cast<uint8_t>(80.0f * pulse));
        SDL_RenderFillRect(sdl, &rect);

        // Rising fire sparks (~15/sec).
        if (lastFdt_ > 0.0f &&
            (std::rand() % static_cast<int>(1.0f / (15.0f * lastFdt_) + 1)) == 0) {
            Particle p;
            p.pos   = pos;
            p.vel   = { ((std::rand() % 100) - 50) / 200.0f,   // slight x drift
                        -0.4f - (std::rand() % 60) / 100.0f }; // upward
            p.z     = z;
            p.life  = 0.25f + (std::rand() % 25) / 100.0f;
            p.maxLife = p.life;
            p.size  = 2.0f + (std::rand() % 3);
            // Alternate orange / red / yellow sparks.
            int hue = std::rand() % 3;
            p.color = hue == 0 ? RGBA{255, 160,  30, 220}
                    : hue == 1 ? RGBA{255,  60,   0, 200}
                    :            RGBA{255, 220,  80, 200};
            particles_.push_back(p);
        }
    }

    if (electrified) {
        // Cyan overlay, fast flicker.
        float flicker = 0.5f + 0.5f * std::sin(dayNightT_ * 40.0f + pos.x * 3.0f);
        SDL_SetRenderDrawColor(sdl, 80, 200, 255,
                               static_cast<uint8_t>(90.0f * flicker));
        SDL_RenderFillRect(sdl, &rect);

        // Random-direction electric sparks (~12/sec).
        if (lastFdt_ > 0.0f &&
            (std::rand() % static_cast<int>(1.0f / (12.0f * lastFdt_) + 1)) == 0) {
            float angle = (std::rand() % 628) / 100.0f;  // 0..2π
            float speed = 2.0f + (std::rand() % 200) / 100.0f;
            Particle p;
            p.pos     = pos;
            p.vel     = { std::cos(angle) * speed, std::sin(angle) * speed };
            p.z       = z;
            p.life    = 0.08f + (std::rand() % 12) / 100.0f;
            p.maxLife = p.life;
            p.size    = 1.5f + (std::rand() % 2);
            p.color   = (std::rand() % 2) ? RGBA{180, 230, 255, 240}
                                          : RGBA{255, 255, 255, 255};
            particles_.push_back(p);
        }
    }

    SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_NONE);
}

void Renderer::drawParticles() {
    SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
    for (const Particle& p : particles_) {
        float alpha = p.life / p.maxLife;
        int   px = toPixelX(p.pos.x, p.z);
        int   py = toPixelY(p.pos.y, p.z);
        int   s  = std::max(1, static_cast<int>(p.size * camera_.zoom));
        SDL_SetRenderDrawColor(sdl,
            p.color.r, p.color.g, p.color.b,
            static_cast<uint8_t>(p.color.a * alpha));
        SDL_Rect r = { px - s / 2, py - s / 2, s, s };
        SDL_RenderFillRect(sdl, &r);
    }
    SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_NONE);
}

void Renderer::drawDyingEntities() {
    for (const DyingEntity& d : dying_) {
        SDL_Texture* tex = sprites.get(d.type);
        if (!tex) continue;
        float alpha = d.life / d.maxLife;
        SDL_SetTextureAlphaMod(tex, static_cast<uint8_t>(255.0f * alpha));
        SDL_SetTextureColorMod(tex, 255, 255, 255);
        int iTs = static_cast<int>(std::ceil(TILE_SIZE * camera_.zoom));
        int iH  = static_cast<int>(std::ceil(TILE_H    * camera_.zoom));
        SDL_Rect dst = {
            toPixelX(d.pos.x, d.z),
            toPixelY(d.pos.y, d.z) + iH / 2 - iTs,
            iTs, iTs
        };
        SDL_RenderCopy(sdl, tex, nullptr, &dst);
        SDL_SetTextureAlphaMod(tex, 255);
    }
}

// ─── HUD & Text ──────────────────────────────────────────────────────────────

void Renderer::drawFacingIndicator(Vec2f renderPos, float renderZ, Direction facing) {
    float ts  = TILE_SIZE * camera_.zoom;
    float tsH = TILE_H    * camera_.zoom;
    float cx  = toPixelX(renderPos.x, renderZ) + ts  * 0.5f;
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

void Renderer::drawSummonPreview(const SummonPreview& preview) {
    if (!preview.active) return;

    constexpr int PAD = 8;
    constexpr int H   = 30;

    std::string label = "Summon: " + preview.golemName +
                        "  \xe2\x99\xa6 " + std::to_string(preview.manaCost);  // ♦

    int textW = 0, textH = 0;
    if (font_) TTF_SizeUTF8(font_, label.c_str(), &textW, &textH);

    int W = PAD + textW + PAD;
    int X = (VIEWPORT_W - W) / 2;
    int Y = VIEWPORT_H - H - 10;

    SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl, 10, 10, 10, 195);
    SDL_Rect panel = {X, Y, W, H};
    SDL_RenderFillRect(sdl, &panel);
    SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(sdl, 90, 90, 90, 255);
    SDL_RenderDrawRect(sdl, &panel);

    SDL_Color col = preview.canAfford ? SDL_Color{220, 210, 80, 255}
                                      : SDL_Color{200,  60, 60, 255};
    drawText(label, X + PAD, Y + (H - textH) / 2, col);
}

void Renderer::drawText(const std::string& text, int x, int y, SDL_Color col) const {
    if (!font_ || text.empty()) return;
    uint32_t packed = (uint32_t(col.r) << 24) | (uint32_t(col.g) << 16)
                    | (uint32_t(col.b) <<  8) | col.a;
    TextKey key{text, packed};
    SDL_Texture* tex = nullptr;
    auto it = textCache_.find(key);
    if (it != textCache_.end()) {
        tex = it->second;
    } else {
        SDL_Surface* surf = TTF_RenderUTF8_Blended(font_, text.c_str(), col);
        if (!surf) return;
        tex = SDL_CreateTextureFromSurface(sdl, surf);
        SDL_FreeSurface(surf);
        if (!tex) return;
        textCache_[key] = tex;
    }
    int w, h;
    SDL_QueryTexture(tex, nullptr, nullptr, &w, &h);
    SDL_Rect dst = {x, y, w, h};
    SDL_RenderCopy(sdl, tex, nullptr, &dst);
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

            // Mana cost (right side): ♦ N
            std::string manaStr = "\xe2\x99\xa6 " + std::to_string(rec.manaCost);
            SDL_Color   manaCol = rec.selected ? SDL_Color{220, 185, 50, 255}
                                               : SDL_Color{ 90,  70, 20, 255};
            drawText(manaStr, X + W - PAD - 44, ty, manaCol);

            // Step count (left of mana cost)
            std::string steps = std::to_string(rec.steps);
            drawText(steps, X + W - PAD - 76, ty,
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
        drawText("Q cycle   \xe2\x86\xb5 rename   Del delete", X + PAD, ty, hintCol);
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
    row("E",             "Summon golem");
    row("O",             "Create room portal");
    row("Tab",           "Toggle studio");
    sep();
    row("Arrows",        "Pan camera");
    row("Backspace",     "Re-centre");
    row("Ctrl + Scroll", "Zoom");
    sep();
    row("H",             "Toggle this menu");
    row("I",             "Recordings panel");
    row("K",             "Key rebind panel");
    row("Esc",           "Quit");
}

void Renderer::drawRebindPanel(const InputMap& map, int selectedRow, bool listening) {
    // Display label for each Action — must match Action enum order.
    static constexpr const char* LABELS[INPUT_ACTION_COUNT] = {
        "Move Up",          "Move Down",       "Move Left",        "Move Right",
        "Strafe (hold)",
        "Dig",              "Plant Mushroom",  "Place Portal",
        "Record",           "Cycle Recording", "Summon Golem",
        "Switch Grid",
        "Pan Up",           "Pan Down",        "Pan Left",         "Pan Right",
        "Reset Camera",     "Zoom (hold)",
        "Quit",             "Confirm",         "Toggle Controls",  "Toggle Recordings",
        "Toggle Rebind",
    };

    constexpr int PAD  = 10;
    constexpr int ROW  = 18;
    constexpr int W    = 310;
    const     int H    = PAD + 22 + 10 + INPUT_ACTION_COUNT * ROW + 10 + 20 + PAD;
    constexpr int X    = VIEWPORT_W - W - 10;
    constexpr int Y    = 10;
    constexpr int KEYX = X + 178;   // left edge of the key-name column

    // Background + border
    SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl, 10, 10, 10, 210);
    SDL_Rect panel = {X, Y, W, H};
    SDL_RenderFillRect(sdl, &panel);
    SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(sdl, 90, 90, 90, 255);
    SDL_RenderDrawRect(sdl, &panel);

    SDL_Color titleCol  = {220, 210,  80, 255};
    SDL_Color selLabel  = {255, 255, 255, 255};
    SDL_Color normLabel = {140, 180, 140, 255};
    SDL_Color selKey    = {220, 210, 100, 255};
    SDL_Color normKey   = { 90,  90,  90, 255};
    SDL_Color listenCol = { 80, 210, 230, 255};
    SDL_Color arrowCol  = {100, 160, 220, 255};
    SDL_Color dimCol    = { 70,  70,  70, 255};
    SDL_Color hintCol   = {100, 140, 100, 255};

    int ty = Y + PAD;

    drawText("K E Y   B I N D I N G S", X + 40, ty, titleCol);
    ty += 22;

    SDL_SetRenderDrawColor(sdl, 70, 70, 70, 255);
    SDL_RenderDrawLine(sdl, X + 5, ty + 4, X + W - 5, ty + 4);
    ty += 10;

    for (int i = 0; i < INPUT_ACTION_COUNT; ++i) {
        bool sel = (i == selectedRow);

        if (sel) {
            SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(sdl, 40, 80, 150, 120);
            SDL_Rect rowRect = {X + 2, ty - 1, W - 4, ROW};
            SDL_RenderFillRect(sdl, &rowRect);
            SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_NONE);
            drawText("\xe2\x96\xb6", X + PAD, ty, arrowCol);  // ▶
        }

        drawText(LABELS[i], X + PAD + (sel ? 16 : 0), ty, sel ? selLabel : normLabel);

        if (sel && listening) {
            bool blink = (SDL_GetTicks64() / 400) % 2;
            if (blink) drawText("[ any key ]", KEYX, ty, listenCol);
        } else {
            SDL_Keycode code = map.get(static_cast<Action>(i));
            std::string kn   = (code != SDLK_UNKNOWN) ? SDL_GetKeyName(code) : "-";
            drawText(kn, KEYX, ty, sel ? selKey : normKey);
        }

        ty += ROW;
    }

    SDL_SetRenderDrawColor(sdl, 70, 70, 70, 255);
    SDL_RenderDrawLine(sdl, X + 5, ty + 4, X + W - 5, ty + 4);
    ty += 10;

    if (listening)
        drawText("Esc  cancel", X + PAD, ty, hintCol);
    else
        drawText("\xe2\x86\x91\xe2\x86\x93 select   \xe2\x86\xb5 rebind   K close", X + PAD, ty, hintCol);
}

// ─── Phase 16: Mouse interaction ─────────────────────────────────────────────

void Renderer::drawFluidOverlay(const std::vector<FluidOverlay>& overlay) {
    SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
    for (const FluidOverlay& fw : overlay) {
        float lf = static_cast<float>(fw.pos.z);
        int sx = toPixelX(fw.pos.x,     lf);
        int sy = toPixelY(fw.pos.y,     lf);
        int sw = toPixelX(fw.pos.x + 1, lf) - sx;
        int sh = static_cast<int>(std::ceil(TILE_H * camera_.zoom));

        // Depth → colour: shallow = light blue-grey, deep = vivid blue.
        float wave  = 0.85f + 0.15f * std::sin(dayNightT_ * 2.5f
                              + fw.pos.x * 0.7f + fw.pos.y * 0.8f);
        float depth = std::min(fw.h, 3.0f) / 3.0f;   // normalise to [0,1]
        uint8_t r = static_cast<uint8_t>(30  * wave * depth);
        uint8_t g = static_cast<uint8_t>(80  * wave * depth);
        uint8_t b = static_cast<uint8_t>(210 * wave);
        uint8_t a = static_cast<uint8_t>(120 + 100 * depth);

        SDL_SetRenderDrawColor(sdl, r, g, b, a);
        SDL_Rect rect = { sx, sy, sw, sh };
        SDL_RenderFillRect(sdl, &rect);
    }
    SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_NONE);
}

void Renderer::drawHoverHighlight(TilePos tile) {
    float lf = static_cast<float>(tile.z);
    int sx = toPixelX(tile.x,     lf);
    int sy = toPixelY(tile.y,     lf);
    int sw = toPixelX(tile.x + 1, lf) - sx;
    int sh = static_cast<int>(std::ceil(TILE_H * camera_.zoom));

    SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl, 255, 255, 255, 45);
    SDL_Rect r = { sx, sy, sw, sh };
    SDL_RenderFillRect(sdl, &r);
    SDL_SetRenderDrawColor(sdl, 255, 255, 255, 110);
    SDL_RenderDrawRect(sdl, &r);
    SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_NONE);
}

void Renderer::drawEntityTooltip(const std::string& name, int screenX, int screenY) {
    if (name.empty()) return;
    constexpr int PAD = 6;
    constexpr int H   = 22;
    int tw = 0, th = 0;
    if (font_) TTF_SizeUTF8(font_, name.c_str(), &tw, &th);
    int W = PAD + tw + PAD;
    int X = std::clamp(screenX - W / 2, 0, VIEWPORT_W - W);
    int Y = screenY - H - 4;
    if (Y < 0) Y = screenY + 4;

    SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl, 10, 10, 10, 210);
    SDL_Rect bg = { X, Y, W, H };
    SDL_RenderFillRect(sdl, &bg);
    SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(sdl, 90, 90, 90, 255);
    SDL_RenderDrawRect(sdl, &bg);
    drawText(name, X + PAD, Y + (H - th) / 2, {220, 220, 220, 255});
}

void Renderer::drawContextMenu(const ContextMenu& menu) {
    if (!menu.active || menu.items.empty()) return;

    constexpr int PAD = 8;
    constexpr int ROW = 20;

    SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl, 15, 15, 25, 225);
    SDL_Rect bg = { menu.bounds.x, menu.bounds.y, menu.bounds.w, menu.bounds.h };
    SDL_RenderFillRect(sdl, &bg);
    SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(sdl, 100, 100, 140, 255);
    SDL_RenderDrawRect(sdl, &bg);

    for (int i = 0; i < (int)menu.items.size(); ++i) {
        int iy = menu.bounds.y + PAD + i * ROW;
        if (i == menu.hovered) {
            SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(sdl, 60, 80, 150, 160);
            SDL_Rect row = { menu.bounds.x + 2, iy - 1, menu.bounds.w - 4, ROW };
            SDL_RenderFillRect(sdl, &row);
            SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_NONE);
        }
        SDL_Color col = (i == menu.hovered)
            ? SDL_Color{255, 255, 255, 255}
            : SDL_Color{180, 180, 210, 255};
        drawText(menu.items[i], menu.bounds.x + PAD, iy + 2, col);
    }
}

void Renderer::setHandCursor(bool hand) {
    SDL_Cursor* c = hand ? cursorHand_ : cursorArrow_;
    if (c) SDL_SetCursor(c);
}

// ─── Phase 15: Studio overlays ────────────────────────────────────────────────

void Renderer::drawStudioPaths(const std::vector<StudioPathView>& views,
                                const std::vector<int>& conflicts,
                                int scrubTick, int selectedRec) {
    if (views.empty()) return;

    std::unordered_set<int> conflictSet(conflicts.begin(), conflicts.end());
    SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);

    for (int vi = 0; vi < (int)views.size(); ++vi) {
        const StudioPathView& view = views[vi];
        if (!view.path || view.path->empty()) continue;
        const auto& path = *view.path;
        AgentColor col = view.color;

        for (int tick = 0; tick < (int)path.size(); ++tick) {
            const PathStep& step = path[tick];
            bool isConflict = conflictSet.count(tick) > 0;
            bool isScrub    = (vi == selectedRec && tick == scrubTick);

            uint8_t r = isConflict ? 220 : col.r;
            uint8_t g = isConflict ?  50 : col.g;
            uint8_t b = isConflict ?  50 : col.b;
            uint8_t a = isScrub ? 255 : 160;
            if (isScrub) { r = 255; g = 255; b = 255; }

            SDL_SetRenderDrawColor(sdl, r, g, b, a);

            // Tile centre in screen pixels (studio floor is z=0)
            int cx = toPixelX(step.pos.x + 0.5f, 0.0f);
            int cy = toPixelY(step.pos.y + 0.5f, 0.0f);

            if (step.isWait) {
                SDL_Rect dot = { cx - 2, cy - 2, 4, 4 };
                SDL_RenderFillRect(sdl, &dot);
            } else {
                SDL_Rect body = { cx - 3, cy - 3, 6, 6 };
                SDL_RenderFillRect(sdl, &body);
                // Arrow pointing in facing direction
                TilePos d = dirToDelta(step.facing);
                float ts  = TILE_SIZE * camera_.zoom;
                float tsH = TILE_H    * camera_.zoom;
                int ex = cx + static_cast<int>(d.x * ts * 0.35f);
                int ey = cy + static_cast<int>(d.y * tsH * 0.35f);
                SDL_RenderDrawLine(sdl, cx, cy, ex, ey);
            }
        }
    }
    SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_NONE);
}

void Renderer::drawGhostEntity(TilePos pos, Direction facing, EntityType type) {
    SDL_Texture* tex = sprites.get(type);
    if (!tex) return;
    (void)facing;

    float lf  = static_cast<float>(pos.z);
    int   iTs = static_cast<int>(std::ceil(TILE_SIZE * camera_.zoom));
    int   iH  = static_cast<int>(std::ceil(TILE_H    * camera_.zoom));

    SDL_Rect dst;
    dst.x = toPixelX(static_cast<float>(pos.x), lf);
    dst.y = toPixelY(static_cast<float>(pos.y), lf) + iH / 2 - iTs;
    dst.w = iTs;
    dst.h = iTs;

    SDL_SetTextureAlphaMod(tex, 100);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_RenderCopy(sdl, tex, nullptr, &dst);
    SDL_SetTextureAlphaMod(tex, 255);
}

void Renderer::drawInstructionPanel(const Recording& rec, int selectedRow,
                                     int scrubInstrIdx,
                                     bool insertingWait, bool insertingMove,
                                     const std::string& insertBuffer,
                                     RelDir insertDir) {
    static const char* REL_DIR_NAMES[] = {"FWD", "RIGHT", "BACK", "LEFT"};

    const int PX      = VIEWPORT_W - 212;
    const int PY      = 10;
    const int PW      = 202;
    const int ROW_H   = 16;
    const int MAX_VIS = 22;
    const int PAD     = 5;

    int n      = (int)rec.instructions.size();
    int visN   = std::min(n, MAX_VIS);
    bool hasFooter = (insertingWait || insertingMove);
    int PH     = PAD + ROW_H + PAD + visN * ROW_H + PAD + (hasFooter ? ROW_H + 4 : 0);

    SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl, 8, 8, 18, 215);
    SDL_Rect bg = { PX, PY, PW, PH };
    SDL_RenderFillRect(sdl, &bg);
    SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(sdl, 70, 70, 110, 255);
    SDL_RenderDrawRect(sdl, &bg);

    // Title
    std::string title = rec.name + "  [" + std::to_string(n) + "]";
    drawText(title, PX + PAD, PY + PAD, {180, 180, 255, 255});

    // Scroll so that selectedRow is always visible
    int scrollOffset = 0;
    if (selectedRow >= MAX_VIS)
        scrollOffset = selectedRow - MAX_VIS + 1;
    if (scrollOffset < 0) scrollOffset = 0;

    for (int i = 0; i < MAX_VIS && (scrollOffset + i) < n; ++i) {
        int idx  = scrollOffset + i;
        int rowY = PY + PAD + (i + 1) * ROW_H + PAD;
        const Instruction& instr = rec.instructions[idx];

        bool isSel  = (idx == selectedRow);
        bool isScrub = (idx == scrubInstrIdx);

        if (isSel) {
            SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(sdl, 50, 50, 100, 200);
            SDL_Rect selr = { PX + 2, rowY - 1, PW - 4, ROW_H };
            SDL_RenderFillRect(sdl, &selr);
            SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_NONE);
        }

        std::string text;
        switch (instr.op) {
            case OpCode::MOVE_REL:   text = std::string("MOVE ") + REL_DIR_NAMES[(int)instr.dir]; break;
            case OpCode::WAIT:       text = "WAIT " + std::to_string(instr.ticks);                 break;
            case OpCode::HALT:       text = "HALT";                                                 break;
            case OpCode::DIG:        text = "DIG";                                                  break;
            case OpCode::PLANT:      text = "PLANT";                                                break;
            case OpCode::JUMP:       text = "JUMP "   + std::to_string(instr.addr);                break;
            case OpCode::JUMP_IF:    text = "JMPIF "  + std::to_string(instr.addr);                break;
            case OpCode::JUMP_IF_NOT:text = "JMPIFN " + std::to_string(instr.addr);                break;
            case OpCode::CALL:       text = "CALL "   + std::to_string(instr.addr);                break;
            case OpCode::RET:        text = "RET";                                                  break;
            case OpCode::SUMMON:     text = "SUMMON";                                               break;
        }

        std::string prefix = (isScrub ? ">" : " ") + std::to_string(idx) + " ";
        SDL_Color col = {160, 160, 180, 255};
        if (isScrub) col = {255, 240,  80, 255};
        if (isSel && !isScrub) col = {240, 240, 255, 255};
        drawText(prefix + text, PX + PAD, rowY, col);
    }

    // Insert-mode footer
    if (hasFooter) {
        int footerY = PY + PAD + (visN + 1) * ROW_H + PAD + 2;
        std::string prompt;
        if (insertingWait)
            prompt = "WAIT " + insertBuffer + "_  (Enter/Esc)";
        else {
            prompt = std::string("MOVE ") + REL_DIR_NAMES[(int)insertDir] + " (Arr/Ent/Esc)";
        }
        drawText(prompt, PX + PAD, footerY, {255, 210, 80, 255});
    }
}

void Renderer::drawTimeline(const Recording& rec, int scrubInstrIdx,
                             const std::vector<int>& conflictInstrs) {
    if (rec.instructions.empty()) return;

    std::unordered_set<int> conflictSet(conflictInstrs.begin(), conflictInstrs.end());

    const int BAR_H  = 14;
    const int BAR_Y  = VIEWPORT_H - BAR_H - 2;
    const int MARGIN = 6;

    int n     = (int)rec.instructions.size();
    int avail = VIEWPORT_W - 2 * MARGIN;
    int cellW = std::max(2, avail / n);
    int totalW = cellW * n;
    int startX = MARGIN + (avail - totalW) / 2;

    // Background strip
    SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl, 0, 0, 0, 190);
    SDL_Rect strip = { 0, BAR_Y - 3, VIEWPORT_W, BAR_H + 6 };
    SDL_RenderFillRect(sdl, &strip);

    for (int i = 0; i < n; ++i) {
        const Instruction& instr = rec.instructions[i];
        bool isScrub    = (i == scrubInstrIdx);
        bool isConflict = conflictSet.count(i) > 0;
        bool isHalt     = (instr.op == OpCode::HALT);
        bool isWait     = (instr.op == OpCode::WAIT);

        uint8_t r = isWait ? 40  : 60;
        uint8_t g = isWait ? 60  : 60;
        uint8_t b = isWait ? 100 : 90;
        if (isHalt)     { r =  30; g =  30; b =  30; }
        if (isConflict) { r = 180; g =  40; b =  40; }
        if (isScrub)    { r = 255; g = 255; b = 255; }

        SDL_SetRenderDrawColor(sdl, r, g, b, 220);
        SDL_Rect cell = { startX + i * cellW, BAR_Y, cellW - 1, BAR_H };
        SDL_RenderFillRect(sdl, &cell);
    }
    SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_NONE);
}
