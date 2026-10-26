#include <Windows.h>
#include <cstdio>
#include <cstdlib>
#define PSVR2TK_EXPORT
#include "../src/Psvr2/Effects.hpp"
static void record(const char* action, int hand=-1, int mode=-1) {
    const char* path = std::getenv("DVRPSVR2_FAKE_LOG");
    if (!path) return;
    FILE* f = std::fopen(path,"a");
    if (f) { std::fprintf(f,"%s %d %d\n",action,hand,mode); std::fclose(f); }
}
extern "C" {
__declspec(dllexport) void test_exchange64(volatile LONG64* address, LONG64 value) {
    InterlockedExchange64(address, value);
}
__declspec(dllexport) int psvr2_toolkit_init() { record("init"); return 0; }
__declspec(dllexport) void psvr2_toolkit_deinit() { record("deinit"); }
__declspec(dllexport) bool psvr2_toolkit_get_driver_active() { return true; }
__declspec(dllexport) int psvr2_toolkit_set_trigger_effect(VRControllerType hand,const ScePadTriggerEffectCommand& c) {
    record("set",int(hand),int(c.mode));
    return c.mode != SCE_PAD_TRIGGER_EFFECT_MODE_OFF && std::getenv("DVRPSVR2_FAKE_FAIL") ? -3 : 0;
}
__declspec(dllexport) void* psvr2_toolkit_loader_get_module_handle() {
    HMODULE module{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&psvr2_toolkit_loader_get_module_handle),&module);
    return module;
}
}
