#include "FrameProfile.h"
#ifdef DVR_HAS_PDAFW
#include "AfwLive.h"
#endif
#include "DlssLive.h"
#include "DlssRecovery.h"
#include "DlssStageTiming.h"
#include "TemporalBoundary.h"
#include "GameInfo.h"
#include "ViewMatrixHook.h"
#include "VRRuntime.h"
#include <atomic>
#include <mutex>
#include <sstream>
#include <fstream>
#include <array>
#include <vector>
#include <cmath>
#include <wrl/client.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#ifdef DVR_HAS_NGX
#include "DlssD3D11.h"
#endif
using Microsoft::WRL::ComPtr;
namespace DlssLive {namespace {
std::atomic<int> quality{0};
std::atomic<bool> directColor{false},compactInputs{true},fullscreenScan{true};
std::atomic<unsigned long long> directColorFrames{0};
std::atomic<unsigned> preset{0};
std::atomic<UINT> activeWidth{0},activeHeight{0},outputWidth{0},outputHeight{0};
std::atomic<unsigned long long> eyeEvaluated[2]{},prepared{0};
std::atomic<bool> enabled{false},ready{false};std::atomic<unsigned long long> epoch{1},captured{0},evaluated{0},fallback{0},missingDepth{0},depthObserved{0},lastDepthSerial{0},lastCaptureSerial{0};
RecoveryBudget recovery;std::atomic<bool> resetRecovery{false};std::atomic<unsigned> recoveryCount{0};
std::mutex depthMutex,statusMutex;std::string problem="disabled";
void Problem(const std::string& text){std::lock_guard lock(statusMutex);problem=text;}
void FailStage(const std::string& cause,bool transient) {
 const bool retry=transient&&recovery.Retry();
 std::ofstream diagnostic(GameInfo::GameDir()+L"\\DVR_dlss_failure.txt",std::ios::app);
 diagnostic<<"tick "<<GetTickCount64()<<" retry "<<retry<<" attempt "<<recovery.retries<<" cause "<<cause<<" | "<<Status()<<"\n";
 ready=false;++epoch;
 if(retry){++recoveryCount;Problem("DLSS history reset after transient input failure: "+cause);}
 else{enabled=false;TemporalBoundary::SetEnabled(false);Problem("DLSS SR failed; returning to native stereo. Last status: "+cause);}
}
const GUID batchKey={0x42f99016,0xa899,0x4c4c,{0x9f,0x89,0x6c,0x8b,0x10,0x77,0xb9,0x31}};
struct Depth {unsigned long long serial=0,epoch=0;int eye=-1;ComPtr<ID3D11Texture2D> texture;bool submitted=false;};
std::array<Depth,16> depths;
const GUID depthKey={0xe40eec21,0x71aa,0x47ef,{0x8b,0x36,0x42,0x15,0x23,0x72,0x81,0x19}};
struct Tone {unsigned long long serial=0,epoch=0;int eye=-1;UINT width=0,height=0;ComPtr<ID3D11Resource> color;};
std::array<Tone,16> tones;
struct DepthList final:IUnknown {
 std::atomic<ULONG> refs{1};std::vector<Depth> entries;std::vector<Tone> tones;
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(id!=__uuidof(IUnknown))return E_NOINTERFACE;*out=this;AddRef();return S_OK;}
 ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}ULONG STDMETHODCALLTYPE Release()override{auto n=--refs;if(!n)delete this;return n;}
};
ComPtr<DepthList> DepthData(ID3D11DeviceChild* object){IUnknown* raw=nullptr;UINT bytes=sizeof(raw);ComPtr<DepthList> data;if(SUCCEEDED(object->GetPrivateData(depthKey,&bytes,&raw))&&raw)data.Attach(static_cast<DepthList*>(raw));return data;}

