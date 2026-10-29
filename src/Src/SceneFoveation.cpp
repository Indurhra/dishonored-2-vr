#include "SceneFoveation.h"
#include "ViewMatrixHook.h"
#include "VRRuntime.h"
#ifdef DVR_HAS_NVAPI
#include <nvapi.h>
#include <wrl/client.h>
#include <atomic>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <cmath>
#include <sstream>
using Microsoft::WRL::ComPtr;
namespace {
std::atomic<int> mode{0}, error{0};
std::atomic<unsigned long long> candidates{0},applied{0}, rejected{0};
std::recursive_mutex gate;
struct Mask { unsigned w,h; D3D11_VIEWPORT vp; int eye,cx,cy; ComPtr<ID3D11Texture2D> texture; ComPtr<ID3D11NvShadingRateResourceView> view; };
std::vector<Mask> masks;
std::unordered_map<ID3D11DeviceContext*,ID3D11NvShadingRateResourceView*> active;
std::atomic<unsigned> activeCount{0};
ComPtr<ID3D11Device> device;
bool initialized=false;
struct Sample {ComPtr<ID3D11Query> q;bool pending=false;};
Sample samples[32];int open=-1;
unsigned long long psTotal=0, sampleCount=0;
std::atomic<bool> resetStats{false};
void rotate(const float q[4], const float v[3],float out[3]) {
 float t[3]={2*(q[1]*v[2]-q[2]*v[1]),2*(q[2]*v[0]-q[0]*v[2]),2*(q[0]*v[1]-q[1]*v[0])};
 out[0]=v[0]+q[3]*t[0]+q[1]*t[2]-q[2]*t[1];out[1]=v[1]+q[3]*t[1]+q[2]*t[0]-q[0]*t[2];out[2]=v[2]+q[3]*t[2]+q[0]*t[1]-q[1]*t[0];
}
}
bool SceneFoveation::Enabled(){return mode.load()!=0;}
void SceneFoveation::SetMode(int v){mode= v>=0&&v<=2?v:0;resetStats=true;}
void SceneFoveation::OnPresent(ID3D11Device* d){
 std::lock_guard lock(gate);
 if(device.Get()!=d){masks.clear();device=d;initialized=NvAPI_Initialize()==NVAPI_OK;for(auto& q:samples)q={};open=-1;}
 ComPtr<ID3D11DeviceContext> c;d->GetImmediateContext(&c);
 if(open>=0){c->End(samples[open].q.Get());samples[open].pending=true;open=-1;}
 if(resetStats.exchange(false)){psTotal=sampleCount=0;for(auto& q:samples)q.pending=false;}
 for(auto& q:samples)if(q.pending){D3D11_QUERY_DATA_PIPELINE_STATISTICS stats{};if(c->GetData(q.q.Get(),&stats,sizeof(stats),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK){psTotal+=stats.PSInvocations;++sampleCount;q.pending=false;}}
 if(mode.load())for(int i=0;i<32;++i)if(!samples[i].pending){auto& q=samples[i];if(!q.q){D3D11_QUERY_DESC desc{D3D11_QUERY_PIPELINE_STATISTICS,0};if(FAILED(d->CreateQuery(&desc,&q.q)))break;}c->Begin(q.q.Get());open=i;break;}
}
bool SceneFoveation::Begin(ID3D11DeviceContext* c,unsigned long long serial){
 if(!mode.load()||!serial)return false;
 thread_local unsigned long long cachedSerial=0;thread_local ViewMatrixHook::XrPose pose;thread_local VRRuntime::PoseSet poses;
 if(cachedSerial!=serial){pose=ViewMatrixHook::RenderPoseXr(serial);poses={};ViewMatrixHook::ViewPoses(serial,poses);cachedSerial=serial;}
 if(!pose.valid||pose.flat||!pose.tanValid||pose.eye<0||pose.eye>1||!poses.head.valid)return false;
 ComPtr<ID3D11RenderTargetView> rt;ComPtr<ID3D11DepthStencilView> ds;c->OMGetRenderTargets(1,&rt,&ds);
 if(!rt||!ds)return false;
 ComPtr<ID3D11DepthStencilState> depthState;UINT ref;c->OMGetDepthStencilState(&depthState,&ref);
 if(depthState){D3D11_DEPTH_STENCIL_DESC d;depthState->GetDesc(&d);if(!d.DepthEnable)return false;}
 ComPtr<ID3D11Resource> resource;rt->GetResource(&resource);ComPtr<ID3D11Texture2D> tex;if(FAILED(resource.As(&tex)))return false;
 D3D11_TEXTURE2D_DESC td;tex->GetDesc(&td);
 D3D11_RENDER_TARGET_VIEW_DESC rd;rt->GetDesc(&rd);if(rd.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2D||rd.Texture2D.MipSlice||td.ArraySize!=1||td.SampleDesc.Count!=1)return false;
 ComPtr<ID3D11Resource> dr;ds->GetResource(&dr);ComPtr<ID3D11Texture2D> dt;if(FAILED(dr.As(&dt)))return false;D3D11_TEXTURE2D_DESC dd;dt->GetDesc(&dd);
 D3D11_VIEWPORT vp{};UINT n=1;c->RSGetViewports(&n,&vp);
 if(n!=1||vp.Width<512||vp.Height<512||td.Width!=dd.Width||td.Height!=dd.Height||vp.TopLeftX<0||vp.TopLeftY<0||vp.TopLeftX+vp.Width>td.Width||vp.TopLeftY+vp.Height>td.Height)return false;
 float forward[3]={0,0,-1},world[3],ray[3];rotate(poses.head.quat,forward,world);float inv[4]={-pose.quat[0],-pose.quat[1],-pose.quat[2],pose.quat[3]};rotate(inv,world,ray);
 const float dx=pose.tan[1]-pose.tan[0],dy=pose.tan[2]-pose.tan[3];
 if(ray[2]>=-.01f||dx<=0||dy<=0)return false;
 const float x=(ray[0]/-ray[2]-pose.tan[0])/dx, y=(pose.tan[2]-ray[1]/-ray[2])/dy;
 if(!std::isfinite(x)||!std::isfinite(y)||x<0||x>1||y<0||y>1)return false;
 ++candidates;if(mode==1)return false;
 std::lock_guard lock(gate);if(!initialized||!device)return false;
 const int cx=int(x*1000),cy=int(y*1000);Mask* mask=nullptr;
 for(auto& m:masks)if(m.w==td.Width&&m.h==td.Height&&m.eye==pose.eye&&m.cx==cx&&m.cy==cy&&memcmp(&m.vp,&vp,sizeof(vp))==0){mask=&m;break;}
 if(!mask){
  // A bounded cache: an unrecognised/unstable mapping fails closed, never reuses another eye's mask.
  if(masks.size()>=32){++rejected;return false;}
  Mask m{};m.w=td.Width;m.h=td.Height;m.eye=pose.eye;m.cx=cx;m.cy=cy;m.vp=vp;
  unsigned w=(td.Width+15)/16,h=(td.Height+15)/16;std::vector<unsigned char> rates(w*h,0);
  for(unsigned j=0;j<h;++j)for(unsigned i=0;i<w;++i){float u=(i*16.f+8-vp.TopLeftX)/vp.Width,v=(j*16.f+8-vp.TopLeftY)/vp.Height;float a=(u-x)/.38f,b=(v-y)/.38f;if(u>=0&&u<=1&&v>=0&&v<=1&&a*a+b*b>1)rates[j*w+i]=1;}
  D3D11_TEXTURE2D_DESC md{};md.Width=w;md.Height=h;md.ArraySize=md.MipLevels=1;md.Format=DXGI_FORMAT_R8_UINT;md.SampleDesc.Count=1;md.Usage=D3D11_USAGE_IMMUTABLE;md.BindFlags=D3D11_BIND_SHADER_RESOURCE;
  D3D11_SUBRESOURCE_DATA data{rates.data(),w,0};if(FAILED(device->CreateTexture2D(&md,&data,&m.texture))){++rejected;return false;}
  NV_D3D11_SHADING_RATE_RESOURCE_VIEW_DESC vd{};vd.version=NV_D3D11_SHADING_RATE_RESOURCE_VIEW_DESC_VER;vd.Format=DXGI_FORMAT_R8_UINT;vd.ViewDimension=NV_SRRV_DIMENSION_TEXTURE2D;
  auto rc=NvAPI_D3D11_CreateShadingRateResourceView(device.Get(),m.texture.Get(),&vd,&m.view);if(rc!=NVAPI_OK){error=rc;++rejected;return false;}
  masks.push_back(std::move(m));mask=&masks.back();
 }
 if(active[c]==mask->view.Get()){++applied;return true;}
 NV_D3D11_VIEWPORT_SHADING_RATE_DESC rate{};rate.enableVariablePixelShadingRate=true;for(auto& r:rate.shadingRateTable)r=NV_PIXEL_X1_PER_RASTER_PIXEL;rate.shadingRateTable[1]=NV_PIXEL_X1_PER_2X2_RASTER_PIXELS;
 NV_D3D11_VIEWPORTS_SHADING_RATE_DESC desc{};desc.version=NV_D3D11_VIEWPORTS_SHADING_RATE_DESC_VER;desc.numViewports=1;desc.pViewports=&rate;
 auto rc=NvAPI_D3D11_RSSetShadingRateResourceView(c,mask->view.Get());if(rc==NVAPI_OK)rc=NvAPI_D3D11_RSSetViewportsPixelShadingRates(c,&desc);
 if(rc!=NVAPI_OK){error=rc;++rejected;End(c);return false;}active[c]=mask->view.Get();activeCount=unsigned(active.size());++applied;return true;
}
void SceneFoveation::End(ID3D11DeviceContext* c){if(!activeCount.load())return;std::lock_guard lock(gate);if(!active.erase(c))return;activeCount=unsigned(active.size()); NV_D3D11_VIEWPORT_SHADING_RATE_DESC r{};for(auto& rate:r.shadingRateTable)rate=NV_PIXEL_X1_PER_RASTER_PIXEL; NV_D3D11_VIEWPORTS_SHADING_RATE_DESC d{};d.version=NV_D3D11_VIEWPORTS_SHADING_RATE_DESC_VER;d.numViewports=1;d.pViewports=&r;NvAPI_D3D11_RSSetViewportsPixelShadingRates(c,&d);NvAPI_D3D11_RSSetShadingRateResourceView(c,nullptr);}
std::string SceneFoveation::Status(){std::lock_guard lock(gate);std::ostringstream s;s<<"scenevrs mode "<<mode<<" candidates "<<candidates<<" applied "<<applied<<" rejected "<<rejected<<" error "<<error<<" gpu_samples "<<sampleCount<<" ps_per_interval "<<(sampleCount?psTotal/sampleCount:0);return s.str();}

#else
bool SceneFoveation::Enabled(){return false;}
void SceneFoveation::SetMode(int){}
bool SceneFoveation::Begin(ID3D11DeviceContext*, unsigned long long){return false;}
void SceneFoveation::End(ID3D11DeviceContext*){}
void SceneFoveation::OnPresent(ID3D11Device*){}
std::string SceneFoveation::Status(){return "scenevrs unavailable: build without NVAPI";}
#endif
