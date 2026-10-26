#include "pch.h"
#include "Psvr2Triggers.h"
#include "../Psvr2/Shared.hpp"
#include "VRRuntime.h"
#include "GameInfo.h"
#include "BodyHide.h"
#include <filesystem>
namespace {
using namespace dvrpsvr2;
std::atomic<bool> observe{false};
std::atomic<uint64_t> equipment{0};
Shared* shared=nullptr;
HANDLE mapping=nullptr,helper=nullptr;
bool attempted=false;
bool Readable(const void* p,size_t n) {
    MEMORY_BASIC_INFORMATION m{};
    return p && VirtualQuery(p,&m,sizeof(m)) && m.State==MEM_COMMIT && !(m.Protect&(PAGE_GUARD|PAGE_NOACCESS))
        && uintptr_t(p)>=uintptr_t(m.BaseAddress) && n<=m.RegionSize && uintptr_t(p)-uintptr_t(m.BaseAddress)<=m.RegionSize-n;
}
Profile Classify(uintptr_t vt) {
    const auto base=GameInfo::Base();
    if(vt==base+BodyHide::kGunCpntVtable) return Profile::Gun;
    if(vt==base+BodyHide::kCrossbowCpntVtable) return Profile::Crossbow;
    // MSVC x64 RTTI points to immutable image data. Inspect only on item-class changes.
    if(vt<base+8 || vt>=base+0x4000000 || !Readable((void*)(vt-8),8)) return Profile::Off;
    auto col=*(const unsigned char**)(vt-8);
    if(!Readable(col,24) || *(const uint32_t*)col!=1) return Profile::Off;
    auto rva=*(const uint32_t*)(col+12);
    if(rva>=0x4000000) return Profile::Off;
    auto name=(const char*)(base+rva+16);
    if(!Readable(name,128)) return Profile::Off;
    std::string_view text(name,strnlen(name,128));
    LOGI("PSVR2: left item class {}",text);
    return PowerClass(text)?Profile::Power:Profile::Off;
}
bool Connect() {
    attempted=true;
    const auto name=L"Local\\DVRPsvr2_"+std::to_wstring(GetCurrentProcessId());
    mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Shared),name.c_str());
    if(!mapping) return false;
    shared=(Shared*)MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(Shared));
    if(!shared) return false;
    *shared={};shared->magicValue=magic;shared->version=1;publish(shared,Profile::Off);
    auto exe=std::filesystem::path(GameInfo::GameDir())/L"DVR_psvr2"/L"dvr_psvr2_bridge.exe";
    auto command=L"\""+exe.wstring()+L"\" "+name+L" "+std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;
    PROCESS_INFORMATION pi{};
    if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,exe.parent_path().c_str(),&si,&pi)) {
        LOGI("PSVR2: bridge could not start ({})",GetLastError());return false;
    }
    helper=pi.hProcess;CloseHandle(pi.hThread);
    LOGI("PSVR2: left-trigger bridge started; pistol 2/3/5, crossbow 2/3/7, power vibration 60 Hz amplitude 1");return true;
}
}
void Psvr2Triggers::ObserveEquipment(void* player) {
    if(!observe.load()) return;
    Profile p=Profile::Off;
    if(player && GameInfo::IsSupportedBuild()) {
        auto base=GameInfo::Base();
        auto eq=(unsigned char*)((void*(*)(void*,void*,int))(base+BodyHide::kGetComponent))(player,(void*)(base+BodyHide::kEquipCpntTypeInfo),2);
        if(Readable(eq,0x78)) {
            auto item=*(unsigned char**)(eq+0x70);
            if(Readable(item,0x18)) {
                auto cpnt=*(uintptr_t**)(item+0x10);
                if(Readable(cpnt,8)) {
                    static uintptr_t last=0;static Profile cached=Profile::Off;
                    if(*cpnt!=last) {last=*cpnt;cached=Classify(last);}
                    p=cached;
                }
            }
        }
    }
    equipment=pack(tick(),p,0);
}
dvrpsvr2::Profile Psvr2Triggers::Item() { return decode(equipment.load(),tick(),true).profile; }
void Psvr2Triggers::Update(Profile desired) {
    static const bool enabled=GetPrivateProfileIntW(L"Gameplay",L"Psvr2Triggers",1,
        (std::filesystem::path(GameInfo::GameDir())/L"DishonoredVR.ini").c_str())!=0;
    if(!enabled) return;
    const auto xr=VRRuntime::GetStatus();
    const bool headset=xr.initialized && xr.running && Headset(xr.runtime,xr.system);
    observe=headset;
    if(!headset || xr.state!=5) desired=Profile::Off;
    if(!headset && !shared) return; // No mapping, helper or Toolkit call on Quest/VDXR.
    if(shared && InterlockedCompareExchange(&shared->stop,0,0)) {
        if(helper && WaitForSingleObject(helper,0)==WAIT_TIMEOUT) return;
        if(helper) {CloseHandle(helper);helper=nullptr;}
        UnmapViewOfFile(shared);shared=nullptr;
        if(mapping) {CloseHandle(mapping);mapping=nullptr;}
        attempted=false;
    }
    if(headset && !attempted) Connect();
    if(!shared || !helper) return;
    static LONG previous=-1;
    LONG st=InterlockedCompareExchange(&shared->status,0,0);
    if(st!=previous) {previous=st;LOGI("PSVR2: bridge status {} result {}",st,InterlockedCompareExchange(&shared->result,0,0));}
    if(WaitForSingleObject(helper,0)!=WAIT_TIMEOUT) return;
    publish(shared,desired);
    static Profile last=Profile::Off;
    if(last!=desired) {last=desired;LOGI("PSVR2: left trigger profile {} (0 off, 1 pistol, 2 power, 3 crossbow)",(unsigned)desired);}
}
void Psvr2Triggers::Stop() {
    observe=false;
    if(shared) {publish(shared,Profile::Off);InterlockedExchange(&shared->stop,1);}
}
