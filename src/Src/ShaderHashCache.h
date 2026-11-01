#pragma once
#include <cstdint>
#include <array>
// Per-thread cache. Every shader registration advances the generation, so a
// pointer recycled for a different shader cannot retain the old hash.
struct ShaderHashCache {
    struct Entry {const void* pointer=nullptr;uint64_t generation=0,hash=0;};
    std::array<Entry,64> entries{};
    Entry& Slot(const void* p){return entries[(reinterpret_cast<uintptr_t>(p)>>4)%entries.size()];}
    bool Find(const void* p,uint64_t generation,uint64_t& hash){
        auto& e=Slot(p);if(e.pointer!=p||e.generation!=generation)return false;
        hash=e.hash;return true;
    }
    void Store(const void* p,uint64_t generation,uint64_t hash){Slot(p)={p,generation,hash};}
};
