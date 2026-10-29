#include "DlssD3D11.h"
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_helpers.h>
#include <wrl/client.h>
#include <filesystem>
#include <sstream>
using Microsoft::WRL::ComPtr;
namespace DlssD3D11 {
struct Backend::Impl {
    ComPtr<ID3D11Device> device;
    NVSDK_NGX_Parameter* params=nullptr;
    NVSDK_NGX_Handle* feature[2]={};
    ComPtr<ID3D11Texture2D> output[2];
    ComPtr<ID3D11ShaderResourceView> srv[2];
    TemporalFrame::Stamp previous[2]{};
    Config config{};
    bool outputValid[2]={};
    std::string problem="not initialized";
    bool initialized=false,configured=false;
    bool Fail(const char* stage,NVSDK_NGX_Result r) {
        std::ostringstream s; s<<stage<<" failed (NGX 0x"<<std::hex<<unsigned(r)<<")"; problem=s.str();return false;
    }
    void ReleaseFeatures() {
        configured=false;
        for(int e=0;e<2;++e) {
            if(feature[e]) NVSDK_NGX_D3D11_ReleaseFeature(feature[e]);
            feature[e]=nullptr; srv[e].Reset();output[e].Reset();previous[e]={};outputValid[e]=false;
        }
    }
    bool Context(ID3D11DeviceContext* c) {
        if(!c || c->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE) {problem="DLSS requires the immediate context";return false;}
        ComPtr<ID3D11Device> d;c->GetDevice(&d);
        if(d.Get()!=device.Get()){problem="DLSS context belongs to another device";return false;}
        return true;
    }
};
Backend::Backend():p(std::make_unique<Impl>()){}
Backend::~Backend(){Shutdown();}
void Backend::Shutdown(){
    p->ReleaseFeatures();
    if(p->params) NVSDK_NGX_D3D11_DestroyParameters(p->params);
    p->params=nullptr;
    if(p->initialized) NVSDK_NGX_D3D11_Shutdown1(p->device.Get());
    p->initialized=false;p->device.Reset();p->problem="not initialized";
}
const std::string& Backend::Problem() const{return p->problem;}
bool Backend::Initialize(ID3D11Device* device,const std::wstring& runtime,const std::wstring& cache){
    Shutdown();
    if(!device){p->problem="no D3D11 device";return false;}
    std::error_code ec;
    const auto runtimePath=std::filesystem::absolute(runtime,ec);
    if(ec){p->problem="invalid NGX runtime path";return false;}
    const auto cachePath=std::filesystem::absolute(cache,ec);
    if(ec){p->problem="invalid NGX cache path";return false;}
    if(!std::filesystem::is_regular_file(runtimePath/L"nvngx_dlss.dll",ec)) {
        p->problem="nvngx_dlss.dll missing from the selected runtime directory";return false;
    }
    std::filesystem::create_directories(cachePath,ec);
    if(ec){p->problem="NGX cache directory unavailable";return false;}
    p->device=device;
    const wchar_t* paths[]={runtimePath.c_str()};
    NVSDK_NGX_FeatureCommonInfo info{};info.PathListInfo.Path=paths;info.PathListInfo.Length=1;
    auto r=NVSDK_NGX_D3D11_Init_with_ProjectID("d67ab865-26de-4f84-8386-6d935ee2cb37",
        NVSDK_NGX_ENGINE_TYPE_CUSTOM,"Dishonored2VR",cachePath.c_str(),device,&info,NVSDK_NGX_Version_API);
    if(NVSDK_NGX_FAILED(r))return p->Fail("initialization",r);
    p->initialized=true;
    NVSDK_NGX_Parameter* caps=nullptr;r=NVSDK_NGX_D3D11_GetCapabilityParameters(&caps);
    if(NVSDK_NGX_FAILED(r) || !caps)return p->Fail("capabilities",r);
    int available=0;caps->Get(NVSDK_NGX_Parameter_SuperSampling_Available,&available);
    NVSDK_NGX_D3D11_DestroyParameters(caps);
    if(!available){p->problem="DLSS unavailable on this adapter/driver";return false;}
    r=NVSDK_NGX_D3D11_AllocateParameters(&p->params);
    if(NVSDK_NGX_FAILED(r))return p->Fail("parameter allocation",r);
    p->problem.clear();return true;
}
bool Backend::Configure(ID3D11DeviceContext* ctx,const Config& c){
    if(!p->initialized || !p->params){p->problem="DLSS not initialized";return false;}
    if(!p->Context(ctx))return false;
    if(!c.renderWidth || !c.renderHeight || c.outputWidth<c.renderWidth || c.outputHeight<c.renderHeight ||
       c.outputWidth>16384 || c.outputHeight>16384 || int(c.quality)<0 || int(c.quality)>4 ||
       (c.quality==Quality::Dlaa && (c.renderWidth!=c.outputWidth || c.renderHeight!=c.outputHeight))) {
        p->problem="invalid DLSS dimensions or quality";return false;
    }
    p->ReleaseFeatures();p->config=c;
    constexpr NVSDK_NGX_PerfQuality_Value modes[]={NVSDK_NGX_PerfQuality_Value_DLAA,
        NVSDK_NGX_PerfQuality_Value_MaxQuality,NVSDK_NGX_PerfQuality_Value_Balanced,
        NVSDK_NGX_PerfQuality_Value_MaxPerf,NVSDK_NGX_PerfQuality_Value_UltraPerformance};
    NVSDK_NGX_DLSS_Create_Params cp{};
    cp.Feature.InWidth=c.renderWidth;cp.Feature.InHeight=c.renderHeight;
    cp.Feature.InTargetWidth=c.outputWidth;cp.Feature.InTargetHeight=c.outputHeight;
    cp.Feature.InPerfQualityValue=modes[int(c.quality)];
    cp.InFeatureCreateFlags=NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
    if(c.autoExposure)cp.InFeatureCreateFlags|=NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
    if(c.hdr)cp.InFeatureCreateFlags|=NVSDK_NGX_DLSS_Feature_Flags_IsHDR;
    if(c.depthInverted)cp.InFeatureCreateFlags|=NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;
    for(const char* key:{NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA,NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality,
        NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced,NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance,
        NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance})p->params->Set(key,c.preset);
    D3D11_TEXTURE2D_DESC d{};d.Width=c.outputWidth;d.Height=c.outputHeight;d.MipLevels=1;d.ArraySize=1;
    d.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;
    d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
    for(int e=0;e<2;++e){
        if(FAILED(p->device->CreateTexture2D(&d,nullptr,&p->output[e])) ||
           FAILED(p->device->CreateShaderResourceView(p->output[e].Get(),nullptr,&p->srv[e]))) {
            p->ReleaseFeatures();p->problem="DLSS output allocation failed";return false;
        }
        auto r=NGX_D3D11_CREATE_DLSS_EXT(ctx,&p->feature[e],p->params,&cp);
        if(NVSDK_NGX_FAILED(r)){p->ReleaseFeatures();return p->Fail("feature creation",r);}
    }
    p->configured=true;p->problem.clear();return true;
}
bool Backend::Evaluate(ID3D11DeviceContext* ctx,const Input& in){
    p->outputValid[0]=p->outputValid[1]=false;
    if(!p->configured){p->problem="DLSS features not configured";return false;}
    if(!p->Context(ctx))return false;
    if(!TemporalFrame::SameImage(in.colorStamp,in.depthStamp) || !TemporalFrame::SameImage(in.colorStamp,in.motionStamp)){
        p->problem="colour/depth/motion do not identify the same rendered image";return false;
    }
    const auto& c=p->config;const int eye=in.colorStamp.eye;
    if(in.colorStamp.width!=c.renderWidth || in.colorStamp.height!=c.renderHeight ||
       !std::isfinite(in.jitterX) || !std::isfinite(in.jitterY) || !std::isfinite(in.mvScaleX) ||
       !std::isfinite(in.mvScaleY) || !in.mvScaleX || !in.mvScaleY ||
       !std::isfinite(in.preExposure) || in.preExposure<=0 || !std::isfinite(in.frameTimeMs) || in.frameTimeMs<0){
        p->problem="invalid dimensions, jitter, motion scale, exposure or frame time";return false;
    }
    for(auto* t:{in.color,in.depth,in.motion}){
        if(!t){p->problem="missing DLSS input";return false;}
        ComPtr<ID3D11Device> td;t->GetDevice(&td);D3D11_TEXTURE2D_DESC d{};t->GetDesc(&d);
        if(td.Get()!=p->device.Get() || d.Width<c.renderWidth || d.Height<c.renderHeight ||
           d.ArraySize!=1 || d.SampleDesc.Count!=1 || !(d.BindFlags&D3D11_BIND_SHADER_RESOURCE)){
            p->problem="invalid DLSS input resource";return false;
        }
    }
    NVSDK_NGX_D3D11_DLSS_Eval_Params ep{};
    ep.Feature.pInColor=in.color;ep.Feature.pInOutput=p->output[eye].Get();
    ep.pInDepth=in.depth;ep.pInMotionVectors=in.motion;ep.InJitterOffsetX=in.jitterX;ep.InJitterOffsetY=in.jitterY;
    ep.InMVScaleX=in.mvScaleX;ep.InMVScaleY=in.mvScaleY;ep.InPreExposure=in.preExposure;
    ep.InFrameTimeDeltaInMsec=in.frameTimeMs;ep.InRenderSubrectDimensions={c.renderWidth,c.renderHeight};
    ep.InReset=in.reset || !TemporalFrame::HistoryUsable(p->previous[eye],in.colorStamp);
    auto r=NGX_D3D11_EVALUATE_DLSS_EXT(ctx,p->feature[eye],p->params,&ep);
    if(NVSDK_NGX_FAILED(r)){p->previous[eye]={};return p->Fail("evaluation",r);}
    p->previous[eye]=in.colorStamp;p->outputValid[eye]=true;p->problem.clear();return true;
}
ID3D11ShaderResourceView* Backend::Output(int eye) const {
    return eye>=0 && eye<2 && p->outputValid[eye] ? p->srv[eye].Get() : nullptr;
}
}
