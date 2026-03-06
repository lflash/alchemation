#pragma once

#include <string>

// ─── UIRect ───────────────────────────────────────────────────────────────────
//
// Screen-space rectangle used for layout and mouse hit testing (Phase 16).

struct UIRect {
    int x = 0, y = 0, w = 0, h = 0;
    bool contains(int px, int py) const noexcept {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

// ─── ActivePanel ─────────────────────────────────────────────────────────────

enum class ActivePanel { None, Controls, Recordings, Rebind };

// ─── UIState ─────────────────────────────────────────────────────────────────
//
// All overlay panel state in one place. Owned by main.cpp.
// Replaces the loose showControls/showRecordings/showRebind booleans.

struct UIState {
    ActivePanel active          = ActivePanel::None;
    int         rebindRow       = 0;
    bool        rebindListening = false;
    bool        renamingScript  = false;
    std::string renameBuffer;

    // Open a panel (closes whatever was open; resets sub-state).
    void open(ActivePanel p) {
        active          = p;
        rebindListening = false;
        renamingScript  = false;
    }

    // Close the active panel.
    void close() {
        active          = ActivePanel::None;
        rebindListening = false;
    }

    bool isOpen()       const { return active != ActivePanel::None; }
    bool is(ActivePanel p) const { return active == p; }
};
