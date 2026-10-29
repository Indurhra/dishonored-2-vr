#include "FrameProfile.h"
#include "pch.h"
#include <algorithm>
#include <cmath>
#include "Overlay.h"
#include "XRCapture.h"
#include "MenuHold.h"
#include "GameInfo.h"
#include "CVars.h"
#include "XRProbe.h"
#include "VRRuntime.h"
#include "Reticle.h"
#include "LiveWheel.h"
#include "RenderRes.h"
#include "Commands.h"
#include "StereoSync.h"
#include "StereoTaa.h"
#ifdef DVR_HAS_PDAFW
#include "AfwLive.h"
#include "DlssLive.h"
#include "Settings.h"
#endif
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace
{
    MenuHold g_MenuHold;
    std::atomic<unsigned> g_Nav{0};
    std::atomic<bool> g_ControllerFocus{false};
    unsigned g_NavLatch=0;
    bool g_Init = false;
    std::atomic<bool> g_Visible{ false };
    HWND g_Hwnd = nullptr;
    WNDPROC g_OrigWndProc = nullptr;
    ID3D11Device* g_Device = nullptr;
    ID3D11DeviceContext* g_Ctx = nullptr;
    ID3D11RenderTargetView* g_RTV = nullptr;
    ID3D11Texture2D* g_MenuTex=nullptr;
    ID3D11RenderTargetView* g_MenuRtv=nullptr;
    ID3D11ShaderResourceView* g_MenuSrv=nullptr;
    bool g_MenuReady=false;
    UINT g_W = 0, g_H = 0;
    double g_Fps = 0; LARGE_INTEGER g_LastT{}; int g_FrameCount = 0;

    // vr window: the window may be larger than the desktop (see Overlay::SetWindowClient)
    std::atomic<bool> g_BigWindow{ false };
    std::atomic<int> g_WantW{ 0 }, g_WantH{ 0 };
    RECT g_OrigRect{}; bool g_OrigSaved = false;

    LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l)
    {
        if (m == WM_GETMINMAXINFO && g_BigWindow.load())
        {
            const LRESULT r = CallWindowProcW(g_OrigWndProc, h, m, w, l);
            MINMAXINFO* mm = (MINMAXINFO*)l;
            mm->ptMaxTrackSize.x = mm->ptMaxTrackSize.y = 16384;
            return r;
        }
        // A lone Alt (or Alt+Space) puts a window into Windows' menu mode: a modal loop inside DefWindowProc on the
        // game's own thread, which stops the game until ESC or a click. Nothing in VR needs the window's system menu,
        // and the mod's own input can produce a lone Alt (a lean key, the focus fallback in KbmInject).
        if (m == WM_SYSCOMMAND && (w & 0xFFF0) == SC_KEYMENU) return 0;
        if (m == WM_KEYDOWN && w == VK_INSERT) { g_Visible = !g_Visible; return 0; }
        if (g_Visible)
        {
            ImGui_ImplWin32_WndProcHandler(h, m, w, l);
            switch (m)
            {
            case WM_MOUSEMOVE: case WM_LBUTTONDOWN: case WM_RBUTTONDOWN:
            case WM_MBUTTONDOWN: case WM_MOUSEWHEEL: case WM_KEYDOWN:
            case WM_CHAR: case WM_SYSKEYDOWN: case WM_SYSKEYUP: case WM_INPUT:
                return 0; // swallow while the overlay owns input
            }
        }
        return CallWindowProcW(g_OrigWndProc, h, m, w, l);
    }

    bool EnsureRTV(IDXGISwapChain* sc)
    {
        if (g_RTV) return true;
        ID3D11Texture2D* bb = nullptr;
        if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb))) return false;
        D3D11_TEXTURE2D_DESC d; bb->GetDesc(&d); g_W = d.Width; g_H = d.Height;
        D3D11_RENDER_TARGET_VIEW_DESC rd{};
        // typeless backbuffers need an explicit view format.
        rd.Format = d.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS ? DXGI_FORMAT_R8G8B8A8_UNORM :
                    d.Format == DXGI_FORMAT_B8G8R8A8_TYPELESS ? DXGI_FORMAT_B8G8R8A8_UNORM : d.Format;
        rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        HRESULT hr = g_Device->CreateRenderTargetView(bb, &rd, &g_RTV);
        bb->Release();
        if (FAILED(hr)) LOGI("Overlay: CreateRTV failed 0x{:08X} fmt={}", (unsigned)hr, (int)d.Format);
        else LOGI("Overlay: backbuffer {}x{} fmt={}", g_W, g_H, (int)d.Format);
        return SUCCEEDED(hr);
    }

    bool Init(IDXGISwapChain* sc)
    {
        if (FAILED(sc->GetDevice(__uuidof(ID3D11Device), (void**)&g_Device))) return false;
        g_Device->GetImmediateContext(&g_Ctx);
        DXGI_SWAP_CHAIN_DESC sd; sc->GetDesc(&sd);
        g_Hwnd = sd.OutputWindow;
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui_ImplWin32_Init(g_Hwnd);
        ImGui_ImplDX11_Init(g_Device, g_Ctx);
        LiveWheel::Prepare(g_Device);
        g_OrigWndProc = (WNDPROC)SetWindowLongPtrW(g_Hwnd, GWLP_WNDPROC, (LONG_PTR)WndProc);
        QueryPerformanceCounter(&g_LastT);
        if (GetEnvironmentVariableW(L"DVR_OVERLAY_AUTOSHOW", nullptr, 0)) g_Visible = true; // headless testing
        LOGI("Overlay: initialized on hwnd={} (INSERT toggles)", (void*)g_Hwnd);
        return true;
    }

    void DrawUI()
    {
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImVec2(0,0), ImGuiCond_Always);
        if (g_ControllerFocus.exchange(false)) {
            ImGui::SetNextWindowFocus();

        }
        ImGui::Begin("DishonoredVR", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
        ImGui::SetWindowFontScale(1.5f);
        ImGui::TextUnformatted("Sticks: navigate | A: confirm | B: close | Hold X 3s: open");
        auto st = VRRuntime::GetStatus();
        if (!st.initialized) ImGui::TextWrapped("Headset session: %s", st.error.c_str());
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen))
        {
#ifdef DVR_HAS_PDAFW
            static std::string saveMessage;
            const int active = AfwLive::Enabled() ? 1 : StereoSync::Active() ? 0 : VRRuntime::Stereo() ? 3 : 2;
            static int renderer = active;
            static int quality = DlssLive::Quality();
            static bool pending = false;
            if (!pending) { renderer = active; quality = DlssLive::Quality(); }
            pending |= ImGui::Combo("Renderer", &renderer, "Native Stereo\0AFW + DLSS (experimental)\0Mono (no depth)\0Alternating eyes (AER)\0");
            ImGui::SetItemDefaultFocus();
            if (renderer == 1)
                pending |= ImGui::Combo("DLSS quality", &quality, "DLAA\0Quality\0Balanced\0Performance\0Ultra Performance\0");
            const char* names[] = {"Native Stereo", "AFW + DLSS", "Mono", "Alternating eyes (AER)"};
            ImGui::Text("Active: %s", names[active]);
            if (ImGui::Button("Apply renderer")) {
                const int mode = renderer == 1 ? quality + 1 : renderer == 2 ? 6 : renderer == 3 ? 7 : 0;
                Commands::Queue(fmt::format("vr afw {}", mode <= 5 ? mode : 0));
                if (mode >= 6) Commands::Queue(mode == 6 ? "vr stereo 0" : "vr stereo 1");
                saveMessage = Settings::SaveRenderer(mode) ? "Choice queued and saved; active mode is shown above." : "Choice queued for this session; settings could not be saved.";
                pending = false;
            }
            if (pending) ImGui::TextWrapped("Selection not applied. Choose Apply renderer to switch.");
            if (!saveMessage.empty()) ImGui::TextWrapped("%s", saveMessage.c_str());
            if (renderer == 1) ImGui::TextWrapped("Close this menu to activate AFW reconstruction.");
            ImGui::TextWrapped("%s", AfwLive::Message().c_str());
            ImGui::TextWrapped("Native renders both eyes at the same moment. Mono has no stereo depth. AER updates one eye per game frame; motion can appear doubled. AFW reconstructs the other eye.");
#else
            int mode = StereoSync::Active() ? 1 : VRRuntime::Stereo() ? 2 : 0;
            if (ImGui::Combo("Stereo mode", &mode, "Mono (no depth)\0Synchronized stereo\0Alternating eyes (AER)\0"))
                Commands::Queue(fmt::format("vr stereo {}", mode == 1 ? 2 : mode == 2 ? 1 : 0));
            ImGui::TextWrapped("Synchronized: both eyes see the same moment. AER: one new eye per game frame; moving objects can appear doubled.");
            int aa = StereoTaa::Mode() == 0 ? 0 : 1;
            if (ImGui::Combo("Stereo anti-aliasing", &aa, "Off (clarity comparison)\0Per-eye temporal AA\0"))
                Commands::Queue(fmt::format("vr stereotaa {}", aa));
            if (StereoTaa::FellBack())
            {
                ImGui::TextWrapped("Temporal AA disabled by safety check: eye identification failed.");
                if (ImGui::Button("Retry per-eye AA")) Commands::Queue("vr stereotaa 1");
            }
            ImGui::TextWrapped("Compare the same stationary scene, then move your head. Changes apply this session only; restart restores your configured stereo mode.");
#endif
        }
        if (ImGui::CollapsingHeader("CVars", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (!CVars::Available()) ImGui::TextDisabled("static cvar offsets disabled");
            for (auto& e : CVars::Known())
            {
                int v = CVars::GetInt(e.name);
                ImGui::PushID(e.name);
                ImGui::SetNextItemWidth(120);
                if (ImGui::InputInt(e.name, &v, 1, 10, ImGuiInputTextFlags_EnterReturnsTrue)) CVars::SetInt(e.name, v);
                ImGui::SameLine(); ImGui::TextDisabled("f=%.3f str=\"%s\"", CVars::GetFloat(e.name), CVars::GetString(e.name).c_str());
                ImGui::PopID();
            }
        }
        if (ImGui::CollapsingHeader("Log", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::BeginChild("log", ImVec2(0, 200), true);
            for (auto& l : Log::Tail(60)) ImGui::TextUnformatted(l.c_str());
            ImGui::SetScrollHereY(1.0f);
            ImGui::EndChild();
        }
        ImGui::End();
    }
}

