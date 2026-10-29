#include "TemporalBoundary.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <iostream>
using Microsoft::WRL::ComPtr;
int main(){
 ComPtr<ID3D11Device> d;ComPtr<ID3D11DeviceContext> c,rec;D3D_FEATURE_LEVEL level;
 if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,&level,&c))||FAILED(d->CreateDeferredContext(0,&rec)))return 1;
 const char shader[]="RWTexture2D<uint> image:register(u0);cbuffer P:register(b0){uint amount;uint3 pad;}[numthreads(1,1,1)]void main(){image[uint2(0,0)]+=amount;}";
 ComPtr<ID3DBlob> code,error;if(FAILED(D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"main","cs_5_0",0,0,&code,&error)))return 1;
 ComPtr<ID3D11ComputeShader> cs;if(FAILED(d->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&cs)))return 1;
 D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=td.MipLevels=td.ArraySize=1;td.Format=DXGI_FORMAT_R32_UINT;td.SampleDesc.Count=1;td.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
 ComPtr<ID3D11Texture2D> image,staging;ComPtr<ID3D11UnorderedAccessView> uav;
 if(FAILED(d->CreateTexture2D(&td,nullptr,&image))||FAILED(d->CreateUnorderedAccessView(image.Get(),nullptr,&uav)))return 1;
 td.Usage=D3D11_USAGE_STAGING;td.BindFlags=0;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;if(FAILED(d->CreateTexture2D(&td,nullptr,&staging)))return 1;
 D3D11_BUFFER_DESC bd{};bd.ByteWidth=16;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;bd.Usage=D3D11_USAGE_DEFAULT;ComPtr<ID3D11Buffer> cb;if(FAILED(d->CreateBuffer(&bd,nullptr,&cb)))return 1;
 TemporalBoundary::SetEnabled(true);
 if(TemporalBoundary::SplitBefore(c.Get()))return 2; // refuses immediate contexts
 UINT initial[4]={4,0,0,0};rec->ClearUnorderedAccessViewUint(uav.Get(),initial);rec->CSSetShader(cs.Get(),nullptr,0);
 auto* target=uav.Get();rec->CSSetUnorderedAccessViews(0,1,&target,nullptr);auto* parameters=cb.Get();rec->CSSetConstantBuffers(0,1,&parameters);
 for(UINT amount:{3u,5u,7u}){UINT values[4]={amount,0,0,0};rec->UpdateSubresource(cb.Get(),0,nullptr,values,0,0);rec->Dispatch(1,1,1);
  if(amount!=7&&!TemporalBoundary::SplitBefore(rec.Get()))return 3;
 }
 // Disabling must not discard already-recorded prefixes.
 TemporalBoundary::SetEnabled(false);ComPtr<ID3D11CommandList> list;
 if(FAILED(rec->FinishCommandList(FALSE,&list))||FAILED(TemporalBoundary::OnFinish(rec.Get(),list.Get())))return 4;
 c->CSSetShader(cs.Get(),nullptr,0);
 for(int repeat=0;repeat<2;++repeat){
  TemporalBoundary::ExecutePrefixes(c.Get(),list.Get(),[](ID3D11DeviceContext* context,ID3D11CommandList* prefix){context->ExecuteCommandList(prefix,TRUE);});
  c->ExecuteCommandList(list.Get(),TRUE);ComPtr<ID3D11ComputeShader> saved;c->CSGetShader(&saved,nullptr,nullptr);if(saved.Get()!=cs.Get())return 5;
  c->CopyResource(staging.Get(),image.Get());D3D11_MAPPED_SUBRESOURCE mapped{};if(FAILED(c->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)))return 6;
  const UINT result=*static_cast<UINT*>(mapped.pData);c->Unmap(staging.Get(),0);if(result!=19){std::cerr<<"Wrong ordered result: "<<result<<'\n';return 7;}
 }
 std::cout<<"PASS two deferred boundaries, command order, retained bindings, repeat execution, toggle drain and immediate-state restore\n"<<TemporalBoundary::Status()<<'\n';
}
