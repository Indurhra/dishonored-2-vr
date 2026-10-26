#pragma once
#include <array>
#include <chrono>
#include <mutex>
#include <string>
#include <fmt/format.h>

// Wall time inside specific calls, not GPU cost. Snapshot windows prevent an
// old loading hitch from obscuring a steady per-frame wait during gameplay.
namespace WaitTrace {
    enum Kind { WaitFrame, BeginFrame, EndFrame, AcquireImage, WaitImage, DesktopPresent, EngineGap, Count };
    struct Sample { unsigned count=0; double sum=0, peak=0; };
    inline std::array<Sample,Count> samples{};
    inline std::mutex mutex;
    inline double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
    inline void Add(Kind k,double seconds) {
        std::lock_guard lock(mutex); auto& s=samples[k]; ++s.count;
        const double ms=seconds*1000.; s.sum+=ms; if(ms>s.peak)s.peak=ms;
    }
    struct Scope {
        Kind kind; double start=Now();
        explicit Scope(Kind k):kind(k){}
        ~Scope(){Add(kind,Now()-start);}
    };
    inline std::string Snapshot() {
        std::array<Sample,Count> snap;
        { std::lock_guard lock(mutex);snap=samples;samples={}; }
        constexpr const char* names[]={"xrWait","xrBegin","xrEnd","acquire","imageWait","desktopPresent","outsidePresent"};
        std::string out="blocking_ms mean/max/count";
        for(int i=0;i<Count;++i) { const auto& s=snap[i];out+=fmt::format(" {} {:.3f}/{:.3f}/{}",names[i],s.count?s.sum/s.count:0.,s.peak,s.count); }
        return out;
    }
}
