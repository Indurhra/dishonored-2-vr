#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
// Readers reuse their own immutable copy until a completed publication changes
// generation. No racing reads of writer-owned memory and no unbounded spin loop.
// Owner must outlive reader caches (the renderer uses a process-lifetime owner).
template<class T> class PublishedSnapshot {
    mutable std::mutex mutex;
    std::atomic<std::uint64_t> generation{0};
    T value{};
public:
    struct Reader {const PublishedSnapshot* owner=nullptr;std::uint64_t generation=0;T value{};};
    void Publish(const T& next) {
        std::lock_guard lock(mutex);value=next;generation.fetch_add(1,std::memory_order_release);
    }
    bool Read(Reader& reader)const {
        const auto observed=generation.load(std::memory_order_acquire);
        if(!observed)return false;
        if(reader.owner==this&&reader.generation==observed)return true;
        std::lock_guard lock(mutex);
        reader.value=value;reader.generation=generation.load(std::memory_order_relaxed);reader.owner=this;
        return true;
    }
};