#ifdef DVR_HAS_NGX
struct Batch final:IUnknown {
 std::atomic<ULONG> refs{1};ComPtr<ID3D11Texture2D> color,motion,depth,linearDepth;
 ComPtr<ID3D11ShaderResourceView> depthSrv;ComPtr<ID3D11UnorderedAccessView> depthUav,result;
 // Stamp dimensions identify the engine allocation. Active dimensions are the
 // same-frame populated rectangle, checked against the later tone viewport.
 UINT activeW=0,activeH=0;bool cropped=false;
 TemporalFrame::Stamp stamp;float jitter[2]{};long long poseTime=0;int quality=0;bool final=false;
 ComPtr<ID3D11ShaderResourceView> colorSrv;ComPtr<ID3D11RenderTargetView> target;ComPtr<ID3D11Resource> sourceIdentity;
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(id!=__uuidof(IUnknown))return E_NOINTERFACE;*out=this;AddRef();return S_OK;}
 ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}ULONG STDMETHODCALLTYPE Release()override{auto n=--refs;if(!n)delete this;return n;}
};
std::array<ComPtr<Batch>,4> packets;
struct State {
 ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext1> context;ComPtr<ID3DDeviceContextState> isolated;
 ComPtr<ID3D11ComputeShader> decode,write,convert;
 ComPtr<ID3D11Texture2D> linearColor;ComPtr<ID3D11UnorderedAccessView> linearColorUav;
 ComPtr<ID3D11VertexShader> fullscreen;ComPtr<ID3D11PixelShader> present;ComPtr<ID3D11RasterizerState> raster;DlssD3D11::Backend backend;UINT width=0,height=0,renderWidth=0,renderHeight=0;int quality=-1;unsigned long long epoch=0;long long lastTime[2]{};unsigned long long seenPrepared=0,seenEvaluated=0;unsigned incompletePresents=0;
};
// Shutdown is performed on the render lane; no NGX calls under DLL loader lock.
State& Get(){static auto* state=new State;return *state;}
const char shaders[]=R"(
Texture2D<float4> source:register(t0);RWTexture2D<float> depth:register(u0);RWTexture2D<float4> color:register(u1);
[numthreads(8,8,1)]void Decode(uint3 p:SV_DispatchThreadID){uint w,h;depth.GetDimensions(w,h);if(p.x<w&&p.y<h)depth[p.xy]=source.Load(int3(p.xy,0)).x;}
[numthreads(8,8,1)]void Convert(uint3 p:SV_DispatchThreadID){uint w,h;color.GetDimensions(w,h);if(p.x<w&&p.y<h)color[p.xy]=source.Load(int3(p.xy,0));}
[numthreads(8,8,1)]void Write(uint3 p:SV_DispatchThreadID){uint w,h;color.GetDimensions(w,h);if(p.x<w&&p.y<h)color[p.xy]=source.Load(int3(p.xy,0));}
)";
bool ActiveSize(const float* scale,UINT w,UINT h,UINT& rw,UINT& rh){
 rw=w;rh=h;if(!scale)return true;
 if(!std::isfinite(scale[0])||!std::isfinite(scale[1])||scale[0]<=0||scale[1]<=0||scale[0]>1||scale[1]>1)return false;
 rw=UINT(std::lround(w*double(scale[0])));rh=UINT(std::lround(h*double(scale[1])));return rw&&rh&&rw<=w&&rh<=h;
}
bool Shader(ID3D11Device* device,const char* entry,ComPtr<ID3D11ComputeShader>& shader){ComPtr<ID3DBlob> code,error;
 if(FAILED(D3DCompile(shaders,sizeof(shaders)-1,"D2 DLSS adapter",nullptr,nullptr,entry,"cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&error)))return false;
 return SUCCEEDED(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader));}
bool PresentShaders(State& s){
 const char vs[]="float4 main(uint id:SV_VertexID):SV_Position{float2 p=float2((id<<1)&2,id&2);return float4(p*float2(2,-2)+float2(-1,1),0,1);}";
 const char ps[]="Texture2D<float4> image:register(t0);float4 main(float4 p:SV_Position):SV_Target{return image.Load(int3(p.xy,0));}";
 ComPtr<ID3DBlob> code,error;
 if(FAILED(D3DCompile(vs,sizeof(vs)-1,nullptr,nullptr,nullptr,"main","vs_5_0",0,0,&code,&error))||FAILED(s.device->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&s.fullscreen)))return false;
 code.Reset();error.Reset();
 if(FAILED(D3DCompile(ps,sizeof(ps)-1,nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&code,&error))||FAILED(s.device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&s.present)))return false;
 D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;
 return SUCCEEDED(s.device->CreateRasterizerState(&rd,&s.raster));
}
bool Configure(State& s,ID3D11DeviceContext* ctx,UINT rw,UINT rh,UINT ow,UINT oh,int q){
 if(s.width==ow&&s.height==oh&&s.renderWidth==rw&&s.renderHeight==rh&&s.quality==q)return true;
 DlssD3D11::Config config;config.renderWidth=rw;config.renderHeight=rh;config.outputWidth=ow;config.outputHeight=oh;
 config.preset=preset;config.quality=DlssD3D11::Quality(q);config.depthInverted=true;config.hdr=q==0;config.autoExposure=q==0;
 if(!s.backend.Configure(ctx,config)){Problem(s.backend.Problem());return false;}
 s.linearColor.Reset();s.linearColorUav.Reset();
 if(q>0){
  D3D11_TEXTURE2D_DESC td{};td.Width=rw;td.Height=rh;td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;td.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
  if(FAILED(s.device->CreateTexture2D(&td,nullptr,&s.linearColor))||FAILED(s.device->CreateUnorderedAccessView(s.linearColor.Get(),nullptr,&s.linearColorUav))){Problem("SR linear colour workspace allocation failed");return false;}
 }

 activeWidth=rw;activeHeight=rh;outputWidth=ow;outputHeight=oh;
 s.width=ow;s.height=oh;s.renderWidth=rw;s.renderHeight=rh;s.quality=q;s.lastTime[0]=s.lastTime[1]=0;return true;
}
struct Restore {ID3D11DeviceContext1* context;ComPtr<ID3DDeviceContextState> prior;
 Restore(State& s):context(s.context.Get()){context->SwapDeviceContextState(s.isolated.Get(),&prior);}
 ~Restore(){context->SwapDeviceContextState(prior.Get(),nullptr);}
};
#endif
}
void SetDirectColor(bool on){directColor=on;}
void SetCompactInputs(bool on){if(compactInputs.exchange(on)!=on){ready=false;++epoch;}}
void SetFullscreenScan(bool on){fullscreenScan=on;}
bool FullscreenScan(){return fullscreenScan.load(std::memory_order_relaxed);}
void SetEnabled(bool on){if(!on && !enabled)return;if(on)resetRecovery=true;enabled=on;ready=false;++epoch;TemporalBoundary::SetEnabled(on);Problem(on?"awaiting render-lane initialization":"disabled");}
bool Enabled(){return enabled.load();}
unsigned long long Generation(){return epoch.load();}
void SetQuality(int q){quality=(q>=0&&q<=4)?q:0;ready=false;++epoch;}
int Quality(){return quality.load();}
void SetPreset(unsigned value){preset=value==11?11:0;ready=false;++epoch;}
unsigned Preset(){return preset.load();}
void ObserveDepth(ID3D11DeviceContext* ctx,unsigned long long serial,int eye,ID3D11Resource* resource){ DVR_PROFILE("DlssLive.ObserveDepth");
 if(!enabled||!serial||eye<0||eye>1||!resource)return;ComPtr<ID3D11Texture2D> texture;
 if(FAILED(resource->QueryInterface(IID_PPV_ARGS(&texture))))return;D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);
 if(d.Format!=DXGI_FORMAT_R24G8_TYPELESS||d.SampleDesc.Count!=1||d.ArraySize!=1||d.MipLevels!=1)return;
 auto data=DepthData(ctx);if(!data){data.Attach(new(std::nothrow) DepthList);if(!data||FAILED(ctx->SetPrivateDataInterface(depthKey,data.Get())))return;}
 data->entries.push_back({serial,epoch.load(),eye,texture});++depthObserved;lastDepthSerial=serial;
}
void OnFinish(ID3D11DeviceContext* ctx,ID3D11CommandList* list){
 auto data=DepthData(ctx);if(!data)return;
 if(SUCCEEDED(list->SetPrivateDataInterface(depthKey,data.Get())))ctx->SetPrivateDataInterface(depthKey,nullptr);
}
void OnPresent(ID3D11Device* device){ DVR_PROFILE("DlssLive.OnPresent");
 DlssStageTiming::Poll(device);
#ifdef DVR_HAS_NGX
 auto& s=Get();
 if(resetRecovery.exchange(false))recovery.Reset();
 if(enabled&&ready&&quality>0&&s.epoch==epoch){
  const auto nowPrepared=prepared.load(),nowEvaluated=evaluated.load();
  if(nowPrepared>s.seenPrepared&&nowEvaluated==s.seenEvaluated)++s.incompletePresents;
  else if(nowEvaluated>s.seenEvaluated)s.incompletePresents=0;
  s.seenPrepared=nowPrepared;s.seenEvaluated=nowEvaluated;
  if(s.incompletePresents>=2)FailStage("DLSS final pass missing for two rendered frames",true);
 }
 if(!enabled){{std::lock_guard lock(depthMutex);for(auto& depth:depths)depth=Depth{};for(auto& tone:tones)tone=Tone{};for(auto& packet:packets)packet.Reset();}if(s.context&&s.isolated){Restore restore(s);s.backend.Shutdown();}s.linearColor.Reset();s.linearColorUav.Reset();s.device.Reset();s.context.Reset();s.isolated.Reset();s.width=s.height=0;return;}
 if(ready&&s.device.Get()==device&&s.epoch==epoch)return;
 ready=false;
 if(s.context&&s.isolated){Restore restore(s);s.backend.Shutdown();}
 s.width=s.height=0;s.lastTime[0]=s.lastTime[1]=0;s.device=device;s.epoch=epoch;s.seenPrepared=prepared;s.seenEvaluated=evaluated;s.incompletePresents=0;
 ComPtr<ID3D11Device1> d;ComPtr<ID3D11DeviceContext> c;device->GetImmediateContext(&c);
 if(FAILED(device->QueryInterface(IID_PPV_ARGS(&d)))||FAILED(c.As(&s.context))){Problem("D3D11.1 state isolation unavailable");enabled=false;return;}
 auto level=device->GetFeatureLevel();D3D_FEATURE_LEVEL chosen;
 if(FAILED(d->CreateDeviceContextState(0,&level,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),&chosen,&s.isolated))){Problem("cannot isolate DLSS graphics state");enabled=false;return;}
 Restore restore(s);
 if(!Shader(device,"Decode",s.decode)||!Shader(device,"Write",s.write)||!Shader(device,"Convert",s.convert)||!PresentShaders(s)){Problem("adapter shader creation failed");enabled=false;return;}
 if(!s.backend.Initialize(device,GameInfo::GameDir(),GameInfo::GameDir()+L"\\DVR_NGXCache")){Problem(s.backend.Problem());enabled=false;return;}
 ready=true;Problem(quality==0?"DLAA diagnostic ready; original TAA retained for fallback history":"DLSS SR diagnostic ready; awaiting matched final pass");
