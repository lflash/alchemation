#include "events.hpp"

void EventBus::subscribe(EventType type, Handler handler) {
    subscribers[type].push_back(std::move(handler));
}

void EventBus::emit(Event event) {
    queue.push_back(std::move(event));
}

void EventBus::flush() {
    // Swap out the queue before dispatching so that any events emitted by
    // handlers during this flush go into the fresh queue, not the current one.
    std::vector<Event> current;
    std::swap(current, queue);

    for (const Event& ev : current) {
        auto it = subscribers.find(ev.type);
        if (it == subscribers.end()) continue;
        for (const Handler& h : it->second)
            h(ev);
    }
}
