#pragma once

#include <cstdint>
#include <string>
#include <vector>

// ─── Rect ────────────────────────────────────────────────────────────────────
//
// Screen-space rectangle used for layout and mouse hit testing.

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    bool contains(int px, int py) const noexcept {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

// ─── Widget primitives ───────────────────────────────────────────────────────
//
// SDL-free data structs. Renderer draw methods accept these.

struct Panel {
    Rect    bounds;
    uint8_t bgR = 10,  bgG = 10,  bgB = 10,  bgA = 195;
    uint8_t bdR = 90,  bdG = 90,  bdB = 90,  bdA = 255;
};

struct Label {
    enum class Align { Left, Centre, Right };
    Rect        bounds;
    std::string text;
    uint8_t     r = 255, g = 255, b = 255, a = 255;
    Align       align = Align::Left;
};

struct ListRow { std::string text; bool selected = false; };

struct ListWidget {
    Rect                 bounds;
    std::vector<ListRow> rows;
    int                  rowH         = 22;
    int                  scrollOffset = 0;

    // Row index under screen y, or -1 if out of range.
    int itemAt(int screenY) const {
        if (screenY < bounds.y || screenY >= bounds.y + bounds.h) return -1;
        int row = scrollOffset + (screenY - bounds.y) / rowH;
        return (row >= 0 && row < (int)rows.size()) ? row : -1;
    }

    // Adjust scrollOffset so that index is visible.
    void scrollTo(int index) {
        int visible = (bounds.h > 0 && rowH > 0) ? bounds.h / rowH : 1;
        if (index < scrollOffset) scrollOffset = index;
        else if (index >= scrollOffset + visible) scrollOffset = index - visible + 1;
        if (scrollOffset < 0) scrollOffset = 0;
    }
};

struct Button {
    Rect        bounds;
    std::string label;
    bool        hovered = false;
    bool        pressed = false;
};

// ─── Context menu ─────────────────────────────────────────────────────────────

struct ContextMenu {
    bool                     active  = false;
    Rect                     bounds;
    std::vector<std::string> items;
    int                      hovered = -1;   // item under cursor (-1 = none)
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
    ContextMenu contextMenu;

    // Open a panel (closes whatever was open; resets sub-state).
    void open(ActivePanel p) {
        active          = p;
        rebindListening = false;
        renamingScript  = false;
        contextMenu.active = false;
    }

    // Close the active panel.
    void close() {
        active          = ActivePanel::None;
        rebindListening = false;
    }

    bool isOpen()          const { return active != ActivePanel::None; }
    bool is(ActivePanel p) const { return active == p; }
};
