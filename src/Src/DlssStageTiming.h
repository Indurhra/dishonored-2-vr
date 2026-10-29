#pragma once
#include <d3d11.h>
#include <string>
namespace DlssStageTiming {
void Request(const std::wstring&,int seconds);
void Poll(ID3D11Device*);
bool Active();
void External(int stage,int eye,unsigned long long serial,double gpuMs);
// Immediate execution only; excludes copies recorded before the split boundary.
class Span {
 ID3D11DeviceContext* context;int token;
public:
 Span(ID3D11DeviceContext*,int stage,int eye,unsigned long long serial);
 ~Span();
 void NgxBegin();void NgxEnd();
};
}
