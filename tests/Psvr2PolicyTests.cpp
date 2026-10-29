#include "../src/Psvr2/Policy.hpp"
#include "../src/Psvr2/Effects.hpp"
#include <cstdlib>
#include <iostream>
using namespace dvrpsvr2;
void check(bool b) { if(!b) {std::cerr<<"PSVR2 policy check failed\n";std::exit(1);} }
int main() {
    check(Headset("SteamVR/OpenXR","PlayStation VR2"));
    check(Headset("SteamVR/OpenXR","SteamVR/OpenXR : playstation_vr2"));
    check(!Headset("VirtualDesktopXR","Meta Quest 3"));
    check(!Headset("SteamVR/OpenXR","Meta Quest 3"));
    check(!Headset("SteamVR/OpenXR","Unknown"));
    for(auto p:{"Blink","Grapple","PhantasmalLink","Bendtime","DarkVision","DevouringSwarm","Doppelganger","Mesmerize","Possession","ShadowWalk","Windblast"})
        check(PowerClass(std::string(".?AVarkCpntPower")+p+"@@"));
    check(!PowerClass(".?AVarkCpntPowerBlinkFactory@@"));
    check(!PowerClass(".?AVarkCpntHeart@@"));
    check(Choose(Profile::Gun,true,true,true,false,0)==Profile::Gun);
    check(Choose(Profile::Power,true,true,true,false,.2f)==Profile::Power);
    check(Choose(Profile::Power,true,true,true,false,0)==Profile::Off);
    check(Choose(Profile::Gun,false,true,true,false,1)==Profile::Off);
    check(Choose(Profile::Gun,true,false,true,false,1)==Profile::Off);
    check(Choose(Profile::Gun,true,true,false,false,1)==Profile::Off);
    check(Choose(Profile::Gun,true,true,true,true,1)==Profile::Off);
    for(auto p:{Profile::Gun,Profile::Crossbow}) {
        check(!WeaponFire(p,.79f,false)); check(WeaponFire(p,.80f,false));
        check(WeaponFire(p,.75f,true)); check(!WeaponFire(p,.69f,true));
    }
    check(WeaponFire(Profile::Power,.2f,false));
    auto gun=makeCommand(Profile::Gun);
    check(gun.mode==SCE_PAD_TRIGGER_EFFECT_MODE_WEAPON && gun.commandData.weaponParam.startPosition==5 &&
        gun.commandData.weaponParam.endPosition==7 && gun.commandData.weaponParam.strength==5);
    auto bow=makeCommand(Profile::Crossbow);
    check(bow.mode==SCE_PAD_TRIGGER_EFFECT_MODE_WEAPON && bow.commandData.weaponParam.startPosition==2 && bow.commandData.weaponParam.endPosition==3 && bow.commandData.weaponParam.strength==4);
    check(decode(pack(100,Profile::Crossbow,0),101,true).profile==Profile::Crossbow);
    check(decode(pack(100,Profile::Crossbow,1),101,true).profile==Profile::Off);
    auto power=makeCommand(Profile::Power);
    check(power.mode==SCE_PAD_TRIGGER_EFFECT_MODE_VIBRATION && power.commandData.vibrationParam.amplitude==1 && power.commandData.vibrationParam.frequency==60);
    check(decode(pack(100,Profile::Gun,0),101,true).profile==Profile::Gun);
    check(decode(pack(100,Profile::Gun,1),101,true).profile==Profile::Off);
    check(decode(pack(100,Profile::Gun,0),350,true).profile==Profile::Off);
    check(decode(pack(100,Profile::Gun,0),101,false).profile==Profile::Off);
    check(decode(pack(0xfffffff0,Profile::Gun,0),10,true).profile==Profile::Gun);
    check(makeCommand(Profile(255)).mode==SCE_PAD_TRIGGER_EFFECT_MODE_OFF);
}
