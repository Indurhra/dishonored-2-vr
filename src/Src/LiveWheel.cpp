#include "pch.h"
#include "LiveWheel.h"
#include "WheelSelection.h"
#include "GameInfo.h"
#include "Game.h"
#include "VRRuntime.h"
#include "KbmInject.h"
#include "XRCapture.h"
#include "WheelRenderer.h"
#include <detours/detours.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <algorithm>
#include <vector>
#include <cstring>

namespace
{
    using StateFn = uintptr_t (*)(void*);
    StateFn g_Enter = nullptr, g_Exit = nullptr, g_Start = nullptr, g_Stop = nullptr, g_StartFx = nullptr;
    StateFn g_Rebuild = nullptr;
    using EquipFn = void (*)(void*);
    EquipFn g_Equip = nullptr;
    std::atomic<bool> g_Enabled{false}, g_Active{false}, g_Confirm{false}, g_Cancel{false};
    std::atomic<bool> g_Closed{false};
    std::atomic<void*> g_Owner{nullptr};
    std::atomic<int> g_Selected{-1};
    std::atomic<int> g_Page{0};
    bool g_PageLatch=false;
    constexpr int kPerPage=WheelModel::PerPage;
    std::atomic<unsigned> g_Generation{0};
    bool g_Held = false;
    std::atomic<bool> g_Ready{false};
    void* g_Player = nullptr;
    using Item=WheelModel::Item;
    WheelRenderer g_Renderer;
    std::atomic<int> g_Equipped{-1};
    std::mutex g_M;
    std::vector<Item> g_Items;
    std::atomic<unsigned long long> g_Opens{0}, g_Equips{0}, g_Cancels{0}, g_SlowSkipped{0}, g_FxSkipped{0};
    ID3D11Texture2D* g_Tex=nullptr;
    ID3D11RenderTargetView* g_Rtv=nullptr;
    ID3D11ShaderResourceView* g_Srv=nullptr;
    unsigned g_Placed=~0u;
    unsigned g_ReferenceGen=0;
    float g_Pos[3]={}, g_Quat[4]={0,0,0,1};
    constexpr unsigned kSize=1024;
    std::atomic<bool> g_Shot{false};
    std::string g_ShotTag;

