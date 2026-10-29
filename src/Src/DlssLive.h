#pragma once
#include <d3d11.h>
#include <string>
namespace DlssLive {
void SetEnabled(bool);
void SetDirectColor(bool);
void SetCompactInputs(bool); // diagnostic A/B; active scene inputs by default
void SetFullscreenScan(bool); // diagnostic A/B; verified fullscreen draws only by default
bool FullscreenScan();
bool Enabled();
unsigned long long Generation();
void SetQuality(int); // 0 DLAA; 1 Quality; 2 Balanced; 3 Performance; 4 Ultra Performance
int Quality();
void SetPreset(unsigned); // 0 runtime default; 11 K, diagnostic comparison
unsigned Preset();
void ObserveTone(ID3D11DeviceContext*,unsigned long long,int,ID3D11RenderTargetView*,const D3D11_VIEWPORT&);
IUnknown* CaptureUpscale(ID3D11DeviceContext*,unsigned long long,int,ID3D11ShaderResourceView*,ID3D11RenderTargetView*,const float* resolution=nullptr);
void OnPresent(ID3D11Device*);
void OnFinish(ID3D11DeviceContext*,ID3D11CommandList*);
void ObserveDepth(ID3D11DeviceContext*,unsigned long long serial,int eye,ID3D11Resource*);
IUnknown* Capture(ID3D11DeviceContext*,unsigned long long serial,int eye,const float jitter[2],ID3D11ShaderResourceView* color,ID3D11ShaderResourceView* motion,ID3D11UnorderedAccessView* result,const float* resolution=nullptr);
void Finish(ID3D11DeviceContext*,IUnknown*);
void AfterExecute(ID3D11DeviceContext*,ID3D11CommandList*);
std::string Status();
}
