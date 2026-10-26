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
    inline float HandScale(float value)
    {
        return std::isfinite(value) ? std::clamp(value, 0.65f, 1.15f) : 1.f;
    }
}
