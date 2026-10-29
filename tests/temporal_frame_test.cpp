#include "TemporalFrame.h"
#include <cstdio>
#include <cstdlib>
using namespace TemporalFrame;
static int checks=0;
void Check(bool value,const char* name){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",name);std::exit(1);}}
int main(){
    Stamp left{10,1,0,1920,1080},right{11,1,1,1920,1080},next{12,1,0,1920,1080};
    Check(SameImage(left,left),"same image accepted");
    Check(!SameImage(left,right),"opposite-eye guides refused");
    Check(!SameImage(left,next),"stale same-eye guides refused");
    Check(HistoryUsable(left,next),"same-eye two-tick history accepted");
    Check(!HistoryUsable(right,next),"other-eye temporal history refused");
    Check(HeldUsable(left,right),"previous opposite eye accepted for AFW");
    Check(!HeldUsable(left,next),"same eye cannot be held source");
    auto cut=next;cut.epoch=2;Check(!HistoryUsable(left,cut),"cut resets history");
    auto resize=next;resize.width=1280;Check(!HistoryUsable(left,resize),"resize resets history");
    auto gap=next;gap.serial=20;Check(!HistoryUsable(left,gap),"gap resets history");
    Check(!HistoryUsable(left,left),"duplicate does not advance history");
    Check(!HistoryUsable(next,left),"out-of-order resets history");
    right.serial=13;Check(!HeldUsable(left,right),"stale held source refused");
    Stamp bad{};Check(!SameImage(bad,bad),"unknown provenance refused");
    bad=left;bad.eye=2;Check(!Valid(bad),"invalid eye refused");
    std::printf("PASS %d temporal provenance checks\n",checks);
}
