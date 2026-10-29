#include "GpuListTiming.h"
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>
#include <vector>
using Microsoft::WRL::ComPtr;
namespace GpuListTiming { namespace {
using Clock=std::chrono::steady_clock;
struct Slot {ComPtr<ID3D11Query> frequency,start,end;bool busy=false,ended=false;unsigned long long frame=0;Clock::time_point cpuStart;double cpuMs=0,wall=0;};
struct Row {unsigned long long frame;double wall,gpu,cpu;};
std::array<Slot,256> slots;std::vector<Row> rows;std::mutex mutex;std::atomic<bool> active{false};
ComPtr<ID3D11Device> owner;Clock::time_point start,stop;std::wstring path;unsigned long long frame=0;unsigned dropped=0,invalid=0;
void Finish(){
 std::ofstream file(path);file<<"present_index,elapsed_seconds,command_list_gpu_ms,execute_cpu_ms\n";file.precision(9);
 for(const auto& r:rows)file<<r.frame<<','<<r.wall<<','<<r.gpu<<','<<r.cpu<<'\n';
 std::ofstream status(path+L".status.txt");status<<"samples "<<rows.size()<<"\ndropped "<<dropped<<"\ninvalid "<<invalid<<"\n";
 status<<"scope ExecuteCommandList only; excludes immediate work and idle gaps; not whole-frame GPU time\n";
 active=false;rows.clear();for(auto& s:slots)s=Slot{};owner.Reset();
}
}
void Request(const std::wstring& destination,int seconds){std::lock_guard lock(mutex);
 if(active)return;path=destination;start=Clock::now();stop=start+std::chrono::seconds(seconds<1?1:seconds>60?60:seconds);
 frame=0;dropped=invalid=0;rows.clear();active=true;
}
int Begin(ID3D11DeviceContext* ctx){if(!active||Clock::now()>=stop)return -1;std::lock_guard lock(mutex);
 if(!active||ctx->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return -1;
 ComPtr<ID3D11Device> device;ctx->GetDevice(&device);if(owner&&owner.Get()!=device.Get()){++invalid;return -1;}owner=device;
 for(int i=0;i<int(slots.size());++i){auto& s=slots[i];if(s.busy)continue;
  if(!s.frequency){D3D11_QUERY_DESC q{D3D11_QUERY_TIMESTAMP_DISJOINT,0};
   if(FAILED(device->CreateQuery(&q,&s.frequency))){++invalid;return -1;}q.Query=D3D11_QUERY_TIMESTAMP;
   if(FAILED(device->CreateQuery(&q,&s.start))||FAILED(device->CreateQuery(&q,&s.end))){s=Slot{};++invalid;return -1;}}
  s.busy=true;s.ended=false;s.frame=frame;s.wall=std::chrono::duration<double>(Clock::now()-start).count();
  ctx->Begin(s.frequency.Get());ctx->End(s.start.Get());s.cpuStart=Clock::now();return i;
 }++dropped;return -1;
}
void End(ID3D11DeviceContext* ctx,int token){if(token<0)return;std::lock_guard lock(mutex);auto& s=slots[token];
 s.cpuMs=std::chrono::duration<double,std::milli>(Clock::now()-s.cpuStart).count();ctx->End(s.end.Get());ctx->End(s.frequency.Get());s.ended=true;
}
void Poll(ID3D11Device* device){if(!active)return;std::lock_guard lock(mutex);++frame;
 if(owner&&owner.Get()!=device){++invalid;Finish();return;}
 ComPtr<ID3D11DeviceContext> ctx;device->GetImmediateContext(&ctx);bool pending=false;
 for(auto& s:slots)if(s.busy){if(!s.ended){pending=true;continue;}
  D3D11_QUERY_DATA_TIMESTAMP_DISJOINT f{};UINT64 a=0,b=0;
  auto hr=ctx->GetData(s.frequency.Get(),&f,sizeof(f),D3D11_ASYNC_GETDATA_DONOTFLUSH);
  if(hr==S_FALSE){pending=true;continue;}if(FAILED(hr)){++invalid;s.busy=false;continue;}
  auto ha=ctx->GetData(s.start.Get(),&a,sizeof(a),D3D11_ASYNC_GETDATA_DONOTFLUSH),hb=ctx->GetData(s.end.Get(),&b,sizeof(b),D3D11_ASYNC_GETDATA_DONOTFLUSH);
  if(ha==S_FALSE||hb==S_FALSE){pending=true;continue;}
  if(FAILED(ha)||FAILED(hb)||f.Disjoint||!f.Frequency||b<a)++invalid;
  else rows.push_back({s.frame,s.wall,1000.0*double(b-a)/double(f.Frequency),s.cpuMs});s.busy=false;
 }
 if(Clock::now()>=stop&&(!pending||Clock::now()>stop+std::chrono::seconds(5))){if(pending)++invalid;Finish();}
}
}
