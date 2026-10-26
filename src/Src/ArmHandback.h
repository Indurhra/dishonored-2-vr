#pragma once
#include <algorithm>
#include <cmath>
namespace ArmHandback
{
    // Ownership is determined by engine state; this only eases control back AFTER release.
    inline float Weight(float elapsed,float duration)
    {
        if (!std::isfinite(duration) || duration<=0) return 1;
        if (!std::isfinite(elapsed)) return 0;
        const float t=std::clamp(elapsed/duration,0.f,1.f);
        return t*t*(3.f-2.f*t);
    }
}
