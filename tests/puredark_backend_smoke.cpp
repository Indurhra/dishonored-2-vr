#include "AfwPlugin.h"
#include <wrl/client.h>
#include <cstdio>
#include <vector>
using Microsoft::WRL::ComPtr;
int wmain(int argc,wchar_t** argv){
 if(argc!=2)return 2;ComPtr<ID3D11Device> d;ComPtr<ID3D11DeviceContext> c;D3D_FEATURE_LEVEL level;
 if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,&level,&c)))return 2;
 AfwPlugin::Backend backend;if(!backend.Initialize(d.Get(),argv[1])){puts(backend.Problem().c_str());return 3;}
 constexpr UINT n=64;auto make=[&](DXGI_FORMAT f,const void* data,UINT stride){ComPtr<ID3D11Texture2D> t;D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=n;td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;td.Format=f;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;D3D11_SUBRESOURCE_DATA init{data,n*stride,0};d->CreateTexture2D(&td,data?&init:nullptr,&t);return t;};
 std::vector<float> z(n*n,.5f);std::vector<UINT> zero(n*n);auto color=make(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,nullptr,4),depth=make(DXGI_FORMAT_R32_FLOAT,z.data(),4),motion=make(DXGI_FORMAT_R16G16_FLOAT,zero.data(),4);
 D3D11_TEXTURE2D_DESC td{};color->GetDesc(&td);td.Usage=D3D11_USAGE_STAGING;td.BindFlags=0;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> staging;if(FAILED(d->CreateTexture2D(&td,nullptr,&staging)))return 4;
 AfwPlugin::Frame f;f.color=color.Get();f.depth=depth.Get();f.motion=motion.Get();f.motionScale[0]=f.motionScale[1]=n;for(auto& m:f.camera.matrices)for(int i=0;i<4;++i)m[i*5]=1;
 unsigned timings=0;f.profile=true;for(int k=0;k<12;++k){f.serial=k;f.eye=k%2;int shift=k<2?0:4;f.camera.matrices[2][12]=2.f*shift/n;f.camera.matrices[3][12]=-2.f*shift/n;
 std::vector<unsigned char> pixels(n*n*4);for(UINT y=0;y<n;++y)for(UINT x=0;x<n;++x){auto* p=&pixels[(y*n+x)*4];p[0]=64+x*2;p[2]=y*3;p[3]=255;}c->UpdateSubresource(color.Get(),0,nullptr,pixels.data(),n*4,0);
 if(!backend.Evaluate(c.Get(),f)){puts(backend.Problem().c_str());return 5;}AfwPlugin::Timing timing{};if(backend.TakeTiming(timing)){if(timing.serial>=f.serial||timing.eye!=int(timing.serial%2)||timing.warpMs<=0||timing.copyMs<0)return 9;++timings;}c->CopyResource(staging.Get(),backend.Output());D3D11_MAPPED_SUBRESOURCE map{};if(FAILED(c->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)))return 6;
 unsigned correct=0;for(UINT y=4;y<n-4;++y)for(UINT x=4;x<n-4;++x){auto* p=(unsigned char*)map.pData+y*map.RowPitch+x*4;if(abs(int(p[0])-(64+(int(x)-shift)*2))<=2&&p[1]<3&&abs(int(p[2])-int(y*3))<=2)++correct;}c->Unmap(staging.Get(),0);if(correct!=3136){printf("FAIL frame %d pixels %u\n",k,correct);return 7;}
 }
 if(timings<6)return 10;printf("Timestamp samples %u\n",timings);auto bad=f;bad.eye=2;if(backend.Evaluate(c.Get(),bad)||backend.Output())return 8;
 puts("PASS persistent resources, 12 alternating-eye identity/translation frames, allocator reuse, invalid eye refusal. Readback only in test.");
}