void Overlay::SetWindowClient(int w, int h) { g_WantW = w; g_WantH = h < 0 ? 0 : h; if (w == 0) g_WantW = -1; }

std::string Overlay::WindowInfo()
{
    RECT c{}, r{}; if (g_Hwnd) { GetClientRect(g_Hwnd, &c); GetWindowRect(g_Hwnd, &r); }
    return fmt::format("backbuffer {}x{} client {}x{} window {},{} {}x{} big {}", g_W, g_H, c.right, c.bottom, r.left, r.top,
                       r.right - r.left, r.bottom - r.top, g_BigWindow.load());
}

namespace
{
    //! On the window's own thread (Present runs on it): resize so the client area is w x h PHYSICAL pixels. The game is
    //! not DPI-aware, so the window API works in scaled units; the scale is the backbuffer over the client rect.
    void ApplyWindowSize()
    {
        const int w = g_WantW.exchange(0), h = g_WantH.exchange(0);
        if (!w || !g_Hwnd) return;
        if (!g_OrigSaved) { GetWindowRect(g_Hwnd, &g_OrigRect); g_OrigSaved = true; }
        if (w < 0)
        {
            g_BigWindow = false;
            SetWindowPos(g_Hwnd, nullptr, g_OrigRect.left, g_OrigRect.top, g_OrigRect.right - g_OrigRect.left, g_OrigRect.bottom - g_OrigRect.top, SWP_NOZORDER | SWP_NOACTIVATE);
            LOGI("vr window: back to {}x{}", g_OrigRect.right - g_OrigRect.left, g_OrigRect.bottom - g_OrigRect.top);
            return;
        }
        RECT c{}; GetClientRect(g_Hwnd, &c);
        const float scale = (c.right > 0 && g_W > 0) ? (float)g_W / (float)c.right : 1.f;
        RECT want = { 0, 0, (LONG)lroundf(w / scale), (LONG)lroundf(h / scale) };
        AdjustWindowRectEx(&want, (DWORD)GetWindowLongPtrW(g_Hwnd, GWL_STYLE), FALSE, (DWORD)GetWindowLongPtrW(g_Hwnd, GWL_EXSTYLE));
        RECT r{}; GetWindowRect(g_Hwnd, &r);
        g_BigWindow = true;
        // keep the top-left corner on the desktop: the window's centre is where KbmInject puts the cursor for a click
        SetWindowPos(g_Hwnd, nullptr, r.left < 0 ? 0 : r.left, 0, want.right - want.left, want.bottom - want.top, SWP_NOZORDER | SWP_NOACTIVATE);
        LOGI("vr window: client {}x{} px requested (dpi scale {:.2f}) -> {}", w, h, scale, Overlay::WindowInfo());
    }
}

