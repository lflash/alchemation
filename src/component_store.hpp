#pragma once

#include "types.hpp"
#include <unordered_map>

// ─── ComponentStore ───────────────────────────────────────────────────────────
//
// Lightweight ECS component store: maps EntityID → T.
// First step toward a full ECS architecture (see DESIGN.md § Architectural Decisions).
// In the GPU rewrite this maps directly to a typed SoA buffer.

template<typename T>
class ComponentStore {
public:
    void add(EntityID id, T val)       { data_[id] = std::move(val); }

    T*       get(EntityID id)       { auto it = data_.find(id); return it != data_.end() ? &it->second : nullptr; }
    const T* get(EntityID id) const { auto it = data_.find(id); return it != data_.end() ? &it->second : nullptr; }

    bool has(EntityID id) const { return data_.count(id) != 0; }
    void remove(EntityID id)    { data_.erase(id); }
    void clear()                { data_.clear(); }

    std::unordered_map<EntityID, T>&       all()       { return data_; }
    const std::unordered_map<EntityID, T>& all() const { return data_; }

private:
    std::unordered_map<EntityID, T> data_;
};
