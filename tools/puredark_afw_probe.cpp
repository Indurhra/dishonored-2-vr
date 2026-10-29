// Standalone compatibility probe; never installs or loads the plugin into D2.
// Uses the public RE4 ABI header supplied via DVR_PDAFW_INCLUDE (not a DLL redistribution).
// CPU waits, readback and per-iteration output allocations are test-only, not a production scheduler.
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <d3d11_4.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include "PDAFWPlugin.h"
using Microsoft::WRL::ComPtr;
static_assert(sizeof(pd::DeviceParams)==40 && sizeof(pd::TextureDesc)==56);
static_assert(sizeof(pd::EyeFrameBuffers)==336 && sizeof(pd::CameraData)==768);
static void check(HRESULT h,const char* what){if(FAILED(h)){std::printf("FAIL %s: %08lx\n",what,h);std::fflush(stdout);ExitProcess(2);}}
int wmain(int argc,wchar_t** argv){
 if(argc!=2){std::puts("Usage: puredark_afw_probe absolute-plugin-path");return 2;}
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
 ComPtr<ID3D12Debug> debug;if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))){debug->EnableDebugLayer();std::puts("D3D12 debug layer enabled");}else{std::puts("D3D12 debug layer unavailable; validation messages not checked");}
 ComPtr<IDXGIFactory4> factory;check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"factory");
 ComPtr<IDXGIAdapter1> adapter;ComPtr<ID3D12Device> device;
 for(UINT i=0;factory->EnumAdapters1(i,&adapter)!=DXGI_ERROR_NOT_FOUND;++i){DXGI_ADAPTER_DESC1 d{};adapter->GetDesc1(&d);if(!(d.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)&&SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))))break;adapter.Reset();}
 if(!device){std::puts("No hardware D3D12 device");return 2;}
 ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};check(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)),"queue");
 auto dll=LoadLibraryExW(argv[1],nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
 if(!dll){std::printf("Load failed %lu\n",GetLastError());return 2;}
 auto init=reinterpret_cast<decltype(&pd::InitDevice)>(GetProcAddress(dll,"InitDevice"));
 auto frames=reinterpret_cast<decltype(&pd::InitFrameWarp)>(GetProcAddress(dll,"InitFrameWarp"));
 auto evaluate=reinterpret_cast<decltype(&pd::EvaluateFrameWarp)>(GetProcAddress(dll,"EvaluateFrameWarp"));
 if(!init||!frames||!evaluate)return 3;
 pd::DeviceParams dp{};dp.d3d12Device=device.Get();dp.d3d12Queue=queue.Get();dp.dxgiAdapter=adapter.Get();
 auto* renderer=init(dp);if(!renderer){std::puts("InitDevice returned null");return 4;}
 std::puts("PASS native D3D12 InitDevice");std::fflush(stdout);
 constexpr int size=64;pd::FrameWarpInitParams fp{size,size,DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM};
 auto eyes=frames(fp);for(auto& e:eyes.eyeFrameBuffers)if(!e.color.pTexture){std::puts("No eye output");return 5;}
 std::puts("PASS two eye framebuffer allocation");std::fflush(stdout);
 ComPtr<ID3D11Device> d11;ComPtr<ID3D11DeviceContext> c11;D3D_FEATURE_LEVEL level;
 check(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d11,&level,&c11),"D3D11 same adapter");
 ComPtr<ID3D11Device5> d115;ComPtr<ID3D11DeviceContext4> c114;check(d11.As(&d115),"D3D11 shared fence device");check(c11.As(&c114),"D3D11 shared fence context");
 ComPtr<ID3D12Fence> sharedFence;ComPtr<ID3D11Fence> fence11;HANDLE handle=nullptr;
 check(device->CreateFence(0,D3D12_FENCE_FLAG_SHARED,IID_PPV_ARGS(&sharedFence)),"shared fence");
 check(device->CreateSharedHandle(sharedFence.Get(),nullptr,GENERIC_ALL,nullptr,&handle),"fence handle");check(d115->OpenSharedFence(handle,IID_PPV_ARGS(&fence11)),"open fence11");CloseHandle(handle);
 UINT64 fenceValue=0;
 auto share=[&](DXGI_FORMAT format,ComPtr<ID3D12Resource>& r12,ComPtr<ID3D11Texture2D>& r11){
  D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=d.Height=size;d.DepthOrArraySize=d.MipLevels=1;d.SampleDesc.Count=1;d.Format=format;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
  check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_SHARED,&d,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&r12)),"shared texture");
  check(device->CreateSharedHandle(r12.Get(),nullptr,GENERIC_ALL,nullptr,&handle),"texture handle");check(d115->OpenSharedResource1(handle,IID_PPV_ARGS(&r11)),"open texture11");CloseHandle(handle);
 };
 ComPtr<ID3D12Resource> sharedInput;ComPtr<ID3D11Texture2D> sharedInput11;
 share(DXGI_FORMAT_R8G8B8A8_UNORM,sharedInput,sharedInput11);
 ComPtr<ID3D11RenderTargetView> inputRtv;check(d11->CreateRenderTargetView(sharedInput11.Get(),nullptr,&inputRtv),"input RTV");
 auto barrier=[](ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){if(before==after)return;D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};list->ResourceBarrier(1,&b);};
 pd::FrameBufferDesc input;
 input.color.pTexture=sharedInput.Get();input.color.initialState=D3D12_RESOURCE_STATE_COMMON;renderer->SetupTextureDesc(input.color);
 if(!renderer->CreateTexture(size,size,DXGI_FORMAT_R32_FLOAT,D3D12_RESOURCE_STATE_COMMON,input.depth,true)||
    !renderer->CreateTexture(size,size,DXGI_FORMAT_R16G16_FLOAT,D3D12_RESOURCE_STATE_COMMON,input.motionVectors,true)){std::puts("Input allocation failed");return 6;}
 pd::CameraData camera{};auto* matrices=reinterpret_cast<float*>(&camera);for(int m=0;m<12;++m)for(int i=0;i<4;++i)matrices[m*16+i*5]=1;
 for(int frame=0;frame<4;++frame){
  int eye=frame%2;const int shift=frame<2?0:4;camera.destViewToClipMatrix.value[12]=shift*2.f/size;camera.destClipToViewMatrix.value[12]=-shift*2.f/size;
  auto* list=renderer->BeginCommandList(eye);const float color[4]={eye?0.f:1.f,eye?1.f:0.f,0.f,1.f};const float depth[4]={.5f,.5f,.5f,1.f};const float mv[4]={};
  std::vector<unsigned char> pattern(size*size*4);for(int y=0;y<size;++y)for(int x=0;x<size;++x){auto* px=&pattern[(y*size+x)*4];px[eye]=64+x*2;px[eye^1]=0;px[2]=y*3;px[3]=255;}
  c11->UpdateSubresource(sharedInput11.Get(),0,nullptr,pattern.data(),size*4,0);
  check(c114->Signal(fence11.Get(),++fenceValue),"11 signal");c11->Flush();check(queue->Wait(sharedFence.Get(),fenceValue),"12 wait");
  barrier(list,input.color.pTexture,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);input.color.initialState=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  renderer->Clear(list,input.depth,depth);renderer->Clear(list,input.motionVectors,mv);
  renderer->EndCommandList(eye);renderer->WaitIdle(eye);
  pd::FrameWarpEvaluateParams p{};p.InEyeFrameBuffer=&input;p.CameraData=&camera;p.EyeIndex=static_cast<pd::EyeIndex>(eye);p.Mode=pd::AlternateEyeWarping;p.MotionVectorsType=pd::Normal;p.InMotionScale[0]=size;p.InMotionScale[1]=size;p.ClearBeforeWarping=true;
  evaluate(p);
  std::printf("Evaluate eye %d returned output %s\n",eye,p.OutEyeFrameBuffer&&p.OutEyeFrameBuffer->color.pTexture?"yes":"no");std::fflush(stdout);
  if(!p.OutEyeFrameBuffer||!p.OutEyeFrameBuffer->color.pTexture)return 7;
  auto& out=p.OutEyeFrameBuffer->color;auto desc=out.pTexture->GetDesc();
  if(desc.Width!=size||desc.Height!=size||desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM){std::printf("Unexpected output shape/format %u\n",desc.Format);return 9;}
  ComPtr<ID3D12Resource> sharedOutput;ComPtr<ID3D11Texture2D> sharedOutput11;share(desc.Format,sharedOutput,sharedOutput11);
  renderer->WaitIdle(2);auto* copy=renderer->BeginCommandList(2);
  barrier(copy,out.pTexture,out.initialState,D3D12_RESOURCE_STATE_COPY_SOURCE);
  barrier(copy,sharedOutput.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);
  copy->CopyResource(sharedOutput.Get(),out.pTexture);
  barrier(copy,out.pTexture,D3D12_RESOURCE_STATE_COPY_SOURCE,out.initialState);
  barrier(copy,sharedOutput.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);
  barrier(copy,input.color.pTexture,input.color.initialState,D3D12_RESOURCE_STATE_COMMON);input.color.initialState=D3D12_RESOURCE_STATE_COMMON;
  renderer->EndCommandList(2);check(queue->Signal(sharedFence.Get(),++fenceValue),"12 signal");check(c114->Wait(fence11.Get(),fenceValue),"11 wait");
  D3D11_TEXTURE2D_DESC rd{};sharedOutput11->GetDesc(&rd);rd.Usage=D3D11_USAGE_STAGING;rd.BindFlags=rd.MiscFlags=0;rd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> read;check(d11->CreateTexture2D(&rd,nullptr,&read),"readback");c11->CopyResource(read.Get(),sharedOutput11.Get());
  D3D11_MAPPED_SUBRESOURCE map{};check(c11->Map(read.Get(),0,D3D11_MAP_READ,0,&map),"readback map");
  unsigned correct=0;for(int y=4;y<size-4;++y)for(int x=4;x<size-4;++x){auto* px=static_cast<unsigned char*>(map.pData)+y*map.RowPitch+x*4;if(std::abs(int(px[eye])-(64+(x-shift)*2))<=2&&px[eye^1]<3&&std::abs(int(px[2])-y*3)<=2)++correct;}
  c11->Unmap(read.Get(),0);std::printf("Eye %d shift %d pixels: %u/%u\n",eye,shift,correct,(size-8)*(size-8));std::fflush(stdout);
  if(correct!=(size-8)*(size-8))return 10;
  renderer->WaitIdle(2);
 }
 ComPtr<ID3D12Fence> fence;check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"fence");check(queue->Signal(fence.Get(),1),"signal");HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);check(fence->SetEventOnCompletion(1,event),"completion");if(WaitForSingleObject(event,10000)!=WAIT_OBJECT_0)return 8;CloseHandle(event);
 check(device->GetDeviceRemovedReason(),"device health");
 ComPtr<ID3D12InfoQueue> messages;unsigned errors=0;
 if(SUCCEEDED(device.As(&messages))){for(UINT64 i=0;i<messages->GetNumStoredMessages();++i){SIZE_T bytes=0;messages->GetMessage(i,nullptr,&bytes);std::vector<unsigned char> buffer(bytes);auto* msg=reinterpret_cast<D3D12_MESSAGE*>(buffer.data());if(SUCCEEDED(messages->GetMessage(i,msg,&bytes))&&msg->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){std::printf("D3D12 error: %s\n",msg->pDescription);++errors;}}}
 if(errors)return 11;
 std::puts("PASS two-eye identity and known clip translation through D3D11 -> D3D12 plugin -> D3D11, shared fence ordering; not a live game or motion-quality test");std::fflush(stdout);
 // No shutdown export exists. Process exit owns teardown; do not unload under plugin GPU resources.
 ExitProcess(0);
}