void Overlay::OnPresent(IDXGISwapChain* sc)
{ DVR_PROFILE("Overlay.Overlay::OnPresent");
    if (!g_Init) { g_Init = Init(sc); if (!g_Init) return; }
    ApplyWindowSize();
    g_FrameCount++;
    LARGE_INTEGER now, f; QueryPerformanceCounter(&now); QueryPerformanceFrequency(&f);
    double dt = double(now.QuadPart - g_LastT.QuadPart) / f.QuadPart;
    if (dt >= 0.5) { g_Fps = g_FrameCount / dt; g_FrameCount = 0; g_LastT = now; }

    // The reticle rides on the same ImGui pass as the debug overlay, but it has to work with the overlay HIDDEN:
    // this is a gameplay aid, not a debug panel. Drawing here is what puts it in the headset -- OnPresent runs
    // before VRRuntime::OnPresent, so the backbuffer the mod submits already has it.
    Reticle::OnPresent();
    const bool reticle = Reticle::Mode() != 0 || Reticle::Axes() != 0;
    const bool wheel = LiveWheel::Active();
    if (!g_Visible && !reticle && !wheel) return;
    if (!EnsureRTV(sc)) return;
    if (wheel) LiveWheel::Render(g_Device, g_Ctx);
    ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame();
    {   // the backbuffer can be larger than the window (RenderRes): draw in its pixels, the mouse mapped into them
        ImGuiIO& io = ImGui::GetIO();
        int cw = 0, ch = 0;
        if (g_W && g_H && RenderRes::RealClient(cw, ch) && ((UINT)cw != g_W || (UINT)ch != g_H))
        {
            const float sx = (float)g_W / cw, sy = (float)g_H / ch;
            io.DisplaySize = ImVec2((float)g_W, (float)g_H);
            if (io.MousePos.x > -FLT_MAX) io.MousePos = ImVec2(io.MousePos.x * sx, io.MousePos.y * sy);
        }
    }
    {
        auto& io = ImGui::GetIO();
        // OpenXR owns navigation. Keyboard navigation avoids duplicate XInput gamepad events
        // from the Win32 backend (the game can expose a synthetic controller).
        const unsigned nav = g_Visible ? g_Nav.load() : 0;
        const ImGuiKey keys[] = {ImGuiKey_UpArrow, ImGuiKey_DownArrow,
            ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_Space};
        for(unsigned i=0;i<5;++i) io.AddKeyEvent(keys[i], (nav & (1u<<i))!=0);
    }
    const bool menu=g_Visible.load();
    if(menu) {
        auto& io=ImGui::GetIO();
        if(io.MousePos.x>-FLT_MAX && g_W && g_H) io.MousePos=ImVec2(io.MousePos.x*1024/g_W,io.MousePos.y*768/g_H);
        io.DisplaySize=ImVec2(1024,768);
        if(!g_MenuTex) {
            D3D11_TEXTURE2D_DESC d{};d.Width=1024;d.Height=768;d.MipLevels=d.ArraySize=1;
            d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
            if(SUCCEEDED(g_Device->CreateTexture2D(&d,nullptr,&g_MenuTex))) {
                g_Device->CreateRenderTargetView(g_MenuTex,nullptr,&g_MenuRtv);
                g_Device->CreateShaderResourceView(g_MenuTex,nullptr,&g_MenuSrv);
            }
        }
    }
    ImGui::NewFrame();
    if (menu) DrawUI();
    if (reticle && !menu) Reticle::Draw();
    ImGui::Render();
    g_MenuReady=menu && g_MenuRtv && g_MenuSrv;
    auto* target=g_MenuReady?g_MenuRtv:g_RTV;
    if(g_MenuReady) { const float clear[4]={0,0,0,0};g_Ctx->ClearRenderTargetView(target,clear); }
    g_Ctx->OMSetRenderTargets(1, &target, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void Overlay::OnResize(IDXGISwapChain*)
{
    if (g_RTV) { g_RTV->Release(); g_RTV = nullptr; }
}

void Overlay::Shutdown()
{
    if(g_MenuSrv){g_MenuSrv->Release();g_MenuSrv=nullptr;}
    if(g_MenuRtv){g_MenuRtv->Release();g_MenuRtv=nullptr;}
    if(g_MenuTex){g_MenuTex->Release();g_MenuTex=nullptr;}
    g_MenuReady=false;
    if (!g_Init) return;
    if (g_Hwnd && g_OrigWndProc) SetWindowLongPtrW(g_Hwnd, GWLP_WNDPROC, (LONG_PTR)g_OrigWndProc);
    ImGui_ImplDX11_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext();
    if (g_RTV) g_RTV->Release();
    if (g_Ctx) g_Ctx->Release();
    if (g_Device) g_Device->Release();
    g_Init = false;
}

bool Overlay::Visible() { return g_Visible; }
bool Overlay::CaptureMenu(const std::wstring& path) { return g_MenuReady && g_Visible && XRCapture::SaveTextureToPng(g_Device,g_Ctx,g_MenuTex,path.c_str()); }
HWND Overlay::Window() { return g_Hwnd; }

bool Overlay::ControllerInput(const XRInputState& in, double now)
{
    const bool wasVisible=g_Visible.load();
    const int action=g_MenuHold.Tick(now,(in.buttons & kXRBtnX)!=0,(in.buttons & kXRBtnB)!=0,wasVisible);
    if(action==1) {
        g_Visible=true; g_ControllerFocus=true; g_NavLatch=in.buttons;
        LOGI("Overlay: opened by X hold");
    }
    if(action==-1) { g_Visible=false; g_Nav=0; LOGI("Overlay: closed by B"); return true; }
    g_NavLatch &= in.buttons;
    unsigned nav=0;
    if(g_Visible) {
        const float x=fabsf(in.moveX)>fabsf(in.turnX)?in.moveX:in.turnX;
        const float y=fabsf(in.moveY)>fabsf(in.turnY)?in.moveY:in.turnY;
        if(fabsf(y)>=fabsf(x)) { if(y>0.55f) nav|=1; if(y< -0.55f) nav|=2; }
        else { if(x< -0.55f) nav|=4; if(x>0.55f) nav|=8; }
        if((in.buttons & ~g_NavLatch) & kXRBtnA) nav|=16;
    }
    g_Nav=nav;
    return g_Visible.load();
}
void Overlay::ControllerLost() { g_MenuHold.Lost(); g_Nav=0; }

void Overlay::Panel(XRSession& xr, float degrees, float distance)
{
    if(!Visible() || !g_MenuReady) return;
    XRPanelLayer l; l.srv=g_MenuSrv;l.srcW=1024;l.srcH=768;l.crop[2]=1024;l.crop[3]=768;
    l.premultiplied=true;l.alpha=1;l.headLocked=true;
    // Match the menu setting, with a conservative angular cap to keep every edge visible.
    const float size=std::clamp(degrees,30.f,62.f);
    l.pos[2]=-distance;l.width=2*distance*tanf(size*0.5f*0.01745329252f);l.height=l.width*0.75f;
    l.outW=1024;l.outH=768;xr.SetPanelLayer(2,l);
}
