#pragma once
#include <d3d11.h>
#include <string>
namespace TemporalBoundary {
using Execute = void(*)(ID3D11DeviceContext*,ID3D11CommandList*);
void SetEnabled(bool);
bool SplitBefore(ID3D11DeviceContext*);
// Called after the real FinishCommandList, including internally requested splits.
HRESULT OnFinish(ID3D11DeviceContext*,ID3D11CommandList*);
// Execute preserved prefixes in order, before executing the engine's returned list.
void ExecutePrefixes(ID3D11DeviceContext*,ID3D11CommandList*,Execute);
HRESULT AttachToPending(ID3D11DeviceContext*,REFGUID,IUnknown*);
std::string Status();
}