#else
 if(enabled){Problem("candidate built without NGX");enabled=false;}
#endif
}
IUnknown* Capture(ID3D11DeviceContext* ctx,unsigned long long serial,int eye,const float jitter[2],ID3D11ShaderResourceView* color,ID3D11ShaderResourceView* motion,ID3D11UnorderedAccessView* result,const float* resolution){ DVR_PROFILE("DlssLive.Capture");
#ifdef DVR_HAS_NGX
 if(!enabled||!ready||ctx->GetType()!=D3D11_DEVICE_CONTEXT_DEFERRED||!serial||eye<0||eye>1||!color||!motion||!result)return nullptr;
 lastCaptureSerial=serial;
 ComPtr<ID3D11Resource> resource;ComPtr<ID3D11Texture2D> rgb,mv,target;color->GetResource(&resource);resource.As(&rgb);motion->GetResource(&resource);resource.As(&mv);result->GetResource(&resource);resource.As(&target);
 if(!rgb||!mv||!target)return nullptr;
 D3D11_TEXTURE2D_DESC rd{},md{},dd{},od{};rgb->GetDesc(&rd);mv->GetDesc(&md);target->GetDesc(&od);
 if(rd.Format!=DXGI_FORMAT_R11G11B10_FLOAT||md.Format!=DXGI_FORMAT_R16G16_FLOAT||od.Format!=rd.Format)return nullptr;
 for(auto desc:{rd,md,od})if(desc.Width!=rd.Width||desc.Height!=rd.Height||desc.SampleDesc.Count!=1||desc.ArraySize!=1||desc.MipLevels!=1)return nullptr;
 ComPtr<Batch> b;b.Attach(new(std::nothrow) Batch);if(!b)return nullptr;ComPtr<ID3D11Device> device;ctx->GetDevice(&device);
 if(!compactInputs)resolution=nullptr;
 if(!ActiveSize(quality>0?resolution:nullptr,rd.Width,rd.Height,b->activeW,b->activeH)){++fallback;Problem("invalid temporal active rectangle");return nullptr;}b->cropped=quality>0&&resolution;
 auto clone=[&](D3D11_TEXTURE2D_DESC desc,ComPtr<ID3D11Texture2D>& out){desc.Width=b->activeW;desc.Height=b->activeH;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.CPUAccessFlags=desc.MiscFlags=0;return SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&out));};
 // The temporal boundary executes immediately after this read-only TAA input is
 // consumed and before the command-list suffix can overwrite it. Keep the engine
 // texture alive through that boundary. Motion still needs a snapshot for AFW.
 const bool direct=directColor.load()&&rgb.Get()!=target.Get();
 if(direct)b->color=rgb;
 if((!direct&&!clone(rd,b->color))||!clone(md,b->motion)){++fallback;return nullptr;}
 b->quality=quality;b->stamp={serial,epoch.load(),eye,rd.Width,rd.Height};b->jitter[0]=-jitter[0]*rd.Width;b->jitter[1]=jitter[1]*rd.Height;b->result=result;
 VRRuntime::PoseSet poses;if(ViewMatrixHook::ViewPoses(serial,poses))b->poseTime=poses.time;
 D3D11_BOX box{0,0,0,b->activeW,b->activeH,1};
 if(!direct)ctx->CopySubresourceRegion(b->color.Get(),0,0,0,0,rgb.Get(),0,&box);else ++directColorFrames;ctx->CopySubresourceRegion(b->motion.Get(),0,0,0,0,mv.Get(),0,&box);++captured;return b.Detach();
