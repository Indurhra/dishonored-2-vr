#include "pch.h"
#include "RenderRes.h"
#include "GameInfo.h"
#include "CVars.h"
#include "CameraHook.h"
#include "VRRuntime.h"
#include "Settings.h"
#include <detours/detours.h>
#include <cmath>
#include <algorithm>

namespace
{
    // Measured on 1.77.9.0 (docs/ENGINE.md, "Render resolution").
    constexpr uintptr_t kRetWndProc = 0xE9FF1;      // WndProc, WM_WINDOWPOSCHANGED: GetClientRect -> r_windowWidth/Height
    constexpr uintptr_t kRetHdcSize = 0x346364;     // GetHdcClientWindowSize: GetClientRect -> render targets, swapchain
    constexpr uintptr_t kRetCursor = 0xE27DF;       // the cursor for the interface: GetCursorPos + ScreenToClient
    constexpr uintptr_t kWriteConfig = 0x41770;     // WriteConfigToFile(common, name, int, mask, bool): every cfg write
    constexpr uintptr_t kPlaceWindow = 0x347630;    // the window's placement from r_windowWidth/Height (SetWindowPos)
    constexpr uintptr_t kDisplaySize = 0x32FAF80;   // int width, height: the engine's window size (render size follows)

    using GetClientRect_t = BOOL(WINAPI*)(HWND, LPRECT);
    using ScreenToClient_t = BOOL(WINAPI*)(HWND, LPPOINT);
    // FIVE arguments: the bool is on the stack (callers write [rsp+0x20], the function reads [rsp+0x60]). A hook with
    // four handed it garbage and the game wrote a config without its cvars.
    using WriteConfig_t = void(*)(void*, const char*, int, int, bool);
    using PlaceWindow_t = void(*)(void*);
    GetClientRect_t g_RealGCR = nullptr;
    ScreenToClient_t g_RealS2C = nullptr;
    WriteConfig_t g_RealWrite = nullptr;
    PlaceWindow_t g_RealPlace = nullptr;
    uintptr_t g_Base = 0;
    bool g_Installed = false;

    HWND g_Hwnd = nullptr;
    std::atomic<int> g_TW{ 0 }, g_TH{ 0 };          // the target in effect (0 = the window's own size)
    int g_WantW = 0, g_WantH = 0, g_WantFrames = 0; // a new target has to hold this many frames before it is applied
    std::atomic<int> g_ManualW{ 0 }, g_ManualH{ 0 }; // vr rendersize <w> <h>: fixed target (research)
    std::atomic<int> g_Mode{ 1 };                   // 1 = auto, 0 = off (the window's size, the old behaviour)
    std::atomic<float> g_Scale{ 1.f };
    std::atomic<bool> g_Reapply{ false };
    std::string g_Why = "starting";
    unsigned long long g_Applies = 0, g_Spoofed = 0, g_Buffers = 0, g_CfgGuards = 0, g_PlaceGuards = 0;
    int g_BufW = 0, g_BufH = 0;                     // the swapchain's buffers, last seen
    int g_RsSaved = -1;                             // rs_enable as the game had it before the mod held it off
    // Present rate, for measuring what the resolution costs
    LARGE_INTEGER g_FpsT{}; int g_FpsN = 0; float g_Fps = 0.f;

    BOOL WINAPI HkGetClientRect(HWND h, LPRECT r)
    {
        const BOOL ok = g_RealGCR(h, r);
        const int tw = g_TW.load(), th = g_TH.load();
        if (ok && r && tw > 0 && th > 0 && h == g_Hwnd)
        {
            const uintptr_t ra = (uintptr_t)_ReturnAddress() - g_Base;
            if (ra == kRetWndProc || ra == kRetHdcSize) { r->left = r->top = 0; r->right = tw; r->bottom = th; g_Spoofed++; }
        }
        return ok;
    }

