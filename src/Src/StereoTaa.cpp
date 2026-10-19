#include "pch.h"
#include "StereoTaa.h"
#include "GpuTrace.h"
#include "GameInfo.h"
#include "VRRuntime.h"
#include "Game.h"
#include "CVars.h"
#include "ScreenMode.h"
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <map>
#include <fstream>
#include <cmath>
#include <algorithm>

namespace
{
    std::atomic<int> g_Mode{ 1 };

    // ---- the main view object (ENGINE.md "Renderer view object"; measured on 1.77.9.0, 2026-10-14) ----
    // The view setup (exe+0x375B7F chunk) copies, once per engine frame (when the frame number at +0x77B4 changes), the
    // current matrices into the "previous" ones, then builds the new current ones: projection (exe+0x3742B0, which also
    // moves the jitter +0x7850 to +0x7858 first), view (CreateViewMatrix -- our hook), inverse, view-projection. The
    // constants upload (exe+0x3586A0) sends the previous ones as cb_previous*matrix and the jitter difference as
    // cb_jitterrelativetopreviousframe: everything the motion vectors are made of.
    constexpr uint32_t kPrevOrg = 0x3A84, kPrevProj = 0x7410, kPrevView = 0x74D0, kPrevVP = 0x7590, kPrevJitter = 0x7858;
    constexpr uint32_t kCurJitter = 0x7850, kFrameNo = 0x77B4, kResetFlag = 0xBCFC;
    constexpr uint32_t kProj = 0x7390, kInvProj = 0x73D0;   // current projection (jitter in row 0 / row 1, column 2) and its inverse
    constexpr unsigned kMaxAge = 6;          // engine frames: an eye's history or matrices older than this are stale

    // ---- vr aaprobe: what the frame's full-screen passes read and write ----
    std::atomic<int> g_ProbeFrames{ 0 };     // frames left to log (0 = off)
    std::atomic<bool> g_ProbeAll{ false };   // every draw, not only the full-screen ones
    std::mutex g_PM;                         // everything below
    std::vector<std::string> g_Lines;
    std::map<void*, int> g_ResId, g_CtxId;
    unsigned long long g_Frame = 0;
    unsigned g_Seq = 0;

