#include "pch.h"
#include "VRRuntime.h"
#include "XRSession.h"
#include "XRCapture.h"
#include "GameInfo.h"
#include "CameraHook.h"
#include "ViewMatrixHook.h"
#include "GpuTrace.h"
#include "PipeTrace.h"
#include "Settings.h"
#include "XRProbe.h"
#include "WristHud.h"
#include "KbmInject.h"
#include "ScreenMode.h"
#include <cmath>

namespace
{
    XRSession g_Session;
    bool g_InitAttempted = false;
    std::atomic<float> g_FovH{ 90.f }, g_FovV{ 0.f };  //!< 0 = derive from aspect
    std::mutex g_CapMutex;
    std::string g_CapTag;
    std::atomic<bool> g_CapRequested{ false };
    std::mutex g_StatusMutex;
    VRRuntime::Status g_Status;
    std::mutex g_PoseMutex;
    VRRuntime::HeadPose g_Head;
    XRInputState g_Input; bool g_InputValid = false;
    ULONGLONG g_InputAt = 0;   //!< GetTickCount64 of the last synced input (no frame = no fresh input)
    VRRuntime::HeadPose g_Eye[2];
    std::atomic<bool> g_Stereo{ false };
    std::atomic<int> g_CurEye{ 0 };
    VRRuntime::Placement g_Place[2];   // under g_StatusMutex
    std::atomic<bool> g_Ortho{ true };
    std::atomic<bool> g_TruePose{ true };
    VRRuntime::RenderPoseStats g_RP;   // under g_StatusMutex

    //! The pose the image about to be presented was rendered from: the view its executed command lists carried
    //! (source 2), else the view its draws last took (1), else the latest view built (0).
    std::atomic<int> g_PoseSource{ 2 };   // research (vr posesource): the most reliable source allowed, 2 exec, 1 draws, 0 latest
    ViewMatrixHook::XrPose PickRenderPose(int& source)
    {
        unsigned long long s = 0, age = 0;
        const int allow = g_PoseSource.load();
        if (allow == 3)
        {   // research: the view BEFORE the latest (the pipeline depth PipeTrace showed)
            const ViewMatrixHook::XrPose last = ViewMatrixHook::RenderPoseXr(0);
            const ViewMatrixHook::XrPose prev = last.valid ? ViewMatrixHook::RenderPoseXr(last.serial - 1) : last;
            source = 0; return prev.valid ? prev : last;
        }
        if (allow >= 2 && GpuTrace::ExecutedViewSerial(s, age) && age <= 1)
        { const ViewMatrixHook::XrPose rp = ViewMatrixHook::RenderPoseXr(s); if (rp.valid) { source = 2; return rp; } }
        if (allow >= 1 && GpuTrace::DrawViewSerial(s, age) && age <= 1)
        { const ViewMatrixHook::XrPose rp = ViewMatrixHook::RenderPoseXr(s); if (rp.valid) { source = 1; return rp; } }
        source = 0;
        return ViewMatrixHook::RenderPoseXr(0);
    }

    void Capture(IDXGISwapChain* sc, ID3D11Device* dev, ID3D11Texture2D* bb)
    {
        std::string tag;
        { std::lock_guard lk(g_CapMutex); tag = g_CapTag; }
        ID3D11DeviceContext* ctx = nullptr; dev->GetImmediateContext(&ctx);
        std::wstring dir = GameInfo::GameDir() + L"\\";
        std::wstring wtag(tag.begin(), tag.end());
        bool ok = XRCapture::SaveTextureToPng(dev, ctx, bb, (dir + L"DVR_" + wtag + L"_frame.png").c_str());
        int eyes = 0;
        if (g_Session.IsInitialized())
            for (int i = 0; i < 2; i++)
                if (ID3D11Texture2D* t = g_Session.LastEyeTexture(i))
                    eyes += XRCapture::SaveTextureToPng(dev, ctx, t, (dir + L"DVR_" + wtag + L"_eye" + std::to_wstring(i) + L".png").c_str()) ? 1 : 0;
        ctx->Release();
        LOGI("Capture '{}': frame={} eyes={}", tag, ok, eyes);
    }
}

