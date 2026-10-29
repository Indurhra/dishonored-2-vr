#pragma once
#include <d3d11.h>
#include <memory>
#include <string>
namespace AfwPlugin {
// Matrices are column-major, column-vector, matching the public PureDark ABI.
struct Camera {float matrices[12][16]{};};
struct Frame {ID3D11Texture2D *color=nullptr,*depth=nullptr,*motion=nullptr;Camera camera;int eye=-1;float motionScale[2]{};bool reset=false;bool profile=false;unsigned long long serial=0;};
struct Timing {unsigned long long serial=0;int eye=-1;double warpMs=0,copyMs=0;};
class Backend {
public:
 Backend();~Backend();
 bool Initialize(ID3D11Device*,const std::wstring& dllPath);
 // Render-thread only. No CPU waits. Caller consumes Output on this immediate context
 // before the next Evaluate; GPU fence ordering protects shared storage.
 bool Evaluate(ID3D11DeviceContext*,const Frame&);
 ID3D11Texture2D* Output()const;
 const std::string& Problem()const;
 bool TakeTiming(Timing&);
private:struct Impl;std::unique_ptr<Impl> p;
};
}
