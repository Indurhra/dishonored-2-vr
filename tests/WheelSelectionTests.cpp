#include "../src/Src/WheelSelection.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <initializer_list>
static void check(bool ok,const char* name) { if(!ok){ std::fprintf(stderr,"FAIL: %s\n",name);std::exit(1); } }
int main()
{
    using namespace WheelSelection;
    for(int n : {1,4,11,24}) for(int i=0;i<n;++i)
    {
        const float a=i*2*pi/n;
        check(Pick(std::sin(a),std::cos(a),n,-1)==i,"each native slot is reachable");
        check(Pick(0,0,n,i)==-1,"centre clears an existing highlight");
    }
    const float boundary=pi/4;
    check(Pick(std::sin(boundary+.05f),std::cos(boundary+.05f),4,0)==0,"boundary jitter retains highlight");
    check(Pick(std::sin(boundary+.15f),std::cos(boundary+.15f),4,0)==1,"intentional motion crosses boundary");
    check(Pick(-.01f,1.f,11,0)==0,"top sector wraps without flicker");
    check(Pick(1,0,0,0)==-1,"empty inventory cancels");
    check(Pick(std::numeric_limits<float>::quiet_NaN(),1,4,1)==-1,"invalid stick cancels");
    std::puts("Wheel selection checks passed");
}
