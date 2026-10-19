#include "pch.h"
#include "CameraHook.h"
#include "GameInfo.h"
#include "VRRuntime.h"
#include "ScreenMode.h"
#include <detours/detours.h>
#include <cmath>

namespace
{
    using Fn = void* (*)(void*, void*, void*, void*);
    Fn g_Real = nullptr;
    std::atomic<int> g_Mode{ 1 };
    std::atomic<float> g_WorldScale{ 1.0f };
    std::atomic<bool> g_RecenterReq{ true };
    float g_RefPos[3] = {};          //!< head position at recenter (XR local space)
    bool g_RefValid = false;
    std::mutex g_StatsMutex;
    CameraHook::Stats g_Stats;

    // ComputeMatrices runs several times per frame on the same view object and the
    // game does not rewrite m_renderRV in between, so we must always derive from the
    // game's ORIGINAL values, never from what we wrote last time.
    struct Baseline { void* view = nullptr; float org[3], axis[9], wOrg[3], wAxis[9]; bool valid = false; } g_Base;
    extern std::atomic<unsigned long long> g_Refreshes;
    extern std::atomic<void*> g_MainView;

    void GetBaseline(void* view, float* org, float* axis, float baseOrg[3], float baseAxis[9])
    {
        if (g_Base.valid && g_Base.view == view &&
            memcmp(org, g_Base.wOrg, 12) == 0 && memcmp(axis, g_Base.wAxis, 36) == 0)
        {   // untouched since our last write -> the game hasn't refreshed it; keep the original
            memcpy(baseOrg, g_Base.org, 12); memcpy(baseAxis, g_Base.axis, 36);
        }
        else
        {   // fresh game values
            memcpy(baseOrg, org, 12); memcpy(baseAxis, axis, 36);
            g_Base.view = view; memcpy(g_Base.org, org, 12); memcpy(g_Base.axis, axis, 36); g_Base.valid = true;
            g_Refreshes++;
        }
    }
    std::atomic<unsigned long long> g_Refreshes{ 0 };
    std::atomic<void*> g_MainView{ nullptr };
    void RememberWritten(const float* org, const float* axis) { memcpy(g_Base.wOrg, org, 12); memcpy(g_Base.wAxis, axis, 36); }

    // --- math helpers -------------------------------------------------------
    // OpenXR: +X right, +Y up, -Z forward.   id Tech: +X forward, +Y left, +Z up.
    inline void XrToId(const float v[3], float out[3]) { out[0] = -v[2]; out[1] = -v[0]; out[2] = v[1]; }

    //! Rotates unit basis vectors by quaternion (x,y,z,w): returns fwd(-Z), right(+X), up(+Y) in XR space.
    void QuatBasis(const float q[4], float fwd[3], float right[3], float up[3])
    {
        float x = q[0], y = q[1], z = q[2], w = q[3];
        // rotation matrix columns
        float m00 = 1 - 2 * (y * y + z * z), m01 = 2 * (x * y - z * w), m02 = 2 * (x * z + y * w);
        float m10 = 2 * (x * y + z * w), m11 = 1 - 2 * (x * x + z * z), m12 = 2 * (y * z - x * w);
        float m20 = 2 * (x * z - y * w), m21 = 2 * (y * z + x * w), m22 = 1 - 2 * (x * x + y * y);
        right[0] = m00; right[1] = m10; right[2] = m20;   // R * (1,0,0)
        up[0] = m01;    up[1] = m11;    up[2] = m21;      // R * (0,1,0)
        fwd[0] = -m02;  fwd[1] = -m12;  fwd[2] = -m22;    // R * (0,0,-1)
    }

