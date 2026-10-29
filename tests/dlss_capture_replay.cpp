#include "DlssD3D11.h"
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>
#include <iostream>
using Microsoft::WRL::ComPtr;
int wmain(int argc,wchar_t** argv){
    if(argc!=5){std::cerr<<"usage: dlss_capture_replay capture-base runtime-dir cache-dir output-half-rgba\n";return 2;}
    const std::wstring base=argv[1];UINT width=0,height=0;uint64_t serial=0;int eye=-1;
    std::ifstream meta(std::filesystem::path(base+L"_metadata.txt"));std::string line;
    while(std::getline(meta,line)){std::istringstream s(line);std::string key;s>>key;
        if(key=="width")s>>width;if(key=="height")s>>height;if(key=="serial")s>>serial;if(key=="eye")s>>eye;}
    if(!width || !height || width>16384 || height>16384 || !serial || eye<0 || eye>1){std::cerr<<"invalid metadata\n";return 1;}
    const size_t count=size_t(width)*height;
    std::vector<uint32_t> raw[3];const wchar_t* suffix[]={L"_color.bin",L"_motion.bin",L"_depth.bin"};
    for(int k=0;k<3;++k){raw[k].resize(count);std::ifstream f(std::filesystem::path(base+suffix[k]),std::ios::binary);
        if(!f.read(reinterpret_cast<char*>(raw[k].data()),count*4) || f.peek()!=EOF){std::cerr<<"invalid capture size\n";return 1;}}
    std::vector<float> depth(count);for(size_t i=0;i<count;++i)depth[i]=float(raw[2][i]&0xffffff)/16777215.f;
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> ctx;D3D_FEATURE_LEVEL level;
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&ctx)))return 1;
    auto texture=[&](DXGI_FORMAT format,const void* data){D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.ArraySize=desc.MipLevels=1;
        desc.Format=format;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;D3D11_SUBRESOURCE_DATA init{data,width*4,0};
        ComPtr<ID3D11Texture2D> result;device->CreateTexture2D(&desc,&init,&result);return result;};
    auto color=texture(DXGI_FORMAT_R11G11B10_FLOAT,raw[0].data()),motion=texture(DXGI_FORMAT_R16G16_FLOAT,raw[1].data()),z=texture(DXGI_FORMAT_R32_FLOAT,depth.data());
    DlssD3D11::Backend backend;
    auto fail=[&](){std::cerr<<backend.Problem()<<'\n';return 1;};
    if(!backend.Initialize(device.Get(),argv[2],argv[3]))return fail();
    DlssD3D11::Config config;config.renderWidth=config.outputWidth=width;config.renderHeight=config.outputHeight=height;config.depthInverted=true;
    if(!backend.Configure(ctx.Get(),config))return fail();
    DlssD3D11::Input input;input.color=color.Get();input.depth=z.Get();input.motion=motion.Get();
    input.colorStamp=input.depthStamp=input.motionStamp={serial,1,eye,width,height};input.mvScaleX=-float(width);input.mvScaleY=-float(height);
    // One reset frame checks actual captured resource compatibility only. Zero jitter
    // deliberately avoids presenting a sign hypothesis as a validated convention.
    input.reset=true;input.frameTimeMs=0;
    if(!backend.Evaluate(ctx.Get(),input))return fail();
    ComPtr<ID3D11Resource> resource;backend.Output(eye)->GetResource(&resource);ComPtr<ID3D11Texture2D> output;resource.As(&output);
    D3D11_TEXTURE2D_DESC desc{};output->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;if(FAILED(device->CreateTexture2D(&desc,nullptr,&staging)))return 1;
    ctx->CopyResource(staging.Get(),output.Get());D3D11_MAPPED_SUBRESOURCE mapped{};if(FAILED(ctx->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)))return 1;
    std::ofstream file(std::filesystem::path(argv[4]),std::ios::binary);size_t invalid=0,nonzero=0;
    for(UINT y=0;y<height;++y){auto* row=static_cast<const char*>(mapped.pData)+size_t(y)*mapped.RowPitch;file.write(row,width*8);
        auto* half=reinterpret_cast<const uint16_t*>(row);for(UINT x=0;x<width;++x)for(int k=0;k<3;++k){auto v=half[x*4+k];invalid+=(v&0x7c00)==0x7c00;nonzero+=(v&0x7fff)!=0;}}
    ctx->Unmap(staging.Get(),0);
    if(!file || invalid || !nonzero){std::cerr<<"invalid output\n";return 1;}
    std::cout<<"PASS captured scene "<<width<<'x'<<height<<" eye "<<eye<<" serial "<<serial<<": finite nonblack DLAA reset output. No temporal or performance claim.\n";
}
