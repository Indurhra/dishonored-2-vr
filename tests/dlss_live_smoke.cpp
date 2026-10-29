#include "DlssLive.h"
#include "DlssRecovery.h"
#include "TemporalBoundary.h"
#include "GameInfo.h"
#include "ViewMatrixHook.h"
#include <wrl/client.h>
#include <iostream>
#include <filesystem>
using Microsoft::WRL::ComPtr;
static std::wstring folder;
namespace GameInfo { const std::wstring& GameDir(){return folder;} }
namespace ViewMatrixHook {bool ViewPoses(unsigned long long,VRRuntime::PoseSet&){return false;}}
int wmain(int argc,wchar_t** argv){
 DlssLive::RecoveryBudget budget;
 if(!budget.Retry()||!budget.Retry()||!budget.Retry()||budget.Retry())return 40;
 for(int i=0;i<119;++i)budget.Success();if(budget.Retry())return 41;
 for(int i=0;i<120;++i)budget.Success();if(!budget.Retry())return 42;
 if(argc<2||argc>3)return 1;DlssLive::SetDirectColor(argc==3);folder=std::filesystem::absolute(argv[1]).wstring();
 ComPtr<ID3D11Device> d;ComPtr<ID3D11DeviceContext> c,r,depthRecorder;
 if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,nullptr,&c))||FAILED(d->CreateDeferredContext(0,&r)))return 2;
 if(FAILED(d->CreateDeferredContext(0,&depthRecorder)))return 2;
 auto texture=[&](DXGI_FORMAT f,UINT flags){D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=256;td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;td.Format=f;td.BindFlags=flags;ComPtr<ID3D11Texture2D> t;if(FAILED(d->CreateTexture2D(&td,nullptr,&t)))std::exit(3);return t;};
 auto rgb=texture(DXGI_FORMAT_R11G11B10_FLOAT,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET);
 auto mv=texture(DXGI_FORMAT_R16G16_FLOAT,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET);
 auto z=texture(DXGI_FORMAT_R24G8_TYPELESS,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_DEPTH_STENCIL);
 auto out=texture(DXGI_FORMAT_R11G11B10_FLOAT,D3D11_BIND_UNORDERED_ACCESS);
 ComPtr<ID3D11ShaderResourceView> rgbSrv,mvSrv;ComPtr<ID3D11UnorderedAccessView> outUav;
 ComPtr<ID3D11RenderTargetView> rgbRtv,mvRtv;ComPtr<ID3D11DepthStencilView> zDsv;
 D3D11_DEPTH_STENCIL_VIEW_DESC zd{};zd.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;zd.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
 if(FAILED(d->CreateShaderResourceView(rgb.Get(),nullptr,&rgbSrv))||FAILED(d->CreateShaderResourceView(mv.Get(),nullptr,&mvSrv))||FAILED(d->CreateUnorderedAccessView(out.Get(),nullptr,&outUav))||FAILED(d->CreateRenderTargetView(rgb.Get(),nullptr,&rgbRtv))||FAILED(d->CreateRenderTargetView(mv.Get(),nullptr,&mvRtv))||FAILED(d->CreateDepthStencilView(z.Get(),&zd,&zDsv)))return 4;
 DlssLive::SetEnabled(true);DlssLive::OnPresent(d.Get());std::cout<<DlssLive::Status()<<'\n';
 float grey[4]={.5f,.5f,.5f,1},zero[4]{},jitter[2]{};
 for(unsigned long long serial=1;serial<=4;++serial){
 r->ClearRenderTargetView(rgbRtv.Get(),grey);r->ClearRenderTargetView(mvRtv.Get(),zero);
 auto* batch=DlssLive::Capture(r.Get(),serial,int((serial-1)%2),jitter,rgbSrv.Get(),mvSrv.Get(),outUav.Get());if(!batch)return 5;
 r->ClearUnorderedAccessViewFloat(outUav.Get(),zero);DlssLive::Finish(r.Get(),batch);
 // Reusing the input in the suffix must not change the boundary evaluation.
 r->ClearRenderTargetView(rgbRtv.Get(),zero);
 ComPtr<ID3D11CommandList> list;if(FAILED(r->FinishCommandList(FALSE,&list))||FAILED(TemporalBoundary::OnFinish(r.Get(),list.Get())))return 6;
 // Record depth AFTER temporal work, but submit it BEFORE the temporal list.
 depthRecorder->ClearDepthStencilView(zDsv.Get(),D3D11_CLEAR_DEPTH,.5f,0);
 DlssLive::ObserveDepth(depthRecorder.Get(),serial,int((serial-1)%2),z.Get());
 ComPtr<ID3D11CommandList> depthList;depthRecorder->FinishCommandList(FALSE,&depthList);DlssLive::OnFinish(depthRecorder.Get(),depthList.Get());c->ExecuteCommandList(depthList.Get(),TRUE);DlssLive::AfterExecute(c.Get(),depthList.Get());
 auto* sentinel=rgbSrv.Get();c->CSSetShaderResources(7,1,&sentinel);
 TemporalBoundary::ExecutePrefixes(c.Get(),list.Get(),[](ID3D11DeviceContext* ctx,ID3D11CommandList* prefix){ctx->ExecuteCommandList(prefix,TRUE);DlssLive::AfterExecute(ctx,prefix);});c->ExecuteCommandList(list.Get(),TRUE);
 ComPtr<ID3D11ShaderResourceView> restored;c->CSGetShaderResources(7,1,&restored);if(restored.Get()!=sentinel)return 7;
 }
 D3D11_TEXTURE2D_DESC td{};out->GetDesc(&td);td.Usage=D3D11_USAGE_STAGING;td.BindFlags=0;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> read;if(FAILED(d->CreateTexture2D(&td,nullptr,&read)))return 8;c->CopyResource(read.Get(),out.Get());D3D11_MAPPED_SUBRESOURCE mapped{};if(FAILED(c->Map(read.Get(),0,D3D11_MAP_READ,0,&mapped)))return 9;
 UINT pixel=*reinterpret_cast<UINT*>(static_cast<char*>(mapped.pData)+128*mapped.RowPitch+128*4);c->Unmap(read.Get(),0);std::cout<<DlssLive::Status()<<"\npixel "<<pixel<<'\n';if(!pixel||DlssLive::Status().find("evaluated 4")==std::string::npos)return 10;
 // Exercise the actual late sRGB path and its separate eye histories.
 auto tone=texture(DXGI_FORMAT_R8G8B8A8_TYPELESS,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET);
 auto finalImage=texture(DXGI_FORMAT_R8G8B8A8_TYPELESS,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET);
 D3D11_RENDER_TARGET_VIEW_DESC rtd{};rtd.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;rtd.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
 D3D11_SHADER_RESOURCE_VIEW_DESC svd{};svd.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;svd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;svd.Texture2D.MipLevels=1;
 ComPtr<ID3D11RenderTargetView> toneRtv,finalRtv;ComPtr<ID3D11ShaderResourceView> toneSrv;
 if(FAILED(d->CreateRenderTargetView(tone.Get(),&rtd,&toneRtv))||FAILED(d->CreateRenderTargetView(finalImage.Get(),&rtd,&finalRtv))||FAILED(d->CreateShaderResourceView(tone.Get(),&svd,&toneSrv)))return 11;
 auto execute=[&](ID3D11DeviceContext* recorder){
  ComPtr<ID3D11CommandList> list;if(FAILED(recorder->FinishCommandList(FALSE,&list)))std::exit(12);
  DlssLive::OnFinish(recorder,list.Get());if(FAILED(TemporalBoundary::OnFinish(recorder,list.Get())))std::exit(13);
  TemporalBoundary::ExecutePrefixes(c.Get(),list.Get(),[](ID3D11DeviceContext* ctx,ID3D11CommandList* prefix){ctx->ExecuteCommandList(prefix,TRUE);DlssLive::AfterExecute(ctx,prefix);});
  c->ExecuteCommandList(list.Get(),TRUE);DlssLive::AfterExecute(c.Get(),list.Get());
 };
 for(int q=1;q<=4;++q){
  DlssLive::SetEnabled(false);DlssLive::OnPresent(d.Get());DlssLive::SetQuality(q);DlssLive::SetEnabled(true);DlssLive::OnPresent(d.Get());
  // A transient unmatched final pass resets histories, then valid stereo evaluates again.
  const auto generation=DlssLive::Generation();
  auto* missing=DlssLive::CaptureUpscale(r.Get(),q*100-1,0,toneSrv.Get(),finalRtv.Get());if(!missing)return 43;
  DlssLive::Finish(r.Get(),missing);execute(r.Get());
  if(!DlssLive::Enabled()||DlssLive::Generation()==generation)return 44;
  DlssLive::OnPresent(d.Get());
  const UINT sizes[]={256,171,148,128,85};const UINT active=sizes[q];const float scale[2]={float(active)/256,float(active)/256};
  for(unsigned n=0;n<4;++n){
   const unsigned long long serial=q*100+n;const int eye=n%2;
   r->ClearRenderTargetView(rgbRtv.Get(),grey);r->ClearRenderTargetView(mvRtv.Get(),zero);
   auto* batch=DlssLive::Capture(r.Get(),serial,eye,jitter,rgbSrv.Get(),mvSrv.Get(),outUav.Get(),scale);if(!batch)return 14;
   r->ClearUnorderedAccessViewFloat(outUav.Get(),zero);DlssLive::Finish(r.Get(),batch);
 // Reusing the input in the suffix must not change the boundary evaluation.
 r->ClearRenderTargetView(rgbRtv.Get(),zero);
   // CPU records temporal work first. GPU receives depth first.
   depthRecorder->ClearDepthStencilView(zDsv.Get(),D3D11_CLEAR_DEPTH,.5f,0);DlssLive::ObserveDepth(depthRecorder.Get(),serial,eye,z.Get());execute(depthRecorder.Get());execute(r.Get());
   D3D11_VIEWPORT viewport{0,0,float(active),float(active),0,1};
   const float eyeColor[4]={eye==0?.5f:.125f,eye==1?.5f:.125f,.25f,1.f};
   r->ClearRenderTargetView(toneRtv.Get(),eyeColor);DlssLive::ObserveTone(r.Get(),serial,eye,toneRtv.Get(),viewport);execute(r.Get());
   batch=DlssLive::CaptureUpscale(r.Get(),serial,eye,toneSrv.Get(),finalRtv.Get(),scale);if(!batch)return 15;
   r->ClearRenderTargetView(finalRtv.Get(),zero);DlssLive::Finish(r.Get(),batch);execute(r.Get());
   ComPtr<ID3D11ShaderResourceView> state;c->CSGetShaderResources(7,1,&state);if(state.Get()!=rgbSrv.Get())return 16;
   DlssLive::OnPresent(d.Get());if(!DlssLive::Enabled())return 24;
  }
  D3D11_TEXTURE2D_DESC desc{};finalImage->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> read;if(FAILED(d->CreateTexture2D(&desc,nullptr,&read)))return 17;c->CopyResource(read.Get(),finalImage.Get());
  D3D11_MAPPED_SUBRESOURCE m{};if(FAILED(c->Map(read.Get(),0,D3D11_MAP_READ,0,&m)))return 18;
  auto* center=reinterpret_cast<unsigned char*>(m.pData)+128*m.RowPitch+128*4;
  const bool good=center[0]>=97&&center[0]<=101&&center[1]>=186&&center[1]<=190&&center[2]>=135&&center[2]<=139;
  std::cout<<"SR quality "<<q<<" pixel "<<unsigned(center[0])<<","<<unsigned(center[1])<<","<<unsigned(center[2])<<" "<<DlssLive::Status()<<'\n';c->Unmap(read.Get(),0);if(!good)return 19;
 }
 // Invalid crop metadata is refused before recording a copy.
 const float invalidScale[2]={1.1f,.5f};
 if(DlssLive::Capture(r.Get(),850,0,jitter,rgbSrv.Get(),mvSrv.Get(),outUav.Get(),invalidScale))return 27;
 if(DlssLive::CaptureUpscale(r.Get(),850,0,toneSrv.Get(),finalRtv.Get(),invalidScale))return 28;
 // Valid temporal inputs with a different final active rectangle must fail closed.
 const float halfScale[2]={.5f,.5f},quarterScale[2]={.25f,.25f};
 auto* mismatch=DlssLive::Capture(r.Get(),851,0,jitter,rgbSrv.Get(),mvSrv.Get(),outUav.Get(),halfScale);if(!mismatch)return 29;
 DlssLive::Finish(r.Get(),mismatch);DlssLive::ObserveDepth(depthRecorder.Get(),851,0,z.Get());execute(depthRecorder.Get());execute(r.Get());
 D3D11_VIEWPORT mismatchViewport{0,0,128,128,0,1};DlssLive::ObserveTone(r.Get(),851,0,toneRtv.Get(),mismatchViewport);execute(r.Get());
 mismatch=DlssLive::CaptureUpscale(r.Get(),851,0,toneSrv.Get(),finalRtv.Get(),quarterScale);if(!mismatch)return 30;
 DlssLive::Finish(r.Get(),mismatch);execute(r.Get());
 if(DlssLive::Enabled()||DlssLive::Status().find("active rectangles disagree")==std::string::npos)return 31;
 // Missing final draws must not leave unfiltered temporal output active indefinitely.
 DlssLive::SetEnabled(false);DlssLive::OnPresent(d.Get());DlssLive::SetQuality(1);DlssLive::SetEnabled(true);DlssLive::OnPresent(d.Get());
 for(unsigned n=0;n<2;++n){
  const unsigned long long serial=900+n;const int eye=n%2;
  r->ClearRenderTargetView(rgbRtv.Get(),grey);r->ClearRenderTargetView(mvRtv.Get(),zero);
  auto* batch=DlssLive::Capture(r.Get(),serial,eye,jitter,rgbSrv.Get(),mvSrv.Get(),outUav.Get());if(!batch)return 25;
  DlssLive::Finish(r.Get(),batch);
  depthRecorder->ClearDepthStencilView(zDsv.Get(),D3D11_CLEAR_DEPTH,.5f,0);DlssLive::ObserveDepth(depthRecorder.Get(),serial,eye,z.Get());execute(depthRecorder.Get());execute(r.Get());
  DlssLive::OnPresent(d.Get());
 }
 if(!DlssLive::Enabled())return 26;
 // An isolated failure recovers; four consecutive failures exhaust the budget.
 DlssLive::SetEnabled(true);DlssLive::OnPresent(d.Get());
 for(unsigned n=0;n<4;++n){
  const auto generation=DlssLive::Generation();
  auto* unmatched=DlssLive::CaptureUpscale(r.Get(),9999+n,n%2,toneSrv.Get(),finalRtv.Get());if(!unmatched)return 20;
  DlssLive::Finish(r.Get(),unmatched);execute(r.Get());
  if(DlssLive::Generation()==generation || DlssLive::Enabled()!=(n<3))return 21;
  DlssLive::OnPresent(d.Get());
 }
 if(DlssLive::Status().find("returning to native stereo")==std::string::npos)return 22;
 DlssLive::SetEnabled(false);DlssLive::OnPresent(d.Get());
 std::cout<<"PASS DLAA/SR both eyes, valid pixels after transient recovery, bounded retries, stale/mismatched input refusal and state restoration\n";
}