    //! The interface lays itself out in the render size, so the cursor it reads is mapped from the window into it (the
    //! way the window shows the frame, scaled). Confining and centring the cursor keep the real window.
    BOOL WINAPI HkScreenToClient(HWND h, LPPOINT p)
    {
        const BOOL ok = g_RealS2C(h, p);
        const int tw = g_TW.load(), th = g_TH.load();
        int rw = 0, rh = 0;
        if (ok && p && tw > 0 && th > 0 && h == g_Hwnd && (uintptr_t)_ReturnAddress() - g_Base == kRetCursor && RenderRes::RealClient(rw, rh))
        {
            p->x = (LONG)lround((double)p->x * tw / rw);
            p->y = (LONG)lround((double)p->y * th / rh);
        }
        return ok;
    }

    //! A window-size cvar's live value (int, float and string) replaced without the engine's modified flags, so nothing
    //! reacts to it: only what reads it in between sees it. The string is the inline buffer of the idStr (CVars.cpp).
    struct CvarInt
    {
        uintptr_t a = 0; int i = 0; float f = 0; char s[16] = {}; int len = 0; bool ok = false;
        static bool Writable(uintptr_t a) { return a && *(uintptr_t*)a == a + 0x10 && *(uint32_t*)(a + 0x0C) >= 16; }
        void Save(const char* name)
        {
            a = CVars::Address(name); ok = Writable(a);
            if (!ok) return;
            i = *(int*)(a + 0x28); f = *(float*)(a + 0x2C); len = *(int*)(a + 0x08); memcpy(s, (void*)(a + 0x10), 16);
        }
        void Put(int v) const
        {
            if (!ok) return;
            *(int*)(a + 0x28) = v; *(float*)(a + 0x2C) = (float)v;
            char b[16]; const int n = snprintf(b, sizeof b, "%d", v);
            memcpy((void*)(a + 0x10), b, (size_t)n + 1); *(int*)(a + 0x08) = n;
        }
        void Restore() const
        {
            if (!ok) return;
            *(int*)(a + 0x28) = i; *(float*)(a + 0x2C) = f; memcpy((void*)(a + 0x10), s, 16); *(int*)(a + 0x08) = len;
        }
    };

    //! While the target is in effect, the window-size cvars hold it (the engine put it there). For the length of `fn`
    //! they hold the real window instead -- unless they hold something else, i.e. the player just picked a window size
    //! in the options, which is theirs to keep.
    template <class F> bool WithRealWindow(F&& fn)
    {
        const int tw = g_TW.load(), th = g_TH.load();
        int rw = 0, rh = 0;
        CvarInt cw, ch; cw.Save("r_windowWidth"); ch.Save("r_windowHeight");
        const bool swap = tw > 0 && cw.ok && ch.ok && cw.i == tw && ch.i == th && RenderRes::RealClient(rw, rh);
        int* ds = (int*)(g_Base + kDisplaySize);
        const int dw = ds[0], dh = ds[1];
        if (swap) { cw.Put(rw); ch.Put(rh); ds[0] = rw; ds[1] = rh; }
        fn();
        if (swap) { cw.Restore(); ch.Restore(); ds[0] = dw; ds[1] = dh; }
        return swap;
    }

    void HkWriteConfig(void* common, const char* name, int a, int mask, bool b)
    {
        if (WithRealWindow([&] { g_RealWrite(common, name, a, mask, b); }))
        {
            g_CfgGuards++;
            LOGI("RenderRes: {} written with the real window size", name ? name : "config");
        }
    }

    void HkPlaceWindow(void* display)
    {
        if (WithRealWindow([&] { g_RealPlace(display); }))
        {
            g_PlaceGuards++;
            LOGI("RenderRes: window placed at the real window size (the engine renders at {}x{})", g_TW.load(), g_TH.load());
        }
    }

