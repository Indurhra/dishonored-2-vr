#include "AfwRecovery.h"
#include <cstdio>
#include <cstdlib>
void Check(bool b){if(!b)std::abort();}
int main(){
 Check(AfwFailureMayRecover(AfwFailure::MissingMatchingFrame,false)); // DLSS still enabled after menu transition
 Check(AfwFailureMayRecover(AfwFailure::MissingDlss,true));
 Check(!AfwFailureMayRecover(AfwFailure::MissingDlss,false));
 Check(!AfwFailureMayRecover(AfwFailure::Hard,true)); // stale DLSS flag cannot retry a backend failure
 Check(!AfwFailureMayRecover(AfwFailure::None,true));
 AfwRecovery r;
 Check(!r.Schedule(0,3,false));Check(!r.Take(100,true));
 Check(r.Schedule(100,3,true));Check(!r.Take(101,true));
 Check(!r.Take(105,false)); // mantle or menu still active: preserve the retry
 Check(r.Take(106,true)&&r.quality==3&&r.attempts==1);Check(!r.Take(107,true));
 Check(r.Schedule(107,3,true));Check(!r.Take(111,true));Check(r.Take(112,true));
 Check(r.Schedule(113,3,true));Check(!r.Take(127,true));Check(r.Take(128,true));
 Check(!r.Schedule(129,3,true)); // persistent fault does not thrash
 for(int i=0;i<120;++i)r.Accepted();Check(r.Schedule(200,3,true));
 r.Cancel();Check(!r.Take(1000,true)); // explicit native renderer wins
 Check(!r.Schedule(1000,5,true));Check(r.Schedule(1000,0,true));Check(r.Take(1002,true)&&r.quality==0);
 std::puts("PASS transient-only AFW recovery, mantle/menu deferral, quality preservation, backoff, exhaustion, success and user cancellation");
}
