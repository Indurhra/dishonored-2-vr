#include "FrameProfile.h"
#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <psapi.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>
namespace FrameProfile {
std::atomic<bool> active=false,detail=false;
namespace {
using Clock=std::chrono::steady_clock;
double Now(){return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();}
struct Row {const char* name;std::uint64_t frame;double start,inclusive,exclusive;};
struct Buffer {DWORD thread=GetCurrentThreadId();std::vector<Row> rows;unsigned dropped=0;Buffer(){rows.reserve(200000);}};
struct MemoryRow {double seconds;SIZE_T privateBytes,workingBytes;UINT64 localBytes,localBudget;bool processValid,videoValid;};
std::vector<MemoryRow> memory;double lastMemory=0;
std::mutex mutex;std::vector<Buffer*> buffers;thread_local Buffer* buffer=nullptr;thread_local Scope* top=nullptr;
std::atomic<unsigned> inflight=0;std::atomic<std::uint64_t> epoch=0;
std::atomic<bool> busy=false;std::atomic<unsigned> stride{16};double started=0,stop=0;std::wstring destination;
Buffer& GetBuffer(){if(!buffer){buffer=new Buffer;std::lock_guard lock(mutex);buffers.push_back(buffer);}return *buffer;}
}
bool Request(const std::wstring& path,int seconds,unsigned step,bool detailed){
 bool expected=false;if(!busy.compare_exchange_strong(expected,true))return false;
 {std::lock_guard lock(mutex);for(auto* b:buffers){b->rows.clear();b->dropped=0;}}
 memory.clear();memory.reserve(64);lastMemory=0;destination=path;stride=std::clamp(step,1u,128u);detail=detailed;started=Now();stop=started+std::clamp(seconds,1,60);active=true;return true;
}
void Frame(){
 static double previous=0;const double now=Now();const auto frame=++epoch;
 if(active){++inflight;if(active&&previous){auto& b=GetBuffer();if(b.rows.size()<b.rows.capacity())b.rows.push_back({"interval.gamePresent",frame,previous,now-previous,0});else ++b.dropped;}--inflight;}previous=now;
}
void CompleteStereo(bool afw){
 if(!active)return;++inflight;
 if(active){static double previous[2]{};const double now=Now();auto& last=previous[afw?1:0];
  if(last>=started){auto& b=GetBuffer();if(b.rows.size()<b.rows.capacity())b.rows.push_back({afw?"interval.afwStereoAccepted":"interval.nativeStereoAccepted",epoch.load(),last,now-last,0});else ++b.dropped;}last=now;
 }--inflight;
}
void Memory(ID3D11Device* device){
 if(!active||Now()-lastMemory<1)return;++inflight;
 if(active){lastMemory=Now();Microsoft::WRL::ComPtr<IDXGIDevice> dx;Microsoft::WRL::ComPtr<IDXGIAdapter> a;Microsoft::WRL::ComPtr<IDXGIAdapter3> a3;DXGI_QUERY_VIDEO_MEMORY_INFO v{};
  bool videoValid=false;
  if(device&&SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dx)))&&SUCCEEDED(dx->GetAdapter(&a))&&SUCCEEDED(a.As(&a3)))videoValid=SUCCEEDED(a3->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&v));
  PROCESS_MEMORY_COUNTERS_EX m{};m.cb=sizeof(m);const bool processValid=GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m),sizeof(m))!=0;
  if(memory.size()<64)memory.push_back({lastMemory-started,m.PrivateUsage,m.WorkingSetSize,v.CurrentUsage,v.Budget,processValid,videoValid});
 }--inflight;
}
void Scope::Begin(const char* label){
 if(!active.load(std::memory_order_relaxed))return;
 frame=epoch.load(std::memory_order_relaxed);if(!top&&frame%stride)return;
 ++inflight;if(!active.load()){--inflight;return;}
 GetBuffer();name=label;parent=top;top=this;start=Now();
}
void Scope::End(){
 if(!name)return;const double end=Now(),elapsed=end-start;
 top=parent;if(parent)parent->children+=elapsed;
 auto& b=*buffer;if(b.rows.size()<b.rows.capacity())b.rows.push_back({name,frame,start,elapsed,std::max(0.,elapsed-children)});else ++b.dropped;
 --inflight;
}
void Poll(){
 if(!active||Now()<stop)return;active=false;
 // File writing and waiting occur off the render thread, once per capture.
 std::thread([]{
   while(inflight.load())Sleep(1);
   std::lock_guard lock(mutex);std::ofstream out(destination);out.precision(12);
   out<<"thread,present_epoch,name,start_seconds,inclusive_ms,exclusive_ms\n";
   unsigned dropped=0;for(auto* b:buffers){dropped+=b->dropped;for(const auto& r:b->rows)
    out<<b->thread<<','<<r.frame<<','<<r.name<<','<<r.start-started<<','<<r.inclusive*1000<<','<<r.exclusive*1000<<'\n';}
   out.close();std::ofstream mem(destination+L".memory.csv");mem<<"seconds,private_bytes,working_bytes,local_video_bytes,local_video_budget,process_valid,video_valid\n";for(const auto& m:memory)mem<<m.seconds<<','<<m.privateBytes<<','<<m.workingBytes<<','<<m.localBytes<<','<<m.localBudget<<','<<m.processValid<<','<<m.videoValid<<'\n';mem.close();std::ofstream status(destination+L".status.txt");status<<"stride "<<stride<<" detailed "<<detail<<" dropped "<<dropped<<"\nCPU wall durations include waits. Exclusive excludes instrumented children only. Present epoch is not engine view identity. Counts cover sampled epochs only. Do not sum overlapping threads or inclusive parents. First two seconds include first-use profiler allocation; exclude them.\n";status.close();busy=false;
 }).detach();
}
}