    //! The size the flat frame needs: each eye's pixel density (the runtime's recommended size over its fov, linear in
    //! tan like the eye buffer) times the tan extent the frame covers (XRSession draws it there at its true angles).
    bool ComputeTarget(int& w, int& h, std::string& why)
    {
        if (g_Mode.load() == 0) { why = "off"; return false; }
        const int mw = g_ManualW.load(), mh = g_ManualH.load();
        if (mw > 0 && mh > 0) { w = mw; h = mh; why = "manual"; return true; }
        const VRRuntime::Status st = VRRuntime::GetStatus();
        if (!st.running || !st.eyeW || !st.eyeH) { why = "no headset session"; return false; }
        // Windowed only. Borderless (2) is a window too, but the engine takes the MONITOR's size for its render there
        // whatever GetClientRect says: measured 2026-10-16, buffers 2266x2348 with the engine at 1920x1080 = a torn picture.
        if (CVars::GetInt("r_fullscreen", 0) != 0) { why = "game in fullscreen or borderless mode (windowed needed)"; return false; }
        float dx = 0.f, dy = 0.f;
        for (int e = 0; e < 2; e++)
        {
            const VRRuntime::Placement pl = VRRuntime::GetPlacement(e);
            if (!pl.valid) continue;
            const float tw = tanf(pl.fov[1]) - tanf(pl.fov[0]), th = tanf(pl.fov[2]) - tanf(pl.fov[3]);
            if (tw > 0.1f) dx = std::max(dx, st.eyeW / tw);
            if (th > 0.1f) dy = std::max(dy, st.eyeH / th);
        }
        if (dx <= 0.f || dy <= 0.f) { why = "eye fov not known yet"; return false; }
        const float s = std::clamp(g_Scale.load(), 0.25f, 2.f);
        float ox = 0.f, oy = 0.f; CameraHook::FovOverride(ox, oy);
        float fw, fh;
        if (ox > 1.f && oy > 1.f)
        {   // the mod's render fov (vr fov auto: both eyes' fov): any aspect, so both axes at the eye's density
            fw = dx * 2.f * tanf(ox * 0.5f / 57.29578f) * s;
            fh = dy * 2.f * tanf(oy * 0.5f / 57.29578f) * s;
            why = "headset";
        }
        else
        {   // the game's own fov follows the window's aspect: keep the aspect, the horizontal at the eye's density. Only
            // ever larger than the window: a menu's camera can be narrow (38 deg at the title) and its 2D interface is
            // laid out in pixels, so a smaller frame would only blur the text.
            float gx = 0.f, gy = 0.f; unsigned long long n = 0; CameraHook::BuiltFov(gx, gy, n);
            int rw = 0, rh = 0;
            if (gx < 1.f || !RenderRes::RealClient(rw, rh)) { why = "render fov not known yet"; return false; }
            fw = dx * 2.f * tanf(gx * 0.5f / 57.29578f) * s;
            if (fw <= (float)rw) { why = "the window is enough"; return false; }
            fh = fw * (float)rh / (float)rw;
            why = "headset, game fov";
        }
        // A budget: SteamVR's automatic supersampling on a strong GPU, a wide headset (Pimax) or a high setting in the
        // headset's app can ask for 4000+ px per eye, and the game's whole set of render targets follows this size (an
        // 8192x8192 frame is ~67 MP of G-buffers: out of video memory). At most 4096 a side and 16 MP, the aspect kept;
        // the eye swapchain holds this frame, and 4096 is what every runtime measured so far allows.
        constexpr float kMaxSide = 4096.f, kMaxPixels = 16.0e6f;
        float k = std::min({ 1.f, kMaxSide / fw, kMaxSide / fh, sqrtf(kMaxPixels / (fw * fh)) });
        if (k < 1.f)
        {
            static int lastW = 0;
            if (lastW != (int)fw) { lastW = (int)fw; LOGI("RenderRes: the headset asks for {}x{}: capped to {}x{} (at most 4096 a side, 16 MP)", (int)fw, (int)fh, (int)(fw * k), (int)(fh * k)); }
            fw *= k; fh *= k;
        }
        w = std::clamp((int)lroundf(fw), 320, 4096);
        h = std::clamp((int)lroundf(fh), 240, 4096);
        return true;
    }

