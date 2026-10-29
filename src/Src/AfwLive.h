#pragma once
#include "ViewMatrixHook.h"
#include <d3d11.h>
#include <string>
namespace AfwLive {
void Enable(bool);
bool Enabled();
void Fallback(); // retain the failure reason for the VR menu
std::string Message();
void Target(unsigned long long,const float*,const ViewMatrixHook::XrPose&);
void Matrices(unsigned long long,int,const float*,const float*,const float*);
void Prepared(unsigned long long,int,unsigned long long,ID3D11Texture2D*,ID3D11Texture2D*,unsigned,unsigned);
ID3D11Texture2D* Rebuild(ID3D11DeviceContext*,ID3D11Texture2D*,const ViewMatrixHook::XrPose&,ViewMatrixHook::XrPose&);
void Submitted(bool accepted);
std::string Status();
}
