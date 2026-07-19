#pragma once
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// A tiny append-only registry keyed by a string id, addressed by an integer
// handle. Definitions are registered once at startup (see content.cpp) and
// referenced everywhere by their handle for cheap, copy-free lookup.
//
//   int sword = weapons.add({ "sword", "Arming Sword", ... });
//   const WeaponDef& def = weapons[sword];
//
// Handles are stable for the lifetime of the program. A missing id yields -1.
// ---------------------------------------------------------------------------
template <class T>
class Registry {
public:
    int add(T def) {
        defs_.push_back(std::move(def));
        return static_cast<int>(defs_.size()) - 1;
    }

    const T& operator[](int handle) const { return defs_[handle]; }
    T&       operator[](int handle)       { return defs_[handle]; }  // load-time wiring only
    int size() const { return static_cast<int>(defs_.size()); }
    bool valid(int handle) const { return handle >= 0 && handle < size(); }

    int find(const std::string& id) const {
        for (int i = 0; i < size(); ++i)
            if (defs_[i].id == id) return i;
        return -1;
    }

    auto begin() const { return defs_.begin(); }
    auto end() const { return defs_.end(); }

private:
    std::vector<T> defs_;
};