    void* Hook(void* a, void* view, void* c, void* d)
    {
        // Signature: ComputeMatrices(constantBlock* rcx, RenderView* rdx) — the view object is the 2nd arg.
        uint8_t* rv = (uint8_t*)view + CameraHook::kRenderRV;
        float* org = (float*)(rv + CameraHook::kRV_ViewOrg);
        float* axis = (float*)(rv + CameraHook::kRV_ViewAxis);
        int w = *(int*)((uint8_t*)view + CameraHook::kView_RenderW);
        int h = *(int*)((uint8_t*)view + CameraHook::kView_RenderH);
        // Main view heuristic: the largest render target (UI/portal/shadow views are smaller).
        bool main = w >= 1024 && h >= 576;
        if (main) g_MainView.store(view);

        // Experiments: 3 = halve the FOV, 4 = forceIdentityViewMatrix. If these don't alter the
        // image either, this object does not feed the scene pass.
        if (main && g_Mode.load() == 3) { *(float*)(rv + CameraHook::kRV_FovX) *= 0.5f; *(float*)(rv + CameraHook::kRV_FovY) *= 0.5f; }
        if (main && g_Mode.load() == 4) { *(uint8_t*)(rv + 0x15C) = 1; }
        if (main && (g_Mode.load() == 1 || g_Mode.load() == 2))
        {
            VRRuntime::HeadPose hp = VRRuntime::GetHeadPose();
            if (hp.valid)
            {
                if (g_RecenterReq.exchange(false) || !g_RefValid) { memcpy(g_RefPos, hp.pos, 12); g_RefValid = true; }

                float bOrg[3], bAxis[9];
                GetBaseline(view, org, axis, bOrg, bAxis);

                // Body frame = the game's yaw (mouse/controller), flattened.
                float yaw = atan2f(bAxis[1], bAxis[0]);
                float cy = cosf(yaw), sy = sinf(yaw);
                auto toWorld = [&](const float l[3], float out[3]) {   // local (x fwd, y left, z up) -> world
                    out[0] = l[0] * cy - l[1] * sy;
                    out[1] = l[0] * sy + l[1] * cy;
                    out[2] = l[2];
                };

                float fx[3], rx[3], ux[3]; QuatBasis(hp.quat, fx, rx, ux);
                float fl[3], rl[3], ul[3]; XrToId(fx, fl); XrToId(rx, rl); XrToId(ux, ul);
                float fw[3], rw[3], uw[3]; toWorld(fl, fw); toWorld(rl, rw); toWorld(ul, uw);
                axis[0] = fw[0]; axis[1] = fw[1]; axis[2] = fw[2];          // forward
                axis[3] = -rw[0]; axis[4] = -rw[1]; axis[5] = -rw[2];       // left = -right
                axis[6] = uw[0]; axis[7] = uw[1]; axis[8] = uw[2];          // up

                float dxr[3] = { hp.pos[0] - g_RefPos[0], hp.pos[1] - g_RefPos[1], hp.pos[2] - g_RefPos[2] };
                float dl[3]; XrToId(dxr, dl);
                float dw[3]; toWorld(dl, dw);
                float s = g_WorldScale.load();
                org[0] = bOrg[0] + dw[0] * s; org[1] = bOrg[1] + dw[1] * s; org[2] = bOrg[2] + dw[2] * s;
                RememberWritten(org, axis);
                if (g_Mode.load() == 2)
                {   // experiment: also push into m_gameRV (+0) of the same object
                    memcpy((uint8_t*)view + CameraHook::kRV_ViewOrg, org, 12);
                    memcpy((uint8_t*)view + CameraHook::kRV_ViewAxis, axis, 36);
                }

                std::lock_guard lk(g_StatsMutex); g_Stats.applied++;
            }
        }
        {
            std::lock_guard lk(g_StatsMutex);
            g_Stats.calls++;
            if (main)
            {
                g_Stats.mainViewCalls++;
                memcpy(g_Stats.lastOrg, org, 12); memcpy(g_Stats.lastFwd, axis, 12);
                g_Stats.lastFovX = *(float*)(rv + CameraHook::kRV_FovX);
                g_Stats.lastFovY = *(float*)(rv + CameraHook::kRV_FovY);
                g_Stats.lastW = w; g_Stats.lastH = h;
                // Tell the flat-frame layer the real game FOV so the image lands at its true angular size.
                if (g_Stats.lastFovX > 0.1f && g_Stats.lastFovY > 0.1f)
                    VRRuntime::SetGameFov(g_Stats.lastFovX * 57.29578f, g_Stats.lastFovY * 57.29578f);
            }
        }
        void* ret = g_Real(a, view, c, d);
        return ret;
    }