    int IdOf(std::map<void*, int>& m, void* p) { auto it = m.find(p); if (it != m.end()) return it->second; const int id = (int)m.size(); m[p] = id; return id; }
    std::string Res(ID3D11Resource* r)
    {
        if (!r) return "-";
        const int id = IdOf(g_ResId, r);
        D3D11_RESOURCE_DIMENSION d = D3D11_RESOURCE_DIMENSION_UNKNOWN; r->GetType(&d);
        if (d == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
        {
            D3D11_TEXTURE2D_DESC t; ((ID3D11Texture2D*)r)->GetDesc(&t);
            std::string s = fmt::format("T{}:{}x{}:f{}", id, t.Width, t.Height, (int)t.Format);
            if (t.MipLevels > 1) s += fmt::format(":m{}", t.MipLevels);
            if (t.ArraySize > 1) s += fmt::format(":a{}", t.ArraySize);
            if (t.SampleDesc.Count > 1) s += fmt::format(":ms{}", t.SampleDesc.Count);
            return s;
        }
        if (d == D3D11_RESOURCE_DIMENSION_BUFFER) { D3D11_BUFFER_DESC b; ((ID3D11Buffer*)r)->GetDesc(&b); return fmt::format("B{}:{}b", id, b.ByteWidth); }
        if (d == D3D11_RESOURCE_DIMENSION_TEXTURE3D) { D3D11_TEXTURE3D_DESC t; ((ID3D11Texture3D*)r)->GetDesc(&t); return fmt::format("V{}:{}x{}x{}:f{}", id, t.Width, t.Height, t.Depth, (int)t.Format); }
        return fmt::format("R{}", id);
    }
    std::string ViewRes(ID3D11View* v)
    {
        if (!v) return "-";
        ID3D11Resource* r = nullptr; v->GetResource(&r);
        std::string s = Res(r);
        if (r) r->Release();
        return s;
    }
    template <class T> void ReleaseAll(T** a, int n) { for (int i = 0; i < n; i++) if (a[i]) a[i]->Release(); }
    std::string SrvList(ID3D11ShaderResourceView** v, int n)
    {
        std::string s;
        for (int i = 0; i < n; i++) if (v[i]) s += fmt::format(" t{}={}", i, ViewRes(v[i]));
        return s;
    }
    std::string Hash(const void* sh) { const uint64_t h = sh ? GpuTrace::ShaderHashOf(sh) : 0; return sh ? fmt::format("{:016x}", h) : "-"; }

    // ---- the TAA compute shader, recognised by what it binds ----
    // Found 2026-10-14 by diffing every pass of a frame with r_skipTemporalAA 0 and 1: one 16x16 compute shader reading
    // ro_taahistory_read, ro_motionvectors and ro_viewcolormap and writing rw_taahistory_write and rw_taaresult. The
    // names, not the hash, identify it, so a variant of the shader (another quality setting) is recognised too.
    struct TaaCs { int histSrv = -1, histUav = -1, mvSrv = -1, viewCb = -1, viewMatOff = -1; };
    std::mutex g_CsM;
    std::map<void*, TaaCs> g_TaaCs;
    std::atomic<unsigned> g_TaaShaders{ 0 };
    bool FindTaa(void* cs, TaaCs& out)
    {
        std::lock_guard lk(g_CsM);
        auto it = g_TaaCs.find(cs);
        if (it == g_TaaCs.end()) return false;
        out = it->second; return true;
    }

    // ---- the views ViewMatrixHook built, with their eye ----
    struct ViewRec
    {
        unsigned frame = 0; int eye = -1; float m[16] = {}; float jitter[2] = {}; unsigned long long seq = 0;
        bool prevMine = false;   // the engine's "previous" matrices for this view are its own eye's
        bool hasC = false;       // C = VP(this eye, its last frame) * VP(the frame before, the other eye)^-1
        float C[16] = {};        // takes a clip position computed with the other eye's last VP to this eye's
    };
    std::mutex g_VM;                          // the ring, the previous-matrix store and the counters below
    ViewRec g_Ring[16]; unsigned long long g_ViewSeq = 0;
    struct PrevSet { bool valid = false; unsigned frame = 0; uint8_t org[12], proj[64], view[64], vp[64], jit[8]; };
    PrevSet g_Prev[2];
    unsigned g_LastFrame = 0; int g_LastEye = -1; bool g_HaveLast = false;
    unsigned long long g_Patched = 0, g_SameEye = 0, g_SecondBuild = 0, g_Resets = 0, g_NoPrev = 0;
    std::atomic<bool> g_FixPrev{ false };
    // ---- each eye walks the WHOLE jitter sequence ----
    // The game's TAA jitter repeats every 8 frames (measured 2026-10-14). With alternating eyes each eye got every other
    // sample: 4 sub-pixel positions instead of 8, half the anti-aliasing. The sequence is learned as the engine makes it
    // (by frame number modulo its period) and each eye is handed sample k of it at its own k-th frame.
    std::atomic<bool> g_JitterRemap{ true };  // vr aajitter 0|1
    struct JitObs { unsigned frame = 0; float j[2] = {}; bool valid = false; };
    JitObs g_JitObs[64];                      // under g_VM, by frame % 64
    unsigned g_EyeK[2] = {}; unsigned g_JitPeriod = 0; unsigned long long g_JitRemapped = 0;
    //! Smallest period P (2..32, dividing 64) the last 2P observed frames repeat with, 0 = none (random or no jitter).
    unsigned JitterPeriod(unsigned frame)
    {
        for (unsigned P : { 2u, 4u, 8u, 16u, 32u })
        {
            bool ok = true; unsigned n = 0; bool nonzero = false;
            for (unsigned f = frame - P + 1; f <= frame && ok; f++)
            {
                const JitObs& a = g_JitObs[f % 64]; const JitObs& b = g_JitObs[(f - P) % 64];
                if (!a.valid || !b.valid || a.frame != f || b.frame != f - P) { ok = false; break; }
                ok = fabsf(a.j[0] - b.j[0]) < 1e-7f && fabsf(a.j[1] - b.j[1]) < 1e-7f; n++;
                nonzero |= a.j[0] != 0.f || a.j[1] != 0.f;
            }
            if (ok && n == P) return nonzero ? P : 0;
        }
        return 0;
    }
    std::atomic<int> g_ViewLog{ 0 };          // vr aaviews <n>: log the next n main views' projection and jitter
    std::atomic<int> g_DrawDiag{ 0 };         // vr aadraws <n>: log n corrected draws against the static world's previous     // per-draw previous MVPs are being corrected (mode 1 and stereo)
    std::atomic<unsigned long long> g_CFound{ 0 }, g_CKept{ 0 }, g_CNoView{ 0 }, g_CNoMatch{ 0 }, g_CNone{ 0 };

    //! 4x4 inverse (row-major), false when singular.
    bool Inverse4(const float* m, float* o)
    {
        float inv[16];
        inv[0] = m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
        inv[4] = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
        inv[8] = m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
        inv[12] = -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
        inv[1] = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
        inv[5] = m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
        inv[9] = -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
        inv[13] = m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
        inv[2] = m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];
        inv[6] = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];
        inv[10] = m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];
        inv[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];
        inv[3] = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];
        inv[7] = m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];
        inv[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11] - m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];
        inv[15] = m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10] + m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];
        const double det = (double)m[0] * inv[0] + (double)m[1] * inv[4] + (double)m[2] * inv[8] + (double)m[3] * inv[12];
        if (fabs(det) < 1e-20) return false;
        for (int i = 0; i < 16; i++) o[i] = (float)(inv[i] / det);
        return true;
    }
    void Mul4(const float* a, const float* b, float* o)
    {
        for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++)
        { double v = 0; for (int k = 0; k < 4; k++) v += (double)a[i * 4 + k] * b[k * 4 + j]; o[i * 4 + j] = (float)v; }
    }

    void ReadPrev(const uint8_t* v, PrevSet& p)
    {
        memcpy(p.org, v + kPrevOrg, 12); memcpy(p.proj, v + kPrevProj, 64); memcpy(p.view, v + kPrevView, 64);
        memcpy(p.vp, v + kPrevVP, 64); memcpy(p.jit, v + kPrevJitter, 8);
    }
    void WritePrev(uint8_t* v, const PrevSet& p)
    {
        memcpy(v + kPrevOrg, p.org, 12); memcpy(v + kPrevProj, p.proj, 64); memcpy(v + kPrevView, p.view, 64);
        memcpy(v + kPrevVP, p.vp, 64); memcpy(v + kPrevJitter, p.jit, 8);
    }

    // ---- the TAA's PerViewCB, shadowed on its way in ----
    // The view matrix in it says which view (hence which eye) a dispatch belongs to. Only the buffers seen bound as the
    // TAA's PerViewCB are shadowed (learned at the dispatch), so the cost for every other Map is a few compares.
    constexpr int kViewCbMax = 16;
    std::atomic<void*> g_ViewCbs[kViewCbMax];
    std::atomic<int> g_ViewMatOff{ -1 };      // byte offset of cb_viewmatrix in PerViewCB (from reflection)
    std::atomic<int> g_VPOff{ -1 }, g_PrevVPOff{ -1 };   // cb_viewprojectionmatrix, cb_previousviewprojectionmatrix (diagnostics)
    thread_local ID3D11Resource* t_MapRes = nullptr; thread_local void* t_MapPtr = nullptr;
    struct CbShadow { void* buf = nullptr; void* ctx = nullptr; float m[16] = {}, vp[16] = {}, pvp[16] = {}; unsigned long long stamp = 0; };
    std::mutex g_SM; CbShadow g_Sh[32]; unsigned long long g_ShStamp = 0;
    bool IsViewCb(void* r)
    {
        if (!r) return false;
        for (auto& b : g_ViewCbs) if (b.load(std::memory_order_relaxed) == r) return true;
        return false;
    }
    //! The bytes a shadowed PerViewCB must hold: the furthest matrix read from it. A buffer smaller than that is never
    //! taken for one (after a level load an address can come back as another, smaller buffer).
    UINT ViewCbBytesNeeded()
    {
        int need = g_ViewMatOff.load() + 64;
        need = std::max(need, g_VPOff.load() + 64); need = std::max(need, g_PrevVPOff.load() + 64);
        return (UINT)need;
    }
    bool BigEnough(void* r)
    {
        D3D11_RESOURCE_DIMENSION dim{}; ((ID3D11Resource*)r)->GetType(&dim);
        if (dim != D3D11_RESOURCE_DIMENSION_BUFFER) return false;
        D3D11_BUFFER_DESC bd{}; ((ID3D11Buffer*)r)->GetDesc(&bd);
        return bd.ByteWidth >= ViewCbBytesNeeded();
    }
    void LearnViewCb(void* r)
    {
        if (IsViewCb(r) || !BigEnough(r)) return;
        for (auto& b : g_ViewCbs) { void* e = nullptr; if (b.compare_exchange_strong(e, r)) return; }
        static std::atomic<unsigned> next{ 0 };
        g_ViewCbs[next++ % kViewCbMax] = r;   // full: the oldest learned goes (a level load makes new buffers)
    }
    void StoreCb(void* ctx, void* buf, const void* data)
    {
        const int off = g_ViewMatOff.load();
        if (off < 0 || !data) return;
        std::lock_guard lk(g_SM);
        int slot = 0;
        for (int i = 0; i < 32; i++)
        {
            if (g_Sh[i].buf == buf && g_Sh[i].ctx == ctx) { slot = i; break; }
            if (g_Sh[i].stamp < g_Sh[slot].stamp) slot = i;
        }
        CbShadow& s = g_Sh[slot];
        s.buf = buf; s.ctx = ctx; s.stamp = ++g_ShStamp;
        memcpy(s.m, (const uint8_t*)data + off, 64);
        if (g_VPOff.load() >= 0) memcpy(s.vp, (const uint8_t*)data + g_VPOff.load(), 64);
        if (g_PrevVPOff.load() >= 0) memcpy(s.pvp, (const uint8_t*)data + g_PrevVPOff.load(), 64);
    }

    // ---- each eye's history ----
    std::mutex g_HM;                          // all of this block
    D3D11_TEXTURE2D_DESC g_HistDesc{}; bool g_HistDescValid = false;
    D3D11_SHADER_RESOURCE_VIEW_DESC g_SrvDesc{}; bool g_SrvDescValid = false;
    ID3D11Texture2D* g_Hist[2] = {}; ID3D11ShaderResourceView* g_HistSrv[2] = {};
    bool g_HistValid[2] = {}; unsigned g_HistFrame[2] = {};
    unsigned long long g_HistGen = 0, g_IdleFrames = 0;
    std::atomic<unsigned long long> g_Dispatches{ 0 }, g_Swapped{ 0 }, g_FirstUse{ 0 }, g_NoEye{ 0 }, g_NoTex{ 0 }, g_EyeN[2];
    std::atomic<unsigned long long> g_NoShadow{ 0 }, g_NoMatch{ 0 };
    // ---- the safety net ----
    // If the eyes cannot be told apart at the TAA (an engine path not met in testing), TAA would blend the two eyes again
    // (the ghosting): then stereo falls back to what it did before, TAA off, and says so in the log. Judged over windows
    // of 120 presents in which the TAA ran at least 60 times; three bad windows in a row (the TAA hardly ever given its
    // own eye's history: fewer than a quarter of the presents) trip it. Undone when stereo ends (the player's
    // r_skipTemporalAA comes back).
    bool g_FellBack = false; int g_BadWindows = 0, g_WinFrames = 0;
    // r_skipTemporalAA is only ever touched here: forced to 1 while stereo runs with TAA off (vr stereotaa 0, or the
    // safety net), and given back to what the player had the moment that ends. Called on the game thread (commands,
    // Present).
    std::mutex g_SkipM; bool g_SkipForced = false; int g_SkipSaved = 0;
    void ApplySkip(bool stereoOn)
    {
        std::lock_guard lk(g_SkipM);
        const bool want = stereoOn && (g_Mode.load() == 0 || g_FellBack);
        if (want && !g_SkipForced)
        {
            g_SkipSaved = CVars::Address("r_skipTemporalAA") ? CVars::GetInt("r_skipTemporalAA") : 0;
            Game::BufferCommandText("r_skipTemporalAA 1");
            g_SkipForced = true;
            LOGI("StereoTaa: TAA off in stereo ({}; the player's r_skipTemporalAA {} kept)", g_FellBack ? "safety net" : "vr stereotaa 0", g_SkipSaved);
        }
        else if (!want && g_SkipForced)
        {
            Game::BufferCommandText(g_SkipSaved ? "r_skipTemporalAA 1" : "r_skipTemporalAA 0");
            g_SkipForced = false;
            LOGI("StereoTaa: r_skipTemporalAA back to the player's {}", g_SkipSaved);
        }
    }
    unsigned long long g_WinDisp0 = 0, g_WinSwap0 = 0, g_WinFirst0 = 0;
    bool g_WasActive = false;
    std::atomic<bool> g_Break{ false };       // vr aabreak 1: research, the eye is never found (tests the safety net)
    bool SameDesc(const D3D11_TEXTURE2D_DESC& a, const D3D11_TEXTURE2D_DESC& b)
    { return a.Width == b.Width && a.Height == b.Height && a.Format == b.Format && a.MipLevels == b.MipLevels && a.ArraySize == b.ArraySize && a.SampleDesc.Count == b.SampleDesc.Count; }
    void ReleaseHist()
    {
        for (int i = 0; i < 2; i++)
        {
            if (g_HistSrv[i]) { g_HistSrv[i]->Release(); g_HistSrv[i] = nullptr; }
            if (g_Hist[i]) { g_Hist[i]->Release(); g_Hist[i] = nullptr; }
            g_HistValid[i] = false;
        }
    }

    //! The eye (0/1) of the view the dispatch's PerViewCB holds, or a negative reason.
    int EyeOfDispatch(ID3D11DeviceContext* c, int cbSlot, unsigned& frame, std::string* why = nullptr)
    {
        if (g_Break.load()) { if (why) *why = "vr aabreak"; return -5; }
        ID3D11Buffer* cb = nullptr; c->CSGetConstantBuffers(cbSlot, 1, &cb);
        if (!cb) { if (why) *why = "no PerViewCB bound"; return -2; }
        LearnViewCb(cb);
        float m[16]; bool have = false;
        {
            std::lock_guard lk(g_SM);
            const CbShadow* best = nullptr;
            for (auto& s : g_Sh)
                if (s.buf == cb && s.stamp && (!best || (s.ctx == c) > (best->ctx == c) || ((s.ctx == c) == (best->ctx == c) && s.stamp > best->stamp))) best = &s;
            if (best) { memcpy(m, best->m, 64); have = true; }
        }
        cb->Release();
        if (!have) { g_NoShadow++; if (why) *why = "PerViewCB never seen written"; return -3; }
        std::lock_guard lk(g_VM);
        float bestErr = 1e9f; const ViewRec* bestRec = nullptr;
        for (auto& r : g_Ring)
        {
            if (!r.seq) continue;
            float e0 = 0, e1 = 0;
            for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++)
            {
                e0 = std::max(e0, fabsf(m[i * 4 + j] - r.m[i * 4 + j]));
                e1 = std::max(e1, fabsf(m[i * 4 + j] - r.m[j * 4 + i]));   // stored transposed
            }
            const float e = std::min(e0, e1);
            if (e < bestErr) { bestErr = e; bestRec = &r; }
        }
        if (!bestRec || bestErr > 1e-3f) { g_NoMatch++; if (why) *why = fmt::format("no built view matches (best {:.5f})", bestErr); return -4; }
        frame = bestRec->frame;
        if (why) *why = fmt::format("view frame {} eye {} err {:.6f}", bestRec->frame, bestRec->eye, bestErr);
        return bestRec->eye;
    }

    // ---- vr aamv: the motion vectors one TAA dispatch read, as statistics ----
    std::atomic<int> g_MvReq{ 0 };
    std::mutex g_MvM; D3D11_TEXTURE2D_DESC g_MvDesc{}; bool g_MvDescValid = false; ID3D11Texture2D* g_MvTex = nullptr;
    int g_MvPending = 0; int g_MvEye = -1; unsigned g_MvFrame = 0; std::string g_MvTag;
    float Half(uint16_t h)
    {
        const uint32_t s = (h >> 15) & 1, e = (h >> 10) & 31, m = h & 1023;
        float v = e == 0 ? ldexpf((float)m, -24) : e == 31 ? 65504.f : ldexpf((float)(m | 1024), (int)e - 25);
        return s ? -v : v;
    }
    void MvStats(ID3D11Device* dev, ID3D11DeviceContext* imm)
    {
        D3D11_TEXTURE2D_DESC d = g_MvDesc; d.Usage = D3D11_USAGE_STAGING; d.BindFlags = 0; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; d.MiscFlags = 0;
        ID3D11Texture2D* st = nullptr;
        if (FAILED(dev->CreateTexture2D(&d, nullptr, &st)) || !st) { LOGI("aamv: no staging texture"); return; }
        imm->CopyResource(st, g_MvTex);
        D3D11_MAPPED_SUBRESOURCE ms{};
        if (SUCCEEDED(imm->Map(st, 0, D3D11_MAP_READ, 0, &ms)))
        {
            std::vector<float> mag; mag.reserve((size_t)d.Width * d.Height / 4);
            double sx = 0, sy = 0; size_t n = 0, over025 = 0, over1 = 0, zero = 0; float mx = 0;
            std::vector<float> cmag;   // the centre 40% x 40%
            // and a picture of it (half size, grey = |mv| on a log scale: 1 px ~ 90, 10 px ~ 170, 100 px ~ 255)
            const UINT pw = d.Width / 2, ph = d.Height / 2;
            std::vector<uint8_t> pic((size_t)pw * ph);
            for (UINT y = 0; y < d.Height; y += 2)
            {
                const uint16_t* row = (const uint16_t*)((const uint8_t*)ms.pData + (size_t)y * ms.RowPitch);
                for (UINT x = 0; x < d.Width; x += 2)
                {
                    const float u = Half(row[x * 2]) * d.Width, v = Half(row[x * 2 + 1]) * d.Height;   // pixels
                    const float l = sqrtf(u * u + v * v);
                    if (x / 2 < pw && y / 2 < ph) pic[(size_t)(y / 2) * pw + x / 2] = (uint8_t)std::min(255.f, 38.f * log2f(1.f + l * 4.f));
                    mag.push_back(l); sx += u; sy += v; n++;
                    if (l > 0.25f) over025++; if (l > 1.f) over1++; if (l == 0.f) zero++; if (l > mx) mx = l;
                    if (x > d.Width * 0.3f && x < d.Width * 0.7f && y > d.Height * 0.3f && y < d.Height * 0.7f) cmag.push_back(l);
                }
            }
            imm->Unmap(st, 0);
            if (FILE* f = _wfopen((GameInfo::GameDir() + L"\\DVR_aamv_" + std::wstring(g_MvTag.begin(), g_MvTag.end()) + L".pgm").c_str(), L"wb"))
            {
                fprintf(f, "P5\n%u %u\n255\n", pw, ph); fwrite(pic.data(), 1, pic.size(), f); fclose(f);
            }
            auto pct =[](std::vector<float>& v, float p) { if (v.empty()) return 0.f; size_t k = (size_t)(p * (v.size() - 1)); std::nth_element(v.begin(), v.begin() + k, v.end()); return v[k]; };
            const float p50 = pct(mag, 0.5f), p90 = pct(mag, 0.9f), c50 = pct(cmag, 0.5f), c90 = pct(cmag, 0.9f);
            LOGI("aamv '{}': eye {} frame {} {}x{} | |mv| px: median {:.3f} p90 {:.3f} max {:.1f} | centre median {:.3f} p90 {:.3f} | mean ({:.3f},{:.3f}) | >0.25px {:.1f}% >1px {:.1f}% zero {:.1f}%",
                 g_MvTag, g_MvEye, g_MvFrame, d.Width, d.Height, p50, p90, mx, c50, c90, n ? sx / n : 0, n ? sy / n : 0,
                 n ? 100.0 * over025 / n : 0, n ? 100.0 * over1 / n : 0, n ? 100.0 * zero / n : 0);
        }
        st->Release();
    }
}

