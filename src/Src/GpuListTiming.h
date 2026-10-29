#pragma once
#include <d3d11.h>
#include <string>
namespace GpuListTiming {
void Request(const std::wstring& path,int seconds);
int Begin(ID3D11DeviceContext*);
void End(ID3D11DeviceContext*,int token);
void Poll(ID3D11Device*);
}
