#include "../src/Src/WheelSelection.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <initializer_list>
static void check(bool ok,const char* name) { if(!ok){ std::fprintf(stderr,"FAIL: %s\n",name);std::exit(1); } }
int main()
{
    using namespace WheelSelection;
    for(int n : {1,4,11,22,24,64}) for(int i=0;i<n;++i)
    {
        const float step=2*pi/n, a=i*step;
        check(Pick(std::sin(a),std::cos(a),n)==i,"each native slot is reachable");
        check(Pick(0,0,n)==-1,"centre cancels");
        const float before=a+step*.499f, after=a+step*.501f;
        check(Pick(std::sin(before),std::cos(before),n)==i,"inside edge selects current sector");
        check(Pick(std::sin(after),std::cos(after),n)==(i+1)%n,"crossing edge selects neighbour immediately");
        check(Pick(std::sin(before),std::cos(before),n)==i,"crossing back immediately restores original sector");
    }
    check(Pick(-.01f,1.f,11)==0,"top sector wraps");
    check(Pick(1,0,0)==-1,"empty inventory cancels");
    check(Pick(std::numeric_limits<float>::quiet_NaN(),1,4)==-1,"invalid direction cancels");
    std::puts("Wheel selection checks passed");
}
