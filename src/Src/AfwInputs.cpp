#include "FrameProfile.h"
#include "AfwInputs.h"
#include "DlssStageTiming.h"
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cmath>
#include <atomic>
using Microsoft::WRL::ComPtr;
namespace AfwInputs {
namespace {std::atomic<bool> identityFastPath{false};std::atomic<unsigned long long> identityFrames{0};}
void SetIdentityFastPath(bool on){identityFastPath=on;}
unsigned long long IdentityFrames(){return identityFrames;}
struct Adapter::Impl {
 ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext1> context;ComPtr<ID3DDeviceContextState> state;
 ComPtr<ID3D11ComputeShader> shader;ComPtr<ID3D11Buffer> params;
 ComPtr<ID3D11Texture2D> depth,motion;ComPtr<ID3D11UnorderedAccessView> zOut,mvOut;UINT width=0,height=0;bool valid=false,bypass=false;ComPtr<ID3D11Texture2D> directDepth,directMotion;
};
Adapter::Adapter():p(std::make_unique<Impl>()){}Adapter::~Adapter()=default;
ID3D11Texture2D* Adapter::Depth()const{return p->valid?(p->bypass?p->directDepth.Get():p->depth.Get()):nullptr;}ID3D11Texture2D* Adapter::Motion()const{return p->valid?(p->bypass?p->directMotion.Get():p->motion.Get()):nullptr;}
bool Adapter::Prepare(ID3D11DeviceContext* c,ID3D11Texture2D* depth,ID3D11Texture2D* motion,UINT rw,UINT rh,UINT ow,UINT oh,float jx,float jy,int eye,unsigned long long serial){ DVR_PROFILE("AfwInputs.Adapter::Prepare");
 p->valid=p->bypass=false;p->directDepth.Reset();p->directMotion.Reset();if(!c||!depth||!motion||!rw||!rh||!ow||!oh||ow>16384||oh>16384||!std::isfinite(jx)||!std::isfinite(jy)||c->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return false;
 ComPtr<ID3D11Device> device;c->GetDevice(&device);D3D11_TEXTURE2D_DESC zd{},md{};depth->GetDesc(&zd);motion->GetDesc(&md);
 for(auto* t:{depth,motion}){ComPtr<ID3D11Device> d;t->GetDevice(&d);if(d.Get()!=device.Get())return false;}
 if(zd.Format!=DXGI_FORMAT_R32_FLOAT||md.Format!=DXGI_FORMAT_R16G16_FLOAT||zd.Width<rw||zd.Height<rh||md.Width<rw||md.Height<rh||zd.SampleDesc.Count!=1||md.SampleDesc.Count!=1)return false;
 // At equal active/output dimensions, nearest-neighbour sampling is identity
 // when raster jitter stays inside a pixel. Keep 0.01px margin for float rounding
 // in the shader's multiply/divide at the maximum supported texture size.
 // Retain inputs; shared bridge copies stay ordered after their producers.
 if(identityFastPath&&rw==ow&&rh==oh&&zd.Width==ow&&zd.Height==oh&&md.Width==ow&&md.Height==oh&&
    zd.ArraySize==1&&md.ArraySize==1&&zd.MipLevels==1&&md.MipLevels==1&&std::abs(jx)<=.49f&&std::abs(jy)<=.49f){
  p->directDepth=depth;p->directMotion=motion;p->bypass=p->valid=true;++identityFrames;return true;
 }
 if(p->device.Get()!=device.Get()){p=std::make_unique<Impl>();p->device=device;ComPtr<ID3D11Device1> d;if(FAILED(device.As(&d))||FAILED(c->QueryInterface(IID_PPV_ARGS(&p->context))))return false;
  auto level=device->GetFeatureLevel();D3D_FEATURE_LEVEL chosen;if(FAILED(d->CreateDeviceContextState(0,&level,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),&chosen,&p->state)))return false;
  const char code[]=R"(
Texture2D<float> depth:register(t0);Texture2D<float2> motion:register(t1);
RWTexture2D<float> outputDepth:register(u0);RWTexture2D<float2> outputMotion:register(u1);
cbuffer Params:register(b0){float2 activeSize,outputSize,rasterJitter,padding;};
[numthreads(8,8,1)]void main(uint3 id:SV_DispatchThreadID){if(any(id.xy>=uint2(outputSize)))return;
 int2 source=clamp(int2(floor((float2(id.xy)+.5)*activeSize/outputSize+rasterJitter)),int2(0,0),int2(activeSize)-1);
 outputDepth[id.xy]=depth.Load(int3(source,0));outputMotion[id.xy]=motion.Load(int3(source,0));}
)";
  ComPtr<ID3DBlob> blob,error;if(FAILED(D3DCompile(code,sizeof(code)-1,nullptr,nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&blob,&error))||FAILED(device->CreateComputeShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,&p->shader)))return false;
  D3D11_BUFFER_DESC b{};b.ByteWidth=32;b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;if(FAILED(device->CreateBuffer(&b,nullptr,&p->params)))return false;
 }
 if(!p->shader||!p->state||!p->params)return false;
 if(ow!=p->width||oh!=p->height){p->depth.Reset();p->motion.Reset();p->zOut.Reset();p->mvOut.Reset();D3D11_TEXTURE2D_DESC d{};d.Width=ow;d.Height=oh;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;d.Format=DXGI_FORMAT_R32_FLOAT;
 if(FAILED(device->CreateTexture2D(&d,nullptr,&p->depth))||FAILED(device->CreateUnorderedAccessView(p->depth.Get(),nullptr,&p->zOut)))return false;d.Format=DXGI_FORMAT_R16G16_FLOAT;if(FAILED(device->CreateTexture2D(&d,nullptr,&p->motion))||FAILED(device->CreateUnorderedAccessView(p->motion.Get(),nullptr,&p->mvOut)))return false;p->width=ow;p->height=oh;}
 ComPtr<ID3D11ShaderResourceView> z,mv;if(FAILED(device->CreateShaderResourceView(depth,nullptr,&z))||FAILED(device->CreateShaderResourceView(motion,nullptr,&mv)))return false;
 ComPtr<ID3DDeviceContextState> prior;p->context->SwapDeviceContextState(p->state.Get(),&prior);
 {DlssStageTiming::Span timing(c,2,eye,serial);
 float values[8]={float(rw),float(rh),float(ow),float(oh),jx,jy};c->UpdateSubresource(p->params.Get(),0,nullptr,values,0,0);auto* cb=p->params.Get();c->CSSetConstantBuffers(0,1,&cb);
 ID3D11ShaderResourceView* srvs[]={z.Get(),mv.Get()};ID3D11UnorderedAccessView* uavs[]={p->zOut.Get(),p->mvOut.Get()};c->CSSetShaderResources(0,2,srvs);c->CSSetUnorderedAccessViews(0,2,uavs,nullptr);c->CSSetShader(p->shader.Get(),nullptr,0);c->Dispatch((ow+7)/8,(oh+7)/8,1);
 ID3D11ShaderResourceView* noSrv[2]{};ID3D11UnorderedAccessView* noUav[2]{};c->CSSetShaderResources(0,2,noSrv);c->CSSetUnorderedAccessViews(0,2,noUav,nullptr);}
 p->context->SwapDeviceContextState(prior.Get(),nullptr);p->valid=true;return true;
}
}
