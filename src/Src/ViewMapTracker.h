#pragma once
#include <array>
#include <cstddef>
// No ownership: pointers are valid only between the matching Map and Unmap.
template<std::size_t Capacity> class ViewMapTracker {
    struct Entry { void* context{}; void* resource{}; void* data{}; };
    std::array<Entry, Capacity> entries{};
public:
    bool Remember(void* context, void* resource, void* data) {
        for (auto& e : entries) if(e.resource==resource && e.context==context) { e.data=data; return true; }
        for (auto& e : entries) if(!e.resource) { e={context,resource,data}; return true; }
        return false;
    }
    void* Take(void* context, void* resource) {
        for(auto& e : entries) if(e.resource==resource && e.context==context) {
            void* data=e.data; e={}; return data;
        }
        return nullptr;
    }
};
