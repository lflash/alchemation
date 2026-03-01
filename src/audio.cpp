#include "audio.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

// ─── Asset paths ──────────────────────────────────────────────────────────────

static constexpr std::pair<SFX, const char*> SFX_FILES[] = {
    { SFX::Step,            "assets/sfx/step.wav"           },
    { SFX::Dig,             "assets/sfx/dig.wav"            },
    { SFX::Plant,           "assets/sfx/plant.wav"          },
    { SFX::CollectMushroom, "assets/sfx/collect.wav"        },
    { SFX::RecordStart,     "assets/sfx/record_start.wav"   },
    { SFX::RecordStop,      "assets/sfx/record_stop.wav"    },
    { SFX::DeployAgent,     "assets/sfx/deploy.wav"         },
    { SFX::PortalCreate,    "assets/sfx/portal_create.wav"  },
    { SFX::PortalEnter,     "assets/sfx/portal_enter.wav"   },
    { SFX::GridSwitch,      "assets/sfx/grid_switch.wav"    },
    { SFX::GoblinHit,       "assets/sfx/goblin_hit.wav"     },
    { SFX::AgentStep,       "assets/sfx/agent_step.wav"     },
};

static constexpr const char* MUSIC_FILES[4] = {
    "assets/music/world_calm.wav",      // MusicLayer::WorldCalm
    "assets/music/goblin_tension.wav",  // MusicLayer::GoblinTension
    "assets/music/studio.wav",          // MusicLayer::Studio
    "assets/music/room_interior.wav",   // MusicLayer::RoomInterior
};

static constexpr const char* AMBIENT_FILES[] = {
    "assets/music/ambient_1.wav",
    "assets/music/ambient_2.wav",
    "assets/music/ambient_3.wav",
};

// ─── Helpers ─────────────────────────────────────────────────────────────────

Mix_Chunk* AudioSystem::loadChunk(const char* path) {
    Mix_Chunk* c = Mix_LoadWAV(path);
    if (!c)
        std::fprintf(stderr, "AudioSystem: could not load '%s': %s\n",
                     path, Mix_GetError());
    return c;
}

// ─── Construction / destruction ───────────────────────────────────────────────

AudioSystem::AudioSystem() {
    if (Mix_Init(MIX_INIT_OGG) == 0)
        std::fprintf(stderr, "AudioSystem: OGG support unavailable: %s\n", Mix_GetError());

    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) != 0) {
        std::fprintf(stderr, "AudioSystem: Mix_OpenAudio failed: %s\n", Mix_GetError());
        return;
    }

    Mix_AllocateChannels(TOTAL_CHANNELS);
    Mix_GroupChannels(0, SFX_CHANNELS - 1, 0);  // group 0 = SFX channels

    // Load SFX
    for (auto& [sfx, path] : SFX_FILES) {
        if (Mix_Chunk* c = loadChunk(path))
            sfxMap_[sfx] = c;
    }

    // Load music layers and start them looping at volume 0.
    // Keeping them always running means fading is just a volume change.
    for (int i = 0; i < 4; ++i) {
        layers_[i].channel = MUSIC_BASE + i;
        layers_[i].chunk   = loadChunk(MUSIC_FILES[i]);
        Mix_Volume(layers_[i].channel, 0);
        if (layers_[i].chunk)
            Mix_PlayChannel(layers_[i].channel, layers_[i].chunk, -1);
    }

    // Load idle ambient tracks
    for (auto* path : AMBIENT_FILES) {
        if (Mix_Chunk* c = loadChunk(path))
            ambientTracks_.push_back(c);
    }
    Mix_Volume(AMBIENT_CHANNEL, 0);
}

AudioSystem::~AudioSystem() {
    Mix_HaltChannel(-1);
    for (auto& [sfx, c] : sfxMap_)  Mix_FreeChunk(c);
    for (auto& L        : layers_)  if (L.chunk) Mix_FreeChunk(L.chunk);
    for (auto* c        : ambientTracks_) Mix_FreeChunk(c);
    Mix_CloseAudio();
    Mix_Quit();
}

// ─── Public interface ─────────────────────────────────────────────────────────

void AudioSystem::playSFX(SFX sfx) {
    auto it = sfxMap_.find(sfx);
    if (it == sfxMap_.end()) return;

    int ch = Mix_GroupAvailable(0);
    if (ch == -1) ch = Mix_GroupOldest(0);   // steal oldest SFX channel if all busy
    if (ch != -1) Mix_PlayChannel(ch, it->second, 0);
}

void AudioSystem::setLayerTarget(MusicLayer layer, float target) {
    layers_[static_cast<int>(layer)].target = std::clamp(target, 0.0f, 1.0f);
}

void AudioSystem::update(float dt) {
    // ── Music layer fades ────────────────────────────────────────────────────
    float maxActive = 0.0f;

    for (auto& L : layers_) {
        if (!L.chunk) continue;

        float diff = L.target - L.current;
        float step = MUSIC_FADE * dt;
        if (std::abs(diff) <= step)
            L.current = L.target;
        else
            L.current += (diff > 0.0f ? step : -step);

        Mix_Volume(L.channel, static_cast<int>(L.current * MIX_MAX_VOLUME));
        maxActive = std::max(maxActive, L.current);
    }

    // ── Idle ambient ─────────────────────────────────────────────────────────
    if (maxActive > ACTIVITY_CUTOFF) {
        // Music is playing — reset idle timer, fade ambient out if running.
        idleTimer_ = 0.0f;
        if (ambientPlaying_) {
            ambientVol_ = std::max(0.0f, ambientVol_ - AMBIENT_FADE * dt);
            Mix_Volume(AMBIENT_CHANNEL, static_cast<int>(ambientVol_ * MIX_MAX_VOLUME));
            if (ambientVol_ <= 0.0f) {
                Mix_HaltChannel(AMBIENT_CHANNEL);
                ambientPlaying_ = false;
            }
        }
    } else {
        // Silence — count up; start ambient once threshold is reached.
        idleTimer_ += dt;
        if (!ambientPlaying_) {
            if (idleTimer_ >= IDLE_THRESHOLD && !ambientTracks_.empty()) {
                int idx = std::rand() % static_cast<int>(ambientTracks_.size());
                Mix_PlayChannel(AMBIENT_CHANNEL, ambientTracks_[idx], 0);
                Mix_Volume(AMBIENT_CHANNEL, 0);
                ambientPlaying_ = true;
                ambientVol_     = 0.0f;
                idleTimer_      = 0.0f;
            }
        } else {
            // Fade ambient in; detect when it finishes playing naturally.
            ambientVol_ = std::min(1.0f, ambientVol_ + AMBIENT_FADE * dt);
            Mix_Volume(AMBIENT_CHANNEL, static_cast<int>(ambientVol_ * MIX_MAX_VOLUME));
            if (!Mix_Playing(AMBIENT_CHANNEL)) {
                ambientPlaying_ = false;
                idleTimer_      = 0.0f;
            }
        }
    }
}
