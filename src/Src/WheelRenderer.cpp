#include "WheelRenderer.h"
#include "WheelSelection.h"
#include <wincodec.h>
#include <cmath>
#include <cstdio>

using Microsoft::WRL::ComPtr;
bool WheelRenderer::Load(ID3D11Device* dev,const std::filesystem::path& file,int key)
{
    ComPtr<IWICImagingFactory> factory;ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;ComPtr<IWICFormatConverter> cv;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))) ||
       FAILED(factory->CreateDecoderFromFilename(file.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder)) ||
       FAILED(decoder->GetFrame(0,&frame)) || FAILED(factory->CreateFormatConverter(&cv)) ||
       FAILED(cv->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom))) return false;
    UINT w=0,h=0;if(FAILED(cv->GetSize(&w,&h)) || !w || !h || w>4096 || h>4096) return false;
    std::vector<unsigned char> pixels((size_t)w*h*4);
    if(FAILED(cv->CopyPixels(nullptr,w*4,(UINT)pixels.size(),pixels.data()))) return false;
    D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=d.ArraySize=1;
    d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{pixels.data(),w*4,0};ComPtr<ID3D11Texture2D> tex;Texture t;t.w=w;t.h=h;
    if(FAILED(dev->CreateTexture2D(&d,&data,&tex)) || FAILED(dev->CreateShaderResourceView(tex.Get(),nullptr,&t.srv))) return false;
    textures_.emplace(key,std::move(t));return true;
}
bool WheelRenderer::Initialize(ID3D11Device* dev,const std::filesystem::path& root)
{
    if(attempted_) return ready_;
    attempted_=true;
    const auto hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    bool ok=true;
    for(int id:{29,31,33,36,38,40,42,45,47,49,51,53,55,57,61,63,65,67,69,71,73,75,77,79,81,86,89,119,121,124})
    {
        wchar_t name[64];swprintf_s(name,L"lib_wheel_id%03d.png",id);
        ok=Load(dev,root/L"lib_wheel"/name,id)&&ok;
    }
    for(int id:{46,49,52,55,58,64,67,70,73,76,94,113,121})
    {
        wchar_t name[64];swprintf_s(name,L"quick_access_wheel_id%03d.png",id);
        ok=Load(dev,root/L"quick_access_wheel"/name,1000+id)&&ok;
    }
    if(SUCCEEDED(hr)) CoUninitialize();
    wchar_t win[MAX_PATH]{};GetWindowsDirectoryW(win,MAX_PATH);
    auto path=std::filesystem::path(win)/L"Fonts"/L"georgia.ttf";
    ImFontConfig config;config.OversampleH=2;config.OversampleV=2;
    if(std::filesystem::exists(path)) font_=ImGui::GetIO().Fonts->AddFontFromFileTTF(path.string().c_str(),26,&config);
    if(!font_) font_=ImGui::GetIO().Fonts->AddFontDefault();
    ready_=ok;return ready_;
}
bool WheelRenderer::Image(int key,ImVec2 centre,ImVec2 bounds,ImU32 colour,float angle)
{
    auto it=textures_.find(key);if(it==textures_.end()) return false;
    auto& t=it->second;const float scale=std::min(bounds.x/t.w,bounds.y/t.h);
    const float w=t.w*scale*.5f,h=t.h*scale*.5f,c=std::cos(angle),s=std::sin(angle);
    const auto at=[&](float x,float y){return ImVec2(centre.x+x*c-y*s,centre.y+x*s+y*c);};
    ImGui::GetForegroundDrawList()->AddImageQuad((ImTextureID)t.srv.Get(),at(-w,-h),at(w,-h),at(w,h),at(-w,h),ImVec2(0,0),ImVec2(1,0),ImVec2(1,1),ImVec2(0,1),colour);
    return true;
}
void WheelRenderer::Text(const std::string& value,ImVec2 centre,float size,ImU32 colour,float wrap)
{
    if(value.empty()) return;
    ImFont* font=font_?font_:ImGui::GetFont();const auto extent=font->CalcTextSizeA(size,1000,wrap,value.c_str());
    const ImVec2 pos(centre.x-extent.x*.5f,centre.y-extent.y*.5f);
    auto* dl=ImGui::GetForegroundDrawList();
    dl->AddText(font,size,ImVec2(pos.x+1.5f,pos.y+2.f),IM_COL32(0,0,0,210),value.c_str(),nullptr,wrap);
    dl->AddText(font,size,pos,colour,value.c_str(),nullptr,wrap);
}
void WheelRenderer::Draw(const std::vector<WheelModel::Item>& items,int page,int selected,int equippedId)
{
    const ImVec2 centre(512,512);auto* dl=ImGui::GetForegroundDrawList();
    const int start=0,n=(int)items.size();
    const auto ivory=IM_COL32(240,235,214,255),muted=IM_COL32(195,197,201,240);
    // Local translucent backing stops at the etched ring; no full-screen veil.
    dl->AddCircleFilled(centre,410.f,IM_COL32(12,14,18,105),128);
    Image(1094,centre,ImVec2(842,842),IM_COL32(225,224,214,175));
    Image(1113,centre,ImVec2(824,824),IM_COL32(230,231,222,170));
    for(int slot=0;slot<n;++slot)
    {
        const int i=start+slot;const auto& item=items[i];const auto art=WheelModel::Artwork(item);
        const bool chosen=i==selected,equipped=item.id==equippedId;
        const float angle=slot*2.f*WheelSelection::pi/n;
        const ImVec2 p(512+350*std::sin(angle),512-350*std::cos(angle));
        const float base=n>1 ? std::min(146.f,0.86f*700.f*std::sin(WheelSelection::pi/n)) : 146.f;
        const float diameter=base*(chosen ? 94.f/86.f : 1.f);
        Image(art.hero?119:121,p,ImVec2(diameter,diameter),IM_COL32(255,255,255,230));
        if(!Image(chosen?33:29,p,ImVec2(diameter,diameter),chosen?ivory:muted))
            dl->AddCircle(p,diameter*.5f,chosen?ivory:muted,64,chosen?3.f:1.f);
        if(!Image(art.icon,ImVec2(p.x,p.y-4),ImVec2(diameter*.64f,diameter*.64f)))
            Text(item.name,p,18,ivory,118);
        if(item.ammo>=0) Text(std::to_string(item.ammo),ImVec2(p.x,p.y+base*.34f),std::max(12.f,base*.16f),item.ammo==0?IM_COL32(170,160,150,255):ivory);
        if(equipped) dl->AddCircleFilled(ImVec2(p.x,p.y-diameter*.57f),3.5f,ivory,16);
        if(chosen)
        {
            dl->AddCircleFilled(p,diameter*.54f,IM_COL32(240,185,70,45),64);
            dl->AddCircle(p,diameter*.57f,IM_COL32(255,211,101,255),64,5.f);
            dl->AddLine(ImVec2(512+265*std::sin(angle),512-265*std::cos(angle)),
                        ImVec2(512+285*std::sin(angle),512-285*std::cos(angle)),IM_COL32(255,211,101,255),5.f);
            Image(1121,ImVec2(512+242*std::sin(angle),512-242*std::cos(angle)),ImVec2(66,30),ivory,angle);
        }
    }
    if(selected>=start && selected<start+n)
    {
        const auto& item=items[selected];const auto art=WheelModel::Artwork(item);
        if(art.hero) Image(1000+art.hero,ImVec2(512,454),ImVec2(300,210));
        else Image(art.icon,ImVec2(512,454),ImVec2(166,166));
        Text(item.name,ImVec2(512,594),30,ivory,310);
        if(item.ammo>=0)
            Text(std::to_string(item.ammo)+" / "+std::to_string(item.maxAmmo),ImVec2(512,642),20,muted);
    }
}