    //! The engine re-reads its window size: SWP_FRAMECHANGED sends WM_WINDOWPOSCHANGED without moving the window.
    void Trigger()
    {
        SetWindowPos(g_Hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }

    //! The game's dynamic resolution aims at the MONITOR's refresh rate and dropped to 85% under the headset's pacing.
    //! Only the live int is written (no modified flag, the string untouched): nothing of it reaches the config.
    void HoldDynamicResolution(bool hold)
    {
        const uintptr_t a = CVars::Address("rs_enable");
        if (!a) return;
        const int v = *(int*)(a + 0x28);
        if (hold)
        {
            if (v != 0) { if (g_RsSaved < 0) g_RsSaved = v; CVars::SetInt("rs_enable", 0); LOGI("RenderRes: the game's dynamic resolution held off (rs_enable was {})", v); }
        }
        else if (g_RsSaved >= 0)
        {
            if (v == 0) CVars::SetInt("rs_enable", g_RsSaved);
            LOGI("RenderRes: the game's dynamic resolution back (rs_enable {})", CVars::GetInt("rs_enable", -1));
            g_RsSaved = -1;
        }
    }
}

void RenderRes::Install()
{
    if (!GameInfo::IsSupportedBuild()) { LOGI("RenderRes: unsupported build, not installed"); return; }
    g_Base = GameInfo::Base();
    g_Scale = Settings::RenderScale();
    if (g_Scale.load() <= 0.f) g_Mode = 0;
    g_RealGCR = (GetClientRect_t)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetClientRect");
    g_RealS2C = (ScreenToClient_t)GetProcAddress(GetModuleHandleW(L"user32.dll"), "ScreenToClient");
    g_RealWrite = (WriteConfig_t)(g_Base + kWriteConfig);
    g_RealPlace = (PlaceWindow_t)(g_Base + kPlaceWindow);
    DetourTransactionBegin(); DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)g_RealGCR, HkGetClientRect);
    DetourAttach(&(PVOID&)g_RealS2C, HkScreenToClient);
    DetourAttach(&(PVOID&)g_RealWrite, HkWriteConfig);
    DetourAttach(&(PVOID&)g_RealPlace, HkPlaceWindow);
    const LONG r = DetourTransactionCommit();
    g_Installed = r == NO_ERROR;
    LOGI("RenderRes: GetClientRect + ScreenToClient + WriteConfigToFile@exe+0x{:X} + window placement@exe+0x{:X} detour={} (render scale {:.2f}{})",
         kWriteConfig, kPlaceWindow, r, g_Scale.load(), g_Mode.load() ? "" : ", off: the window's size");
}

bool RenderRes::RealClient(int& w, int& h)
{
    RECT c{};
    if (!g_Hwnd || !(g_RealGCR ? g_RealGCR(g_Hwnd, &c) : GetClientRect(g_Hwnd, &c))) return false;
    w = c.right - c.left; h = c.bottom - c.top;
    return w > 0 && h > 0;
}

void RenderRes::OnResizeBuffers(IDXGISwapChain*, UINT& w, UINT& h)
{
    const int tw = g_TW.load(), th = g_TH.load();
    if (tw > 0 && th > 0)
    {
        LOGI("RenderRes: swapchain buffers {}x{} (asked {}x{})", tw, th, w, h);
        w = (UINT)tw; h = (UINT)th; g_Buffers++;
    }
}

