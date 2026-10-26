#include "../src/Src/WheelHand.h"
#include "../src/Src/WheelSelection.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
void check(bool b,const char* s){if(!b){std::fprintf(stderr,"FAIL %s\n",s);std::exit(1);}}
int main(){
    for(int angle=0;angle<360;angle+=30){
        const float yaw=angle*WheelSelection::pi/180;
        float hand[3]={.1f,1.2f,-.5f};
        float head[3]={hand[0]+.5f*std::sin(yaw),1.6f,hand[2]+.5f*std::cos(yaw)};
        WheelHand d;check(d.Capture(hand,head),"capture opening grip");
        float x,y;check(d.Direction(hand,x,y) && x==0 && y==0,"opening centre cancels");
        for(int slot=0;slot<8;++slot){
            float a=slot*2*WheelSelection::pi/8;
            float moved[3]={hand[0]+.02f*std::sin(a)*d.right[0],hand[1]+.02f*std::cos(a),hand[2]+.02f*std::sin(a)*d.right[2]};
            check(d.Direction(moved,x,y) && WheelSelection::Pick(x,y,8)==slot,"all hand directions select correctly at every opening yaw");
            check(d.center[0]==hand[0] && d.center[1]==hand[1] && d.center[2]==hand[2],"selection does not chase the hand");
        }
        float jitter[3]={hand[0],hand[1]+.001f,hand[2]};
        check(d.Direction(jitter,x,y) && x==0 && y==0,"1 mm tracking jitter is neutral");
        float depth[3]={hand[0]+.1f*std::sin(yaw),hand[1],hand[2]+.1f*std::cos(yaw)};
        check(d.Direction(depth,x,y) && x==0 && y==0,"depth movement cannot choose a segment");
        float bad[3]={std::numeric_limits<float>::quiet_NaN(),1,1};
        check(!d.Direction(bad,x,y) && x==0 && y==0,"invalid pose cannot equip");
    }
    WheelHand d;float p[3]={0,1,0};check(!d.Capture(p,p),"degenerate plane refuses opening");
    std::puts("Opening hand, yaw, eight sectors, deadzone, depth and invalid pose checks passed");
}
