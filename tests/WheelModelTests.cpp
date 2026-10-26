#include "../src/Src/WheelModel.h"
#include <cstdlib>
#include <iostream>
using WheelModel::Item;
void require(bool b,const char* message){if(!b){std::cerr<<message<<'\n';std::exit(1);}}
int main()
{
    Item gun{12,20,20,"Pistolet","models/weapons/gun_player/gun_emily.inventoryitem","components/equipment/ammo/bullet.inventoryitem"};
    auto art=WheelModel::Artwork(gun);
    require(art.icon==86 && art.hero==70,"Localized/upgraded display name must not decide weapon artwork");
    gun.secondary="components/equipment/ammo/bullet_explosive.inventoryitem";
    require(WheelModel::Artwork(gun).icon==53,"Ammo variants need distinct symbols with the same weapon illustration");
    Item unknown{90,2,3,"New modded item","mod/custom.inventoryitem",""};
    require(WheelModel::Artwork(unknown).icon==0,"Unknown items must not inherit another item's icon");
    std::vector<Item> old{gun,unknown},fresh=old;int page=0,pick=1;bool rearm=false;
    fresh[0].ammo=19;fresh[0].name="Renamed upgrade";
    WheelModel::Reconcile(old,fresh,page,pick,rearm);
    require(pick==1 && !rearm,"Live count/name updates must preserve the deliberate selection");
    std::swap(fresh[0],fresh[1]);
    WheelModel::Reconcile(old,fresh,page,pick,rearm);
    require(pick==-1 && rearm,"Reordering while held must require a fresh selection");
    fresh=old;fresh[1].primary="replacement.inventoryitem";pick=1;rearm=false;
    WheelModel::Reconcile(old,fresh,page,pick,rearm);
    require(pick==-1 && rearm,"Reused native ID with different declaration must clear selection");
    page=2;pick=17;rearm=false;fresh.clear();
    WheelModel::Reconcile(old,fresh,page,pick,rearm);
    require(page==0 && pick==-1 && rearm,"Lost inventory must clear selection and clamp the page");
    std::cout<<"Wheel identity, artwork and live inventory reconciliation passed\n";
}
