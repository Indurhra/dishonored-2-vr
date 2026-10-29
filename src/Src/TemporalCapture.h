#pragma once
#include <d3d11.h>
#include <cstdint>
#include <string>
namespace TemporalCapture {
bool Active();
void Request(const std::wstring& base,int count=1);
// Diagnostic-only snapshots; copies are recorded on the source context and
// polled on the immediate context. Never interpreted as validated renderer inputs.
void Observe(ID3D11DeviceContext*,int component,ID3D11Resource*,uint64_t serial,int eye,
             const float* view=nullptr,const float* projection=nullptr,const float* jitter=nullptr);
void Poll(ID3D11Device*);
}
