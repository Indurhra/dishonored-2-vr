#include "AfwInputs.h"
#include <wrl/client.h>
#include <vector>
#include <cstdio>
using Microsoft::WRL::ComPtr;
int main(){ComPtr<ID3D11Device>d;ComPtr<ID3D11DeviceContext>c;D3D_FEATURE_LEVEL level;if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,&level,&c)))return 1;
 constexpr UINT n=16;std::vector<float> z(n*n,-99);std::vector<UINT> mv(n*n,0);for(UINT y=0;y<8;++y)for(UINT x=0;x<8;++x){z[y*n+x]=x<4?.8f:.2f;mv[y*n+x]=0xb8003400;}
 auto make=[&](DXGI_FORMAT f,const void* data){D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=n;td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;td.Format=f;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;D3D11_SUBRESOURCE_DATA init{data,n*4,0};ComPtr<ID3D11Texture2D> t;d->CreateTexture2D(&td,&init,&t);return t;};auto depth=make(DXGI_FORMAT_R32_FLOAT,z.data()),motion=make(DXGI_FORMAT_R16G16_FLOAT,mv.data());
 D3D11_BUFFER_DESC bd{};bd.ByteWidth=16;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;ComPtr<ID3D11Buffer> cb;d->CreateBuffer(&bd,nullptr,&cb);auto* cbp=cb.Get();c->CSSetConstantBuffers(8,1,&cbp);
 AfwInputs::Adapter a;ID3D11Texture2D* first=nullptr;
 for(int shift=0;shift<2;++shift){if(!a.Prepare(c.Get(),depth.Get(),motion.Get(),8,8,n,n,float(shift),0))return 2;if(first&&first!=a.Depth())return 3;first=a.Depth();
 ComPtr<ID3D11Buffer> after;c->CSGetConstantBuffers(8,1,&after);if(after.Get()!=cb.Get())return 4;
 for(int k=0;k<2;++k){auto* output=k?a.Motion():a.Depth();D3D11_TEXTURE2D_DESC td{};output->GetDesc(&td);td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> read;if(FAILED(d->CreateTexture2D(&td,nullptr,&read)))return 5;c->CopyResource(read.Get(),output);D3D11_MAPPED_SUBRESOURCE map{};if(FAILED(c->Map(read.Get(),0,D3D11_MAP_READ,0,&map)))return 6;bool valid=true;
 for(UINT y=0;y<n;++y)for(UINT x=0;x<n;++x){auto* row=(char*)map.pData+y*map.RowPitch;if(k)valid&=((UINT*)row)[x]==0xb8003400;else valid&=((float*)row)[x]==(x<UINT(8-shift*2)?.8f:.2f);}c->Unmap(read.Get(),0);if(!valid)return 7;}
 }
 // Identity bypass must retain exact inputs and safely return to shader work.
 AfwInputs::SetIdentityFastPath(true);
 for(float j:{-.49f,-.375f,0.f,.375f,.49f}){
  if(!a.Prepare(c.Get(),depth.Get(),motion.Get(),n,n,n,n,j,-j)||a.Depth()!=depth.Get()||a.Motion()!=motion.Get())return 9;
 }
 if(!a.Prepare(c.Get(),depth.Get(),motion.Get(),n,n,n,n,.5f,0)||a.Depth()==depth.Get())return 10;
 if(!a.Prepare(c.Get(),depth.Get(),motion.Get(),8,8,n,n,0,0)||a.Depth()==depth.Get())return 11;
 AfwInputs::SetIdentityFastPath(false);
 if(!a.Prepare(c.Get(),depth.Get(),motion.Get(),n,n,n,n,.375f,-.375f)||a.Depth()==depth.Get())return 12;
 // Compare all finite depth/motion values from the real shader against the bypass.
 for(int k=0;k<2;++k){auto* output=k?a.Motion():a.Depth();D3D11_TEXTURE2D_DESC td{};output->GetDesc(&td);td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> read;if(FAILED(d->CreateTexture2D(&td,nullptr,&read)))return 13;c->CopyResource(read.Get(),output);D3D11_MAPPED_SUBRESOURCE map{};if(FAILED(c->Map(read.Get(),0,D3D11_MAP_READ,0,&map)))return 14;bool valid=true;
 for(UINT y=0;y<n;++y)for(UINT x=0;x<n;++x){auto* row=(char*)map.pData+y*map.RowPitch;if(k)valid&=((UINT*)row)[x]==mv[y*n+x];else valid&=((float*)row)[x]==z[y*n+x];}c->Unmap(read.Get(),0);if(!valid)return 15;}
 puts("PASS identity bypass equals shader output; edge jitter and scaled views retain shader; transitions preserve output ownership");
 if(a.Prepare(c.Get(),depth.Get(),motion.Get(),0,8,n,n,0,0)||a.Depth())return 8;puts("PASS active viewport crop, 2x depth edge, raster jitter correction, normalized motion preservation, state restoration, reuse and invalid dimensions");}
