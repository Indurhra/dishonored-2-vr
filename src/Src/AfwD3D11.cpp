#include "AfwD3D11.h"
#include "AfwShader.h"
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include <cmath>
#include <cstring>
using Microsoft::WRL::ComPtr;
namespace AfwD3D11 {
struct Backend::Impl {
 ComPtr<ID3D11Device> device;ComPtr<ID3D11ComputeShader> seed,compose;
 ComPtr<ID3D11Buffer> parameters;ComPtr<ID3D11SamplerState> linear,point;
 ComPtr<ID3D11Texture2D> seeds[2],output;ComPtr<ID3D11ShaderResourceView> seedSrv[2],outputSrv;
 ComPtr<ID3D11UnorderedAccessView> seedUav[2],outputUav;UINT width=0,height=0;bool valid=false;std::string problem="not initialized";
 struct alignas(16) Params {float f[16],fi[16],h[16],hi[16],sizes[4],limits[4];UINT enabled,foregroundAligned,padding[2];};
 bool Fail(const char* reason){problem=reason;valid=false;return false;}
 bool Allocate(UINT w,UINT h){
   if(width==w&&height==h&&output)return true;
   output.Reset();outputSrv.Reset();outputUav.Reset();for(int i=0;i<2;++i){seeds[i].Reset();seedSrv[i].Reset();seedUav[i].Reset();}
   D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=d.ArraySize=1;d.SampleDesc.Count=1;
   d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;d.Format=DXGI_FORMAT_R32_UINT;
   for(int i=0;i<2;++i)if(FAILED(device->CreateTexture2D(&d,nullptr,&seeds[i]))||FAILED(device->CreateShaderResourceView(seeds[i].Get(),nullptr,&seedSrv[i]))||FAILED(device->CreateUnorderedAccessView(seeds[i].Get(),nullptr,&seedUav[i])))return Fail("seed allocation failed");
   d.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
   if(FAILED(device->CreateTexture2D(&d,nullptr,&output))||FAILED(device->CreateShaderResourceView(output.Get(),nullptr,&outputSrv))||FAILED(device->CreateUnorderedAccessView(output.Get(),nullptr,&outputUav)))return Fail("output allocation failed");
   width=w;height=h;return true;
 }
};
Backend::Backend():p(std::make_unique<Impl>()){}Backend::~Backend()=default;
const std::string& Backend::Problem()const{return p->problem;}
ID3D11ShaderResourceView* Backend::Output()const{return p->valid?p->outputSrv.Get():nullptr;}
bool Backend::Initialize(ID3D11Device* device){
 p=std::make_unique<Impl>();if(!device)return p->Fail("no device");p->device=device;
 auto compile=[&](const char* entry,ComPtr<ID3D11ComputeShader>& shader){ComPtr<ID3DBlob> code,error;
   HRESULT hr=D3DCompile(kAfwShader,sizeof(kAfwShader)-1,"D2 AFW",nullptr,nullptr,entry,"cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&error);
   if(FAILED(hr)){p->problem=error?static_cast<const char*>(error->GetBufferPointer()):"shader compilation failed";return false;}
   return SUCCEEDED(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader));};
 if(!compile("Seed",p->seed)||!compile("Compose",p->compose))return false;
 D3D11_BUFFER_DESC b{};b.ByteWidth=sizeof(Impl::Params);b.Usage=D3D11_USAGE_DEFAULT;b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
 if(FAILED(device->CreateBuffer(&b,nullptr,&p->parameters)))return p->Fail("parameters allocation failed");
 D3D11_SAMPLER_DESC s{};s.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;s.MaxLOD=D3D11_FLOAT32_MAX;
 if(FAILED(device->CreateSamplerState(&s,&p->linear)))return p->Fail("linear sampler failed");s.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
 if(FAILED(device->CreateSamplerState(&s,&p->point)))return p->Fail("point sampler failed");p->problem.clear();return true;
}
bool Backend::Rebuild(ID3D11DeviceContext* ctx,const Source& fresh,const Source* held,UINT width,UINT height,const Settings& settings){
 p->valid=false;if(!p->compose || !ctx || ctx->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return p->Fail("immediate initialized context required");
 ComPtr<ID3D11Device> device;ctx->GetDevice(&device);if(device.Get()!=p->device.Get())return p->Fail("wrong device");
 if(!TemporalFrame::Valid(fresh.stamp)||!fresh.color||!fresh.depth||!fresh.mask||!width||!height||width>16384||height>16384)return p->Fail("invalid fresh image");
 if(held && (!TemporalFrame::HeldUsable(held->stamp,fresh.stamp)||!held->color||!held->depth||!held->mask))return p->Fail("invalid held history");
 if(!std::isfinite(settings.pixelTolerance)||settings.pixelTolerance<=0||!std::isfinite(settings.relativeDepthTolerance)||settings.relativeDepthTolerance<0||!std::isfinite(settings.depthQuantization)||settings.depthQuantization<0)return p->Fail("invalid tolerances");
 Impl::Params params{};
 auto matrix=[&](const float* source,float* forward,float* inverse){
   for(int i=0;i<16;++i)if(!std::isfinite(source[i]))return false;
   DirectX::XMFLOAT4X4 value;memcpy(&value,source,64);auto m=DirectX::XMLoadFloat4x4(&value);DirectX::XMVECTOR determinant;
   auto inv=DirectX::XMMatrixInverse(&determinant,m);if(std::abs(DirectX::XMVectorGetX(determinant))<1e-12f)return false;
   DirectX::XMStoreFloat4x4(&value,inv);memcpy(inverse,&value,64);memcpy(forward,source,64);return true;};
 if(!matrix(fresh.toTarget,params.f,params.fi)||(held&&!matrix(held->toTarget,params.h,params.hi)))return p->Fail("invalid clip transform");
 auto validate=[&](const Source& source){
   ID3D11ShaderResourceView* views[]={source.color,source.depth,source.mask};
   for(int k=0;k<3;++k){ComPtr<ID3D11Resource> resource;views[k]->GetResource(&resource);ComPtr<ID3D11Texture2D> texture;
     if(FAILED(resource.As(&texture)))return false;ComPtr<ID3D11Device> owner;texture->GetDevice(&owner);D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
     D3D11_SHADER_RESOURCE_VIEW_DESC view{};views[k]->GetDesc(&view);
     if(owner.Get()!=p->device.Get()||desc.ArraySize!=1||desc.SampleDesc.Count!=1||view.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D||view.Texture2D.MostDetailedMip!=0)return false;
     if(k>0&&(desc.Width!=source.stamp.width||desc.Height!=source.stamp.height))return false;
     if(k==1&&view.Format!=DXGI_FORMAT_R32_FLOAT)return false;
     if(k==2&&view.Format!=DXGI_FORMAT_R8_UINT)return false;
   }return true;
 };
 if(!validate(fresh)||(held&&!validate(*held)))return p->Fail("invalid source resources");
 if(!p->Allocate(width,height))return false;
 params.sizes[0]=float(fresh.stamp.width);params.sizes[1]=float(fresh.stamp.height);params.sizes[2]=float(width);params.sizes[3]=float(height);
 params.limits[1]=settings.pixelTolerance;params.limits[2]=settings.relativeDepthTolerance;params.limits[3]=settings.depthQuantization;params.enabled=held?1:0;params.foregroundAligned=settings.heldForegroundAligned?1:0;
 ID3D11ShaderResourceView* resources[]={fresh.color,fresh.depth,fresh.mask,held?held->color:nullptr,held?held->depth:nullptr,held?held->mask:nullptr,nullptr,nullptr};
 ctx->CSSetShaderResources(0,8,resources);ID3D11SamplerState* samplers[]={p->linear.Get(),p->point.Get()};ctx->CSSetSamplers(0,2,samplers);
 auto* cb=p->parameters.Get();ctx->CSSetConstantBuffers(0,1,&cb);UINT zero[4]{};
 for(int i=0;i<(held?2:1);++i){
   ctx->ClearUnorderedAccessViewUint(p->seedUav[i].Get(),zero);params.limits[0]=float(i);ctx->UpdateSubresource(cb,0,nullptr,&params,0,0);
   auto* uav=p->seedUav[i].Get();ctx->CSSetUnorderedAccessViews(0,1,&uav,nullptr);ctx->CSSetShader(p->seed.Get(),nullptr,0);
   ctx->Dispatch((fresh.stamp.width+7)/8,(fresh.stamp.height+7)/8,1);
 }
 ID3D11UnorderedAccessView* noUav[2]{};ctx->CSSetUnorderedAccessViews(0,2,noUav,nullptr);
 resources[6]=p->seedSrv[0].Get();resources[7]=held?p->seedSrv[1].Get():nullptr;ctx->CSSetShaderResources(0,8,resources);
 auto* out=p->outputUav.Get();ctx->CSSetUnorderedAccessViews(1,1,&out,nullptr);ctx->CSSetShader(p->compose.Get(),nullptr,0);ctx->Dispatch((width+7)/8,(height+7)/8,1);
 ctx->CSSetUnorderedAccessViews(0,2,noUav,nullptr);ID3D11ShaderResourceView* noSrv[8]{};ctx->CSSetShaderResources(0,8,noSrv);
 p->valid=true;p->problem.clear();return true;
}
}
