#include "DlssColorPool.h"
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstdlib>
using Microsoft::WRL::ComPtr;
void Check(bool b){if(!b)std::abort();}
int main(){
 ComPtr<ID3D11Device> d;ComPtr<ID3D11DeviceContext> c;
 Check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,nullptr,&c)));
 DlssColorPool pool;D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=64;td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R11G11B10_FLOAT;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
 auto first=pool.Acquire(d.Get(),td,DXGI_FORMAT_UNKNOWN);Check(bool(first));auto* original=first->texture.Get();
 DlssColorPool::Submitted(c.Get(),first);c->Flush();
 // Even completed GPU work cannot release a live command-list lease.
 pool.Poll(c.Get(),d.Get(),true);auto second=pool.Acquire(d.Get(),td,DXGI_FORMAT_UNKNOWN);Check(second&&second->texture.Get()!=original);
 second.reset();first.reset();
 for(int n=0;n<100&&pool.hits==0;++n){
  pool.Poll(c.Get(),d.Get(),true);auto next=pool.Acquire(d.Get(),td,DXGI_FORMAT_UNKNOWN);
  if(pool.hits){Check(next->texture.Get()==original);break;}
  next.reset();std::this_thread::sleep_for(std::chrono::milliseconds(2));
 }
 Check(pool.hits==1);
 // A discarded/unsubmitted recording never becomes reusable.
 auto pending=pool.Acquire(d.Get(),td,DXGI_FORMAT_UNKNOWN);pending.reset();const auto hits=pool.hits.load();
 pool.Poll(c.Get(),d.Get(),true);auto other=pool.Acquire(d.Get(),td,DXGI_FORMAT_UNKNOWN);Check(pool.hits==hits);
 td.Width=32;auto resized=pool.Acquire(d.Get(),td,DXGI_FORMAT_UNKNOWN);Check(resized&&resized->width==32);
 td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;Check(!pool.Acquire(d.Get(),td,DXGI_FORMAT_UNKNOWN));
 other.reset();resized.reset();pool.Poll(c.Get(),d.Get(),false);
 std::puts("PASS GPU completion, retained leases, unsubmitted recordings, resize and unsupported formats");
}
