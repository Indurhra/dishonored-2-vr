#include "DlssStageTiming.h"
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>
#include <vector>
using Microsoft::WRL::ComPtr;
namespace DlssStageTiming {namespace {
using Clock=std::chrono::steady_clock;
struct Slot {ComPtr<ID3D11Query> frequency,t[4];bool busy=false,ended=false,ngxStarted=false,ngxEnded=false;int stage=0,eye=-1;unsigned long long serial=0;Clock::time_point cpuStart;double wall=0,cpu=0;};
struct Row {int stage,eye;unsigned long long serial;double wall,total,ngx,cpu;};
std::array<Slot,32> slots;std::vector<Row> rows;std::mutex mutex;std::atomic<bool> active{false};
ComPtr<ID3D11Device> owner;Clock::time_point start,stop;std::wstring path;unsigned sample=0,dropped=0,invalid=0;
void Finish(){
 std::ofstream file(path);file<<"stage,eye,serial,elapsed_seconds,total_gpu_ms,ngx_gpu_ms,record_cpu_ms\n";file.precision(9);
 for(const auto& r:rows)file<<r.stage<<','<<r.eye<<','<<r.serial<<','<<r.wall<<','<<r.total<<','<<r.ngx<<','<<r.cpu<<'\n';
 std::ofstream status(path+L".status.txt");status<<"samples "<<rows.size()<<" dropped "<<dropped<<" invalid "<<invalid<<"\nStage 0: depth preparation/DLAA. Stage 1: SR conversion, NGX and output. Stage 2: AFW input conversion. Stage 3: D3D11 bridge elapsed span including queue waits, overlaps stages 4/5; do not add. Stage 4: D3D12 warp. Stage 5: D3D12 output copy. D3D12 timings use their own queue frequency. Excludes earlier deferred copies and original engine passes. Samples every seventeenth callback.\n";
 active=false;rows.clear();for(auto& s:slots)s=Slot{};owner.Reset();
}
int Begin(ID3D11DeviceContext* ctx,int stage,int eye,unsigned long long serial){
 if(!active||Clock::now()>=stop)return -1;std::lock_guard lock(mutex);
 if(!active||ctx->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE||++sample%17)return -1;
 ComPtr<ID3D11Device> device;ctx->GetDevice(&device);if(owner&&owner.Get()!=device.Get()){++invalid;return -1;}owner=device;
 for(int i=0;i<int(slots.size());++i){auto& s=slots[i];if(s.busy)continue;
  if(!s.frequency){D3D11_QUERY_DESC q{D3D11_QUERY_TIMESTAMP_DISJOINT,0};if(FAILED(device->CreateQuery(&q,&s.frequency))){++invalid;return -1;}q.Query=D3D11_QUERY_TIMESTAMP;
   for(auto& t:s.t)if(FAILED(device->CreateQuery(&q,&t))){s=Slot{};++invalid;return -1;}}
  s.busy=true;s.ended=s.ngxStarted=s.ngxEnded=false;s.stage=stage;s.eye=eye;s.serial=serial;s.wall=std::chrono::duration<double>(Clock::now()-start).count();
  ctx->Begin(s.frequency.Get());ctx->End(s.t[0].Get());s.cpuStart=Clock::now();return i;
 }++dropped;return -1;
}
}
bool Active(){return active;}
void External(int stage,int eye,unsigned long long serial,double gpuMs){if(!active)return;std::lock_guard lock(mutex);if(active)rows.push_back({stage,eye,serial,std::chrono::duration<double>(Clock::now()-start).count(),gpuMs,0,0});}
void Request(const std::wstring& destination,int seconds){std::lock_guard lock(mutex);if(active)return;path=destination;start=Clock::now();stop=start+std::chrono::seconds(seconds<1?1:seconds>60?60:seconds);sample=dropped=invalid=0;rows.clear();active=true;}
Span::Span(ID3D11DeviceContext* ctx,int stage,int eye,unsigned long long serial):context(ctx),token(Begin(ctx,stage,eye,serial)){}
Span::~Span(){if(token<0)return;std::lock_guard lock(mutex);auto& s=slots[token];s.cpu=std::chrono::duration<double,std::milli>(Clock::now()-s.cpuStart).count();context->End(s.t[3].Get());context->End(s.frequency.Get());s.ended=true;}
void Span::NgxBegin(){if(token<0)return;auto& s=slots[token];context->End(s.t[1].Get());s.ngxStarted=true;}
void Span::NgxEnd(){if(token<0)return;auto& s=slots[token];context->End(s.t[2].Get());s.ngxEnded=true;}
void Poll(ID3D11Device* device){if(!active)return;std::lock_guard lock(mutex);if(owner&&owner.Get()!=device){++invalid;Finish();return;}
 ComPtr<ID3D11DeviceContext> ctx;device->GetImmediateContext(&ctx);bool pending=false;
 for(auto& s:slots)if(s.busy){if(!s.ended){pending=true;continue;}
  D3D11_QUERY_DATA_TIMESTAMP_DISJOINT f{};UINT64 t[4]{};
  auto hr=ctx->GetData(s.frequency.Get(),&f,sizeof(f),D3D11_ASYNC_GETDATA_DONOTFLUSH);if(hr==S_FALSE){pending=true;continue;}if(FAILED(hr)){++invalid;s.busy=false;continue;}
  bool ready=true,bad=f.Disjoint||!f.Frequency;
  for(int i=0;i<4;++i){if((i==1||i==2)&&!(s.ngxStarted&&s.ngxEnded))continue;hr=ctx->GetData(s.t[i].Get(),&t[i],sizeof(t[i]),D3D11_ASYNC_GETDATA_DONOTFLUSH);if(hr==S_FALSE)ready=false;else if(FAILED(hr))bad=true;}
  if(!ready){pending=true;continue;}
  if(bad||t[3]<t[0]||(s.ngxEnded&&t[2]<t[1]))++invalid;
  else rows.push_back({s.stage,s.eye,s.serial,s.wall,1000.0*double(t[3]-t[0])/f.Frequency,s.ngxStarted&&s.ngxEnded?1000.0*double(t[2]-t[1])/f.Frequency:0,s.cpu});
  s.busy=false;
 }
 if(Clock::now()>=stop&&(!pending||Clock::now()>stop+std::chrono::seconds(5))){if(pending)++invalid;Finish();}
}
}
