#pragma once

#include <SDL2/SDL_mixer.h>
#include <array>
#include <unordered_map>
#include <vector>

// ─── SFX identifiers ─────────────────────────────────────────────────────────

enum class SFX {
    Step, Dig, Plant, CollectMushroom,
    RecordStart, RecordStop, DeployAgent,
    PortalCreate, PortalEnter, GridSwitch,
    GoblinHit, AgentStep,
};

// ─── Music layer identifiers ──────────────────────────────────────────────────
//
// Each layer is a looping track whose volume target is set each frame based on
// game state (what entities are nearby, which grid is active). The AudioSystem
// smoothly fades each layer toward its target.

enum class MusicLayer {
    WorldCalm     = 0,   // base ambience in the open world
    GoblinTension = 1,   // rises with goblin proximity on screen
    Studio        = 2,   // active inside the studio grid
    RoomInterior  = 3,   // active inside any bounded room grid
};

// ─── AudioSystem ─────────────────────────────────────────────────────────────

class AudioSystem {
public:
    AudioSystem();
    ~AudioSystem();

    AudioSystem(const AudioSystem&)            = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;

    // Play a one-shot sound effect on the next free SFX channel.
    void playSFX(SFX sfx);

    // Set the target volume for a music layer (0.0 = silent, 1.0 = full).
    // The actual volume smoothly fades toward the target in update().
    void setLayerTarget(MusicLayer layer, float target);

    // Call once per render frame. Advances all fades and manages the idle
    // ambient track. dt is real elapsed seconds.
    void update(float dt);

private:
    // Channel layout:
    //   0 – 7  : SFX (group 0)
    //   8 – 11 : music layers (WorldCalm, GoblinTension, Studio, RoomInterior)
    //   12     : idle ambient
    static constexpr int   SFX_CHANNELS     = 8;
    static constexpr int   MUSIC_BASE       = SFX_CHANNELS;
    static constexpr int   AMBIENT_CHANNEL  = MUSIC_BASE + 4;
    static constexpr int   TOTAL_CHANNELS   = AMBIENT_CHANNEL + 1;

    static constexpr float MUSIC_FADE       = 1.0f;   // vol units/sec for layers
    static constexpr float AMBIENT_FADE     = 0.25f;  // vol units/sec for idle track
    static constexpr float IDLE_THRESHOLD   = 30.0f;  // seconds of quiet before ambient
    static constexpr float ACTIVITY_CUTOFF  = 0.05f;  // layer volume below this = "quiet"

    struct LayerState {
        Mix_Chunk* chunk   = nullptr;
        int        channel = -1;
        float      current = 0.0f;
        float      target  = 0.0f;
    };

    std::unordered_map<SFX, Mix_Chunk*> sfxMap_;
    std::array<LayerState, 4>           layers_;
    std::vector<Mix_Chunk*>             ambientTracks_;

    float idleTimer_      = 0.0f;
    float ambientVol_     = 0.0f;
    bool  ambientPlaying_ = false;

    static Mix_Chunk* loadChunk(const char* path);
};