namespace
{
    // ---- vr aadump: the three inputs of one TAA dispatch, as the GPU had them (history AFTER our swap) ----
    // Written as raw float32 RGB (DVR_aadump_<tag>_<name>.f32, header: width height channels) so a script can check
    // that history(x - motion) lands on the current frame: history and motion vectors of the same age.
    std::atomic<int> g_DumpReq{ 0 };
    std::mutex g_DumpM; std::string g_DumpTag; int g_DumpPending = 0; int g_DumpEye = -9;
    D3D11_TEXTURE2D_DESC g_DumpDesc[3] = {}; bool g_DumpDescValid[3] = {}; ID3D11Texture2D* g_DumpTex[3] = {};
    const char* kDumpNames[3] = { "history", "motion", "current" };
    float F11(uint32_t v) { const uint32_t e = (v >> 6) & 31, m = v & 63; return e == 0 ? ldexpf((float)m, -20) : e == 31 ? 65504.f : ldexpf((float)(m | 64), (int)e - 21); }
    float F10(uint32_t v) { const uint32_t e = (v >> 5) & 31, m = v & 31; return e == 0 ? ldexpf((float)m, -19) : e == 31 ? 65504.f : ldexpf((float)(m | 32), (int)e - 20); }
    void DumpWrite(ID3D11Device* dev, ID3D11DeviceContext* imm, int k)
    {
        D3D11_TEXTURE2D_DESC d = g_DumpDesc[k]; d.Usage = D3D11_USAGE_STAGING; d.BindFlags = 0; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; d.MiscFlags = 0;
        ID3D11Texture2D* st = nullptr;
        if (FAILED(dev->CreateTexture2D(&d, nullptr, &st)) || !st) return;
        imm->CopyResource(st, g_DumpTex[k]);
        D3D11_MAPPED_SUBRESOURCE ms{};
        if (SUCCEEDED(imm->Map(st, 0, D3D11_MAP_READ, 0, &ms)))
        {
            const int ch = d.Format == DXGI_FORMAT_R16G16_FLOAT ? 2 : 3;
            std::vector<float> out((size_t)d.Width * d.Height * ch);
            bool known = true;
            for (UINT y = 0; y < d.Height && known; y++)
            {
                const uint8_t* row = (const uint8_t*)ms.pData + (size_t)y * ms.RowPitch;
                float* o = out.data() + (size_t)y * d.Width * ch;
                for (UINT x = 0; x < d.Width; x++)
                {
                    if (d.Format == DXGI_FORMAT_R11G11B10_FLOAT)
                    { const uint32_t v = ((const uint32_t*)row)[x]; o[x * 3] = F11(v & 2047); o[x * 3 + 1] = F11((v >> 11) & 2047); o[x * 3 + 2] = F10(v >> 22); }
                    else if (d.Format == DXGI_FORMAT_R16G16_FLOAT)
                    { const uint16_t* h = (const uint16_t*)row + x * 2; o[x * 2] = Half(h[0]); o[x * 2 + 1] = Half(h[1]); }
                    else if (d.Format == DXGI_FORMAT_R16G16B16A16_FLOAT)
                    { const uint16_t* h = (const uint16_t*)row + x * 4; o[x * 3] = Half(h[0]); o[x * 3 + 1] = Half(h[1]); o[x * 3 + 2] = Half(h[2]); }
                    else { known = false; break; }
                }
            }
            imm->Unmap(st, 0);
            if (known)
                if (FILE* f = _wfopen((GameInfo::GameDir() + L"\\DVR_aadump_" + std::wstring(g_DumpTag.begin(), g_DumpTag.end()) + L"_" +
                                       std::wstring(kDumpNames[k], kDumpNames[k] + strlen(kDumpNames[k])) + L".f32").c_str(), L"wb"))
                {
                    const int32_t hdr[3] = { (int32_t)d.Width, (int32_t)d.Height, ch };
                    fwrite(hdr, 4, 3, f); fwrite(out.data(), 4, out.size(), f); fclose(f);
                }
            LOGI("aadump '{}': {} {}x{} format {} {}", g_DumpTag, kDumpNames[k], d.Width, d.Height, (int)d.Format, known ? "written" : "NOT written (format)");
        }
        st->Release();
    }
    //! At the TAA dispatch, after the history swap: copy what is bound.
    void DumpAtDispatch(ID3D11DeviceContext* c, const TaaCs& t, int eye)
    {
        if (g_DumpReq.load() <= 0) return;
        const int slots[3] = { t.histSrv, t.mvSrv, 2 };
        std::lock_guard lk(g_DumpM);
        if (g_DumpPending) return;
        bool ready = true;
        ID3D11Resource* res[3] = {};
        for (int k = 0; k < 3; k++)
        {
            ID3D11ShaderResourceView* v = nullptr; c->CSGetShaderResources(slots[k], 1, &v);
            if (v) { v->GetResource(&res[k]); v->Release(); }
            if (!res[k]) { ready = false; continue; }
            D3D11_TEXTURE2D_DESC d{}; ((ID3D11Texture2D*)res[k])->GetDesc(&d);
            if (!g_DumpTex[k] || !g_DumpDescValid[k] || !SameDesc(d, g_DumpDesc[k])) { g_DumpDesc[k] = d; g_DumpDescValid[k] = true; ready = false; }
        }
        if (ready)
        {
            for (int k = 0; k < 3; k++) c->CopyResource(g_DumpTex[k], res[k]);
            g_DumpPending = 4; g_DumpEye = eye; g_DumpReq = 0;
            LOGI("aadump '{}': copied at a TAA dispatch of eye {}", g_DumpTag, eye);
        }
        for (auto* r : res) if (r) r->Release();
    }
    void DumpPresent(ID3D11Device* dev)
    {
        std::lock_guard lk(g_DumpM);
        for (int k = 0; k < 3; k++)
            if (g_DumpReq.load() > 0 && g_DumpDescValid[k] && (!g_DumpTex[k] || [&] { D3D11_TEXTURE2D_DESC h{}; g_DumpTex[k]->GetDesc(&h); return !SameDesc(h, g_DumpDesc[k]); }()))
            {
                if (g_DumpTex[k]) { g_DumpTex[k]->Release(); g_DumpTex[k] = nullptr; }
                D3D11_TEXTURE2D_DESC d = g_DumpDesc[k]; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = 0; d.CPUAccessFlags = 0; d.MiscFlags = 0;
                dev->CreateTexture2D(&d, nullptr, &g_DumpTex[k]);
            }
        if (g_DumpPending > 0 && --g_DumpPending == 0)
        {
            ID3D11DeviceContext* imm = nullptr; dev->GetImmediateContext(&imm);
            if (imm) { for (int k = 0; k < 3; k++) if (g_DumpTex[k]) DumpWrite(dev, imm, k); imm->Release(); }
        }
    }
}

