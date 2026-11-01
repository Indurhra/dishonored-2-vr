#pragma once
#include <mutex>
#include <condition_variable>
#include <thread>
// Synchronous, allocation-free mailbox. Callbacks and their arguments live on
// the caller's stack until completion. One worker owns all runtime frame calls.
class FrameDispatch {
    std::mutex serial, mutex;
    std::condition_variable wake, done;
    void* argument=nullptr;
    int (*invoke)(void*)=nullptr;
    int result=0;
    bool completed=false, stop=false;
    std::thread worker;
public:
    FrameDispatch():worker([this]{
        std::unique_lock<std::mutex> lock(mutex);
        for(;;){
            wake.wait(lock,[this]{return invoke||stop;});
            if(stop)return;
            auto fn=invoke;auto arg=argument;invoke=nullptr;
            lock.unlock();const int value=fn(arg);lock.lock();
            result=value;completed=true;done.notify_one();
        }
    }){}
    ~FrameDispatch(){
        {std::lock_guard<std::mutex> lock(mutex);stop=true;}
        wake.notify_one();worker.join();
    }
    template<class F> int Call(F& fn){
        std::lock_guard<std::mutex> caller(serial);
        std::unique_lock<std::mutex> lock(mutex);
        completed=false;argument=&fn;
        invoke=[](void* p)->int{return static_cast<int>((*static_cast<F*>(p))());};
        wake.notify_one();done.wait(lock,[this]{return completed;});
        return result;
    }
};