    template<class T> T Fn(uintptr_t rva) { return reinterpret_cast<T>(GameInfo::Base()+rva); }
    int Token(void* self) { return *reinterpret_cast<int*>(static_cast<char*>(self)+232); }
    bool Owned(void* self) { return self && self==g_Owner.load(); }
    std::string DeclName(const void* decl)
    {
        if(!decl) return {};
        const char* text=*reinterpret_cast<const char*const*>(static_cast<const char*>(decl)+8);
        MEMORY_BASIC_INFORMATION m{};
        if(!text || !VirtualQuery(text,&m,sizeof(m)) || m.State!=MEM_COMMIT || (m.Protect&(PAGE_GUARD|PAGE_NOACCESS))) return {};
        const size_t length=std::min<size_t>(512,static_cast<const char*>(m.BaseAddress)+m.RegionSize-text);
        return std::string(text,strnlen(text,length));
    }
    void Populate(void* self)
    {
        std::vector<Item> items;
        auto* b=static_cast<char*>(self);
        const int n=*reinterpret_cast<unsigned*>(b+4056)&0xffffff;
        const auto* entries=*reinterpret_cast<const char**>(b+4048);
        if (entries && n>=0 && n<=128)
            for(int i=0;i<n;++i)
            {
                const char* e=entries+i*40;
                if (!e[20]) continue; // use the game's availability flag, not a declaration catalogue
                auto* localized=Fn<void*(*)(const void*)>(0x1653A0)(e);
                const char* text=localized ? Fn<const char*(*)(void*)>(0x1D9090)(localized) : nullptr;
                items.push_back({*reinterpret_cast<const int*>(e+8), *reinterpret_cast<const int*>(e+12),
                                 *reinterpret_cast<const int*>(e+16), text ? text : "",
                                 DeclName(*reinterpret_cast<void*const*>(e+24)),DeclName(*reinterpret_cast<void*const*>(e+32))});
            }
        g_Equipped=Fn<int(*)(void*)>(0x4F5B30)(self);
        std::lock_guard lk(g_M);
        if(g_Active.load())
        {
            int page=g_Page.load(),pick=g_Selected.load();
            WheelModel::Reconcile(g_Items,items,page,pick,g_PageLatch);
            if(pick<0) g_Confirm=false;
            g_Page=page;g_Selected=pick;
        }
        g_Items=std::move(items);
    }
    uintptr_t Rebuild(void* self)
    {
        const auto result=g_Rebuild(self);
        // Observe native rebuilds on the engine's own thread. Do not mutate or
        // poll the inventory from the render/OpenXR thread.
        if(Owned(self) && g_Active.load() && Game::LocalPlayer()==g_Player) Populate(self);
        return result;
    }
    uintptr_t Enter(void* self)
    {
        if (!g_Enabled.load()) return g_Enter(self);
        g_Closed=false;
        g_Owner=self; g_Confirm=false; g_Cancel=false; g_Selected=-1; g_Held=false; g_Ready=false; g_Page=0; g_PageLatch=false;
        g_Player=Game::LocalPlayer();
        const auto result=g_Enter(self); // constructs the native, owned/available inventory list
        if (!*reinterpret_cast<unsigned char*>(static_cast<char*>(self)+4064))
        {
            Populate(self); g_Generation++; g_Active=true; g_Opens++;
            LOGI("LiveWheel: opened native inventory ({} entries)", g_Items.size());
        }
        else { g_Owner=nullptr;g_Closed=true; }
        return result;
    }
    uintptr_t Exit(void* self)
    {
        const bool owned=Owned(self);
        if(g_Enabled.load() && *reinterpret_cast<uintptr_t*>(self)==GameInfo::Base()+0x1B6B918) g_Closed=true;
        if (owned) { g_Active=false; g_Confirm=false; g_Cancel=true; }
        const auto result=g_Exit(self);
        if (owned) { g_Owner=nullptr; g_Selected=-1; }
        return result;
    }
    uintptr_t Start(void* self)
    {
        if (Owned(self)) { g_SlowSkipped++; return 0; } // only this wheel's own BendTime token
        return g_Start(self);
    }
    uintptr_t Stop(void* self)
    {
        if (Owned(self) && Token(self)==-1) return 0;
        return g_Stop(self); // never discard an existing token or a power's time effect
    }
    uintptr_t StartFx(void* self)
    {
        // The UI base also serves pause/journal/shop states. Only this exact
        // native power-wheel class loses its menu-only post effect.
        if (g_Enabled.load() && *reinterpret_cast<uintptr_t*>(self)==GameInfo::Base()+0x1B6B918) { g_FxSkipped++; return 0; }
        return g_StartFx(self);
    }
    void Equip(void* self)
    {
        if (!Owned(self)) { g_Equip(self); return; }
        // The custom panel must complete both native Flash callbacks. WheelClosing
        // alone stops its effects but does not queue UI-state removal on cancellation.
        // OnWheelDisabled queues this state's ID once and sets its normal-exit flag.
        Fn<void(*)(void*)>(0x4F6510)(self);
        int id=-1;Item selected;
        { std::lock_guard lk(g_M); const int pick=g_Selected.load();
          if (pick>=0 && pick<(int)g_Items.size()) { selected=g_Items[pick];id=selected.id; } }
        DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
        if (!g_Confirm.exchange(false) || g_Cancel.load() || id<0 || !g_Ready || foreground!=GetCurrentProcessId() ||
            Game::LocalPlayer()!=g_Player || KbmInject::GetStats().mode!=2)
        { g_Cancels++; LOGI("LiveWheel: cancelled, no equip"); return; }
        // Rebuild from CURRENT inventory on the engine callback, then resolve its
        // controller and use the same equip call as WheelClosing. Never retain decl pointers.
        Fn<void(*)(void*)>(0x4F5360)(self);
        auto* b=static_cast<char*>(self);
        const unsigned n=*reinterpret_cast<unsigned*>(b+4056)&0xffffff;
        const char* entries=*reinterpret_cast<const char**>(b+4048);
        void* controller=Fn<void*(*)(void*,int)>(0x3DA910)(b+168,2);
        if (!controller || !entries || n>128) { g_Cancels++; return; }
        for (unsigned i=0;i<n;++i)
        {
            const char* e=entries+i*40;
            if (*reinterpret_cast<const int*>(e+8)!=id || !e[20]) continue;
            auto* primary=*reinterpret_cast<void*const*>(e+24);
            auto* secondary=*reinterpret_cast<void*const*>(e+32);
            if(DeclName(primary)!=selected.primary || DeclName(secondary)!=selected.secondary) break;
            Fn<void(*)(void*,void*,void*)>(0x7FB350)(controller,primary,secondary);
            g_Equips++; LOGI("LiveWheel: native equip item {}",id); return;
        }
        g_Cancels++; LOGI("LiveWheel: selection no longer available, cancelled");
    }
}

