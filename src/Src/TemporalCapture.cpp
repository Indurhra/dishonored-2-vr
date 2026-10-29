#include "TemporalCapture.h"
#include "ViewMatrixHook.h"
#include "VRRuntime.h"
#include "HandAimHook.h"
#include "GpuTrace.h"
#include <wrl/client.h>
#include <atomic>
#include <mutex>
#include <fstream>
#include <cstring>
#include <chrono>
using Microsoft::WRL::ComPtr;
namespace TemporalCapture {
namespace {
std::atomic<bool> active{false};std::mutex mutex;
struct Part {ComPtr<ID3D11Texture2D> image;ComPtr<ID3D11Query> complete;D3D11_TEXTURE2D_DESC desc{};};
struct Frame {Part parts[3];uint64_t serial=0;int eye=-1;float view[16]{},projection[16]{},jitter[2]{};bool metadata=false,posesValid=false,geometryValid=false;VRRuntime::PoseSet poses;float origin[3]{},axis[9]{},headAxis[9]{},handWorld[2][16]{};bool handWorldValid[2]{};};
Frame frames[3];int frameCount=1;std::wstring base;std::chrono::steady_clock::time_point started;
void Clear(){for(auto& frame:frames)frame=Frame{};}
void Fail(const char* message){std::ofstream f(base+L"_status.txt");f<<"FAILED: "<<message<<"\n";active=false;Clear();}
}
bool Active(){return active.load();}
void Request(const std::wstring& destination,int count){
    std::lock_guard lock(mutex);active=false;Clear();base=destination;frameCount=count>=1&&count<=3?count:1;
    started=std::chrono::steady_clock::now();active=true;
}
void Observe(ID3D11DeviceContext* ctx,int component,ID3D11Resource* resource,uint64_t serial,int eye,
             const float* view,const float* projection,const float* jitter){
    if(!active || !ctx || !resource || !serial || eye<0 || eye>1 || component<0 || component>2)return;
    std::lock_guard lock(mutex);if(!active)return;
    int index=0;
    if(frames[0].serial && serial!=frames[0].serial){
        if(serial<frames[0].serial || serial-frames[0].serial>=uint64_t(frameCount))return;
        index=int(serial-frames[0].serial);if(eye!=(frames[0].eye^(index&1)))return;
    }
    auto& frame=frames[index];if(frame.serial && (frame.serial!=serial || frame.eye!=eye))return;
    auto& part=frame.parts[component];if(part.image)return;
    ComPtr<ID3D11Texture2D> source;if(FAILED(resource->QueryInterface(IID_PPV_ARGS(&source))))return;
    D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);
    if(desc.SampleDesc.Count!=1 || desc.ArraySize!=1 || desc.MipLevels!=1)return;
    // These formats were observed in D2, not copied from the reference engine.
    const DXGI_FORMAT expected[]={DXGI_FORMAT_R11G11B10_FLOAT,DXGI_FORMAT_R16G16_FLOAT,DXGI_FORMAT_R24G8_TYPELESS};
    if(desc.Format!=expected[component])return;
    for(const auto& record:frames)for(const auto& p:record.parts)
        if(p.image && (p.desc.Width!=desc.Width || p.desc.Height!=desc.Height))return;
    ComPtr<ID3D11Device> device;ctx->GetDevice(&device);
    part.desc=desc;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=0;desc.CPUAccessFlags=0;desc.MiscFlags=0;
    if(FAILED(device->CreateTexture2D(&desc,nullptr,&part.image))){Fail("snapshot allocation");return;}
    D3D11_QUERY_DESC query{D3D11_QUERY_EVENT,0};
    if(FAILED(device->CreateQuery(&query,&part.complete))){Fail("completion query allocation");return;}
        frame.serial=serial;frame.eye=eye;
    // Snapshot while the serial is still in the view ring. Never use latest poses
    // during later GPU readback, which may belong to an unrelated frame.
    if(!frame.posesValid)frame.posesValid=ViewMatrixHook::ViewPoses(serial,frame.poses);
    if(!frame.geometryValid){float unused[16];frame.geometryValid=ViewMatrixHook::ViewBySerial(serial,unused,frame.origin,frame.axis,frame.headAxis);}
    if(frame.geometryValid&&frame.posesValid&&frame.poses.inputValid&&GpuTrace::HandsFromView()){
        auto input=frame.poses.input;const auto& eyePose=frame.poses.eye[eye];
        for(int h=0;h<2;++h){const auto& grip=input.gripHand[h];
            if(frame.handWorldValid[h]||!grip.valid||grip.held||!input.hand[h].valid||input.hand[h].held||!eyePose.valid||eyePose.held)continue;
            input.hand[h].posX=grip.posX;input.hand[h].posY=grip.posY;input.hand[h].posZ=grip.posZ;
            float origin[3],axis[9];
            if(!HandAimHook::ControllerPoseAt(h,frame.origin,frame.headAxis,eyePose.pos,origin,axis,&input,frame.poses.head.quat))continue;
            auto& matrix=frame.handWorld[h];
            for(int r=0;r<3;++r){for(int c=0;c<3;++c)matrix[r*4+c]=axis[c*3+r];matrix[r*4+3]=origin[r];}matrix[15]=1;
            frame.handWorldValid[h]=true;
        }
    }
    ctx->CopyResource(part.image.Get(),source.Get());ctx->End(part.complete.Get());
    if(view && projection && jitter){memcpy(frame.view,view,64);memcpy(frame.projection,projection,64);memcpy(frame.jitter,jitter,8);frame.metadata=true;}
}
void Poll(ID3D11Device* device){
    if(!active || !device)return;std::lock_guard lock(mutex);if(!active)return;
    if(std::chrono::steady_clock::now()-started>std::chrono::seconds(15)){Fail("matching inputs or GPU completion timed out");return;}
    ComPtr<ID3D11DeviceContext> ctx;device->GetImmediateContext(&ctx);
    for(int i=0;i<frameCount;++i){if(!frames[i].metadata)return;for(const auto& part:frames[i].parts){
        if(!part.image || !part.complete)return;BOOL done=FALSE;
        const HRESULT result=ctx->GetData(part.complete.Get(),&done,sizeof(done),D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if(FAILED(result)){Fail("GPU completion query");return;}if(result!=S_OK || !done)return;
    }}
    const wchar_t* names[]={L"_color.bin",L"_motion.bin",L"_depth.bin"};
    // All copies must execute before readback, including both eyes of a pair.
    // Readback stalls are diagnostic-only and excluded from performance samples.
    for(int i=0;i<frameCount;++i){auto& frame=frames[i];const auto destination=base+(frameCount>1?L"_"+std::to_wstring(i):L"");
        for(int k=0;k<3;++k){
            auto desc=frame.parts[k].desc;desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
            ComPtr<ID3D11Texture2D> staging;if(FAILED(device->CreateTexture2D(&desc,nullptr,&staging))){Fail("readback allocation");return;}
            ctx->CopyResource(staging.Get(),frame.parts[k].image.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
            if(FAILED(ctx->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped))){Fail("readback Map");return;}
            std::ofstream file(destination+names[k],std::ios::binary);
            for(UINT y=0;y<desc.Height;++y)file.write(static_cast<const char*>(mapped.pData)+size_t(y)*mapped.RowPitch,size_t(desc.Width)*4);
            ctx->Unmap(staging.Get(),0);if(!file){Fail("snapshot file write");return;}
        }
        std::ofstream out(destination+L"_metadata.txt");out.precision(10);
        out<<"serial "<<frame.serial<<"\neye "<<frame.eye<<"\nwidth "<<frame.parts[0].desc.Width<<"\nheight "<<frame.parts[0].desc.Height;
        out<<"\nformats 26 34 44\nraw_bytes_per_pixel 4 4 4\nview";for(float v:frame.view)out<<' '<<v;
        out<<"\nprojection";for(float v:frame.projection)out<<' '<<v;
        out<<"\nnative_jitter "<<frame.jitter[0]<<' '<<frame.jitter[1]<<"\n";
        out<<"view_layout transposed_constant_buffer\nprojection_layout engine_row_major\n";
        out<<"poses_valid "<<frame.posesValid<<"\ngeometry_valid "<<frame.geometryValid<<"\n";
        if(frame.geometryValid){out<<"engine_origin";for(float v:frame.origin)out<<' '<<v;out<<"\nengine_axis";for(float v:frame.axis)out<<' '<<v;out<<'\n';}
        for(int h=0;h<2;++h){out<<"hand_world"<<h<<"_valid "<<frame.handWorldValid[h]<<"\n";if(frame.handWorldValid[h]){out<<"hand_world"<<h;for(float v:frame.handWorld[h])out<<' '<<v;out<<"\n";}}
        if(frame.posesValid){
            const auto& poses=frame.poses;
            out<<"pose_time "<<poses.time<<"\npose_base "<<poses.base<<"\npose_kind "<<poses.kind<<"\npose_predicted "<<poses.predicted<<"\ninput_valid "<<poses.inputValid<<"\ninput_time "<<poses.input.time<<'\n';
            auto head=[&](const char* name,const VRRuntime::HeadPose& pose){out<<name<<' '<<pose.valid<<' '<<pose.held<<' '<<pose.gen;for(float v:pose.pos)out<<' '<<v;for(float v:pose.quat)out<<' '<<v;out<<'\n';};
            head("xr_head",poses.head);head("xr_eye0",poses.eye[0]);head("xr_eye1",poses.eye[1]);
            if(poses.inputValid)for(int h=0;h<2;++h){const auto& hand=poses.input.gripHand[h];
                out<<"xr_grip"<<h<<' '<<hand.valid<<' '<<hand.held<<' '<<hand.posX<<' '<<hand.posY<<' '<<hand.posZ<<' '<<hand.quatX<<' '<<hand.quatY<<' '<<hand.quatZ<<' '<<hand.quatW<<'\n';}
            out<<"pose_space XR_LOCAL_metres; engine geometry remains in engine units\n";
        }
        if(!out){Fail("metadata file write");return;}
    }
    std::ofstream status(base+L"_status.txt");status<<"COMPLETE: "<<frameCount<<" matched serial/eye snapshots; input conventions require pixel validation\n";
    active=false;Clear();
}
}
