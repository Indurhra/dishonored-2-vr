#include "../src/Src/ViewMapTracker.h"
#include <cstdlib>
#include <cstdio>
void check(bool ok) { if(!ok) { std::puts("Map tracking failed"); std::exit(1); } }
int main() {
    int c1,c2,r1,r2,r3,d1,d2,d3;
    ViewMapTracker<2> t;
    check(t.Remember(&c1,&r1,&d1)); check(t.Remember(&c2,&r1,&d2));
    check(t.Take(&c1,&r2)==nullptr); // unrelated unmap cannot consume anything
    check(!t.Remember(&c1,&r3,&d3)); // overflow preserves both outstanding maps
    check(t.Take(&c2,&r1)==&d2); check(t.Take(&c1,&r1)==&d1);
    check(t.Take(&c1,&r1)==nullptr); // no stale reuse
    check(t.Remember(&c1,&r1,&d1)); check(t.Remember(&c1,&r2,&d2));
    check(t.Take(&c1,&r1)==&d1); check(t.Take(&c1,&r2)==&d2);
    check(t.Remember(&c1,&r1,&d3)); check(t.Take(&c1,&r1)==&d3);
}
