#pragma once
#include <cstdint>
#include <cmath>

// Shared provenance contract for DLSS and AFW. Serial identifies rendered work,
// not Present parity. Epoch changes on cuts, resize, recenter or renderer changes.
namespace TemporalFrame {
struct Stamp {
    uint64_t serial=0, epoch=0;
    int eye=-1;
    uint32_t width=0, height=0;
};
inline bool Valid(const Stamp& s) {
    return s.serial && s.epoch && (s.eye==0 || s.eye==1) && s.width && s.height;
}
inline bool SameImage(const Stamp& a,const Stamp& b) {
    return Valid(a) && Valid(b) && a.serial==b.serial && a.epoch==b.epoch &&
           a.eye==b.eye && a.width==b.width && a.height==b.height;
}
inline bool HistoryUsable(const Stamp& previous,const Stamp& current) {
    return Valid(previous) && Valid(current) && previous.epoch==current.epoch &&
           previous.eye==current.eye && previous.width==current.width && previous.height==current.height &&
           current.serial>previous.serial && current.serial-previous.serial<=2;
}
inline bool HeldUsable(const Stamp& held,const Stamp& fresh) {
    return Valid(held) && Valid(fresh) && held.epoch==fresh.epoch && held.eye!=fresh.eye &&
           held.width==fresh.width && held.height==fresh.height && fresh.serial>held.serial &&
           fresh.serial-held.serial==1;
}
}