    // ---- the projection (see kBuildProjection) ----
    // The game renders a 95.4 x 58.7 deg window (16:9-ish backbuffer); a Quest 3 eye sees 94 x 99 deg, from 44 up to
    // 55 down. A relaxed hand sits 30-50 deg below the gaze, i.e. OUTSIDE the game's image (measured with the mock
    // compositor, BodyView): the arms were "almost never seen". The projection is the one place to change that.
    using ProjFn = void (*)(void* rv, float* out, float a, float b, void* e, int f, int g);
    ProjFn g_RealProj = nullptr;
    std::atomic<float> g_FovOvX{ 0.f }, g_FovOvY{ 0.f };
    std::atomic<float> g_GameFovX{ 0.f }, g_GameFovY{ 0.f }, g_BuiltFovX{ 0.f }, g_BuiltFovY{ 0.f };
    std::atomic<unsigned long long> g_FovOverrides{ 0 }, g_FlatWidened{ 0 }, g_ExplicitBuilds{ 0 };
    void ProjHook(void* rv, float* out, float a, float b, void* e, int f, int g)
    {
        uint8_t* mv = (uint8_t*)g_MainView.load();
        if (mv && rv == mv + CameraHook::kRenderRV && *((uint8_t*)rv + CameraHook::kRV_UseExplicit)) g_ExplicitBuilds++;
        if (mv && rv == mv + CameraHook::kRenderRV && !*((uint8_t*)rv + CameraHook::kRV_UseExplicit))
        {
            float* fx = (float*)((uint8_t*)rv + CameraHook::kRV_FovX);
            float* fy = (float*)((uint8_t*)rv + CameraHook::kRV_FovY);
            g_GameFovX = *fx * 57.29578f; g_GameFovY = *fy * 57.29578f;
            const float ox = g_FovOvX.load(), oy = g_FovOvY.load();
            // On the menu screen (ScreenMode) the picture is the flat game's: its own fov (the title screen's camera is
            // a 19-degree shot; at the headset's 108 degrees it was a wide view of a set). The game's fov follows the
            // render size's aspect from its horizontal, so the 16:9 band shown is exactly the flat game's 16:9 view.
            if (ox > 1.f && oy > 1.f && ox < 170.f && oy < 170.f && !ScreenMode::FlatRender())
            {   // written into the view's own renderView, not just the matrix: whatever reads the fov later this frame
                // (culling, the shader parms) sees the same frustum
                *fx = ox / 57.29578f; *fy = oy / 57.29578f;
                g_FovOverrides++;
            }
            else if (ScreenMode::FlatRender())
            {   // The game keeps its VERTICAL fov whatever the aspect (Hor+; measured on the title: 38.2 x 20.1 deg at
                // 1902x973, 19.4 x 20.1 at 2266x2348), so in the headset's tall frame the 16:9 band the screen shows was
                // the flat game's view zoomed 1.78x. Widen it until that band spans the game's vertical fov: the band is
                // then exactly what a 16:9 monitor shows.
                // (relative, so not twice: a second build on the same values this frame finds our own numbers there)
                static float lastX = 0.f, lastY = 0.f;
                const float tx = tanf(*fx * 0.5f), ty = tanf(*fy * 0.5f), r = tx > 1e-4f ? ty / tx : 0.f;   // r = H / W
                if (r > 9.f / 16.f + 1e-3f && r < 4.f && !(*fx == lastX && *fy == lastY))
                {
                    const float nx = ty * 16.f / 9.f, ny = nx * r;
                    *fx = 2.f * atanf(nx); *fy = 2.f * atanf(ny);
                    lastX = *fx; lastY = *fy;
                    g_FlatWidened++;
                }
            }
            g_BuiltFovX = *fx * 57.29578f; g_BuiltFovY = *fy * 57.29578f;
        }
        g_RealProj(rv, out, a, b, e, f, g);
    }
}

void CameraHook::SetFovOverride(float x, float y) { g_FovOvX = x; g_FovOvY = y; LOGI("CameraHook: fov override {:.1f} x {:.1f} deg{}", x, y, x > 1.f ? "" : " (off: the game's own)"); }
void CameraHook::FovOverride(float& x, float& y) { x = g_FovOvX.load(); y = g_FovOvY.load(); }
void CameraHook::GameFov(float& x, float& y) { x = g_GameFovX.load(); y = g_GameFovY.load(); }
void CameraHook::BuiltFov(float& x, float& y, unsigned long long& n) { x = g_BuiltFovX.load(); y = g_BuiltFovY.load(); n = g_FovOverrides.load(); }
unsigned long long CameraHook::ExplicitBuilds() { return g_ExplicitBuilds.load(); }

bool CameraHook::Install()
{
    if (!GameInfo::IsSupportedBuild()) { LOGI("CameraHook: unsupported build, not installed"); return false; }
    g_Real = (Fn)(GameInfo::Base() + kComputeMatrices);
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)g_Real, Hook);
    g_RealProj = (ProjFn)(GameInfo::Base() + kBuildProjection);
    DetourAttach(&(PVOID&)g_RealProj, ProjHook);
    LONG r = DetourTransactionCommit();
    LOGI("CameraHook: ComputeMatrices@exe+0x{:X} + BuildProjection@exe+0x{:X} detour={}", kComputeMatrices, kBuildProjection, r);
    return r == NO_ERROR;
}

void CameraHook::Uninstall()
{
    if (!g_Real) return;
    DetourTransactionBegin(); DetourUpdateThread(GetCurrentThread());
    DetourDetach(&(PVOID&)g_Real, Hook);
    if (g_RealProj) DetourDetach(&(PVOID&)g_RealProj, ProjHook);
    DetourTransactionCommit();
}

void* CameraHook::MainView() { return g_MainView.load(); }
void CameraHook::SetMode(int m) { g_Mode = m; }
int  CameraHook::Mode() { return g_Mode; }
void CameraHook::Recenter() { g_RecenterReq = true; }
void CameraHook::SetWorldScale(float s) { g_WorldScale = s; }
CameraHook::Stats CameraHook::GetStats() { std::lock_guard lk(g_StatsMutex); auto s = g_Stats; s.refreshes = g_Refreshes.load(); return s; }