void StereoTaa::OnComputeShader(ID3D11ComputeShader* cs, const void* code, size_t len)
{
    {   // an address reused by another shader must not keep the old one's entry
        std::lock_guard lk(g_CsM); g_TaaCs.erase(cs);
    }
    using Reflect_t = HRESULT(WINAPI*)(LPCVOID, SIZE_T, REFIID, void**);
    static Reflect_t reflect = nullptr; static bool tried = false;
    if (!tried) { tried = true; if (HMODULE d = LoadLibraryA("d3dcompiler_47.dll")) reflect = (Reflect_t)GetProcAddress(d, "D3DReflect"); }
    if (!reflect || !code || len < 32) return;
    ID3D11ShaderReflection* r = nullptr;
    if (FAILED(reflect(code, len, __uuidof(ID3D11ShaderReflection), (void**)&r)) || !r) return;
    TaaCs t;
    D3D11_SHADER_DESC sd{};
    if (SUCCEEDED(r->GetDesc(&sd)))
        for (UINT i = 0; i < sd.BoundResources; i++)
        {
            D3D11_SHADER_INPUT_BIND_DESC b{};
            if (FAILED(r->GetResourceBindingDesc(i, &b)) || !b.Name) continue;
            if (!strcmp(b.Name, "ro_taahistory_read")) t.histSrv = (int)b.BindPoint;
            else if (!strcmp(b.Name, "rw_taahistory_write")) t.histUav = (int)b.BindPoint;
            else if (!strcmp(b.Name, "ro_motionvectors")) t.mvSrv = (int)b.BindPoint;
            else if (!strcmp(b.Name, "PerViewCB")) t.viewCb = (int)b.BindPoint;
        }
    if (t.histSrv >= 0 && t.histUav >= 0 && t.viewCb >= 0)
        if (ID3D11ShaderReflectionConstantBuffer* cb = r->GetConstantBufferByName("PerViewCB"))
        {
            if (ID3D11ShaderReflectionVariable* v = cb->GetVariableByName("cb_viewmatrix"))
            {
                D3D11_SHADER_VARIABLE_DESC vd{};
                if (SUCCEEDED(v->GetDesc(&vd))) t.viewMatOff = (int)vd.StartOffset;
            }
            D3D11_SHADER_VARIABLE_DESC vd{};
            if (ID3D11ShaderReflectionVariable* v = cb->GetVariableByName("cb_viewprojectionmatrix")) if (SUCCEEDED(v->GetDesc(&vd))) g_VPOff = (int)vd.StartOffset;
            if (ID3D11ShaderReflectionVariable* v = cb->GetVariableByName("cb_previousviewprojectionmatrix")) if (SUCCEEDED(v->GetDesc(&vd))) g_PrevVPOff = (int)vd.StartOffset;
        }
    r->Release();
    if (t.histSrv < 0 || t.histUav < 0 || t.viewCb < 0 || t.viewMatOff < 0) return;
    g_ViewMatOff = t.viewMatOff;
    { std::lock_guard lk(g_CsM); g_TaaCs[cs] = t; }
    g_TaaShaders++;
    LOGI("StereoTaa: TAA compute shader {} ({:016x}): history t{} -> u{}, motion vectors t{}, PerViewCB b{} (view matrix at +{})",
         (void*)cs, GpuTrace::ShaderHashOf(cs), t.histSrv, t.histUav, t.mvSrv, t.viewCb, t.viewMatOff);
}

