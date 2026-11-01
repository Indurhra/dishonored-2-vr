#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <bit>
// Value cache only: never retain a D3D object, buffer address or view pose.
// Every bit of all four rows participates in equality, including NaN payloads
// and signed zero. Rewritten buffers and reused object addresses need no invalidation.
template<class Value, unsigned Slots=64> class DrawRowsCache {
    static_assert(Slots && !(Slots&(Slots-1)));
    struct Entry { bool valid=false; std::array<std::uint32_t,16> key{}; Value value{}; };
    std::array<Entry,Slots> entries{};
public:
    template<class Compute> Value Get(const float* x,const float* y,const float* z,const float* w,Compute compute) {
        std::array<std::uint32_t,16> key;
        std::memcpy(key.data(),x,16);std::memcpy(key.data()+4,y,16);
        std::memcpy(key.data()+8,z,16);std::memcpy(key.data()+12,w,16);
        std::uint32_t hash=0;
        for(auto word:key)hash=std::rotl(hash,5)^word;
        auto& e=entries[hash&(Slots-1)];
        if(e.valid&&e.key==key)return e.value;
        Value value=compute();e.key=key;e.value=value;e.valid=true;return value;
    }
};
// A witness that this context/serial was actually published to the shared table.
// The owner advances generation whenever a command-list finish removes a row.
struct DrawSerialWitness {
    const void* context=nullptr;std::uint64_t serial=0,generation=0;bool valid=false;
    bool Matches(const void* c,std::uint64_t s,std::uint64_t g)const {
        return valid&&context==c&&serial==s&&generation==g;
    }
    void Published(const void* c,std::uint64_t s,std::uint64_t g){context=c;serial=s;generation=g;valid=true;}
};
