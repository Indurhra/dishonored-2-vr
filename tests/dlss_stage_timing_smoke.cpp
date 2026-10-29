#include "DlssStageTiming.h"
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cmath>
using Microsoft::WRL::ComPtr;
int main(){
 ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> ctx;
 if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&ctx)))return 1;
 D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=512;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.BindFlags=D3D11_BIND_RENDER_TARGET;
 ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11RenderTargetView> target;
 if(FAILED(device->CreateTexture2D(&d,nullptr,&texture))||FAILED(device->CreateRenderTargetView(texture.Get(),nullptr,&target)))return 2;
 auto path=std::filesystem::temp_directory_path()/"dvr-stage-timing-smoke.csv";DlssStageTiming::Request(path.wstring(),1);
 for(unsigned i=0;i<400;++i){DlssStageTiming::Span span(ctx.Get(),i%2,(i/2)%2,i+1);float color[4]={float(i%17)/17.f,0,0,1};ctx->ClearRenderTargetView(target.Get(),color);span.NgxBegin();ctx->ClearRenderTargetView(target.Get(),color);span.NgxEnd();}
 ctx->Flush(); // test only: production polling never flushes/waits for the GPU
 for(unsigned i=0;i<75;++i){Sleep(20);DlssStageTiming::Poll(device.Get());}
 std::ifstream file(path);std::string line;std::getline(file,line);unsigned samples=0;bool eyes[2]{},stages[2]{};
 while(std::getline(file,line)){std::replace(line.begin(),line.end(),',',' ');std::istringstream in(line);int stage,eye;unsigned long long serial;double wall,total,ngx,cpu;
  if(!(in>>stage>>eye>>serial>>wall>>total>>ngx>>cpu)||!std::isfinite(total)||!std::isfinite(ngx)||ngx<0||total<ngx||eye<0||eye>1||stage<0||stage>1)return 3;
  ++samples;eyes[eye]=stages[stage]=true;
 }
 if(samples<4||!eyes[0]||!eyes[1]||!stages[0]||!stages[1])return 4;
 std::cout<<"PASS asynchronous GPU stage timings, nested interval bounds, both eyes/stages; samples "<<samples<<'\n';return 0;
}