namespace
{
    // vr burst: N CONSECUTIVE frames, copied on the GPU at each Present (no stall, so the game's timing is untouched)
    // and written out as PNGs once the last one is in. What a single capture cannot show: how a thing moves from one
    // frame to the next.
    std::mutex g_BurstM; int g_BurstWant = 0, g_BurstStride = 1, g_BurstTick = 0; std::string g_BurstTag; std::vector<ID3D11Texture2D*> g_BurstTex;
    void Burst(ID3D11Device* dev, ID3D11Texture2D* bb)
    {
        std::lock_guard lk(g_BurstM);
        if (g_BurstWant <= 0 && g_BurstTex.empty()) return;
        ID3D11DeviceContext* ctx = nullptr; dev->GetImmediateContext(&ctx);
        if (g_BurstWant > 0)
        {
            D3D11_TEXTURE2D_DESC d; bb->GetDesc(&d);
            d.BindFlags = 0; d.MiscFlags = 0; d.CPUAccessFlags = 0; d.Usage = D3D11_USAGE_DEFAULT;
            ID3D11Texture2D* t = nullptr;
            if (g_BurstTick++ % g_BurstStride == 0)   // every stride-th frame (vr gunburst n stride: a longer window)
            {
                if (SUCCEEDED(dev->CreateTexture2D(&d, nullptr, &t)) && t) { ctx->CopyResource(t, bb); g_BurstTex.push_back(t); }
                g_BurstWant--;
            }
        }
        else
        {
            const std::wstring dir = GameInfo::GameDir() + L"\\DVR_" + std::wstring(g_BurstTag.begin(), g_BurstTag.end()) + L"_b";
            int ok = 0;
            for (size_t i = 0; i < g_BurstTex.size(); i++)
            {
                wchar_t n[16]; swprintf_s(n, L"%02zu.png", i);
                ok += XRCapture::SaveTextureToPng(dev, ctx, g_BurstTex[i], (dir + n).c_str()) ? 1 : 0;
                g_BurstTex[i]->Release();
            }
            LOGI("burst '{}': {} of {} frames written", g_BurstTag, ok, g_BurstTex.size());
            g_BurstTex.clear();
        }
        ctx->Release();
    }
}
void VRRuntime::RequestBurst(int frames, const std::string& tag, int stride)
{
    std::lock_guard lk(g_BurstM);
    if (!g_BurstTex.empty()) return;                 // one burst at a time
    g_BurstWant = frames < 1 ? 1 : frames > 60 ? 60 : frames; g_BurstTag = tag; g_BurstStride = stride < 1 ? 1 : stride; g_BurstTick = 0;
}
void VRRuntime::SetGameFov(float hDeg, float vDeg) { g_FovH = hDeg; g_FovV = vDeg; }

void VRRuntime::RequestCapture(const std::string& tag)
{
    { std::lock_guard lk(g_CapMutex); g_CapTag = tag; }
    g_CapRequested = true;
}