bool LiveWheel::Install()
{
    if (!GameInfo::IsSupportedBuild()) return false;
    const unsigned char common[]={0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b};
    const unsigned char start[]={0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83};
    const unsigned char equip[]={0x48,0x8b,0xc4,0x57,0x48,0x81,0xec,0xf0};
    const unsigned char fx[]={0x48,0x89,0x5c,0x24,0x10,0x56,0x48,0x83};
    const unsigned char rebuild[]={0x48,0x89,0x4c,0x24,0x08,0x53,0x57,0x41};
    for (auto rva : {0x4F5AE0,0x4F56B0,0x4F7530})
        if (memcmp(Fn<void*>(rva),common,8)) { LOGI("LiveWheel: lifecycle bytes differ, disabled"); return false; }
    if (memcmp(Fn<void*>(0x4F7490),start,8) || memcmp(Fn<void*>(0x4F5710),equip,8) || memcmp(Fn<void*>(0x7912E0),fx,8) ||
        memcmp(Fn<void*>(0x4F5360),rebuild,8)) return false;
    g_Enter=Fn<StateFn>(0x4F5AE0); g_Exit=Fn<StateFn>(0x4F56B0);
    g_Start=Fn<StateFn>(0x4F7490); g_Stop=Fn<StateFn>(0x4F7530); g_Equip=Fn<EquipFn>(0x4F5710);
    g_StartFx=Fn<StateFn>(0x7912E0);
    g_Rebuild=Fn<StateFn>(0x4F5360);
    DetourTransactionBegin(); DetourUpdateThread(GetCurrentThread());
    DetourAttach((PVOID*)&g_Enter,Enter); DetourAttach((PVOID*)&g_Exit,Exit);
    DetourAttach((PVOID*)&g_Start,Start); DetourAttach((PVOID*)&g_Stop,Stop); DetourAttach((PVOID*)&g_Equip,Equip);
    DetourAttach((PVOID*)&g_StartFx,StartFx);
    DetourAttach((PVOID*)&g_Rebuild,Rebuild);
    const auto result=DetourTransactionCommit();
    LOGI("LiveWheel: native lifecycle/inventory/equip hooks {}", result==NO_ERROR ? "installed" : "FAILED");
    return result==NO_ERROR;
}
bool LiveWheel::Active() { return g_Active.load(); }
bool LiveWheel::Enabled() { return g_Enabled.load(); }
bool LiveWheel::Closed() { return g_Closed.load(); }
void LiveWheel::RequestOpen() { if(Enabled()) g_Closed=false; }
void LiveWheel::Cancel() { if(Active()) { g_Cancel=true;g_Confirm=false;g_Selected=-1; } }
bool LiveWheel::Input(float x,float y,bool held,bool cancel,bool tracked,int pageDelta)
{
    if (!Active()) return false;
    if (g_Placed==g_Generation.load() && VRRuntime::GetHeadPose().gen!=g_ReferenceGen) tracked=false;
    if (cancel || !tracked || !g_Ready) { g_Cancel=true; g_Selected=-1; g_Confirm=false; return true; }
    if (g_Cancel.load()) return true;
    {
        std::lock_guard lk(g_M);
        const int pages=std::max(1,((int)g_Items.size()+kPerPage-1)/kPerPage);
        if(pageDelta) { g_Page=(g_Page.load()+pageDelta+pages)%pages;g_Selected=-1;g_PageLatch=true; }
        if(g_PageLatch && x*x+y*y<.35f*.35f) g_PageLatch=false;
        const int start=g_Page.load()*kPerPage, count=std::min(kPerPage,(int)g_Items.size()-start);
        const int local=g_PageLatch ? -1 : WheelSelection::Pick(x,y,count,g_Selected.load()-start);
        g_Selected=local<0 ? -1 : start+local;
    }
    if (g_Held && !held) g_Confirm=true;
    g_Held=held;
    return false;
}
void LiveWheel::Prepare(ID3D11Device* dev)
{
    // Load art before the first wheel opening, while the ImGui context is ready.
    const bool ready=g_Renderer.Initialize(dev,std::filesystem::path(GameInfo::GameDir())/L"DVR_assets"/L"wheel");
    static bool logged=false;
    if(!logged) { LOGI("LiveWheel: native artwork {}",ready ? "loaded" : "incomplete; missing icons use item labels"); logged=true; }
}
bool LiveWheel::Render(ID3D11Device* dev,ID3D11DeviceContext* ctx)
{
    if (!Active()) { g_Ready=false; return false; }
    if (!g_Srv)
    {
        D3D11_TEXTURE2D_DESC d{}; d.Width=d.Height=kSize; d.MipLevels=d.ArraySize=1;
        d.Format=DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count=1; d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        ID3D11Texture2D* tex=nullptr; ID3D11RenderTargetView* rtv=nullptr; ID3D11ShaderResourceView* srv=nullptr;
        if (FAILED(dev->CreateTexture2D(&d,nullptr,&tex)) || FAILED(dev->CreateRenderTargetView(tex,nullptr,&rtv)) ||
            FAILED(dev->CreateShaderResourceView(tex,nullptr,&srv)))
        {
            if(srv) srv->Release(); if(rtv) rtv->Release(); if(tex) tex->Release();
            g_Cancel=true; return false;
        }
        g_Tex=tex;g_Rtv=rtv;g_Srv=srv;
    }
    Prepare(dev);
    ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::GetIO().DisplaySize=ImVec2(kSize,kSize); ImGui::NewFrame();
    std::vector<Item> items; int page,pick;
    { std::lock_guard lk(g_M); items=g_Items;page=g_Page.load();pick=g_Selected.load(); }
    g_Renderer.Draw(items,page,pick,g_Equipped.load());
    ImGui::Render(); const float clear[4]={}; ctx->OMSetRenderTargets(1,&g_Rtv,nullptr); ctx->ClearRenderTargetView(g_Rtv,clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData()); g_Ready=true;
    if (g_Shot.exchange(false))
    {
        std::string tag; { std::lock_guard lk(g_M); tag=g_ShotTag; }
        const auto path=GameInfo::GameDir()+L"\\DVR_"+std::wstring(tag.begin(),tag.end())+L"_wheel.png";
        LOGI("LiveWheel: overlay PNG {}",XRCapture::SaveTextureToPng(dev,ctx,g_Tex,path.c_str()) ? "saved" : "FAILED");
    }
    return true;
}
ID3D11ShaderResourceView* LiveWheel::Texture() { return Active() && g_Ready ? g_Srv : nullptr; }
bool LiveWheel::PanelPose(float pos[3],float quat[4])
{
    if (!Active()) return false;
    const unsigned gen=g_Generation.load();
    if (gen!=g_Placed)
    {
        const auto head=VRRuntime::GetHeadPose(); XRInputState in;
        if (!head.valid || !VRRuntime::GetInput(in) || !in.hand[0].valid || in.hand[0].held) return false;
        // Capture beside the opening hand, facing the head; stays fixed in LOCAL space.
        float yaw=std::atan2(2*(head.quat[3]*head.quat[1]+head.quat[0]*head.quat[2]),1-2*(head.quat[1]*head.quat[1]+head.quat[2]*head.quat[2]));
        const float f[3]={-std::sin(yaw),0,-std::cos(yaw)};
        const float hand[3]={in.hand[0].posX,in.hand[0].posY,in.hand[0].posZ};
        for(int i=0;i<3;++i) g_Pos[i]=head.pos[i]*.35f+hand[i]*.65f+f[i]*.50f;
        const float forward=(g_Pos[0]-head.pos[0])*f[0]+(g_Pos[2]-head.pos[2])*f[2];
        if(forward<.55f) for(int i=0;i<3;++i) g_Pos[i]+=f[i]*(.55f-forward);
        g_Pos[1]=std::clamp(g_Pos[1],head.pos[1]-.4f,head.pos[1]+.15f);
        g_ReferenceGen=head.gen;
        g_Quat[0]=g_Quat[2]=0;g_Quat[1]=std::sin(yaw*.5f);g_Quat[3]=std::cos(yaw*.5f);g_Placed=gen;
    }
    memcpy(pos,g_Pos,12);memcpy(quat,g_Quat,16);return true;
}
bool LiveWheel::Command(const std::string& what,std::istringstream& ss)
{
    if (what=="wheelshot")
    {
        std::string tag="overlay"; ss>>tag;
        if (tag.empty() || tag.size()>64 || tag.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos) return true;
        { std::lock_guard lk(g_M); g_ShotTag=tag; }
        g_Shot=true; return true;
    }
    if (what=="livewheelcancel") { Cancel(); return true; }
    if (what!="livewheel") return false;
    int on=0;ss>>on;
    if (g_Owner.load()) LOGI("LiveWheel: close the wheel before changing mode");
    else { g_Enabled=on!=0; LOGI("LiveWheel: {}",on ? "live, native inventory and equip" : "original wheel"); }
    return true;
}
std::string LiveWheel::Status()
{
    return fmt::format("livewheel enabled {} active {} selection {} page {} opens {} equips {} cancels {} slowdownskipped {} menufxskipped {}",
        g_Enabled.load(),Active(),g_Selected.load(),g_Page.load()+1,g_Opens.load(),g_Equips.load(),g_Cancels.load(),g_SlowSkipped.load(),g_FxSkipped.load());
}
