#pragma once
#include <cmath>
#include <algorithm>

// D1-style opening-hand dial: an upright, fixed plane facing the opening
// head position. Translation selects; rotating a wrist or head does not.
struct WheelHand
{
    static constexpr float Width = .35f, Deadzone = .002f;
    float center[3]{}, quat[4]{0,0,0,1}, right[3]{1,0,0};
    bool valid = false;
    bool Capture(const float hand[3], const float head[3])
    {
        valid=false;
        for(int i=0;i<3;++i) if(!std::isfinite(hand[i]) || !std::isfinite(head[i])) return false;
        const float dx=head[0]-hand[0], dz=head[2]-hand[2];
        if(dx*dx+dz*dz<.0001f) return false;
        const float yaw=std::atan2(dx,dz);
        std::copy(hand,hand+3,center);
        quat[0]=quat[2]=0;quat[1]=std::sin(yaw*.5f);quat[3]=std::cos(yaw*.5f);
        right[0]=std::cos(yaw);right[1]=0;right[2]=-std::sin(yaw);
        return valid=true;
    }
    bool Direction(const float hand[3], float& x, float& y) const
    {
        x=y=0;
        if(!valid) return false;
        for(int i=0;i<3;++i) if(!std::isfinite(hand[i])) return false;
        x=(hand[0]-center[0])*right[0]+(hand[2]-center[2])*right[2];
        y=hand[1]-center[1];
        const float distance=std::sqrt(x*x+y*y);
        if(distance<=Deadzone) x=y=0;
        else { x/=distance;y/=distance; }
        return true;
    }
};