void StereoTaa::OnMainView(void* view, int eye, const float vm[16])
{
    if (!view) return;
    uint8_t* v = (uint8_t*)view;
    const unsigned frame = *(const uint32_t*)(v + kFrameNo);
    std::lock_guard lk(g_VM);
    ViewRec& rec = g_Ring[++g_ViewSeq % 16];
    rec.seq = g_ViewSeq; rec.frame = frame; rec.eye = eye; memcpy(rec.m, vm, 64); memcpy(rec.jitter, v + kCurJitter, 8); rec.prevMine = false;
    if (g_Mode.load() != 1 || eye < 0 || !VRRuntime::Stereo())
    {
        g_HaveLast = false; g_Prev[0].valid = g_Prev[1].valid = false;
        return;
    }
    if (g_HaveLast && frame == g_LastFrame)
    {   // the view built again within one engine frame: no copy to "previous" happened, our fields are still in place
        g_SecondBuild++;
        for (auto& r : g_Ring) if (r.seq && r.seq != rec.seq && r.frame == frame) { rec.prevMine = r.prevMine; break; }
        return;
    }
    {   // the jitter: learn this frame's sample, then give this eye the next sample of the whole sequence
        float* cj = (float*)(v + kCurJitter);
        JitObs& o = g_JitObs[frame % 64]; o.frame = frame; o.j[0] = cj[0]; o.j[1] = cj[1]; o.valid = true;
        const unsigned P = JitterPeriod(frame);
        g_JitPeriod = P;
        if (P && g_JitterRemap.load())
        {
            const unsigned k = g_EyeK[eye]++ % P;
            // the engine's sample k: the frame f (among the last P) whose position in the period is k
            const unsigned base = frame - (frame % P);
            unsigned f = base + k; if (f > frame) f -= P;
            const JitObs& want = g_JitObs[f % 64];
            if (want.valid && want.frame == f)
            {
                float* pr = (float*)(v + kProj);
                // the projection carries twice the jitter field (measured: 0.00003 -> p02 0.00006), on top of whatever
                // else is there: move it by the difference, then rebuild the inverse the engine already derived
                pr[2] += 2.f * (want.j[0] - cj[0]); pr[6] += 2.f * (want.j[1] - cj[1]);
                cj[0] = want.j[0]; cj[1] = want.j[1];
                float inv[16]; if (Inverse4(pr, inv)) memcpy(v + kInvProj, inv, 64);
                memcpy(rec.jitter, cj, 8);
                g_JitRemapped++;
            }
        }
    }
    // The engine has just copied the last frame's matrices into "previous": they belong to the eye that frame was
    // rendered from. Keep them for that eye, and hand this frame the ones of ITS eye's last frame.
    if (g_HaveLast && g_LastEye >= 0) { PrevSet& p = g_Prev[g_LastEye]; ReadPrev(v, p); p.valid = true; p.frame = g_LastFrame; }
    const float* enginePrevVP = g_HaveLast && g_LastEye >= 0 ? (const float*)g_Prev[g_LastEye].vp : nullptr;
    const bool reset = v[kResetFlag] != 0;   // a cut: the engine makes previous = current itself, after this
    if (g_ViewLog.load() > 0)
    {
        g_ViewLog--;
        const float* pp = (const float*)(v + kPrevProj); const float* pj = (const float*)(v + kPrevJitter); const float* cj = (const float*)(v + kCurJitter);
        const float* cp = (const float*)(v + 0x7390);
        LOGI("aaviews: frame {} eye {} | cur jitter ({:.5f},{:.5f}) prev jitter ({:.5f},{:.5f}) | cur proj row0 {:.5f} {:.5f} {:.5f} {:.5f} row1 {:.5f} {:.5f} {:.5f} {:.5f} | prev proj row0 {:.5f} {:.5f} {:.5f} {:.5f} row1 {:.5f} {:.5f} {:.5f} {:.5f}",
             frame, eye, cj[0], cj[1], pj[0], pj[1], cp[0], cp[1], cp[2], cp[3], cp[4], cp[5], cp[6], cp[7], pp[0], pp[1], pp[2], pp[3], pp[4], pp[5], pp[6], pp[7]);
    }
    if (reset) g_Resets++;
    else if (g_HaveLast && g_LastEye == eye) { g_SameEye++; rec.prevMine = true; }   // the same eye twice in a row: "previous" is already its own
    else if (g_Prev[eye].valid && frame - g_Prev[eye].frame <= kMaxAge)
    {
        WritePrev(v, g_Prev[eye]); g_Patched++; rec.prevMine = true;
        // The engine's own "previous" stays in every per-object previous MVP it CACHED from the last frame (entities:
        // the arms, the weapons, doors, props -- only the static world is drawn from the view's matrices): those are
        // taken to this eye's last view by C, per draw (GpuTrace).
        float inv[16];
        if (enginePrevVP && Inverse4(enginePrevVP, inv)) { Mul4((const float*)g_Prev[eye].vp, inv, rec.C); rec.hasC = true; }
    }
    else g_NoPrev++;
    g_LastFrame = frame; g_LastEye = eye; g_HaveLast = true;
}

