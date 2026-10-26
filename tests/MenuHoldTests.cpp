#include "../src/Src/MenuHold.h"
#include <cstdlib>
#include <cstdio>
void check(bool b){if(!b){std::puts("menu hold failed");std::exit(1);}}
int main(){
 MenuHold h; double t=0;
 for(int i=0;i<30;i++){check(h.Tick(t,true,false,false)==0);t+=0.1;}
 check(h.Tick(t,true,false,false)==1);
 t+=0.1;check(h.Tick(t,true,true,true)==-1);
 for(int i=0;i<40;i++){t+=0.1;check(h.Tick(t,true,false,false)==0);}
 t+=0.1;check(h.Tick(t,false,false,false)==0);
 for(int i=0;i<15;i++){t+=0.1;check(h.Tick(t,true,false,false)==0);}
 h.Lost();t+=0.1;check(h.Tick(t,true,false,false)==0);
 t+=0.1;h.Tick(t,false,false,false);
 for(int i=0;i<10;i++){t+=0.1;check(h.Tick(t,true,false,false)==0);}
 t+=1;check(h.Tick(t,true,false,false)==0);
 for(int i=0;i<40;i++){t+=0.1;check(h.Tick(t,true,false,false)==0);}
 // Back closes on an edge, not merely because B was held before opening.
 MenuHold b;check(b.Tick(0,false,true,false)==0);
 check(b.Tick(.1,false,true,true)==0);check(b.Tick(.2,false,false,true)==0);
 check(b.Tick(.3,false,true,true)==-1);
}
