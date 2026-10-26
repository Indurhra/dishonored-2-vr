#include "WheelRenderer.h"
#include "WheelSelection.h"
#include "XRCapture.h"
#include <imgui_impl_dx11.h>
#include <iostream>
#include <filesystem>

// An offscreen D3D11/WARP render of the production UI, with explicit fixture
// inventory. It neither launches the game nor writes to its installation.
int wmain(int argc,wchar_t** argv)
{
    if(argc!=3) { std::cerr<<"wheel_preview <extracted asset folder> <output folder>\n";return 2; }
    const std::filesystem::path root=argv[1],out=argv[2];std::filesystem::create_directories(out);
    using Microsoft::WRL::ComPtr;
    ComPtr<ID3D11Device> dev;ComPtr<ID3D11DeviceContext> ctx;D3D_FEATURE_LEVEL level;
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&dev,&level,&ctx))) return 3;
    D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=1024;d.MipLevels=d.ArraySize=1;
    d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> tex;ComPtr<ID3D11RenderTargetView> rtv;
    if(FAILED(dev->CreateTexture2D(&d,nullptr,&tex)) || FAILED(dev->CreateRenderTargetView(tex.Get(),nullptr,&rtv))) return 4;
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=ImVec2(1024,1024);io.DeltaTime=1.f/60;
    if(!ImGui_ImplDX11_Init(dev.Get(),ctx.Get())) return 5;
    WheelRenderer renderer;if(!renderer.Initialize(dev.Get(),root)) { std::cerr<<"Missing native wheel assets\n";return 6; }
    std::vector<WheelModel::Item> items;
    const auto add=[&](const char* name,int ammo,int max,const char* primary,const char* secondary="")
    {items.push_back({100+(int)items.size(),ammo,max,name,primary,secondary});};
    add("Pistol",20,20,"models/weapons/gun_player/gun_player.inventoryitem","components/equipment/ammo/bullet.inventoryitem");
    add("Crossbow",20,20,"models/weapons/crossbow/crossbow_corvo.inventoryitem","components/equipment/ammo/bolt_regular.inventoryitem");
    add("Sleep Dart",5,5,"models/weapons/crossbow/crossbow_corvo.inventoryitem","components/equipment/ammo/bolt_tranquilizer.inventoryitem");
    add("Grenade",4,4,"components/equipment/gadgets/regular_grenade.inventoryitem");
    add("Stun Mine",3,3,"components/equipment/gadgets/stun_mine.inventoryitem");
    add("Springrazor",3,3,"components/equipment/gadgets/springrazor.inventoryitem");
    add("Sticky Grenade",4,4,"components/equipment/gadgets/sticky_grenade.inventoryitem");
    add("Bend Time",-1,-1,"models/powers/bendtime/bendtime.inventoryitem");
    add("Blink",-1,-1,"models/powers/blink/blink.inventoryitem");
    add("Dark Vision",-1,-1,"models/powers/darkvision/darkvision.inventoryitem");
    add("Far Reach",-1,-1,"models/powers/grapple/grapple.inventoryitem");
    add("Domino",-1,-1,"models/powers/phantasmallink/phantasmal_link.inventoryitem");
    add("Doppelganger",-1,-1,"models/powers/doppelganger/doppelganger.inventoryitem");
    add("Mesmerize",-1,-1,"models/powers/mesmerize/mesmerize.inventoryitem");
    add("Shadow Walk",-1,-1,"models/powers/shadow_walk/shadow_walk.inventoryitem");
    add("Possession",-1,-1,"models/powers/possession/possession.inventoryitem");
    add("Devouring Swarm",-1,-1,"models/powers/devouring_swarm/devouring_swarm.inventoryitem");
    add("Windblast",-1,-1,"models/powers/windblast/windblast.inventoryitem");
    add("Explosive Bullet",10,10,"models/weapons/gun_player/gun_player.inventoryitem","components/equipment/ammo/bullet_explosive.inventoryitem");
    add("Incendiary Bolt",5,5,"models/weapons/crossbow/crossbow_corvo.inventoryitem","components/equipment/ammo/bolt_incendiary.inventoryitem");
    add("Stinging Bolt",5,5,"models/weapons/crossbow/crossbow_corvo.inventoryitem","components/equipment/ammo/bolt_burning_venom.inventoryitem");
    add("Howling Bolt",5,5,"models/weapons/crossbow/crossbow_corvo.inventoryitem","components/equipment/ammo/bolt_flashbang.inventoryitem");
    // Keep weapons/ammunition together, then powers. Fixture counts only.
    std::rotate(items.begin()+7,items.end()-4,items.end());
    const auto render=[&](const wchar_t* filename,int page,int slot,int equipped)
    {
        ImGui_ImplDX11_NewFrame();ImGui::NewFrame();
        const int count=(int)items.size();
        const float angle=slot*2.f*WheelSelection::pi/count;
        const int local=slot<0?WheelSelection::Pick(0,0,count,-1):WheelSelection::Pick(std::sin(angle),std::cos(angle),count,-1);
        renderer.Draw(items,page,local<0?-1:local,equipped);
        ImGui::Render();ID3D11RenderTargetView* target=rtv.Get();ctx->OMSetRenderTargets(1,&target,nullptr);
        const float clear[4]={0.045f,0.052f,0.065f,1.f};ctx->ClearRenderTargetView(target,clear);ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        if(!XRCapture::SaveTextureToPng(dev.Get(),ctx.Get(),tex.Get(),(out/filename).c_str())) return false;
        return true;
    };
    bool ok=render(L"one-ring-selected.png",0,0,100);
    ok=render(L"one-ring-neutral.png",0,-1,100)&&ok;
    ImGui_ImplDX11_Shutdown();ImGui::DestroyContext();
    std::cout<<(ok?"Rendered two single-ring preview states with native assets. Fixture data, not a gameplay capture.\n":"Capture failed\n");
    return ok?0:7;
}
