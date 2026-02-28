#pragma once

#include "types.hpp"
#include <functional>
#include <unordered_map>
#include <vector>

// ─── Event ───────────────────────────────────────────────────────────────────

struct Event {
    EventType type;
    EntityID  subject;
    EntityID  other     = INVALID_ENTITY;  // populated for Collided
    int       magnitude = 0;               // populated for Hit
};

// ─── EventBus ────────────────────────────────────────────────────────────────

// Subscribers register handlers per EventType. Events emitted during a tick are
// queued and dispatched together on flush(), so handlers always see a consistent
// game state. Any events emitted by a handler during flush() are deferred to the
// next flush().
class EventBus {
public:
    using Handler = std::function<void(const Event&)>;

    void subscribe(EventType type, Handler handler);

    // Queue an event. Not dispatched until flush().
    void emit(Event event);

    // Dispatch all queued events to their subscribers, then clear the queue.
    // Call once per tick after all systems have run.
    void flush();

private:
    struct EventTypeHash {
        size_t operator()(EventType t) const {
            return std::hash<int>{}(static_cast<int>(t));
        }
    };
    std::unordered_map<EventType, std::vector<Handler>, EventTypeHash> subscribers;
    std::vector<Event> queue;
};
