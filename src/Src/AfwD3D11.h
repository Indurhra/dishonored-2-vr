#pragma once
#include "TemporalFrame.h"
#include <d3d11.h>
#include <memory>
#include <string>
namespace AfwD3D11 {
struct Source {
    ID3D11ShaderResourceView *color=nullptr,*depth=nullptr,*mask=nullptr;
    TemporalFrame::Stamp stamp{};
    // Row-major column-vector transform from this image's clip space to target clip space.
    float toTarget[16]{};
};
struct Settings {float pixelTolerance=0.75f,relativeDepthTolerance=0.01f,depthQuantization=2.f/16777215.f; bool heldForegroundAligned=false;};
// Reconstruction kernel for capture/replay first. Caller preserves context state.
// Output alpha zero identifies a hole; this prototype is not a shipping renderer.
class Backend {
public:
    Backend();~Backend();
    bool Initialize(ID3D11Device*);
    bool Rebuild(ID3D11DeviceContext*,const Source& fresh,const Source* held,UINT width,UINT height,const Settings&);
    ID3D11ShaderResourceView* Output() const;
    const std::string& Problem() const;
private:struct Impl;std::unique_ptr<Impl> p;
};
}