void VRRuntime::OnPresent(IDXGISwapChain* sc)
{
    ID3D11Device* dev = nullptr;
    if (FAILED(sc->GetDevice(__uuidof(ID3D11Device), (void**)&dev)) || !dev) return;
    ID3D11Texture2D* bb = nullptr;
    sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb);

    {   // Start the OpenXR session; again every few seconds while there is no headset yet (it may be switched on, or
        // Link / Air Link / Virtual Desktop connected, after the game started); and from scratch after the connection
        // is lost. Until it runs, the game is the plain flat game (the VR package waits for it: Commands).
        static ULONGLONG nextTry = 0; static std::string lastError;
        const ULONGLONG now = GetTickCount64();
        if (g_Session.IsInitialized() && g_Session.NeedsRestart())
        {   // (Shutdown is false while the OpenXR frame thread is still inside a runtime call: again next Present)
            const bool fresh = g_Session.RestartIsFresh();
            if (g_Session.Shutdown())
            {
                nextTry = fresh ? now : now + 3000; lastError.clear();
                if (fresh) LOGI("VRRuntime: OpenXR session closed: a new one now (the runtime did not offer the old one back)");
                else LOGI("VRRuntime: OpenXR session closed after a loss: starting again in 3 s");
            }
        }
        // Not while the startup probe still holds its own instance (it runs on the init thread and can take seconds):
        // Meta's runtime refuses a second instance in the same process (-10), and then there is no VR at all.
        if (XRProbe::Done() && !g_Session.IsInitialized() && !g_Session.Exited() && (!g_InitAttempted || (g_Session.ShouldRetry() && now >= nextTry)))
        {
            g_InitAttempted = true;
            g_Session.SetPitchOffset(Settings::ControllerPitchOffset());
            g_Session.SetLogInput(Settings::LogInput());
            if (!g_Session.Init(dev))
            {
                nextTry = now + 3000;
                if (g_Session.LastError() != lastError)
                {   // once per distinct reason, not every retry
                    lastError = g_Session.LastError();
                    LOGI("VRRuntime: OpenXR session not started ({}). Running flat{}.", lastError, g_Session.ShouldRetry() ? ", retrying every 3 s" : "");
                }
            }
            else lastError.clear();
        }
    }

    if (g_Session.IsInitialized() && bb)
    {
        g_Session.PollEvents();
        if (g_Session.IsSessionRunning())
        {
            D3D11_TEXTURE2D_DESC d; bb->GetDesc(&d);
            float h = g_FovH.load() * 3.14159265f / 180.f;
            float v = g_FovV.load() > 0 ? g_FovV.load() * 3.14159265f / 180.f
                                        : 2.f * atanf(tanf(h * 0.5f) * (float)d.Height / (float)d.Width);
            g_Session.SetLayerFov(h, v);
            g_Session.SetMatchedFov(false);
            {   // the flat frame's real extent, from the fov the main view was built with (CameraHook::BuiltFov):
                // the image is drawn at exactly those angles in each eye (XRSession::SetOrtho)
                float bx = 0, by = 0; unsigned long long n = 0; CameraHook::BuiltFov(bx, by, n);
                if (bx > 1.f && by > 1.f && bx < 170.f && by < 170.f)
                {
                    const float tx = tanf(bx * 0.5f / 57.29578f), ty = tanf(by * 0.5f / 57.29578f);
                    g_Session.SetSourceTans(-tx, tx, ty, -ty);
                }
                g_Session.SetOrtho(g_Ortho.load());
            }
            if (g_Session.BeginFrame())
            {
                const XREyePose& e0 = g_Session.Eye(0);
                const XREyePose& e1 = g_Session.Eye(g_Session.EyeCount() > 1 ? 1 : 0);
                {
                    std::lock_guard lk(g_PoseMutex);
                    g_Head.valid = e0.valid; g_Head.gen = g_Session.RecenterGeneration();
                    g_Head.pos[0] = (e0.posX + e1.posX) * 0.5f; g_Head.pos[1] = (e0.posY + e1.posY) * 0.5f; g_Head.pos[2] = (e0.posZ + e1.posZ) * 0.5f;
                    g_Head.quat[0] = e0.quatX; g_Head.quat[1] = e0.quatY; g_Head.quat[2] = e0.quatZ; g_Head.quat[3] = e0.quatW;
                    const XREyePose* eyes[2] = { &e0, &e1 };
                    for (int i = 0; i < 2; i++)
                    {
                        g_Eye[i].valid = eyes[i]->valid; g_Eye[i].gen = g_Head.gen;
                        g_Eye[i].pos[0] = eyes[i]->posX; g_Eye[i].pos[1] = eyes[i]->posY; g_Eye[i].pos[2] = eyes[i]->posZ;
                        g_Eye[i].quat[0] = eyes[i]->quatX; g_Eye[i].quat[1] = eyes[i]->quatY; g_Eye[i].quat[2] = eyes[i]->quatZ; g_Eye[i].quat[3] = eyes[i]->quatW;
                    }
                }
                g_Session.SyncInput();
                { std::lock_guard lk(g_PoseMutex); g_Input = g_Session.Input(); g_InputValid = true; g_InputAt = GetTickCount64(); }
                // the watch on the left wrist: decided on this frame's poses, drawn into this frame's layers
                WristHud::OnXrFrame(g_Session, KbmInject::GetStats().mode == 1);
                // Menus, loading screens and cutscenes on a screen in the room (ScreenMode): it submits the frame
                // itself. Which kind of picture this is comes from its own view record (a flat view, or not).
                bool imageFlat = false, imageKnown = false;
                if (g_TruePose.load())
                {
                    int source = 0;
                    const ViewMatrixHook::XrPose rp = PickRenderPose(source);
                    imageKnown = rp.serial != 0; imageFlat = rp.flat;
                }
                if (ScreenMode::OnXrFrame(g_Session, bb, imageFlat, imageKnown))
                    PipeTrace::Event(PipeTrace::Present, (uint64_t)-2);
                else if (g_Stereo.load() && g_Session.EyeCount() == 2)
                {
                    // Pipeline phase: the engine builds the NEXT frame's view (our CreateViewMatrix hook,
                    // which reads g_CurEye) before it presents the PREVIOUS one, so the backbuffer we
                    // hold here was rendered with the eye BEFORE the current value. Measured 2026-08-29
                    // with the mock (near-object disparity came out with the wrong sign otherwise).
                    // Now the view's own record says which eye and pose the image was drawn from (the serial its
                    // draws were placed with); the phase guess is only the fallback.
                    int eye = g_CurEye.load();
                    int target = eye ^ 1;
                    PipeTrace::Event(PipeTrace::Present, (uint64_t)eye);
                    if (g_TruePose.load())
                    {
                        int source = 0;
                        const ViewMatrixHook::XrPose rp = PickRenderPose(source);
                        if (rp.valid && rp.eye >= 0)
                        {
                            g_Session.SetRenderPose(rp.quat, rp.pos);
                            std::lock_guard lk(g_StatusMutex);
                            g_RP.used++; if (source) g_RP.fromDraws++; if (source == 2) g_RP.fromExec++; if (rp.fromGameAxis) g_RP.gameAxis++;
                            if (rp.eye != target) g_RP.phaseFix++;
                            target = rp.eye;
                        }
                        PipeTrace::Event(PipeTrace::Submit, rp.serial, (uint64_t)(int64_t)target);
                    }
                    g_Session.SubmitFrame(bb, target);
                    g_CurEye.store(eye ^ 1);      // next frame renders from the other eye
                }
                else
                {
                    PipeTrace::Event(PipeTrace::Present, (uint64_t)-1);
                    if (g_TruePose.load())
                    {   // the view this image was drawn from (PickRenderPose)
                        int source = 0;
                        ViewMatrixHook::XrPose rp = PickRenderPose(source);
                        const ViewMatrixHook::XrPose latest = ViewMatrixHook::RenderPoseXr(0);
                        PipeTrace::Event(PipeTrace::Submit, rp.serial, (uint64_t)-1);
                        if (rp.valid)
                        {
                            g_Session.SetRenderPose(rp.quat, rp.pos);
                            std::lock_guard lk(g_StatusMutex);
                            g_RP.used++; if (source) g_RP.fromDraws++; if (source == 2) g_RP.fromExec++; if (rp.fromGameAxis) g_RP.gameAxis++;
                            if (latest.serial > rp.serial) g_RP.behindLatest++;
                            // how far the claimed pose is from the head right now (deg): what the compositor corrects
                            const float* q = rp.quat; const float* h = g_Head.quat;
                            const float d = fabsf(q[0] * h[0] + q[1] * h[1] + q[2] * h[2] + q[3] * h[3]);
                            const float ang = 2.f * acosf(d > 1.f ? 1.f : d) * 57.29578f;
                            g_RP.lastDiff = ang; if (ang > g_RP.maxDiff) g_RP.maxDiff = ang;
                        }
                    }
                    g_Session.SubmitFrame(bb);
                }
                g_Session.EndFrameNoRender();   // a frame nothing above submitted is ended here (no-op otherwise)
            }
            else g_Session.EndFrameNoRender();
        }
    }

    PipeTrace::OnPresent();
    if (g_CapRequested.exchange(false) && bb) Capture(sc, dev, bb);
    if (bb) Burst(dev, bb);

    {
        std::lock_guard lk(g_StatusMutex);
        if (bb && g_Session.IsSessionRunning())
        {
            D3D11_TEXTURE2D_DESC d; bb->GetDesc(&d);
            for (int i = 0; i < 2 && i < g_Session.EyeCount(); i++)
            {
                VRRuntime::Placement& pl = g_Place[i];
                pl.srcW = d.Width; pl.srcH = d.Height;
                g_Session.Placement(i, pl.rect, pl.swap);
                const XREyePose& e = g_Session.Eye(i);
                pl.fov[0] = e.fovLeft; pl.fov[1] = e.fovRight; pl.fov[2] = e.fovUp; pl.fov[3] = e.fovDown;
                pl.valid = pl.rect[2] > 0;
            }
        }
        g_Status.initialized = g_Session.IsInitialized();
        g_Status.running = g_Session.IsSessionRunning();
        g_Status.state = g_Session.SessionState();
        g_Status.framesSubmitted = g_Session.FramesSubmitted();
        g_Status.eyeW = g_Session.EyeWidth(); g_Status.eyeH = g_Session.EyeHeight();
        g_Status.runtime = g_Session.RuntimeName(); g_Status.system = g_Session.SystemName();
        g_Status.error = g_Session.LastError();
    }
    if (bb) bb->Release();
    dev->Release();
}

