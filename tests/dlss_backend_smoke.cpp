#include "DlssD3D11.h"
#include <wrl/client.h>
#include <cstdio>
#include <vector>
using Microsoft::WRL::ComPtr;
int wmain(int argc,wchar_t** argv){
    if(argc!=3){std::fprintf(stderr,"usage: dlss_backend_smoke <runtime-directory> <cache-directory>\n");return 2;}
    ComPtr<ID3D11Device> d;ComPtr<ID3D11DeviceContext> c;D3D_FEATURE_LEVEL fl;
    HRESULT hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,&fl,&c);
    if(FAILED(hr)){std::fprintf(stderr,"device failed %lx\n",hr);return 1;}
    DlssD3D11::Backend b;
    auto fail=[&](){std::fprintf(stderr,"FAIL: %s\n",b.Problem().c_str());return 1;};
    if(!b.Initialize(d.Get(),argv[1],argv[2]))return fail();
    constexpr UINT size=256;
    auto texture=[&](DXGI_FORMAT format,UINT bytes,const void* data){
        D3D11_TEXTURE2D_DESC desc{};desc.Width=size;desc.Height=size;desc.ArraySize=1;desc.MipLevels=1;
        desc.Format=format;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA init{data,size*bytes,0};ComPtr<ID3D11Texture2D> t;
        if(FAILED(d->CreateTexture2D(&desc,&init,&t)))std::fprintf(stderr,"input allocation failed\n");return t;
    };
    // Constant half-float grey, normal-Z far-ish depth, zero UV motion.
    std::vector<unsigned short> color(size*size*4,0x3800),motion(size*size*2,0);
    std::vector<float> depth(size*size,0.5f);
    auto col=texture(DXGI_FORMAT_R16G16B16A16_FLOAT,8,color.data());
    auto dep=texture(DXGI_FORMAT_R32_FLOAT,4,depth.data());
    auto mv=texture(DXGI_FORMAT_R16G16_FLOAT,4,motion.data());
    auto checkOutput=[&](int eye,UINT expected){
        ComPtr<ID3D11Resource> resource;b.Output(eye)->GetResource(&resource);
        ComPtr<ID3D11Texture2D> output;if(FAILED(resource.As(&output)))return false;
        D3D11_TEXTURE2D_DESC desc{};output->GetDesc(&desc);
        if(desc.Width!=expected || desc.Height!=expected || desc.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT)return false;
        desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;if(FAILED(d->CreateTexture2D(&desc,nullptr,&staging)))return false;
        c->CopyResource(staging.Get(),output.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
        if(FAILED(c->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)))return false;
        unsigned nonzero=0,invalid=0;
        for(UINT y=0;y<expected;++y){
            auto row=reinterpret_cast<const unsigned short*>(static_cast<const char*>(mapped.pData)+y*mapped.RowPitch);
            for(UINT x=0;x<expected;++x)for(UINT channel=0;channel<3;++channel){
                const auto v=row[x*4+channel];invalid+=((v&0x7c00)==0x7c00);nonzero+=((v&0x7fff)!=0);
            }
        }
        c->Unmap(staging.Get(),0);return invalid==0 && nonzero>expected*expected;
    };
    for(int q=0;q<5;++q){
        DlssD3D11::Config config;config.renderWidth=config.renderHeight=size;
        config.outputWidth=config.outputHeight=q?size*2:size;config.quality=DlssD3D11::Quality(q);
        if(!b.Configure(c.Get(),config))return fail();
        for(int frame=0;frame<6;++frame){
            DlssD3D11::Input in;in.color=col.Get();in.depth=dep.Get();in.motion=mv.Get();
            in.colorStamp={uint64_t(frame+1),uint64_t(q+1),frame%2,size,size};in.depthStamp=in.motionStamp=in.colorStamp;
            in.mvScaleX=in.mvScaleY=-float(size);in.frameTimeMs=22.22f;
            if(!b.Evaluate(c.Get(),in) || !b.Output(frame%2))return fail();
        }
        if(!checkOutput(1,config.outputWidth)){std::fprintf(stderr,"FAIL: invalid or black output\n");return 1;}
        std::printf("PASS quality %d: two eye features, six evaluations, finite nonblack GPU readback\n",q);
    }
    // A resource from another view must fail before NGX is called.
    DlssD3D11::Input bad;bad.colorStamp={7,5,0,size,size};bad.depthStamp={6,5,1,size,size};bad.motionStamp=bad.colorStamp;
    if(b.Evaluate(c.Get(),bad)){std::fprintf(stderr,"FAIL: mismatched guides accepted\n");return 1;}
    if(b.Output(0) || b.Output(1)){std::fprintf(stderr,"FAIL: stale output exposed after refusal\n");return 1;}
    b.Shutdown();b.Shutdown();
    if(!b.Initialize(d.Get(),argv[1],argv[2]))return fail();
    DlssD3D11::Config restart;restart.renderWidth=restart.renderHeight=restart.outputWidth=restart.outputHeight=size;
    if(!b.Configure(c.Get(),restart))return fail();
    ComPtr<ID3D11DeviceContext> deferred;d->CreateDeferredContext(0,&deferred);
    if(b.Configure(deferred.Get(),restart)){std::fprintf(stderr,"FAIL: deferred context accepted\n");return 1;}
    b.Shutdown();
    std::puts("PASS mismatch refusal and repeated shutdown. Synthetic backend test only; no game quality/performance claim.");
}
