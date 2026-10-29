#include <d3d11.h>
#include <nvapi.h>
#include <wrl/client.h>
#include <cstdio>
using Microsoft::WRL::ComPtr;
int main(int argc,char**){ComPtr<ID3D11Device>d;ComPtr<ID3D11DeviceContext>c;
if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,nullptr,&c)))return 1;
printf("init %d\n",NvAPI_Initialize());fflush(stdout);
ComPtr<ID3D11DeviceContext> def;d->CreateDeferredContext(0,&def);
unsigned char pixels[16]={};D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=4;td.ArraySize=td.MipLevels=1;td.Format=DXGI_FORMAT_R8_UINT;td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_IMMUTABLE;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;D3D11_SUBRESOURCE_DATA data{pixels,4,0};ComPtr<ID3D11Texture2D> tex;d->CreateTexture2D(&td,&data,&tex);
NV_D3D11_SHADING_RATE_RESOURCE_VIEW_DESC vd{};vd.version=NV_D3D11_SHADING_RATE_RESOURCE_VIEW_DESC_VER;vd.Format=DXGI_FORMAT_R8_UINT;vd.ViewDimension=NV_SRRV_DIMENSION_TEXTURE2D;ComPtr<ID3D11NvShadingRateResourceView> view;
printf("view %d\n",NvAPI_D3D11_CreateShadingRateResourceView(d.Get(),tex.Get(),&vd,&view));fflush(stdout);
for(int i=0;i<100;++i){NV_D3D11_VIEWPORT_SHADING_RATE_DESC r{};r.enableVariablePixelShadingRate=true;for(auto& s:r.shadingRateTable)s=NV_PIXEL_X1_PER_RASTER_PIXEL;
NV_D3D11_VIEWPORTS_SHADING_RATE_DESC desc{};desc.version=NV_D3D11_VIEWPORTS_SHADING_RATE_DESC_VER;desc.numViewports=1;desc.pViewports=&r;
NvAPI_D3D11_RSSetShadingRateResourceView(def.Get(),view.Get());
printf("enable %d\n",NvAPI_D3D11_RSSetViewportsPixelShadingRates(def.Get(),&desc));fflush(stdout);
if(argc>1){r.enableVariablePixelShadingRate=false;}else{desc.numViewports=0;desc.pViewports=nullptr;}
printf("disable %d\n",NvAPI_D3D11_RSSetViewportsPixelShadingRates(def.Get(),&desc));fflush(stdout);
NvAPI_D3D11_RSSetShadingRateResourceView(def.Get(),nullptr);
ComPtr<ID3D11CommandList>list;def->FinishCommandList(FALSE,&list);c->ExecuteCommandList(list.Get(),FALSE);}
printf("done\n");}
