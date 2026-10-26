#include "../src/Src/ArmCalibration.h"
#include <cstdlib>
#include <limits>
#include <cstdio>

static void require(bool ok, const char* name)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", name); std::exit(1); }
}
static void transform(const float r[9], const float t[3], const float p[3], float out[3])
{
    for (int i = 0; i < 3; ++i) out[i] = r[i * 3] * p[0] + r[i * 3 + 1] * p[1] + r[i * 3 + 2] * p[2] + t[i];
}
int main()
{
    // A rotated/translated palm, not an origin-only toy: calibration must keep
    // its tracked anchor fixed while changing fingertip distance in every axis.
    const float r0[9] = {0,-1,0, 1,0,0, 0,0,1};
    const float t0[3] = {2,3,4}, palm[3] = {.2f,-.1f,.3f}, tip[3] = {.4f,.05f,.5f};
    float pivot[3], before[3]; transform(r0,t0,palm,pivot); transform(r0,t0,tip,before);
    for (float scale : {0.65f,0.8f,1.f,1.15f})
    {
        float r[9], t[3], actualPalm[3], actualTip[3];
        std::copy(r0,r0+9,r); std::copy(t0,t0+3,t);
        ArmCalibration::ScaleAbout(r,t,pivot,scale);
        transform(r,t,palm,actualPalm); transform(r,t,tip,actualTip);
        for (int i=0;i<3;++i)
        {
            require(std::abs(actualPalm[i]-pivot[i])<1e-6f,"tracked palm remains fixed");
            require(std::abs(actualTip[i]-(pivot[i]+scale*(before[i]-pivot[i])))<1e-6f,"geometry scales about palm");
        }
    }
    require(ArmCalibration::HandScale(-1.f)==0.65f,"lower safety bound");
    require(ArmCalibration::HandScale(2.f)==1.15f,"upper safety bound");
    require(ArmCalibration::HandScale(std::numeric_limits<float>::quiet_NaN())==1.f,"invalid input restores native size");
    std::puts("Arm calibration invariants passed");
}
