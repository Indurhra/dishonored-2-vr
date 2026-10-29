#include "AfwD3D11.h"
#include <wrl/client.h>
#include <cstdio>
#include <vector>
using Microsoft::WRL::ComPtr;
int main(){
 ComPtr<ID3D11Device> d;ComPtr<ID3D11DeviceContext> c;D3D_FEATURE_LEVEL level;
 if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,&level,&c)))return 1;
 AfwD3D11::Backend backend;if(!backend.Initialize(d.Get())){std::fprintf(stderr,"%s\n",backend.Problem().c_str());return 1;}
 constexpr UINT size=64;std::vector<float> depth(size*size,.5f);std::vector<UINT> color(size*size,0xff4080c0);std::vector<unsigned char> mask(size*size,0);
 auto make=[&](DXGI_FORMAT format,UINT bytes,const void* data){D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=size;desc.ArraySize=desc.MipLevels=1;desc.Format=format;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
 D3D11_SUBRESOURCE_DATA init{data,size*bytes,0};ComPtr<ID3D11Texture2D> t;ComPtr<ID3D11ShaderResourceView> srv;if(SUCCEEDED(d->CreateTexture2D(&desc,&init,&t)))d->CreateShaderResourceView(t.Get(),nullptr,&srv);return srv;};
 auto rgb=make(DXGI_FORMAT_R8G8B8A8_UNORM,4,color.data()),z=make(DXGI_FORMAT_R32_FLOAT,4,depth.data()),m=make(DXGI_FORMAT_R8_UINT,1,mask.data());
 AfwD3D11::Source fresh;fresh.color=rgb.Get();fresh.depth=z.Get();fresh.mask=m.Get();fresh.stamp={2,1,0,size,size};for(int i=0;i<4;++i)fresh.toTarget[i*4+i]=1;
 if(!backend.Rebuild(c.Get(),fresh,nullptr,size,size,{})){std::fprintf(stderr,"%s\n",backend.Problem().c_str());return 1;}
 auto read=[&](){ComPtr<ID3D11Resource> r;backend.Output()->GetResource(&r);ComPtr<ID3D11Texture2D> t;r.As(&t);D3D11_TEXTURE2D_DESC desc{};t->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
 ComPtr<ID3D11Texture2D> s;if(FAILED(d->CreateTexture2D(&desc,nullptr,&s)))return false;c->CopyResource(s.Get(),t.Get());D3D11_MAPPED_SUBRESOURCE map{};if(FAILED(c->Map(s.Get(),0,D3D11_MAP_READ,0,&map)))return false;
 bool valid=true;for(UINT y=0;y<size;++y){auto* row=reinterpret_cast<const unsigned short*>(static_cast<const char*>(map.pData)+y*map.RowPitch);for(UINT x=0;x<size;++x){valid&=row[x*4+3]==0x3c00;valid&=row[x*4]>0;for(int k=0;k<3;++k)valid&=(row[x*4+k]&0x7c00)!=0x7c00;}}
 c->Unmap(s.Get(),0);return valid;};
 if(!read()){std::puts("FAIL identity reconstruction");return 1;}
 auto held=fresh;held.stamp={1,1,1,size,size};
 if(!backend.Rebuild(c.Get(),fresh,&held,size,size,{})||!read()){std::puts("FAIL two-source identity");return 1;}
 held.stamp.epoch=2;if(backend.Rebuild(c.Get(),fresh,&held,size,size,{})||backend.Output()){std::puts("FAIL stale history refusal");return 1;}
 fresh.toTarget[0]=0;if(backend.Rebuild(c.Get(),fresh,nullptr,size,size,{})||backend.Output()){std::puts("FAIL singular matrix refusal");return 1;}
 std::puts("PASS GPU identity, two-source identity, stale epoch and singular transform refusal. Prototype only.");
}
