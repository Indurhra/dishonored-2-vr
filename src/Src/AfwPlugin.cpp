#include "AfwPlugin.h"
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <cstdio>
#include "PDAFWPlugin.h"
using Microsoft::WRL::ComPtr;
namespace AfwPlugin {
static_assert(sizeof(Camera)==sizeof(pd::CameraData));
namespace {void Transition(ID3D12GraphicsCommandList* c,ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){if(a==b)return;D3D12_RESOURCE_BARRIER t{};t.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;t.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b};c->ResourceBarrier(1,&t);}}
struct Backend::Impl {
 ComPtr<ID3D11Device5> d11;ComPtr<ID3D11DeviceContext4> c11;ComPtr<ID3D12Device> d12;ComPtr<ID3D12CommandQueue> queue;
 ComPtr<ID3D12Fence> fence;ComPtr<ID3D11Fence> fence11;UINT64 value=0;
 HMODULE module=nullptr;pd::D3D12RendererAPI* renderer=nullptr;decltype(&pd::InitFrameWarp) initFrames=nullptr;decltype(&pd::EvaluateFrameWarp) warp=nullptr;
 struct Shared {ComPtr<ID3D12Resource> r12;ComPtr<ID3D11Texture2D> r11;D3D11_TEXTURE2D_DESC desc{};};
 Shared input[3],output;pd::FrameBufferDesc framebuffer;
 struct Slot {ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;UINT64 done=0;bool timed=false;unsigned long long serial=0;int eye=-1;};std::array<Slot,3> slots;unsigned next=0;
 ComPtr<ID3D12QueryHeap> timestamps;ComPtr<ID3D12Resource> readback;UINT64 frequency=0;Timing timing;bool haveTiming=false;
 bool configured=false,valid=false;std::string problem="uninitialized";
 bool Fail(const char* text,HRESULT hr=E_FAIL){char b[32];sprintf_s(b," (0x%08lx)",hr);problem=std::string(text)+b;valid=false;return false;}
 bool Share(const D3D11_TEXTURE2D_DESC& src,Shared& out){
  D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=src.Width;d.Height=src.Height;d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;d.Format=src.Format;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
  D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
  auto hr=d12->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_SHARED,&d,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&out.r12));if(FAILED(hr))return Fail("shared texture allocation",hr);
  HANDLE h=nullptr;hr=d12->CreateSharedHandle(out.r12.Get(),nullptr,GENERIC_ALL,nullptr,&h);if(FAILED(hr))return Fail("shared texture handle",hr);
  hr=d11->OpenSharedResource1(h,IID_PPV_ARGS(&out.r11));CloseHandle(h);if(FAILED(hr)){char message[96];sprintf_s(message,"open D3D11 texture format %u",unsigned(src.Format));return Fail(message,hr);}out.desc=src;return true;
 }
};
Backend::Backend():p(std::make_unique<Impl>()){}
// Plugin has process-global state and no shutdown export. Retain its device/resources
// until process exit instead of releasing objects still referenced by the DLL.
Backend::~Backend(){if(p->renderer)p.release();}
bool Backend::TakeTiming(Timing& t){if(!p->haveTiming)return false;t=p->timing;p->haveTiming=false;return true;}
const std::string& Backend::Problem()const{return p->problem;}
ID3D11Texture2D* Backend::Output()const{return p->valid?p->output.r11.Get():nullptr;}
bool Backend::Initialize(ID3D11Device* device,const std::wstring& path){
 if(p->renderer)return p->Fail("already initialized; process-global plugin");if(!device)return p->Fail("no device");
 HRESULT hr=device->QueryInterface(IID_PPV_ARGS(&p->d11));if(FAILED(hr))return p->Fail("D3D11.4 device required",hr);
 ComPtr<ID3D11DeviceContext> ctx;device->GetImmediateContext(&ctx);if(FAILED(hr=ctx.As(&p->c11)))return p->Fail("D3D11.4 context required",hr);
 ComPtr<IDXGIDevice> dx;ComPtr<IDXGIAdapter> adapter;if(FAILED(hr=device->QueryInterface(IID_PPV_ARGS(&dx)))||FAILED(hr=dx->GetAdapter(&adapter)))return p->Fail("adapter",hr);
 if(FAILED(hr=D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&p->d12))))return p->Fail("same-adapter D3D12 device",hr);
 D3D12_COMMAND_QUEUE_DESC q{};if(FAILED(hr=p->d12->CreateCommandQueue(&q,IID_PPV_ARGS(&p->queue))))return p->Fail("D3D12 queue",hr);
 if(FAILED(hr=p->d12->CreateFence(0,D3D12_FENCE_FLAG_SHARED,IID_PPV_ARGS(&p->fence))))return p->Fail("shared fence",hr);
 HANDLE h=nullptr;if(FAILED(hr=p->d12->CreateSharedHandle(p->fence.Get(),nullptr,GENERIC_ALL,nullptr,&h)))return p->Fail("fence handle",hr);
 hr=p->d11->OpenSharedFence(h,IID_PPV_ARGS(&p->fence11));CloseHandle(h);if(FAILED(hr))return p->Fail("D3D11 shared fence",hr);
 p->module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);if(!p->module)return p->Fail("plugin load",HRESULT_FROM_WIN32(GetLastError()));
 auto init=reinterpret_cast<decltype(&pd::InitDevice)>(GetProcAddress(p->module,"InitDevice"));p->initFrames=reinterpret_cast<decltype(p->initFrames)>(GetProcAddress(p->module,"InitFrameWarp"));p->warp=reinterpret_cast<decltype(p->warp)>(GetProcAddress(p->module,"EvaluateFrameWarp"));
 if(!init||!p->initFrames||!p->warp)return p->Fail("plugin exports");
 pd::DeviceParams dp{};dp.d3d12Device=p->d12.Get();dp.d3d12Queue=p->queue.Get();dp.dxgiAdapter=adapter.Get();p->renderer=init(dp);if(!p->renderer)return p->Fail("plugin InitDevice");
 for(auto& s:p->slots){if(FAILED(hr=p->d12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&s.allocator)))||FAILED(hr=p->d12->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,s.allocator.Get(),nullptr,IID_PPV_ARGS(&s.list)))||FAILED(hr=s.list->Close()))return p->Fail("command resources",hr);}
 p->problem.clear();return true;
}
bool Backend::Evaluate(ID3D11DeviceContext* context,const Frame& frame){
 p->valid=false;if(!p->renderer||!context||context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE||frame.eye<0||frame.eye>1)return p->Fail("invalid frame/context");
 ComPtr<ID3D11Device> owner;context->GetDevice(&owner);ComPtr<ID3D11Device5> owner5;if(FAILED(owner.As(&owner5))||owner5.Get()!=p->d11.Get())return p->Fail("wrong device");
 for(const auto& m:frame.camera.matrices)for(float v:m)if(!std::isfinite(v))return p->Fail("nonfinite camera");
 if(!std::isfinite(frame.motionScale[0])||!std::isfinite(frame.motionScale[1])||!frame.motionScale[0]||!frame.motionScale[1])return p->Fail("invalid motion scale");
 ID3D11Texture2D* textures[]={frame.color,frame.depth,frame.motion};D3D11_TEXTURE2D_DESC desc[3]{};
 for(int i=0;i<3;++i){if(!textures[i])return p->Fail("missing input");textures[i]->GetDesc(&desc[i]);ComPtr<ID3D11Device> o;textures[i]->GetDevice(&o);if(o.Get()!=owner.Get()||desc[i].ArraySize!=1||desc[i].MipLevels!=1||desc[i].SampleDesc.Count!=1)return p->Fail("invalid resource");if(i==0&&(desc[i].Format==DXGI_FORMAT_R8G8B8A8_TYPELESS||desc[i].Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB))desc[i].Format=DXGI_FORMAT_R8G8B8A8_UNORM;
  if(p->configured&&(desc[i].Width!=p->input[i].desc.Width||desc[i].Height!=p->input[i].desc.Height||desc[i].Format!=p->input[i].desc.Format))return p->Fail("resize requires native fallback and backend restart");}
 if(desc[0].Format!=DXGI_FORMAT_R8G8B8A8_UNORM||desc[1].Format!=DXGI_FORMAT_R32_FLOAT||desc[2].Format!=DXGI_FORMAT_R16G16_FLOAT){char message[128];sprintf_s(message,"unsupported verified input formats %u/%u/%u",unsigned(desc[0].Format),unsigned(desc[1].Format),unsigned(desc[2].Format));return p->Fail(message);}
 if(!p->configured){for(int i=0;i<3;++i)if(!p->Share(desc[i],p->input[i]))return false;D3D11_TEXTURE2D_DESC outputDesc{};frame.color->GetDesc(&outputDesc);if(outputDesc.Format==DXGI_FORMAT_R8G8B8A8_TYPELESS)outputDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;if(!p->Share(outputDesc,p->output))return false;
  pd::FrameWarpInitParams init{int(desc[0].Width),int(desc[0].Height),desc[0].Format,desc[0].Format};auto eyes=p->initFrames(init);if(!eyes.eyeFrameBuffers[0].color.pTexture||!eyes.eyeFrameBuffers[1].color.pTexture)return p->Fail("plugin output allocation");
  pd::TextureDesc* t[]={&p->framebuffer.color,&p->framebuffer.depth,&p->framebuffer.motionVectors};for(int i=0;i<3;++i){t[i]->pTexture=p->input[i].r12.Get();p->renderer->SetupTextureDesc(*t[i]);}p->configured=true;}
 auto& slot=p->slots[p->next%p->slots.size()];const auto completed=p->fence->GetCompletedValue();if(completed==UINT64_MAX)return p->Fail("D3D12 device lost");if(completed<slot.done)return p->Fail("GPU command slot busy");
 if(slot.timed){
  const UINT64 offset=(p->next%p->slots.size())*3*sizeof(UINT64);D3D12_RANGE range{SIZE_T(offset),SIZE_T(offset+3*sizeof(UINT64))};void* data=nullptr;
  if(SUCCEEDED(p->readback->Map(0,&range,&data))){auto* t=reinterpret_cast<const UINT64*>(static_cast<const char*>(data)+offset);
   if(p->frequency&&t[2]>=t[1]&&t[1]>=t[0]){p->timing={slot.serial,slot.eye,1000.*double(t[1]-t[0])/p->frequency,1000.*double(t[2]-t[1])/p->frequency};p->haveTiming=true;}
   D3D12_RANGE none{};p->readback->Unmap(0,&none);
  }slot.timed=false;
 }
 if(frame.profile&&!p->timestamps){
  D3D12_QUERY_HEAP_DESC q{};q.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;q.Count=9;
  D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=9*sizeof(UINT64);rd.Height=rd.DepthOrArraySize=rd.MipLevels=rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  if(FAILED(p->queue->GetTimestampFrequency(&p->frequency))||FAILED(p->d12->CreateQueryHeap(&q,IID_PPV_ARGS(&p->timestamps)))||FAILED(p->d12->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&p->readback)))){p->timestamps.Reset();p->readback.Reset();}
 }
 const bool timed=frame.profile&&p->timestamps&&p->readback;const UINT queryIndex=(p->next%p->slots.size())*3;
 HRESULT hr;if(FAILED(hr=slot.allocator->Reset())||FAILED(hr=slot.list->Reset(slot.allocator.Get(),nullptr)))return p->Fail("command reset",hr);
 for(int i=0;i<3;++i)context->CopyResource(p->input[i].r11.Get(),textures[i]);
 if(FAILED(hr=p->c11->Signal(p->fence11.Get(),++p->value)))return p->Fail("D3D11 signal",hr);context->Flush();if(FAILED(hr=p->queue->Wait(p->fence.Get(),p->value)))return p->Fail("D3D12 wait",hr);
 auto* list=slot.list.Get();pd::TextureDesc* t[]={&p->framebuffer.color,&p->framebuffer.depth,&p->framebuffer.motionVectors};
 for(int i=0;i<3;++i){Transition(list,t[i]->pTexture,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);t[i]->initialState=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;}
 pd::CameraData camera;memcpy(&camera,&frame.camera,sizeof(camera));pd::FrameWarpEvaluateParams ep{};ep.InCmdList=list;ep.InEyeFrameBuffer=&p->framebuffer;ep.CameraData=&camera;ep.Mode=pd::CombinedWarping;ep.EyeIndex=pd::EyeIndex(frame.eye);ep.MotionVectorsType=pd::Normal;ep.InMotionScale[0]=frame.motionScale[0];ep.InMotionScale[1]=frame.motionScale[1];ep.ClearBeforeWarping=frame.reset;if(timed)list->EndQuery(p->timestamps.Get(),D3D12_QUERY_TYPE_TIMESTAMP,queryIndex);p->warp(ep);if(timed)list->EndQuery(p->timestamps.Get(),D3D12_QUERY_TYPE_TIMESTAMP,queryIndex+1);
 if(!ep.OutEyeFrameBuffer||!ep.OutEyeFrameBuffer->color.pTexture){list->Close();return p->Fail("plugin returned no image");}
 auto& out=ep.OutEyeFrameBuffer->color;
 const auto actual=out.pTexture->GetDesc();
 if(actual.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||actual.Width!=p->output.desc.Width||actual.Height!=p->output.desc.Height||actual.Format!=DXGI_FORMAT_R8G8B8A8_UNORM||actual.SampleDesc.Count!=1||actual.MipLevels!=1||actual.DepthOrArraySize!=1){list->Close();return p->Fail("plugin returned incompatible output");}
 Transition(list,out.pTexture,out.initialState,D3D12_RESOURCE_STATE_COPY_SOURCE);Transition(list,p->output.r12.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);list->CopyResource(p->output.r12.Get(),out.pTexture);Transition(list,out.pTexture,D3D12_RESOURCE_STATE_COPY_SOURCE,out.initialState);Transition(list,p->output.r12.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);
 for(int i=0;i<3;++i){Transition(list,t[i]->pTexture,t[i]->initialState,D3D12_RESOURCE_STATE_COMMON);t[i]->initialState=D3D12_RESOURCE_STATE_COMMON;}
 if(timed){list->EndQuery(p->timestamps.Get(),D3D12_QUERY_TYPE_TIMESTAMP,queryIndex+2);list->ResolveQueryData(p->timestamps.Get(),D3D12_QUERY_TYPE_TIMESTAMP,queryIndex,3,p->readback.Get(),queryIndex*sizeof(UINT64));slot.timed=true;slot.serial=frame.serial;slot.eye=frame.eye;}
 if(FAILED(hr=list->Close()))return p->Fail("command close",hr);ID3D12CommandList* execute[]={list};p->queue->ExecuteCommandLists(1,execute);slot.done=++p->value;
 if(FAILED(hr=p->queue->Signal(p->fence.Get(),slot.done))||FAILED(hr=p->c11->Wait(p->fence11.Get(),slot.done)))return p->Fail("output fence",hr);
 ++p->next;p->valid=true;p->problem.clear();return true;
}
}
