#pragma once
#include <d3d11.h>
#include <memory>
namespace AfwInputs {
void SetIdentityFastPath(bool);
unsigned long long IdentityFrames();
class Adapter {
public:
 Adapter();~Adapter();
 bool Prepare(ID3D11DeviceContext*,ID3D11Texture2D* depth,ID3D11Texture2D* motion,UINT activeWidth,UINT activeHeight,UINT outputWidth,UINT outputHeight,float jitterX,float jitterY,int eye=-1,unsigned long long serial=0);
 ID3D11Texture2D* Depth()const;ID3D11Texture2D* Motion()const;
private:struct Impl;std::unique_ptr<Impl> p;
};
}