void RenderRes::OnPresent(IDXGISwapChain* sc)
{
    if (!g_Installed) return;
    if (!g_Hwnd) { DXGI_SWAP_CHAIN_DESC d{}; if (SUCCEEDED(sc->GetDesc(&d))) g_Hwnd = d.OutputWindow; if (!g_Hwnd) return; }
    {
        LARGE_INTEGER now, f; QueryPerformanceCounter(&now); QueryPerformanceFrequency(&f);
        if (!g_FpsT.QuadPart) g_FpsT = now;
        g_FpsN++;
        const double dt = double(now.QuadPart - g_FpsT.QuadPart) / f.QuadPart;
        if (dt >= 1.0) { g_Fps = (float)(g_FpsN / dt); g_FpsN = 0; g_FpsT = now; }
        DXGI_SWAP_CHAIN_DESC d{};
        if (SUCCEEDED(sc->GetDesc(&d))) { g_BufW = (int)d.BufferDesc.Width; g_BufH = (int)d.BufferDesc.Height; }
    }

    int w = 0, h = 0; std::string why;
    if (!ComputeTarget(w, h, why)) w = h = 0;
    g_Why = why;

    // A changed target is applied once it has held for 45 frames (the fov override and the eye fov arrive a few frames
    // apart when the package switches on); a vanished one at once. Changes under 1% are not worth a resize.
    const int cw = g_TW.load(), ch = g_TH.load();
    const bool close = cw > 0 && w > 0 && std::abs(w - cw) <= cw / 100 && std::abs(h - ch) <= ch / 100;
    if ((w == cw && h == ch) || close) g_WantFrames = 0;
    else
    {
        if (w != g_WantW || h != g_WantH) { g_WantW = w; g_WantH = h; g_WantFrames = 0; }
        if (w == 0 || ++g_WantFrames >= 45)
        {
            int rw = 0, rh = 0; RealClient(rw, rh);
            g_TW = w; g_TH = h; g_WantFrames = 0; g_Applies++;
            if (w > 0) LOGI("RenderRes: the game renders at {}x{} ({}; window {}x{}, scale {:.2f})", w, h, why, rw, rh, g_Scale.load());
            else LOGI("RenderRes: the game renders at its window's size {}x{} ({})", rw, rh, why);
            g_Reapply = true;
        }
    }
    // held off whenever the headset shows the game, with or without a target: with the game's own fov the window can
    // already be dense enough (no target), and the 85% would still be there
    HoldDynamicResolution(g_Mode.load() != 0 && VRRuntime::GetStatus().running);
    if (g_Reapply.exchange(false)) Trigger();
}

std::string RenderRes::Status()
{
    int rw = 0, rh = 0; RealClient(rw, rh);
    const int* ds = g_Base ? (const int*)(g_Base + kDisplaySize) : nullptr;
    return fmt::format("renderres target {}x{} ({}) engine {}x{} buffers {}x{} window {}x{} scale {:.2f} rs {} fps {:.1f} applies {} spoofed {} bufs {} cfgguard {} placeguard {}",
                       g_TW.load(), g_TH.load(), g_Why, ds ? ds[0] : 0, ds ? ds[1] : 0, g_BufW, g_BufH, rw, rh, g_Scale.load(),
                       CVars::GetInt("rs_enable", -1), g_Fps, g_Applies, g_Spoofed, g_Buffers, g_CfgGuards, g_PlaceGuards);
}

bool RenderRes::Command(const std::string& what, std::istringstream& ss)
{
    if (what == "rendersize")
    {   // vr rendersize auto | off | <w> <h>
        std::string a; ss >> a; int h = 0; ss >> h;
        if (a == "off" || a == "0") { g_Mode = 0; g_ManualW = 0; g_ManualH = 0; }
        else if (a == "auto" || a.empty()) { g_Mode = 1; g_ManualW = 0; g_ManualH = 0; }
        else { g_Mode = 1; g_ManualW = atoi(a.c_str()); g_ManualH = h; }
        LOGI("cmd vr rendersize {} {} -> mode {} manual {}x{}", a, h, g_Mode.load(), g_ManualW.load(), g_ManualH.load());
        return true;
    }
    if (what == "renderscale")
    {   // vr renderscale <s>: the target per axis relative to the eye's pixel density (0 = off: the window's size)
        float s = 1.f; ss >> s;
        if (s <= 0.f) g_Mode = 0; else { g_Mode = 1; g_Scale = s; }
        LOGI("cmd vr renderscale {} -> mode {} scale {:.2f}", s, g_Mode.load(), g_Scale.load());
        return true;
    }
    if (what == "renderres") { LOGI("{}", Status()); return true; }
    return false;
}