bool StereoTaa::ViewMatrixOf(ID3D11DeviceContext* c, ID3D11Buffer* viewCb, float m[16])
{
    if (!IsViewCb(viewCb)) return false;
    std::lock_guard lk(g_SM);
    const CbShadow* best = nullptr;
    for (auto& s : g_Sh)
        if (s.buf == viewCb && s.stamp && (!best || (s.ctx == c) > (best->ctx == c) || ((s.ctx == c) == (best->ctx == c) && s.stamp > best->stamp))) best = &s;
    if (!best) return false;
    memcpy(m, best->m, 64);
    return true;
}

bool StereoTaa::FixingPrev() { return g_FixPrev.load(std::memory_order_relaxed); }

bool StereoTaa::PrevCorrection(ID3D11DeviceContext* c, ID3D11Buffer* viewCb, float C[16], const float* prevRows, const float* curRows)
{
    if (!IsViewCb(viewCb)) { g_CNoView++; return false; }
    float m[16], vp[16], pvp[16]; bool have = false;
    {
        std::lock_guard lk(g_SM);
        const CbShadow* best = nullptr;
        for (auto& s : g_Sh)
            if (s.buf == viewCb && s.stamp && (!best || (s.ctx == c) > (best->ctx == c) || ((s.ctx == c) == (best->ctx == c) && s.stamp > best->stamp))) best = &s;
        if (best) { memcpy(m, best->m, 64); memcpy(vp, best->vp, 64); memcpy(pvp, best->pvp, 64); have = true; }
    }
    if (!have) { g_CNoView++; return false; }
    float Cr[16]; unsigned frame = 0; int eye = -1; bool tr = false;
    {
        std::lock_guard lk(g_VM);
        const ViewRec* rec = nullptr;
        for (auto& r : g_Ring)
        {
            if (!r.seq) continue;
            float e0 = 0, e1 = 0;
            for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++)
            { e0 = std::max(e0, fabsf(m[i * 4 + j] - r.m[i * 4 + j])); e1 = std::max(e1, fabsf(m[i * 4 + j] - r.m[j * 4 + i])); }
            if (std::min(e0, e1) > 1e-4f) continue;
            rec = &r; tr = e1 < e0; break;
        }
        if (!rec) { g_CNoMatch++; return false; }
        if (!rec->hasC) { g_CNone++; return false; }
        memcpy(Cr, rec->C, 64); frame = rec->frame; eye = rec->eye;
    }
    // Two kinds of draws carry a previous MVP in cb2 (measured 2026-10-14 with vr aadraws): most entities a matrix the
    // engine CACHED from the last frame (the other eye's view: 3-8 px off, exactly right after C), a few one it built
    // from the view's previous matrices (already this eye's: C would put them ~5 px off). The draw's own data decides:
    // C is applied when it brings the previous position closer to the static world's rule, previous VP * model (the
    // model from the current MVP and the current VP, both in this draw's PerViewCB). A moving object adds the same
    // motion to both candidates; far away, where the eyes barely differ, either choice is right to a fraction of a pixel.
    bool apply = true; float eEng = -1, eFix = -1;
    if (prevRows && curRows && g_VPOff.load() >= 0 && g_PrevVPOff.load() >= 0)
    {
        float VP[16], PVP[16];
        for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) { VP[i * 4 + j] = tr ? vp[j * 4 + i] : vp[i * 4 + j]; PVP[i * 4 + j] = tr ? pvp[j * 4 + i] : pvp[i * 4 + j]; }
        float vpi[16], Mw[16], ref[16], fixd[16];
        if (Inverse4(VP, vpi))
        {
            Mul4(vpi, curRows, Mw); Mul4(PVP, Mw, ref); Mul4(Cr, prevRows, fixd);
            static const float pts[3][4] = { { 0, 0, 0, 1 }, { 0.2f, 0.1f, 0.f, 1 }, { -0.1f, 0.f, 0.2f, 1 } };
            eEng = 0; eFix = 0;
            for (auto& P : pts)
            {
                auto ndc = [&](const float* M, float& x, float& y)
                {
                    float c4[4]; for (int i = 0; i < 4; i++) c4[i] = M[i * 4] * P[0] + M[i * 4 + 1] * P[1] + M[i * 4 + 2] * P[2] + M[i * 4 + 3] * P[3];
                    const float w = fabsf(c4[3]) > 1e-6f ? c4[3] : 1e-6f; x = c4[0] / w; y = c4[1] / w;
                };
                float rx, ry, ex, ey, fx, fy; ndc(ref, rx, ry); ndc(prevRows, ex, ey); ndc(fixd, fx, fy);
                eEng += sqrtf((ex - rx) * (ex - rx) + (ey - ry) * (ey - ry));
                eFix += sqrtf((fx - rx) * (fx - rx) + (fy - ry) * (fy - ry));
            }
            apply = eFix < eEng;
            const float px = 1133.f / 3.f;   // summed NDC distance -> mean pixels of a 2266-wide frame
            eEng *= px; eFix *= px;
        }
    }
    if (g_DrawDiag.load() > 0 && g_DrawDiag.fetch_sub(1) > 0)
        LOGI("aadraws: frame {} eye {} | from the static world's previous: engine {:.3f} px, corrected {:.3f} px -> {}", frame, eye, eEng, eFix, apply ? "corrected" : "engine's kept");
    if (!apply) { g_CKept++; return false; }
    memcpy(C, Cr, 64); g_CFound++;
    return true;
}

bool StereoTaa::PrevIsSameEye(const float vm[16])
{
    std::lock_guard lk(g_VM);
    for (auto& r : g_Ring) if (r.seq && !memcmp(r.m, vm, 64)) return r.prevMine;
    return false;
}

void StereoTaa::OnMap(ID3D11DeviceContext* c, ID3D11Resource* r, void* data)
{
    if (!IsViewCb(r) || !BigEnough(r)) return;
    t_MapRes = r; t_MapPtr = data;
}
void StereoTaa::OnUnmap(ID3D11DeviceContext* c, ID3D11Resource* r)
{
    if (!t_MapRes || t_MapRes != r) return;
    StoreCb(c, r, t_MapPtr);
    t_MapRes = nullptr; t_MapPtr = nullptr;
}
void StereoTaa::OnUpdate(ID3D11DeviceContext* c, ID3D11Resource* r, const void* data)
{
    if (IsViewCb(r) && BigEnough(r)) StoreCb(c, r, data);
}

void StereoTaa::OnDraw(ID3D11DeviceContext* c, int kind, int type, unsigned count, Pass& p)
{
    if (g_ProbeFrames.load() <= 0) return;
    const bool all = g_ProbeAll.load();
    if (!all && count > 6) return;
    ID3D11RenderTargetView* rtv[8] = {}; ID3D11DepthStencilView* dsv = nullptr;
    c->OMGetRenderTargets(8, rtv, &dsv);
    ID3D11VertexShader* vs = nullptr; c->VSGetShader(&vs, nullptr, nullptr);
    ID3D11PixelShader* ps = nullptr; c->PSGetShader(&ps, nullptr, nullptr);
    ID3D11ShaderResourceView* srv[16] = {}; if (!all) c->PSGetShaderResources(0, 16, srv);
    D3D11_VIEWPORT vp{}; UINT nvp = 1; c->RSGetViewports(&nvp, &vp);
    {
        std::lock_guard lk(g_PM);
        std::string rt;
        for (int i = 0; i < 8; i++) if (rtv[i]) rt += fmt::format(" o{}={}", i, ViewRes(rtv[i]));
        g_Lines.push_back(fmt::format("f{} #{} c{} k{} draw{} n{} vs {} ps {} vp {}x{} |{} ds={} |{}", g_Frame, g_Seq++, IdOf(g_CtxId, c), kind, type, count,
                                      Hash(vs), Hash(ps), (int)vp.Width, (int)vp.Height, rt, ViewRes(dsv), SrvList(srv, 16)));
    }
    ReleaseAll(rtv, 8); if (dsv) dsv->Release(); if (vs) vs->Release(); if (ps) ps->Release(); ReleaseAll(srv, 16);
}