void VRRuntime::SetOrtho(bool on) { g_Ortho = on; LOGI("VRRuntime: flat frame placement {}", on ? "orthoscopic (true angular size)" : "1:1 pixels at the optical centre (old)"); }
bool VRRuntime::Ortho() { return g_Ortho.load(); }
void VRRuntime::SetTruePose(bool on) { g_TruePose = on; LOGI("VRRuntime: layer pose {}", on ? "= the pose each image was rendered from" : "= the head pose located at submit (old)"); }
bool VRRuntime::TruePose() { return g_TruePose.load(); }
void VRRuntime::SetPoseSource(int s) { g_PoseSource = s; LOGI("VRRuntime: render pose source {}", s); }
VRRuntime::RenderPoseStats VRRuntime::GetRenderPoseStats() { std::lock_guard lk(g_StatusMutex); return g_RP; }
unsigned long long VRRuntime::OrthoDraws() { return g_Session.OrthoDraws(); }
VRRuntime::Placement VRRuntime::GetPlacement(int i) { std::lock_guard lk(g_StatusMutex); return g_Place[i & 1]; }
VRRuntime::Status VRRuntime::GetStatus() { std::lock_guard lk(g_StatusMutex); return g_Status; }
VRRuntime::HeadPose VRRuntime::GetHeadPose() { std::lock_guard lk(g_PoseMutex); return g_Head; }
VRRuntime::HeadPose VRRuntime::GetEyePose(int i) { std::lock_guard lk(g_PoseMutex); return g_Eye[i & 1]; }
void VRRuntime::SetStereo(bool on) { g_Stereo = on; }
bool VRRuntime::Stereo() { return g_Stereo; }
int  VRRuntime::CurrentEye() { return g_Stereo.load() ? g_CurEye.load() : -1; }
unsigned VRRuntime::RecenterEvents() { return g_Session.RecenterEvents(); }
unsigned long long VRRuntime::LostFrames(int dev) { return g_Session.LostFrames(dev); }
bool VRRuntime::GetInput(XRInputState& out)
{
    std::lock_guard lk(g_PoseMutex);
    // Stale = the runtime has given no frame for a while (stalled, or the session going away): the controllers' last
    // state must not keep driving the game (a stick held at that moment walked the player into the wall for good).
    if (!g_InputValid || !g_Session.IsSessionRunning() || GetTickCount64() - g_InputAt > 500) return false;
    out = g_Input; return true;
}

