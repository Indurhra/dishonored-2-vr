#include "FrameProfile.h"
#include "AfwLive.h"
#include "AfwPlugin.h"
#include "AfwInputs.h"
#include "DlssStageTiming.h"
#include "DlssLive.h"
#include "GameInfo.h"
#include "VRMath.h"
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <mutex>
#include <sstream>
using Microsoft::WRL::ComPtr;
namespace AfwLive {namespace {
std::atomic<bool> enabled=false;std::mutex mutex;std::string reason="disabled";
struct Record{unsigned long long serial=0,prepared=0,epoch=0;int eye=-1;unsigned rw=0,rh=0;float jitter[2]{},view[16]{},projection[16]{},target[16]{};bool matrices=false;ViewMatrixHook::XrPose targetPose;ComPtr<ID3D11Texture2D> depth,motion;};
std::array<Record,16> records;unsigned long long renders=0,warps=0,accepted=0,refused=0,last=0,lastEpoch=0;bool initialized=false;AfwPlugin::Backend& Backend(){static auto* b=new AfwPlugin::Backend;return *b;}
Record& At(unsigned long long s){auto& r=records[s%records.size()];if(r.serial!=s){r=Record{};r.serial=s;}return r;}
void Column(const float* row,float* col){for(int y=0;y<4;++y)for(int x=0;x<4;++x)col[x*4+y]=row[y*4+x];}
}
void Enable(bool on){std::lock_guard lock(mutex);enabled=on;records={};last=0;reason=on?"awaiting matching DLSS frame":"disabled";}
bool Enabled(){return enabled;}
void Fallback(){std::lock_guard lock(mutex);enabled=false;records={};last=0;reason="Native Stereo fallback: "+reason;}
std::string Message(){std::lock_guard lock(mutex);return reason;}
void Target(unsigned long long serial,const float* m,const ViewMatrixHook::XrPose& pose){if(!enabled)return;std::lock_guard lock(mutex);auto& r=At(serial);memcpy(r.target,m,64);r.targetPose=pose;}
void Matrices(unsigned long long serial,int eye,const float* view,const float* projection,const float* jitter){if(!enabled||!serial)return;std::lock_guard lock(mutex);auto& r=At(serial);r.eye=eye;for(int y=0;y<4;++y)for(int x=0;x<4;++x)r.view[y*4+x]=view[x*4+y];memcpy(r.projection,projection,64);memcpy(r.jitter,jitter,8);r.matrices=true;}
void Prepared(unsigned long long serial,int eye,unsigned long long epoch,ID3D11Texture2D* depth,ID3D11Texture2D* motion,unsigned rw,unsigned rh){if(!enabled)return;std::lock_guard lock(mutex);auto& r=At(serial);r.prepared=serial;r.eye=eye;r.epoch=epoch;r.depth=depth;r.motion=motion;r.rw=rw;r.rh=rh;}
ID3D11Texture2D* Rebuild(ID3D11DeviceContext* ctx,ID3D11Texture2D* color,const ViewMatrixHook::XrPose& pose,ViewMatrixHook::XrPose& target){ DVR_PROFILE("AfwLive.Rebuild");
 if(!enabled)return nullptr;Record r;{std::lock_guard lock(mutex);r=records[pose.serial%records.size()];}
 auto fail=[&](const char* text)->ID3D11Texture2D*{std::lock_guard lock(mutex);++refused;reason=text;return nullptr;};
 if(!DlssLive::Enabled())return fail("AFW requires matching DLSS evaluation");
 if(!pose.valid||pose.flat||pose.eye<0||pose.eye>1||r.serial!=pose.serial||r.prepared!=pose.serial||r.epoch!=DlssLive::Generation()||!r.matrices||r.eye!=pose.eye||!r.targetPose.valid||r.targetPose.eye!=(pose.eye^1)||!r.depth||!r.motion||pose.serial<=last)return fail("missing matching eye/frame data");
 ComPtr<ID3D11Device> device;ctx->GetDevice(&device);
 if(!initialized){if(!Backend().Initialize(device.Get(),GameInfo::GameDir()+L"\\PDAFWPlugin.dll"))return fail(Backend().Problem().c_str());initialized=true;}
 D3D11_TEXTURE2D_DESC outputDesc{};color->GetDesc(&outputDesc);static AfwInputs::Adapter adapter;
 {
 if(!adapter.Prepare(ctx,r.depth.Get(),r.motion.Get(),r.rw,r.rh,outputDesc.Width,outputDesc.Height,-r.jitter[0]*r.rw,r.jitter[1]*r.rh,pose.eye,pose.serial))return fail("AFW depth/motion viewport conversion failed");}
 // Colour has been unjittered by DLSS; depth/motion now sample the corresponding raster locations.
 r.projection[2]-=2*r.jitter[0];r.projection[6]-=2*r.jitter[1];
 AfwPlugin::Frame f;f.color=color;f.depth=adapter.Depth();f.motion=adapter.Motion();f.eye=pose.eye;f.serial=pose.serial;f.profile=DlssStageTiming::Active()&&pose.serial%17==0;f.reset=lastEpoch!=r.epoch||pose.serial!=last+1;
 float targetProjection[16];memcpy(targetProjection,r.projection,64);
 if(!pose.tanValid||!r.targetPose.tanValid)return fail("missing frustum");
 const auto* a=pose.tan;const auto* b=r.targetPose.tan;const float sx=(a[1]-a[0])/(b[1]-b[0]),sy=(a[2]-a[3])/(b[2]-b[3]);
 targetProjection[0]*=sx;targetProjection[2]=sx*r.projection[2]+(sx*(a[1]+a[0])/(a[1]-a[0])-(b[1]+b[0])/(b[1]-b[0]))*r.projection[14];
 targetProjection[5]*=sy;targetProjection[6]=sy*r.projection[6]+(sy*(a[2]+a[3])/(a[2]-a[3])-(b[2]+b[3])/(b[2]-b[3]))*r.projection[14];
 const float* pairs[]={r.target,targetProjection,r.view,r.projection};
 for(int i=0;i<4;++i){float inv[16];if(!VRMath::Inverse4(pairs[i],inv))return fail("singular camera");Column(pairs[i],f.camera.matrices[i*2]);Column(inv,f.camera.matrices[i*2+1]);}
 D3D11_TEXTURE2D_DESC d{};color->GetDesc(&d);f.motionScale[0]=-float(d.Width);f.motionScale[1]=-float(d.Height);
 {std::lock_guard lock(mutex);++renders;}
 {DVR_PROFILE("AfwLive.bridge");DlssStageTiming::Span timing(ctx,3,pose.eye,pose.serial);
 if(!Backend().Evaluate(ctx,f))return fail(Backend().Problem().c_str());}
 AfwPlugin::Timing timing;if(Backend().TakeTiming(timing)){DlssStageTiming::External(4,timing.eye,timing.serial,timing.warpMs);DlssStageTiming::External(5,timing.eye,timing.serial,timing.copyMs);}
 target=r.targetPose;{std::lock_guard lock(mutex);last=pose.serial;lastEpoch=r.epoch;++warps;reason="AFW reconstruction ready; image quality not accepted";}return Backend().Output();
}
void Submitted(bool ok){std::lock_guard lock(mutex);if(ok)++accepted;}
std::string Status(){std::lock_guard lock(mutex);std::ostringstream s;s<<"afw requested "<<enabled<<" fresh "<<renders<<" rebuilt "<<warps<<" accepted "<<accepted<<" refused "<<refused<<" last "<<last<<" identity "<<AfwInputs::IdentityFrames()<<" reason "<<reason;return s.str();}
}
