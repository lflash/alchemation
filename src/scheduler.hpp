#pragma once

#include "types.hpp"
#include <variant>
#include <vector>
#include <queue>

// ─── Action payloads ─────────────────────────────────────────────────────────

struct ChangeManaPayload { int delta; };

// Payloads for future phases are added here as needed.
using ActionPayload = std::variant<std::monostate, ChangeManaPayload>;

// ─── ScheduledAction ─────────────────────────────────────────────────────────

struct ScheduledAction {
    Tick          tick;
    EntityID      entity;
    ActionType    type;
    ActionPayload payload = std::monostate{};
};

// ─── Scheduler ───────────────────────────────────────────────────────────────

// Min-heap of ScheduledActions ordered by tick. O(log n) push and pop.
// Actions are immutable after insertion.
class Scheduler {
public:
    void push(ScheduledAction action);

    // Returns and removes all actions with tick <= currentTick, in tick order.
    std::vector<ScheduledAction> popDue(Tick currentTick);

    bool empty() const;

private:
    struct Cmp {
        bool operator()(const ScheduledAction& a, const ScheduledAction& b) const {
            return a.tick > b.tick;  // min-heap: smaller tick = higher priority
        }
    };
    std::priority_queue<ScheduledAction, std::vector<ScheduledAction>, Cmp> heap;
};