void StereoTaa::OnDispatch(ID3D11DeviceContext* c, int kind, unsigned x, unsigned y, unsigned z, bool indirect, Pass& p)
{
    ID3D11ComputeShader* cs = nullptr; c->CSGetShader(&cs, nullptr, nullptr);
    TaaCs t; const bool taa = cs && FindTaa(cs, t);
    if (g_ProbeFrames.load() > 0)
    {
        ID3D11ShaderResourceView* srv[16] = {}; c->CSGetShaderResources(0, 16, srv);
        ID3D11UnorderedAccessView* uav[8] = {}; c->CSGetUnorderedAccessViews(0, 8, uav);
        std::string extra;
        if (taa) { unsigned f = 0; std::string why; const int e = EyeOfDispatch(c, t.viewCb, f, &why); extra = fmt::format(" | TAA eye {} ({})", e, why); }
        {
            std::lock_guard lk(g_PM);
            std::string u;
            for (int i = 0; i < 8; i++) if (uav[i]) u += fmt::format(" u{}={}", i, ViewRes(uav[i]));
            g_Lines.push_back(fmt::format("f{} #{} c{} k{} dispatch{} {}x{}x{} cs {} |{} |{}{}", g_Frame, g_Seq++, IdOf(g_CtxId, c), kind, indirect ? "I" : "",
                                          x, y, z, Hash(cs), u, SrvList(srv, 16), extra));
        }
        ReleaseAll(srv, 16); ReleaseAll(uav, 8);
    }
    if (cs) cs->Release();
    if (!taa) return;
    g_Dispatches++;

    if (g_MvReq.load() > 0 && t.mvSrv >= 0)
    {   // vr aamv: copy the motion vectors this dispatch reads (read back at a later Present)
        ID3D11ShaderResourceView* mv = nullptr; c->CSGetShaderResources(t.mvSrv, 1, &mv);
        if (mv)
        {
            ID3D11Resource* r = nullptr; mv->GetResource(&r); mv->Release();
            D3D11_TEXTURE2D_DESC d{}; ((ID3D11Texture2D*)r)->GetDesc(&d);
            std::lock_guard lk(g_MvM);
            if (g_MvTex && g_MvDescValid && SameDesc(d, g_MvDesc) && !g_MvPending)
            {
                c->CopyResource(g_MvTex, r); g_MvPending = 4; g_MvReq = 0;
                unsigned f = 0; g_MvEye = EyeOfDispatch(c, t.viewCb, f); g_MvFrame = f;
            }
            else { g_MvDesc = d; g_MvDescValid = true; }
            r->Release();
        }
    }

    if (g_Mode.load() != 1 || !VRRuntime::Stereo() || ScreenMode::FlatRender()) { DumpAtDispatch(c, t, -1); return; }
    unsigned frame = 0;
    const int eye = EyeOfDispatch(c, t.viewCb, frame);
    if (eye < 0) { g_NoEye++; DumpAtDispatch(c, t, -2); return; }
    g_EyeN[eye & 1]++;
    ID3D11UnorderedAccessView* uav = nullptr; c->CSGetUnorderedAccessViews(t.histUav, 1, &uav);
    if (!uav) return;
    ID3D11Resource* out = nullptr; uav->GetResource(&out); uav->Release();
    D3D11_RESOURCE_DIMENSION dim{}; out->GetType(&dim);
    if (dim != D3D11_RESOURCE_DIMENSION_TEXTURE2D) { out->Release(); return; }
    D3D11_TEXTURE2D_DESC d{}; ((ID3D11Texture2D*)out)->GetDesc(&d);
    ID3D11ShaderResourceView* srv = nullptr; c->CSGetShaderResources(t.histSrv, 1, &srv);
    std::lock_guard lk(g_HM);
    if (!g_HistDescValid || !SameDesc(d, g_HistDesc)) { g_HistDesc = d; g_HistDescValid = true; }   // (re)made at the next Present
    if (srv && !g_SrvDescValid) { srv->GetDesc(&g_SrvDesc); g_SrvDescValid = true; }
    if (!g_Hist[eye] || !g_HistSrv[eye] || !SameDesc(d, g_HistDesc))
    {
        g_NoTex++; if (srv) srv->Release(); out->Release(); return;
    }
    p.what = 1; p.eye = eye; p.out = out;
    p.keep = g_Hist[eye]; p.keep->AddRef();
    if (g_HistValid[eye] && frame - g_HistFrame[eye] <= kMaxAge)
    {   // read this eye's own history
        ID3D11ShaderResourceView* mine = g_HistSrv[eye];
        c->CSSetShaderResources(t.histSrv, 1, &mine);
        p.srvSlot = t.histSrv; p.savedSrv = srv; srv = nullptr;
        g_Swapped++;
    }
    else g_FirstUse++;   // none yet (or stale): this one frame blends with the engine's, as before
    g_HistValid[eye] = true; g_HistFrame[eye] = frame;
    if (srv) srv->Release();
    DumpAtDispatch(c, t, eye);
}

void StereoTaa::AfterPass(ID3D11DeviceContext* c, int kind, Pass& p)
{
    if (p.what != 1) return;
    if (p.srvSlot >= 0) { c->CSSetShaderResources(p.srvSlot, 1, &p.savedSrv); if (p.savedSrv) p.savedSrv->Release(); }
    if (p.keep && p.out) c->CopyResource(p.keep, p.out);   // what this eye's TAA wrote is its history next time
    if (p.keep) p.keep->Release();
    if (p.out) p.out->Release();
    p = Pass();
}

void StereoTaa::OnCopy(ID3D11DeviceContext* c, int kind, ID3D11Resource* dst, ID3D11Resource* src, bool region)
{
    if (g_ProbeFrames.load() <= 0) return;
    std::lock_guard lk(g_PM);
    g_Lines.push_back(fmt::format("f{} #{} c{} k{} copy{} {} <- {}", g_Frame, g_Seq++, IdOf(g_CtxId, c), kind, region ? "region" : "", Res(dst), Res(src)));
}

