#include "FrameDispatch.h"
#include "ShaderHashCache.h"
#include <vector>
#include <atomic>
#include <cstdio>
#include <cstdlib>
void Check(bool b){if(!b)std::abort();}
int main(){
    FrameDispatch dispatch;std::thread::id owner;std::mutex graphics;
    auto identify=[&]{owner=std::this_thread::get_id();return 0;};dispatch.Call(identify);
    std::vector<std::thread> callers;std::atomic<int> count{0};
    for(int t=0;t<4;++t)callers.emplace_back([&,t]{for(int n=0;n<1000;++n){
        const int expected=t*1000+n;
        auto callback=[&]{std::lock_guard<std::mutex> lock(graphics);Check(std::this_thread::get_id()==owner);++count;return expected;};
        Check(dispatch.Call(callback)==expected);
    }});
    for(auto& t:callers)t.join();Check(count==4000);
    ShaderHashCache cache;int shader;uint64_t hash;
    Check(!cache.Find(&shader,1,hash));cache.Store(&shader,1,123);Check(cache.Find(&shader,1,hash)&&hash==123);
    Check(!cache.Find(&shader,2,hash));cache.Store(&shader,2,456);Check(cache.Find(&shader,2,hash)&&hash==456);
    cache.Store(&shader,3,0);Check(cache.Find(&shader,3,hash)&&hash==0);
    std::puts("PASS 4000 concurrent frame calls, single worker, callback lock ownership, shader address reuse");
}
