#include "AfwD3D11.h"
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <thread>
using Microsoft::WRL::ComPtr;
int wmain(int argc,wchar_t** argv){
 if(argc!=4 && argc!=6 && argc!=7){std::cerr<<"usage: afw_capture_replay capture-base clip-transform.txt output-half-rgba [held-capture-base held-clip-transform.txt]\n";return 2;}
 const std::wstring base=argv[1];UINT width=0,height=0;uint64_t serial=0;int eye=-1;std::string line;
 std::ifstream meta(std::filesystem::path(base+L"_metadata.txt"));while(std::getline(meta,line)){std::istringstream s(line);std::string key;s>>key;
 if(key=="width")s>>width;if(key=="height")s>>height;if(key=="serial")s>>serial;if(key=="eye")s>>eye;}
 if(!width||!height||width>16384||height>16384||!serial||eye<0||eye>1)return 1;
 const size_t count=size_t(width)*height;std::vector<uint32_t> color(count),depthRaw(count);
 auto read=[&](const wchar_t* suffix,void* data){std::ifstream f(std::filesystem::path(base+suffix),std::ios::binary);return bool(f.read(static_cast<char*>(data),count*4));};
 if(!read(L"_color.bin",color.data())||!read(L"_depth.bin",depthRaw.data()))return 1;
 std::vector<float> depth(count);std::vector<unsigned char> mask(count);for(size_t i=0;i<count;++i){depth[i]=float(depthRaw[i]&0xffffff)/16777215.f;mask[i]=static_cast<unsigned char>(depthRaw[i]>>24);}
 ComPtr<ID3D11Device> d;ComPtr<ID3D11DeviceContext> c;D3D_FEATURE_LEVEL level;
 if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,&level,&c)))return 1;
 auto make=[&](DXGI_FORMAT format,UINT bytes,const void* data){D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.ArraySize=desc.MipLevels=1;desc.Format=format;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
 D3D11_SUBRESOURCE_DATA init{data,width*bytes,0};ComPtr<ID3D11Texture2D> t;ComPtr<ID3D11ShaderResourceView> srv;if(SUCCEEDED(d->CreateTexture2D(&desc,&init,&t)))d->CreateShaderResourceView(t.Get(),nullptr,&srv);return srv;};
 auto rgb=make(DXGI_FORMAT_R11G11B10_FLOAT,4,color.data()),z=make(DXGI_FORMAT_R32_FLOAT,4,depth.data()),m=make(DXGI_FORMAT_R8_UINT,1,mask.data());
 AfwD3D11::Source fresh;fresh.color=rgb.Get();fresh.depth=z.Get();fresh.mask=m.Get();fresh.stamp={serial,1,eye,width,height};
 std::ifstream transform{std::filesystem::path(argv[2])};for(auto& v:fresh.toTarget)if(!(transform>>v))return 1;
 AfwD3D11::Source held;ComPtr<ID3D11ShaderResourceView> heldRgb,heldDepth,heldMask;
 if(argc>=6){
   const std::wstring heldBase=argv[4];UINT hw=0,hh=0;std::ifstream hm(std::filesystem::path(heldBase+L"_metadata.txt"));
   while(std::getline(hm,line)){std::istringstream s(line);std::string key;s>>key;
     if(key=="width")s>>hw;if(key=="height")s>>hh;if(key=="serial")s>>held.stamp.serial;if(key=="eye")s>>held.stamp.eye;}
   held.stamp.epoch=1;held.stamp.width=hw;held.stamp.height=hh;if(hw!=width||hh!=height)return 1;
   std::ifstream hc(std::filesystem::path(heldBase+L"_color.bin"),std::ios::binary),hd(std::filesystem::path(heldBase+L"_depth.bin"),std::ios::binary);
   if(!hc.read(reinterpret_cast<char*>(color.data()),count*4)||!hd.read(reinterpret_cast<char*>(depthRaw.data()),count*4))return 1;
   for(size_t i=0;i<count;++i){depth[i]=float(depthRaw[i]&0xffffff)/16777215.f;mask[i]=static_cast<unsigned char>(depthRaw[i]>>24);}
   heldRgb=make(DXGI_FORMAT_R11G11B10_FLOAT,4,color.data());heldDepth=make(DXGI_FORMAT_R32_FLOAT,4,depth.data());heldMask=make(DXGI_FORMAT_R8_UINT,1,mask.data());
   held.color=heldRgb.Get();held.depth=heldDepth.Get();held.mask=heldMask.Get();
   std::ifstream ht{std::filesystem::path(argv[5])};for(auto& v:held.toTarget)if(!(ht>>v))return 1;
 }
 AfwD3D11::Backend b;auto fail=[&](){std::cerr<<b.Problem()<<'\n';return 1;};if(!b.Initialize(d.Get()))return fail();
 AfwD3D11::Settings settings;if(argc==7){if(std::wstring(argv[6])!=L"--stationary-foreground")return 2;settings.heldForegroundAligned=true;}
 if(!b.Rebuild(c.Get(),fresh,argc>=6?&held:nullptr,width,height,settings))return fail();
 // Isolated GPU timing excludes upload, compilation and readback. Never report
 // 1000 / kernel milliseconds as game FPS: scene rendering is absent here.
 for(int warm=0;warm<8;++warm)if(!b.Rebuild(c.Get(),fresh,argc>=6?&held:nullptr,width,height,settings))return fail();
 struct Sample {ComPtr<ID3D11Query> disjoint,start,end;};
 std::vector<Sample> samples(60);
 for(auto& sample:samples){
   D3D11_QUERY_DESC q{D3D11_QUERY_TIMESTAMP_DISJOINT,0};
   if(FAILED(d->CreateQuery(&q,&sample.disjoint)))return 1;
   q.Query=D3D11_QUERY_TIMESTAMP;
   if(FAILED(d->CreateQuery(&q,&sample.start))||FAILED(d->CreateQuery(&q,&sample.end)))return 1;
   c->Begin(sample.disjoint.Get());c->End(sample.start.Get());
   if(!b.Rebuild(c.Get(),fresh,argc>=6?&held:nullptr,width,height,settings))return fail();
   c->End(sample.end.Get());c->End(sample.disjoint.Get());
 }
 c->Flush();std::vector<double> milliseconds;
 const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
 auto wait=[&](ID3D11Query* query,void* data,UINT bytes){for(;;){
   const auto hr=c->GetData(query,data,bytes,D3D11_ASYNC_GETDATA_DONOTFLUSH);
   if(hr==S_OK)return true;if(FAILED(hr)||std::chrono::steady_clock::now()>deadline)return false;
   std::this_thread::sleep_for(std::chrono::milliseconds(1));
 }};
 for(auto& sample:samples){D3D11_QUERY_DATA_TIMESTAMP_DISJOINT frequency{};UINT64 start=0,end=0;
   if(!wait(sample.disjoint.Get(),&frequency,sizeof(frequency))||!wait(sample.start.Get(),&start,sizeof(start))||!wait(sample.end.Get(),&end,sizeof(end)))return 1;
   if(!frequency.Disjoint&&frequency.Frequency&&end>=start)milliseconds.push_back(1000.0*double(end-start)/double(frequency.Frequency));
 }
 if(milliseconds.size()<54){std::cerr<<"Insufficient valid GPU timing samples\n";return 1;}
 std::sort(milliseconds.begin(),milliseconds.end());
 std::cout<<"Isolated AFW GPU milliseconds: median "<<milliseconds[milliseconds.size()/2]<<", p95 "<<milliseconds[(milliseconds.size()-1)*95/100]<<", samples "<<milliseconds.size()<<" (not game FPS)\n";
 ComPtr<ID3D11Resource> r;b.Output()->GetResource(&r);ComPtr<ID3D11Texture2D> t;r.As(&t);D3D11_TEXTURE2D_DESC desc{};t->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
 ComPtr<ID3D11Texture2D> staging;if(FAILED(d->CreateTexture2D(&desc,nullptr,&staging)))return 1;c->CopyResource(staging.Get(),t.Get());D3D11_MAPPED_SUBRESOURCE mapped{};if(FAILED(c->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)))return 1;
 std::ofstream out(std::filesystem::path(argv[3]),std::ios::binary);size_t holes=0,invalid=0;
 for(UINT y=0;y<height;++y){auto* row=static_cast<const char*>(mapped.pData)+size_t(y)*mapped.RowPitch;out.write(row,width*8);auto* half=reinterpret_cast<const uint16_t*>(row);
 for(UINT x=0;x<width;++x){holes+=half[x*4+3]==0;for(int k=0;k<3;++k)invalid+=(half[x*4+k]&0x7c00)==0x7c00;}}
 c->Unmap(staging.Get(),0);if(!out||invalid)return 1;
 std::cout<<"GPU reconstruction replay complete: "<<width<<'x'<<height<<", holes "<<100.0*holes/count<<"%. Capture replay only; no in-game submission or performance claim.\n";
}