void StereoTaa::OnPresent(IDXGISwapChain* sc)
{
    ID3D11Device* dev = nullptr;
    if (sc && SUCCEEDED(sc->GetDevice(__uuidof(ID3D11Device), (void**)&dev)) && dev)
    {
        {   // the eyes' history textures: made (or remade) here, never from a recording thread
            std::lock_guard lk(g_HM);
            const bool active = g_Mode.load() == 1 && VRRuntime::Stereo() && !ScreenMode::FlatRender();   // the menu screen renders mono
            g_FixPrev = active;
            g_IdleFrames = active ? 0 : g_IdleFrames + 1;
            if (g_IdleFrames > 300 && g_Hist[0]) { ReleaseHist(); LOGI("StereoTaa: eye histories released (stereo TAA not in use)"); }
            if (active && g_HistDescValid && (!g_Hist[0] || !g_Hist[1] || [&] { D3D11_TEXTURE2D_DESC h{}; g_Hist[0]->GetDesc(&h); return !SameDesc(h, g_HistDesc); }()))
            {
                ReleaseHist();
                D3D11_TEXTURE2D_DESC d = g_HistDesc;
                d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_SHADER_RESOURCE; d.CPUAccessFlags = 0; d.MiscFlags = 0;
                bool ok = true;
                for (int i = 0; i < 2 && ok; i++)
                {
                    ok = SUCCEEDED(dev->CreateTexture2D(&d, nullptr, &g_Hist[i])) && g_Hist[i]
                      && SUCCEEDED(dev->CreateShaderResourceView(g_Hist[i], g_SrvDescValid ? &g_SrvDesc : nullptr, &g_HistSrv[i])) && g_HistSrv[i];
                }
                if (!ok) { ReleaseHist(); g_HistDescValid = false; LOGI("StereoTaa: could not create the eye histories ({}x{} format {})", d.Width, d.Height, (int)d.Format); }
                else { g_HistGen++; LOGI("StereoTaa: eye histories {}x{} format {} (generation {})", d.Width, d.Height, (int)d.Format, g_HistGen); }
            }
        }
        DumpPresent(dev);
        {   // the safety net (see g_FellBack)
            const bool active = g_Mode.load() == 1 && VRRuntime::Stereo() && !ScreenMode::FlatRender();   // the menu screen renders mono
            if (!VRRuntime::Stereo() && g_FellBack) { g_FellBack = false; ApplySkip(false); }
            if (active != g_WasActive) { g_BadWindows = 0; g_WinFrames = 0; g_WinDisp0 = g_Dispatches; g_WinSwap0 = g_Swapped; g_WinFirst0 = g_FirstUse; }
            g_WasActive = active;
            if (active && !g_FellBack && ++g_WinFrames >= 120)
            {
                const unsigned long long d = g_Dispatches - g_WinDisp0, sw = g_Swapped - g_WinSwap0, fu = g_FirstUse - g_WinFirst0;
                // per present one main-view TAA ran with its eye's history (other views' TAA, if any, do not count against it)
                if (d >= 60) g_BadWindows = sw + fu < 30 ? g_BadWindows + 1 : 0;
                g_WinFrames = 0; g_WinDisp0 = g_Dispatches; g_WinSwap0 = g_Swapped; g_WinFirst0 = g_FirstUse;
                if (g_BadWindows >= 3)
                {
                    g_FellBack = true;
                    ApplySkip(true);
                    LOGI("StereoTaa: FALLBACK -- the TAA could not be given each eye's own history ({} of {} dispatches, no eye {}, no shadow {}, no match {}, no texture {}): TAA off for this stereo session, as before 2026-10-14",
                         sw, d, g_NoEye.load(), g_NoShadow.load(), g_NoMatch.load(), g_NoTex.load());
                }
            }
        }
        {   // vr aamv
            std::lock_guard lk(g_MvM);
            if (g_MvReq.load() > 0 && g_MvDescValid && !g_MvTex)
            {
                D3D11_TEXTURE2D_DESC d = g_MvDesc; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = 0; d.CPUAccessFlags = 0; d.MiscFlags = 0;
                dev->CreateTexture2D(&d, nullptr, &g_MvTex);
            }
            if (g_MvTex && g_MvDescValid) { D3D11_TEXTURE2D_DESC h{}; g_MvTex->GetDesc(&h); if (!SameDesc(h, g_MvDesc)) { g_MvTex->Release(); g_MvTex = nullptr; g_MvPending = 0; } }
            if (g_MvPending > 0 && --g_MvPending == 0 && g_MvTex)
            {
                ID3D11DeviceContext* imm = nullptr; dev->GetImmediateContext(&imm);
                if (imm) { MvStats(dev, imm); imm->Release(); }
            }
        }
        dev->Release();
    }

    const int left = g_ProbeFrames.load();
    if (left <= 0) return;
    std::lock_guard lk(g_PM);
    g_Lines.push_back(fmt::format("---- present (frame {})", g_Frame));
    g_Frame++;
    if (left == 1)
    {
        std::ofstream f(GameInfo::GameDir() + L"\\DVR_aaprobe.txt", std::ios::trunc);
        for (auto& l : g_Lines) f << l << "\n";
        LOGI("aaprobe: {} lines -> DVR_aaprobe.txt ({} resources, {} contexts)", g_Lines.size(), g_ResId.size(), g_CtxId.size());
        g_Lines.clear(); g_ResId.clear(); g_CtxId.clear();
    }
    g_ProbeFrames = left - 1;
}

void StereoTaa::SetMode(int m) { g_Mode = m < 0 ? 0 : m > 2 ? 2 : m; ApplySkip(VRRuntime::Stereo()); }
void StereoTaa::OnStereoChange(bool on) { if (!on) g_FellBack = false; ApplySkip(on); }
int StereoTaa::Mode() { return g_Mode.load(); }
bool StereoTaa::KeepTaaInStereo() { return g_Mode.load() != 0; }

bool StereoTaa::Command(const std::string& what, std::istringstream& ss)
{
    if (what == "aaprobe")
    {
        int n = 2; std::string mode; ss >> n >> mode;
        {
            std::lock_guard lk(g_PM);
            g_Lines.clear(); g_ResId.clear(); g_CtxId.clear(); g_Frame = 0; g_Seq = 0;
        }
        g_ProbeAll = mode == "all";
        g_ProbeFrames = n < 1 ? 1 : n > 8 ? 8 : n;
        LOGI("cmd vr aaprobe {} {}", n, mode);
        return true;
    }
    if (what == "aamv")
    {
        std::string tag = "mv"; ss >> tag;
        { std::lock_guard lk(g_MvM); g_MvTag = tag; }
        g_MvReq = 1;
        LOGI("cmd vr aamv {}", tag);
        return true;
    }
    if (what == "aadump")
    {
        std::string tag = "d"; ss >> tag;
        { std::lock_guard lk(g_DumpM); g_DumpTag = tag; }
        g_DumpReq = 1;
        LOGI("cmd vr aadump {}", tag);
        return true;
    }
    if (what == "aabreak") { int m = 1; ss >> m; g_Break = m != 0; LOGI("cmd vr aabreak {}", m); return true; }
    if (what == "aaviews") { int n = 8; ss >> n; g_ViewLog = n; return true; }
    if (what == "aajitter") { int m = 1; ss >> m; g_JitterRemap = m != 0; LOGI("cmd vr aajitter {} (each eye {} of the jitter sequence)", m, m ? "walks the whole" : "gets every other sample"); return true; }
    if (what == "aadraws") { int n = 12; ss >> n; g_DrawDiag = n; return true; }
    if (what == "aaarms")
    {
        int m = 1; ss >> m;
        GpuTrace::SetPrevPerEye(m != 0);
        LOGI("cmd vr aaarms {} (moved draws' previous delta: {})", m, m ? "the same eye's last frame" : "the last frame (either eye)");
        return true;
    }
    if (what == "stereotaa")
    {
        int m = 1; ss >> m;
        SetMode(m);
        LOGI("cmd vr stereotaa {} ({})", g_Mode.load(), g_Mode.load() == 1 ? "each eye its own TAA" : g_Mode.load() == 0 ? "TAA off in stereo" : "research: TAA with the engine's shared history");
        return true;
    }
    return false;
}

std::string StereoTaa::Status()
{
    std::lock_guard lk(g_VM);
    return fmt::format("stereotaa mode {}{} shaders {} dispatches {} swapped {} firstuse {} noeye {} (noshadow {} nomatch {}) notex {} eyes {}/{} | prev patched {} sameeye {} second {} resets {} noprev {} | probe {}",
                       g_Mode.load(), g_FellBack ? " FELLBACK" : "", g_TaaShaders.load(), g_Dispatches.load(), g_Swapped.load(), g_FirstUse.load(), g_NoEye.load(),
                       g_NoShadow.load(), g_NoMatch.load(), g_NoTex.load(), g_EyeN[0].load(), g_EyeN[1].load(),
                       g_Patched, g_SameEye, g_SecondBuild, g_Resets, g_NoPrev, g_ProbeFrames.load())
         + fmt::format(" | jitter period {} remapped {}", g_JitPeriod, g_JitRemapped) + " | " + GpuTrace::XfViewStats()
         + fmt::format(" | drawfix {} corrected {} kept {} noview {} nomatch {} none {}", GpuTrace::EyeFixedDraws(), g_CFound.load(), g_CKept.load(), g_CNoView.load(), g_CNoMatch.load(), g_CNone.load());
}
