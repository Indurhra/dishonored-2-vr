#pragma once
#include <atomic>
#include <cstdint>
#include <string>
struct ID3D11Device;
namespace FrameProfile {
#ifdef DVR_FRAME_PROFILE
extern std::atomic<bool> active,detail;
bool Request(const std::wstring& path,int seconds,unsigned stride=16,bool detailed=false);
void CompleteStereo(bool afw);
void Frame(); // Present epoch, not an engine view serial
void Poll();
void Memory(ID3D11Device*);
class Scope {
    const char* name=nullptr; Scope* parent=nullptr;
    double start=0,children=0;std::uint64_t frame=0;
public:
    void Begin(const char*); void End();
    explicit Scope(const char* label,bool fine=false){if(active.load(std::memory_order_relaxed)&&(!fine||detail.load(std::memory_order_relaxed)))Begin(label);}
    ~Scope(){if(name)End();}
    Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
};
#else
inline std::atomic<bool> active=false;
inline bool Request(const std::wstring&,int,unsigned=16,bool=false){return false;}
inline void CompleteStereo(bool){}
inline void Frame(){} inline void Poll(){} inline void Memory(ID3D11Device*){}
struct Scope{explicit Scope(const char*,bool=false){}};
#endif
}
#define DVR_JOIN_(a,b) a##b
#define DVR_JOIN(a,b) DVR_JOIN_(a,b)
#define DVR_PROFILE(name) FrameProfile::Scope DVR_JOIN(profileScope_,__LINE__)(name)

#define DVR_PROFILE_DETAIL(name) FrameProfile::Scope DVR_JOIN(profileScope_,__LINE__)(name,true)