bool VRRuntime::Vibrate(int hand, float amplitude, float seconds, float hz)
{
    if (!Settings::Haptics()) return false;   // [Controls] Haptics = 0
    return g_Session.IsInitialized() && g_Session.Vibrate(hand, amplitude, seconds, hz);
}
unsigned long long VRRuntime::HapticPulses() { return g_Session.HapticPulses(); }

void VRRuntime::Shutdown(bool processExit)
{
    if (g_Session.IsInitialized()) g_Session.Shutdown(processExit);
}
void VRRuntime::SetKeepAlive(int m) { g_Session.SetKeepAlive(m); LOGI("cmd vr keepalive {} ({})", m, m ? "the headset gets the last picture while the game sends none" : "off: the headset waits (the hourglass)"); }
void VRRuntime::SetWaitBound(int ms) { g_Session.SetWaitBound(ms); LOGI("cmd vr xrwaitbound {} ms", ms); }
void VRRuntime::SetKick(float s, bool any) { g_Session.SetKick(s, any); LOGI("cmd vr xrkick {} s ({}{})", s, s > 0 ? "a session the runtime stopped and did not offer back is replaced by a new one" : "off", s > 0 ? (any ? ", on any runtime" : ", on Meta's runtime only") : ""); }
std::string VRRuntime::PumpStatus() { return g_Session.PumpStatus(); }
void VRRuntime::SetD3DProtect(bool on) { g_Session.SetD3DProtect(on); }
