#include "scheduler.hpp"

void Scheduler::push(ScheduledAction action) {
    heap.push(std::move(action));
}

std::vector<ScheduledAction> Scheduler::popDue(Tick currentTick) {
    std::vector<ScheduledAction> result;
    while (!heap.empty() && heap.top().tick <= currentTick) {
        result.push_back(heap.top());
        heap.pop();
    }
    return result;
}

bool Scheduler::empty() const {
    return heap.empty();
}
