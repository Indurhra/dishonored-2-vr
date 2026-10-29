#pragma once
#include "TemporalFrame.h"
#include <d3d11.h>
#include <memory>
#include <string>

// Render-thread-only backend. The adapter must supply verified, same-image inputs
// and preserve engine D3D state around calls. No engine convention is guessed here.
namespace DlssD3D11 {
enum class Quality { Dlaa, Quality, Balanced, Performance, UltraPerformance };
struct Config {
    UINT renderWidth=0,renderHeight=0,outputWidth=0,outputHeight=0;
    Quality quality=Quality::Dlaa;
    bool hdr=true,depthInverted=false,autoExposure=true;
    unsigned preset=0; // SDK default unless explicitly selected.
};
struct Input {
    ID3D11Texture2D *color=nullptr,*depth=nullptr,*motion=nullptr;
    TemporalFrame::Stamp colorStamp{},depthStamp{},motionStamp{};
    float jitterX=0,jitterY=0,mvScaleX=0,mvScaleY=0,preExposure=1,frameTimeMs=0;
    bool reset=false;
};
class Backend {
public:
    Backend(); ~Backend();
    Backend(const Backend&)=delete; Backend& operator=(const Backend&)=delete;
    bool Initialize(ID3D11Device*,const std::wstring& runtimeDirectory,const std::wstring& cacheDirectory);
    bool Configure(ID3D11DeviceContext*,const Config&);
    bool Evaluate(ID3D11DeviceContext*,const Input&);
    ID3D11ShaderResourceView* Output(int eye) const;
    const std::string& Problem() const;
    void Shutdown();
private:
    struct Impl; std::unique_ptr<Impl> p;
};
}
