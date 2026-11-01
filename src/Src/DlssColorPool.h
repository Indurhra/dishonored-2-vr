#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <memory>
#include <vector>
#include <mutex>
#include <atomic>
// Only private colour snapshots belong here. Motion/depth retained by AFW are
// deliberately excluded. A command-list batch holds a lease until destruction;
// the immediate lane separately proves GPU completion without blocking/flushing.
class DlssColorPool {
    template<class T> using ComPtr=Microsoft::WRL::ComPtr<T>;
public:
    struct Slot {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11ShaderResourceView> view;
        ComPtr<ID3D11Query> completion;
        ID3D11Device* device=nullptr;UINT width=0,height=0;
        DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN,viewFormat=DXGI_FORMAT_UNKNOWN;
        std::atomic<bool> issued{false};bool ready=false;
        size_t Bytes()const{return size_t(width)*height*4;}
    };
    using Lease=std::shared_ptr<Slot>;
    std::atomic<unsigned long long> hits{0},misses{0};
private:
    std::mutex mutex;
    std::vector<Lease> slots;
    static constexpr size_t limit=128ull*1024*1024;
public:
    Lease Acquire(ID3D11Device* device,const D3D11_TEXTURE2D_DESC& desc,DXGI_FORMAT viewFormat){
        // Caller descriptors are private, single-level, single-sample 32-bit snapshots.
        if(desc.MipLevels!=1||desc.ArraySize!=1||desc.SampleDesc.Count!=1||desc.SampleDesc.Quality!=0||
           desc.Usage!=D3D11_USAGE_DEFAULT||desc.BindFlags!=D3D11_BIND_SHADER_RESOURCE||desc.CPUAccessFlags||desc.MiscFlags||
           (desc.Format!=DXGI_FORMAT_R11G11B10_FLOAT&&desc.Format!=DXGI_FORMAT_R8G8B8A8_TYPELESS))return {};
        std::lock_guard<std::mutex> lock(mutex);
        for(auto& slot:slots)if(slot.use_count()==1&&slot->ready&&slot->device==device&&
            slot->width==desc.Width&&slot->height==desc.Height&&slot->format==desc.Format&&slot->viewFormat==viewFormat){
            slot->ready=false;slot->issued=false;++hits;return slot;
        }
        ++misses;
        const size_t needed=size_t(desc.Width)*desc.Height*4;
        size_t bytes=0;for(auto& slot:slots)bytes+=slot->Bytes();
        // Eviction releases our ownership only. D3D retains resources still used by
        // submitted work; an evicted object is never recycled into a different batch.
        for(auto it=slots.begin();it!=slots.end()&&(slots.size()>=8||bytes+needed>limit);){
            if(it->use_count()==1){bytes-=(*it)->Bytes();it=slots.erase(it);}else ++it;
        }
        if(slots.size()>=8||bytes+needed>limit)return {};
        auto slot=std::make_shared<Slot>();
        if(FAILED(device->CreateTexture2D(&desc,nullptr,&slot->texture)))return {};
        if(viewFormat!=DXGI_FORMAT_UNKNOWN){
            D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.Format=viewFormat;view.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;view.Texture2D.MipLevels=1;
            if(FAILED(device->CreateShaderResourceView(slot->texture.Get(),&view,&slot->view)))return {};
        }
        D3D11_QUERY_DESC query{D3D11_QUERY_EVENT,0};
        if(FAILED(device->CreateQuery(&query,&slot->completion)))return {};
        slot->device=device;slot->width=desc.Width;slot->height=desc.Height;slot->format=desc.Format;slot->viewFormat=viewFormat;
        slots.push_back(slot);return slot;
    }
    static void Submitted(ID3D11DeviceContext* ctx,const Lease& lease){
        if(lease){ctx->End(lease->completion.Get());lease->issued.store(true,std::memory_order_release);}
    }
    void Poll(ID3D11DeviceContext* ctx,ID3D11Device* device,bool enabled){
        std::lock_guard<std::mutex> lock(mutex);
        for(auto it=slots.begin();it!=slots.end();){
            auto& slot=*it;
            if(slot.use_count()!=1){++it;continue;}
            if(!enabled||slot->device!=device){it=slots.erase(it);continue;}
            if(!slot->ready&&slot->issued.load(std::memory_order_acquire)){
                BOOL complete=FALSE;
                slot->ready=ctx->GetData(slot->completion.Get(),&complete,sizeof(complete),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK&&complete;
            }
            ++it;
        }
    }
};
