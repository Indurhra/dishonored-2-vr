#include "DrawCpuCache.h"
#include "PublishedSnapshot.h"
#include <cstdlib>
#include <cstdio>
#include <thread>
#include <vector>
void Check(bool b){if(!b)std::abort();}
int main(){
    DrawRowsCache<unsigned,1> cache; // Force every key to collide.
    float rows[16]{};unsigned computes=0;
    auto get=[&]{return cache.Get(rows,rows+4,rows+8,rows+12,[&]{return ++computes;});};
    Check(get()==1&&get()==1);
    for(unsigned i=0;i<16;++i){rows[i]=float(i+1);const auto n=computes;Check(get()==n+1&&get()==n+1);}
    rows[0]=0;const auto positive=get();rows[0]=-0.0f;Check(get()==positive+1);
    rows[1]=std::bit_cast<float>(0x7fc00001u);auto nan=get();Check(get()==nan);
    rows[1]=std::bit_cast<float>(0x7fc00002u);Check(get()==nan+1);
    DrawSerialWitness witness;int a,b;
    Check(!witness.Matches(&a,0,0));witness.Published(&a,12,3);Check(witness.Matches(&a,12,3));
    Check(!witness.Matches(&a,12,4)); // finish/reset, same address and serial
    Check(!witness.Matches(&b,12,3));Check(!witness.Matches(&a,13,3));
    std::vector<std::thread> workers;
    for(unsigned i=0;i<4;++i)workers.emplace_back([i]{DrawRowsCache<unsigned> c;float r[16]{};
        for(unsigned n=0;n<10000;++n)Check(c.Get(r,r+4,r+8,r+12,[&]{return i;})==i);});
    for(auto& t:workers)t.join();
    struct View{unsigned serial=0;std::array<unsigned,64> fields{};};
    PublishedSnapshot<View> published,other;
    PublishedSnapshot<View>::Reader reader;
    Check(!published.Read(reader));View v;v.serial=1;v.fields.fill(1);published.Publish(v);
    Check(published.Read(reader)&&reader.value.serial==1);
    v.serial=2;v.fields.fill(2);other.Publish(v);Check(other.Read(reader)&&reader.value.fields[0]==2);
    Check(published.Read(reader)&&reader.value.fields[0]==1);
    std::atomic<bool> done=false;workers.clear();
    for(unsigned i=0;i<4;++i)workers.emplace_back([&]{PublishedSnapshot<View>::Reader local;
        do{if(published.Read(local))for(auto n:local.value.fields)Check(n==local.value.serial);}while(!done.load());
        Check(published.Read(local)&&local.value.serial==20000);
    });
    for(unsigned n=2;n<=20000;++n){v.serial=n;v.fields.fill(n);published.Publish(v);}
    done=true;for(auto& t:workers)t.join();
    std::puts("PASS exact rows, changed/reused inputs, collisions, signed zero, NaNs, serial invalidation, worker independence, 20000 concurrent coherent snapshot publications");
}
