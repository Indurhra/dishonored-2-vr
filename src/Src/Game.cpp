#include "pch.h"
#include "Game.h"
#include "GameInfo.h"

namespace
{
    bool Readable(const void* p, size_t n)
    {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(p, &mbi, sizeof mbi)) return false;
        if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
        return (uintptr_t)p + n <= (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
    }
}

void* Game::GameLocal()
{
    if (!GameInfo::IsSupportedBuild()) return nullptr;
    void** pp = (void**)(GameInfo::Base() + kGameLocalPtr);
    return Readable(pp, 8) ? *pp : nullptr;
}

void* Game::LocalPlayer()
{
    void* gl = GameLocal();
    if (!gl) return nullptr;
    using Fn = void* (*)(void*);
    __try { return ((Fn)(GameInfo::Base() + kGetLocalPlayer))(gl); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

bool Game::BufferCommandText(const char* text)
{
    if (!GameInfo::IsSupportedBuild()) return false;
    void** pp = (void**)(GameInfo::Base() + kCmdSystemPtr);
    if (!Readable(pp, 8) || !*pp) return false;
    void* obj = *pp;
    void** vt = *(void***)obj;
    using Fn = void (*)(void*, const char*);
    __try { ((Fn)vt[kCmdSlotBufferText])(obj, text); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool Game::PlayerView(float origin[3], float axis[9])
{
    uint8_t* p = (uint8_t*)LocalPlayer();
    if (!p || !Readable(p + kPlayerViewOrigin, 12 * 4)) return false;
    memcpy(origin, p + kPlayerViewOrigin, 12);
    memcpy(axis, p + kPlayerViewAxis, 36);
    return true;
}