#else
 return nullptr;
#endif
}
void Finish(ID3D11DeviceContext* ctx,IUnknown* batch){if(!batch)return;
 if(!TemporalBoundary::SplitBefore(ctx)||FAILED(TemporalBoundary::AttachToPending(ctx,batchKey,batch)))++fallback;
 batch->Release();
}
void ObserveTone(ID3D11DeviceContext* ctx,unsigned long long serial,int eye,ID3D11RenderTargetView* target,const D3D11_VIEWPORT& viewport){ DVR_PROFILE("DlssLive.ObserveTone");
 if(!enabled||quality==0||!serial||eye<0||eye>1||!target||viewport.TopLeftX!=0||viewport.TopLeftY!=0||viewport.Width<1||viewport.Height<1)return;
 auto data=DepthData(ctx);if(!data){data.Attach(new(std::nothrow) DepthList);if(!data||FAILED(ctx->SetPrivateDataInterface(depthKey,data.Get())))return;}
 Tone tone;tone.serial=serial;tone.epoch=epoch;tone.eye=eye;tone.width=UINT(viewport.Width);tone.height=UINT(viewport.Height);target->GetResource(&tone.color);data->tones.push_back(tone);
}
IUnknown* CaptureUpscale(ID3D11DeviceContext* ctx,unsigned long long serial,int eye,ID3D11ShaderResourceView* color,ID3D11RenderTargetView* target,const float* resolution){ DVR_PROFILE("DlssLive.CaptureUpscale");
#ifdef DVR_HAS_NGX
 if(!enabled||!ready||quality==0||ctx->GetType()!=D3D11_DEVICE_CONTEXT_DEFERRED||!serial||eye<0||eye>1||!color||!target)return nullptr;
 D3D11_SHADER_RESOURCE_VIEW_DESC sd{};D3D11_RENDER_TARGET_VIEW_DESC od{};color->GetDesc(&sd);target->GetDesc(&od);
 if(sd.Format!=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB||od.Format!=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB||sd.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D||od.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2D)return nullptr;
 ComPtr<ID3D11Resource> source;ComPtr<ID3D11Texture2D> texture,destination;color->GetResource(&source);source.As(&texture);target->GetResource(&source);source.As(&destination);if(!texture||!destination)return nullptr;
 D3D11_TEXTURE2D_DESC td{},dd{};texture->GetDesc(&td);destination->GetDesc(&dd);
 if(td.Width!=dd.Width||td.Height!=dd.Height||td.MipLevels!=1||td.ArraySize!=1||td.SampleDesc.Count!=1||dd.SampleDesc.Count!=1)return nullptr;
 ComPtr<Batch> b;b.Attach(new(std::nothrow) Batch);if(!b)return nullptr;ComPtr<ID3D11Device> device;ctx->GetDevice(&device);
 if(!compactInputs)resolution=nullptr;
 if(!ActiveSize(resolution,td.Width,td.Height,b->activeW,b->activeH)){++fallback;Problem("invalid upscale active rectangle");return nullptr;}b->cropped=resolution!=nullptr;
 td.Width=b->activeW;td.Height=b->activeH;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;td.Usage=D3D11_USAGE_DEFAULT;td.CPUAccessFlags=td.MiscFlags=0;
 if(FAILED(device->CreateTexture2D(&td,nullptr,&b->color))||FAILED(device->CreateShaderResourceView(b->color.Get(),&sd,&b->colorSrv))){++fallback;return nullptr;}
 b->final=true;b->quality=quality;b->stamp={serial,epoch.load(),eye,dd.Width,dd.Height};b->target=target;b->sourceIdentity=texture;
 D3D11_BOX box{0,0,0,b->activeW,b->activeH,1};ctx->CopySubresourceRegion(b->color.Get(),0,0,0,0,texture.Get(),0,&box);return b.Detach();
#else
 return nullptr;
#endif
}
void AfterExecute(ID3D11DeviceContext* ctx,ID3D11CommandList* list){ DVR_PROFILE("DlssLive.AfterExecute");
#ifdef DVR_HAS_NGX
 auto data=DepthData(list);if(data){std::lock_guard lock(depthMutex);
  for(auto entry:data->entries)if(entry.epoch==epoch){for(auto& prior:depths)if(prior.texture.Get()==entry.texture.Get()&&(prior.serial!=entry.serial||prior.eye!=entry.eye||prior.epoch!=entry.epoch))prior=Depth{};entry.submitted=true;depths[entry.serial%depths.size()]=entry;}
  for(auto tone:data->tones)if(tone.epoch==epoch){for(auto& prior:tones)if(prior.color.Get()==tone.color.Get()&&prior.serial!=tone.serial)prior=Tone{};tones[tone.serial%tones.size()]=tone;}
 }
 if(!enabled||!ready)return;IUnknown* raw=nullptr;UINT bytes=sizeof(raw);
 if(FAILED(list->GetPrivateData(batchKey,&bytes,&raw))||!raw)return;ComPtr<Batch> b;b.Attach(static_cast<Batch*>(raw));auto& s=Get();
 if(b->stamp.epoch!=epoch||b->quality!=quality||!s.device){++fallback;return;}
 ComPtr<ID3D11Device> owner;ctx->GetDevice(&owner);if(owner.Get()!=s.device.Get()){++fallback;return;}
 // An unsuccessful SR stage must stop feeding unfiltered colour to later frames.
 // RenderRes releases its temporary scale ownership when Enabled becomes false.
 // Pending lists still drain through TemporalBoundary; their epoch is invalidated.
 struct SrFailureGuard {
  bool armed;bool transient=false;
  ~SrFailureGuard(){if(armed){std::string cause;{std::lock_guard lock(statusMutex);cause=problem;}FailStage(cause,transient);}}
 } failureGuard{b->quality!=0};
 DVR_PROFILE(b->final?"DLSS.spatialEvaluation":"DLSS.temporalEvaluation");
 Restore restore(s);
 DlssStageTiming::Span timing(ctx,b->final?1:0,b->stamp.eye,b->stamp.serial);
 ID3D11ShaderResourceView* noSrv=nullptr;ID3D11UnorderedAccessView* noUav[2]{};
 if(!b->final){
  ComPtr<ID3D11Texture2D> sourceDepth;{std::lock_guard lock(depthMutex);auto& d=depths[b->stamp.serial%depths.size()];if(d.serial==b->stamp.serial&&d.eye==b->stamp.eye&&d.epoch==b->stamp.epoch&&d.submitted)sourceDepth=d.texture;}
  if(!sourceDepth){failureGuard.transient=true;Problem("missing matching submitted depth");++missingDepth;++fallback;return;}
  D3D11_TEXTURE2D_DESC dd{};sourceDepth->GetDesc(&dd);if(dd.Width!=b->stamp.width||dd.Height!=b->stamp.height){++fallback;return;}
  b->depth=sourceDepth;D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.Format=DXGI_FORMAT_R24_UNORM_X8_TYPELESS;view.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;view.Texture2D.MipLevels=1;
  dd.Width=b->activeW;dd.Height=b->activeH;dd.Format=DXGI_FORMAT_R32_FLOAT;dd.Usage=D3D11_USAGE_DEFAULT;dd.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;dd.CPUAccessFlags=dd.MiscFlags=0;
  if(FAILED(owner->CreateShaderResourceView(b->depth.Get(),&view,&b->depthSrv))||FAILED(owner->CreateTexture2D(&dd,nullptr,&b->linearDepth))||FAILED(owner->CreateUnorderedAccessView(b->linearDepth.Get(),nullptr,&b->depthUav))){++fallback;return;}
  auto* depth=b->depthSrv.Get();auto* depthOut=b->depthUav.Get();ctx->CSSetShaderResources(0,1,&depth);ctx->CSSetUnorderedAccessViews(0,1,&depthOut,nullptr);ctx->CSSetShader(s.decode.Get(),nullptr,0);ctx->Dispatch((dd.Width+7)/8,(dd.Height+7)/8,1);
  ctx->CSSetShaderResources(0,1,&noSrv);ctx->CSSetUnorderedAccessViews(0,2,noUav,nullptr);
  if(b->quality!=0){
   // Keep the native history healthy, but feed subsequent effects unfiltered current colour.
   // This diagnostic still pays for the native temporal dispatch.
   ComPtr<ID3D11Resource> destination;b->result->GetResource(&destination);D3D11_BOX box{0,0,0,b->activeW,b->activeH,1};ctx->CopySubresourceRegion(destination.Get(),0,0,0,0,b->color.Get(),0,&box);
   b->color.Reset();b->result.Reset();b->depth.Reset();b->depthSrv.Reset();b->depthUav.Reset();packets[b->stamp.serial%packets.size()]=b;++prepared;failureGuard.armed=false;return;
  }
  if(!Configure(s,ctx,b->stamp.width,b->stamp.height,b->stamp.width,b->stamp.height,0)){++fallback;return;}
  DlssD3D11::Input in;in.color=b->color.Get();in.motion=b->motion.Get();in.depth=b->linearDepth.Get();in.colorStamp=in.motionStamp=in.depthStamp=b->stamp;
  in.mvScaleX=-float(s.width);in.mvScaleY=-float(s.height);in.jitterX=b->jitter[0];in.jitterY=b->jitter[1];
  const auto dt=b->poseTime-s.lastTime[b->stamp.eye];in.frameTimeMs=s.lastTime[b->stamp.eye]&&dt>0?float(dt*1e-6):0;
  timing.NgxBegin();const bool ngxOk=s.backend.Evaluate(ctx,in);timing.NgxEnd();
  if(!ngxOk){Problem(s.backend.Problem());++fallback;return;}
#ifdef DVR_HAS_PDAFW
  AfwLive::Prepared(b->stamp.serial,b->stamp.eye,b->stamp.epoch,b->linearDepth.Get(),b->motion.Get(),b->stamp.width,b->stamp.height);
#endif
  auto* output=s.backend.Output(b->stamp.eye);auto* destination=b->result.Get();
  ctx->CSSetShaderResources(0,1,&output);ctx->CSSetUnorderedAccessViews(1,1,&destination,nullptr);ctx->CSSetShader(s.write.Get(),nullptr,0);ctx->Dispatch((s.width+7)/8,(s.height+7)/8,1);
  ctx->CSSetShaderResources(0,1,&noSrv);ctx->CSSetUnorderedAccessViews(0,2,noUav,nullptr);s.lastTime[b->stamp.eye]=b->poseTime;++evaluated;++eyeEvaluated[b->stamp.eye];return;
 }
 auto packet=packets[b->stamp.serial%packets.size()];Tone tone;{std::lock_guard lock(depthMutex);tone=tones[b->stamp.serial%tones.size()];}
 if(!packet||!TemporalFrame::SameImage(packet->stamp,b->stamp)||tone.serial!=b->stamp.serial||tone.epoch!=b->stamp.epoch||tone.eye!=b->stamp.eye||tone.color.Get()!=b->sourceIdentity.Get()||!tone.width||!tone.height||tone.width>b->stamp.width||tone.height>b->stamp.height){++fallback;failureGuard.transient=true;Problem("SR missing matching temporal inputs or tone viewport");return;}
 if((b->cropped&&(b->activeW!=tone.width||b->activeH!=tone.height))||(packet->cropped&&(packet->activeW!=tone.width||packet->activeH!=tone.height))){++fallback;Problem("scene/temporal/upscale active rectangles disagree");return;}
 if(!Configure(s,ctx,tone.width,tone.height,b->stamp.width,b->stamp.height,b->quality)){++fallback;return;}
 // Immediate-context command order protects this workspace across eyes/frames.
 // Feature reconfiguration recreates it; no texture/UAV allocation per evaluation.
 auto* src=b->colorSrv.Get();auto* dst=s.linearColorUav.Get();ctx->CSSetShaderResources(0,1,&src);ctx->CSSetUnorderedAccessViews(1,1,&dst,nullptr);ctx->CSSetShader(s.convert.Get(),nullptr,0);ctx->Dispatch((tone.width+7)/8,(tone.height+7)/8,1);
 ctx->CSSetShaderResources(0,1,&noSrv);ctx->CSSetUnorderedAccessViews(0,2,noUav,nullptr);
 DlssD3D11::Input in;in.color=s.linearColor.Get();in.depth=packet->linearDepth.Get();in.motion=packet->motion.Get();in.colorStamp=b->stamp;in.colorStamp.width=tone.width;in.colorStamp.height=tone.height;in.depthStamp=in.motionStamp=in.colorStamp;
 in.mvScaleX=-float(tone.width);in.mvScaleY=-float(tone.height);in.jitterX=packet->jitter[0]*tone.width/packet->stamp.width;in.jitterY=packet->jitter[1]*tone.height/packet->stamp.height;
 const auto dt=packet->poseTime-s.lastTime[b->stamp.eye];in.frameTimeMs=s.lastTime[b->stamp.eye]&&dt>0?float(dt*1e-6):0;
 timing.NgxBegin();const bool ngxOk=s.backend.Evaluate(ctx,in);timing.NgxEnd();
  if(!ngxOk){Problem(s.backend.Problem());++fallback;return;}
 auto* output=s.backend.Output(b->stamp.eye);auto* target=b->target.Get();ctx->OMSetRenderTargets(1,&target,nullptr);ctx->OMSetBlendState(nullptr,nullptr,~0u);ctx->OMSetDepthStencilState(nullptr,0);ctx->RSSetState(s.raster.Get());
 D3D11_VIEWPORT vp{0,0,float(s.width),float(s.height),0,1};ctx->RSSetViewports(1,&vp);ctx->IASetInputLayout(nullptr);ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
 ctx->VSSetShader(s.fullscreen.Get(),nullptr,0);ctx->GSSetShader(nullptr,nullptr,0);ctx->HSSetShader(nullptr,nullptr,0);ctx->DSSetShader(nullptr,nullptr,0);ctx->PSSetShader(s.present.Get(),nullptr,0);ctx->PSSetShaderResources(0,1,&output);ctx->Draw(3,0);ctx->PSSetShaderResources(0,1,&noSrv);ctx->OMSetRenderTargets(0,nullptr,nullptr);
#ifdef DVR_HAS_PDAFW
 AfwLive::Prepared(b->stamp.serial,b->stamp.eye,b->stamp.epoch,packet->linearDepth.Get(),packet->motion.Get(),tone.width,tone.height);
#endif
 failureGuard.armed=false;recovery.Success();s.lastTime[b->stamp.eye]=packet->poseTime;++evaluated;++eyeEvaluated[b->stamp.eye];Problem("DLSS SR diagnostic evaluating; native TAA cost retained");packets[b->stamp.serial%packets.size()].Reset();
#endif
}
std::string Status(){std::lock_guard lock(statusMutex);std::ostringstream s;s<<"dlsslive enabled "<<enabled<<" compact "<<compactInputs<<" fullscreenonly "<<fullscreenScan<<" directcolor "<<directColor<<" directframes "<<directColorFrames<<" ready "<<ready<<" quality "<<quality<<" requestedpreset "<<preset<<" viewport "<<activeWidth<<"x"<<activeHeight<<" output "<<outputWidth<<"x"<<outputHeight<<" eyes "<<eyeEvaluated[0]<<"/"<<eyeEvaluated[1]<<" prepared "<<prepared<<" captured "<<captured<<" evaluated "<<evaluated<<" recoveries "<<recoveryCount<<" fallback "<<fallback<<" missingdepth "<<missingDepth<<" depthobserved "<<depthObserved<<" lastdepth "<<lastDepthSerial<<" lastcapture "<<lastCaptureSerial<<" reason "<<problem;return s.str();}
}
