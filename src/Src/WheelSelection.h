#pragma once
#include <cmath>
#include <algorithm>

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
        if (previous >= 0 && previous < count && std::abs(Wrap(a-previous*step)) <= step*.5f + std::min(.10f,step*.30f))
            return previous;
        int pick = (int)std::floor(a/step + .5f) % count;
        return pick < 0 ? pick + count : pick;
    }
    // A new neighbour must stay selected for 80 ms. Returning to the centre
    // cancels immediately; the first deliberate selection is immediate too.
    struct Sticky
    {
        int pending=-1;
        double since=0;
        int Update(float x,float y,int count,int previous,double now)
        {
            const int next=Pick(x,y,count,previous);
            if(next<0 || previous<0 || previous>=count || next==previous || !std::isfinite(now))
            { pending=-1;return next; }
            if(pending!=next || now<since) { pending=next;since=now; }
            if(now-since<.080) return previous;
            pending=-1;return next;
        }
    };
}