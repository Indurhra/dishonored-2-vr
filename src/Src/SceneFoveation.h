#pragma once
#include <d3d11.h>
#include <string>
namespace SceneFoveation {
bool Enabled();
void SetMode(int); // 0 off, 1 classifier-only control, 2 fixed foveation experiment
bool Begin(ID3D11DeviceContext*, unsigned long long serial);
void End(ID3D11DeviceContext*);
void OnPresent(ID3D11Device*);
std::string Status();
}
