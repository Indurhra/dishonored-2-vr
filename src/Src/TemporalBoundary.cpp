#include "TemporalBoundary.h"
#include <wrl/client.h>
#include <atomic>
#include <sstream>
#include <vector>
#include <new>
using Microsoft::WRL::ComPtr;
namespace TemporalBoundary { namespace {
// Private interfaces retain prefix lifetime with its owning context/list. No raw
// command-list address cache, eviction, timeout or discarded-command assumption.
const GUID prefixKey={0xb3bc5d81,0xe07d,0x4787,{0xaf,0xa9,0x48,0x77,0x95,0xa2,0x71,0x5c}};
const GUID pendingKey={0xdd19da10,0x056f,0x43bc,{0x9b,0xc7,0xec,0x52,0xab,0x78,0x3f,0x0d}};
struct Pending final:IUnknown {
 std::atomic<ULONG> refs{1};ComPtr<ID3D11CommandList> prefix;
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override {if(!out)return E_POINTER;*out=nullptr;if(id!=__uuidof(IUnknown))return E_NOINTERFACE;*out=this;AddRef();return S_OK;}
 ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
 ULONG STDMETHODCALLTYPE Release()override{const auto n=--refs;if(!n)delete this;return n;}
};
ComPtr<Pending> State(ID3D11DeviceContext* ctx,bool create){
 IUnknown* raw=nullptr;UINT bytes=sizeof(raw);ComPtr<Pending> state;
 if(SUCCEEDED(ctx->GetPrivateData(pendingKey,&bytes,&raw))&&raw)state.Attach(static_cast<Pending*>(raw));
 else if(create){state.Attach(new(std::nothrow) Pending);if(state&&FAILED(ctx->SetPrivateDataInterface(pendingKey,state.Get())))state.Reset();}
 return state;
}
std::atomic<bool> enabled{false},used{false};
std::atomic<unsigned long long> split{0},attached{0},executed{0},refused{0};
ComPtr<ID3D11CommandList> Prefix(ID3D11DeviceChild* object){
 IUnknown* raw=nullptr;UINT bytes=sizeof(raw);ComPtr<ID3D11CommandList> list;
 if(SUCCEEDED(object->GetPrivateData(prefixKey,&bytes,&raw))&&raw){ComPtr<IUnknown> owner;owner.Attach(raw);owner.As(&list);}return list;
}
}
void SetEnabled(bool on){enabled=on;}
bool SplitBefore(ID3D11DeviceContext* ctx){
 if(!enabled)return false;
 if(!ctx||ctx->GetType()!=D3D11_DEVICE_CONTEXT_DEFERRED){++refused;return false;}
 auto state=State(ctx,true);if(!state){++refused;return false;}
 used=true;ComPtr<ID3D11CommandList> prefix;
 // TRUE preserves deferred bindings for the suffix. The Finish hook attaches any
 // earlier prefix first, allowing more than one boundary in a command list.
 const auto hr=ctx->FinishCommandList(TRUE,&prefix);
 if(FAILED(hr)||!prefix){++refused;enabled=false;return false;}
 if(FAILED(OnFinish(ctx,prefix.Get()))){++refused;enabled=false;return false;}
 state->prefix=prefix;
 ++split;return true;
}
HRESULT OnFinish(ID3D11DeviceContext* ctx,ID3D11CommandList* list){
 if(!used||!ctx||!list)return S_OK;
 auto state=State(ctx,false);if(!state||!state->prefix)return S_OK;
 auto prefix=state->prefix;
 const auto hr=list->SetPrivateDataInterface(prefixKey,prefix.Get());
 if(FAILED(hr)){++refused;enabled=false;return hr;}
 state->prefix.Reset();++attached;return S_OK;
}
void ExecutePrefixes(ID3D11DeviceContext* ctx,ID3D11CommandList* list,Execute execute){
 if(!used||!ctx||!list||!execute)return;
 std::vector<ComPtr<ID3D11CommandList>> chain;
 for(auto prefix=Prefix(list);prefix;prefix=Prefix(prefix.Get()))chain.push_back(prefix);
 for(auto it=chain.rbegin();it!=chain.rend();++it){execute(ctx,it->Get());++executed;}
}
HRESULT AttachToPending(ID3D11DeviceContext* ctx,REFGUID key,IUnknown* value){auto state=State(ctx,false);return state&&state->prefix?state->prefix->SetPrivateDataInterface(key,value):E_FAIL;}
std::string Status(){std::ostringstream s;s<<"temporalboundary enabled "<<enabled<<" splits "<<split<<" attached "<<attached<<" executed "<<executed<<" refused "<<refused;return s.str();}
}
