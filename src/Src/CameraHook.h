#pragma once
#include <cstdint>

//! The render view: exe+0x3586A0 (named kComputeMatrices for history) only UPLOADS the view's matrices, already built
//! from m_renderRV (renderView_t at +0x39D0): rewriting vieworg/viewaxis there does not move the scene. The hook runs
//! as a pass-through that finds the main view and reads its fov. The head pose reaches the scene through
//! idRenderMatrix::CreateViewMatrix (exe+0x109CC0, ViewMatrixHook), and the fov override (`vr fov`) through the
//! projection builder below (exe+0x3742B0). See docs/ENGINE.md, "Rendering".
namespace CameraHook
{
    // RVAs / offsets (1.77.9.0) — see docs/ENGINE.md
    constexpr uint32_t kComputeMatrices = 0x3586A0;
    constexpr uint32_t kRenderRV        = 0x39D0;   //!< renderView_t m_renderRV inside the view object
    constexpr uint32_t kRV_FovX         = 0x9C;     //!< radians
    constexpr uint32_t kRV_FovY         = 0xA0;
    constexpr uint32_t kRV_ViewOrg      = 0xA8;
    constexpr uint32_t kRV_ViewAxis     = 0xCC;     //!< idMat3: rows fwd, left, up
    constexpr uint32_t kRV_ExplicitProj = 0x100;    //!< 4x4 floats
    constexpr uint32_t kRV_UseExplicit  = 0x140;    //!< bool
    constexpr uint32_t kView_RenderW    = 0x7848;
    constexpr uint32_t kView_RenderH    = 0x784C;

    //! The engine's projection builder: `BuildProjection(renderView_t* rv, float out[16], ...)` copies
    //! rv->explicitProjectionMatrix when rv->useExplicitProjectionMatrix, else builds it from rv->fov_x/fov_y (TAA
    //! jitter included). Called from the view setup at exe+0x375AE4, BEFORE the inverse projection and the
    //! view-projection are derived from it, so a fov changed here is consistent everywhere downstream.
    constexpr uint32_t kBuildProjection = 0x3742B0;

    bool Install();
    void Uninstall();

    //! The main view's fov, in degrees (full angles). 0/0 = the game's own. Takes effect on the next frame.
    void SetFovOverride(float xDeg, float yDeg);
    void FovOverride(float& xDeg, float& yDeg);
    //! The fov the GAME asked for on the last main-view projection, before any override (degrees).
    void GameFov(float& xDeg, float& yDeg);
    //! The fov the main view was actually built with last (after any override), and how many times it was overridden.
    void BuiltFov(float& xDeg, float& yDeg, unsigned long long& overrides);
    //! Projections of the main view built from an explicit matrix (the fov is not used): counted, research.
    unsigned long long ExplicitBuilds();

    //! 0 = passthrough (log only), 1 = HMD orientation replaces the game's
    //! pitch/roll on top of the game yaw + positional offset.
    void SetMode(int mode);
    int  Mode();
    //! Zeroes the positional offset at the current head position.
    void Recenter();
    void SetWorldScale(float metersToUnits);

    //! The main (scene) view object last seen by the hook, or null. Used by ViewMatrixHook.
    void* MainView();

    struct Stats
    {
        unsigned long long calls = 0, mainViewCalls = 0, applied = 0, refreshes = 0;
        float lastOrg[3] = {}, lastFwd[3] = {}, lastFovX = 0, lastFovY = 0;
        int lastW = 0, lastH = 0;
    };
    Stats GetStats();
}
