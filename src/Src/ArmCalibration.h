#pragma once
#include <algorithm>
#include <cmath>

namespace ArmCalibration
{
    // Affine output only. Keep the rigid rotation separately for joint axes.
    inline void ScaleAbout(float r[9], float t[3], const float pivot[3], float scale)
    {
        for (int i = 0; i < 9; ++i) r[i] *= scale;
        for (int i = 0; i < 3; ++i) t[i] = pivot[i] + scale * (t[i] - pivot[i]);
    }
    inline float ArmScale(float target, float measured, float maximum)
    {
        if (!std::isfinite(target) || !std::isfinite(measured) || target <= 0.f || measured <= 0.f) return 1.f;
        const float limit = std::isfinite(maximum) ? std::max(1.f, maximum) : 1.f;
        return std::clamp(target / measured, 0.65f, limit);
    }
    inline float HandScale(float value)
    {
        return std::isfinite(value) ? std::clamp(value, 0.65f, 1.15f) : 1.f;
    }
}
