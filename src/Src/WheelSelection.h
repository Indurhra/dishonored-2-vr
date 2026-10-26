#pragma once
#include <cmath>

namespace WheelSelection
{
    constexpr float pi = 3.14159265358979323846f;
    inline float Wrap(float a)
    {
        while (a > pi) a -= 2.f * pi;
        while (a < -pi) a += 2.f * pi;
        return a;
    }
    // Top is slot zero, clockwise. Returning to centre explicitly clears the pick.
    inline int Pick(float x, float y, int count, int previous)
    {
        if (count <= 0 || !std::isfinite(x) || !std::isfinite(y) || x*x+y*y < .35f*.35f) return -1;
        const float step = 2.f*pi/count;
        const float a = std::atan2(x,y);
        if (previous >= 0 && previous < count && std::abs(Wrap(a-previous*step)) <= step*.5f + .10f)
            return previous;
        int pick = (int)std::floor(a/step + .5f) % count;
        return pick < 0 ? pick + count : pick;
    }
}
