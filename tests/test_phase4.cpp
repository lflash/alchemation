#include "doctest.h"
#include "scheduler.hpp"
#include "events.hpp"
#include "spatial.hpp"

// ─── Scheduler ───────────────────────────────────────────────────────────────

TEST_CASE("Scheduler: actions pop in tick order regardless of insertion order") {
    Scheduler s;
    s.push({ 10, 1, ActionType::Despawn });
    s.push({  3, 2, ActionType::ChangeMana, ChangeManaPayload{1} });
    s.push({  7, 3, ActionType::Despawn });
    s.push({  1, 4, ActionType::ChangeMana, ChangeManaPayload{2} });

    auto due = s.popDue(10);
    REQUIRE(due.size() == 4);
    CHECK(due[0].tick == 1);
    CHECK(due[1].tick == 3);
    CHECK(due[2].tick == 7);
    CHECK(due[3].tick == 10);
}

TEST_CASE("Scheduler: actions with the same tick all pop together") {
    Scheduler s;
    s.push({ 5, 1, ActionType::Despawn });
    s.push({ 5, 2, ActionType::Despawn });
    s.push({ 5, 3, ActionType::Despawn });

    auto due = s.popDue(5);
    CHECK(due.size() == 3);
}

TEST_CASE("Scheduler: only actions up to currentTick are returned") {
    Scheduler s;
    s.push({ 3, 1, ActionType::Despawn });
    s.push({ 5, 2, ActionType::Despawn });
    s.push({ 8, 3, ActionType::Despawn });

    auto due = s.popDue(5);
    REQUIRE(due.size() == 2);
    CHECK(due[0].tick == 3);
    CHECK(due[1].tick == 5);

    // Remaining action is still in the heap
    CHECK(!s.empty());
    auto rest = s.popDue(100);
    REQUIRE(rest.size() == 1);
    CHECK(rest[0].tick == 8);
}

TEST_CASE("Scheduler: popped actions are removed; subsequent popDue is empty") {
    Scheduler s;
    s.push({ 2, 1, ActionType::Despawn });

    auto due = s.popDue(2);
    CHECK(due.size() == 1);
    CHECK(s.empty());
    CHECK(s.popDue(100).empty());
}

TEST_CASE("Scheduler: empty scheduler returns empty vector") {
    Scheduler s;
    CHECK(s.popDue(999).empty());
    CHECK(s.empty());
}

TEST_CASE("Scheduler: future actions not popped until their tick arrives") {
    Scheduler s;
    s.push({ 10, 1, ActionType::Despawn });

    CHECK(s.popDue(5).empty());
    CHECK(!s.empty());

    auto due = s.popDue(10);
    CHECK(due.size() == 1);
}

// ─── EventBus ────────────────────────────────────────────────────────────────

TEST_CASE("EventBus: subscriber does not receive event before flush") {
    EventBus bus;
    int count = 0;
    bus.subscribe(EventType::Arrived, [&](const Event&) { count++; });

    bus.emit({ EventType::Arrived, 1 });
    CHECK(count == 0);  // not dispatched yet

    bus.flush();
    CHECK(count == 1);
}

TEST_CASE("EventBus: multiple subscribers for same event all receive it") {
    EventBus bus;
    int a = 0, b = 0;
    bus.subscribe(EventType::Arrived, [&](const Event&) { a++; });
    bus.subscribe(EventType::Arrived, [&](const Event&) { b++; });

    bus.emit({ EventType::Arrived, 1 });
    bus.flush();
    CHECK(a == 1);
    CHECK(b == 1);
}

TEST_CASE("EventBus: subscriber only receives its own event type") {
    EventBus bus;
    int arrivedCount = 0, despawnedCount = 0;
    bus.subscribe(EventType::Arrived,   [&](const Event&) { arrivedCount++;   });
    bus.subscribe(EventType::Despawned, [&](const Event&) { despawnedCount++; });

    bus.emit({ EventType::Arrived,   1 });
    bus.emit({ EventType::Arrived,   2 });
    bus.emit({ EventType::Despawned, 3 });
    bus.flush();

    CHECK(arrivedCount   == 2);
    CHECK(despawnedCount == 1);
}

TEST_CASE("EventBus: queue is empty after flush") {
    EventBus bus;
    int count = 0;
    bus.subscribe(EventType::Arrived, [&](const Event&) { count++; });

    bus.emit({ EventType::Arrived, 1 });
    bus.flush();
    bus.flush();  // second flush should fire nothing
    CHECK(count == 1);
}

TEST_CASE("EventBus: events emitted during flush are deferred to next flush") {
    EventBus bus;
    int count = 0;

    bus.subscribe(EventType::Arrived, [&](const Event& ev) {
        count++;
        if (count == 1)
            bus.emit({ EventType::Arrived, ev.subject });  // re-emit during flush
    });

    bus.emit({ EventType::Arrived, 1 });
    bus.flush();   // fires once; re-emits one more
    CHECK(count == 1);

    bus.flush();   // fires the deferred one
    CHECK(count == 2);
}

TEST_CASE("EventBus: Arrived fires exactly once per entity arrival") {
    EntityRegistry registry;
    EventBus       bus;

    EntityID id = registry.spawn(EntityType::Player, {0, 0});
    Entity*  e  = registry.get(id);
    e->destination = {1, 0};

    int arrivedCount = 0;
    bus.subscribe(EventType::Arrived, [&](const Event& ev) {
        if (ev.subject == id) arrivedCount++;
    });

    // Drive the entity to its destination, emitting Arrived on arrival.
    while (e->isMoving()) {
        bool arrived = stepMovement(*e);
        if (arrived)
            bus.emit({ EventType::Arrived, id });
        bus.flush();
    }

    CHECK(arrivedCount == 1);
}

// ─── Despawn action ──────────────────────────────────────────────────────────

TEST_CASE("Despawn action removes entity from registry and all spatial cells") {
    EntityRegistry registry;
    SpatialGrid    spatial;
    Scheduler      scheduler;

    EntityID id = registry.spawn(EntityType::Goblin, {3, 3});
    Entity*  e  = registry.get(id);
    spatial.add(id, e->pos, e->size);

    scheduler.push({ 5, id, ActionType::Despawn });

    for (auto& action : scheduler.popDue(5)) {
        if (action.type == ActionType::Despawn) {
            Entity* target = registry.get(action.entity);
            if (target) spatial.remove(target->id, target->pos, target->size);
            registry.destroy(action.entity);
        }
    }

    CHECK(registry.get(id) == nullptr);
    CHECK(spatial.at({3, 3}).empty());
}

// ─── ChangeMana action ───────────────────────────────────────────────────────

TEST_CASE("ChangeMana action modifies entity mana") {
    EntityRegistry registry;
    Scheduler      scheduler;

    EntityID id = registry.spawn(EntityType::Player, {0, 0});
    registry.get(id)->mana = 2;

    scheduler.push({ 1, id, ActionType::ChangeMana, ChangeManaPayload{ 3} });
    scheduler.push({ 2, id, ActionType::ChangeMana, ChangeManaPayload{-1} });

    for (auto& action : scheduler.popDue(2)) {
        if (action.type == ActionType::ChangeMana) {
            Entity* target = registry.get(action.entity);
            if (target)
                target->mana += std::get<ChangeManaPayload>(action.payload).delta;
        }
    }

    CHECK(registry.get(id)->mana == 4);  // 2 + 3 - 1
}
