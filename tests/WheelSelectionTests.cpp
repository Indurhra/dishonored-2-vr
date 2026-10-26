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
    for(int n : {22,24,64}) {
        const float step=2*pi/n;
        for(int i=0;i<n;++i) {
            const float a=(i+1)*step;
            check(Pick(std::sin(a),std::cos(a),n,i)==(i+1)%n,"every neighbour centre remains reachable with resistance");
        }
    }
    Sticky sticky;
    const float step=2*pi/22;
    const auto at=[&](float angle,int prev,double now){return sticky.Update(std::sin(angle),std::cos(angle),22,prev,now);};
    check(at(0,-1,0)==0,"initial selection is immediate");
    check(at(step*.7f,0,.1)==0,"small boundary wobble retains selected item");
    check(at(step,0,.2)==0,"neighbour must settle before switching");
    check(at(step,0,.25)==0,"short wobble does not switch");
    check(at(0,0,.26)==0,"returning resets the candidate timer");
    check(at(step,0,.3)==0,"new attempt starts a fresh timer");
    check(at(step,0,.381)==1,"deliberate settled movement switches");
    check(sticky.Update(0,0,22,1,.4)==-1,"centre cancels immediately without dwell");
    check(at(-step,-1,.5)==21,"all-ring last item selectable directly");
    check(at(0,21,.6)==21 && at(0,21,.681)==0,"sticky selection wraps across top");
    std::puts("Wheel selection checks passed");
}
