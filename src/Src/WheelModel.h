#pragma once
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace WheelModel
{

    struct Item
    {
        int id=-1, ammo=-1, maxAmmo=-1;
        std::string name, primary, secondary;
    };
    struct Art { int icon=0, hero=0; }; // lib_wheel icon / quick_access_wheel illustration IDs
    inline Art Artwork(const Item& item)
    {
        // Declaration paths are stable across languages and inventory ordering.
        const std::string_view p=item.primary, s=item.secondary.empty()?p:std::string_view(item.secondary);
        const auto has=[&](std::string_view part) { return s.find(part)!=s.npos; };
        const bool emily=p.find("emily")!=p.npos;
        const int bow=emily?76:73, pistol=emily?70:67;
        if(has("bolt_regular")) return {36,bow};
        if(has("bolt_tranquilizer")) return {38,bow};
        if(has("bolt_incendiary")) return {40,bow};
        if(has("bolt_flashbang")) return {42,bow};
        if(has("bolt_burning_venom")) return {89,bow};
        if(has("bullet_explosive")) return {53,pistol};
        if(has("ammo/bullet")) return {86,pistol};
        if(has("regular_grenade")) return {49,58};
        if(has("sticky_grenade")) return {51,64};
        if(has("stun_mine")) return {45,49};
        if(has("springrazor")) return {47,55};
        if(has("/heart/")) return {55,46};
        if(has("timeshift") || has("timepiece")) return {57,52};
        if(has("/bendtime/")) return {61,0};
        if(has("/blink/")) return {63,0};
        if(has("/darkvision/")) return {65,0};
        if(has("/doppelganger/")) return {67,0};
        if(has("/mesmerize/")) return {69,0};
        if(has("/possession/")) return {71,0};
        if(has("/devouring_swarm/")) return {73,0};
        if(has("/shadow_walk/")) return {75,0};
        if(has("/windblast/")) return {77,0};
        if(has("/grapple/")) return {79,0};
        if(has("/phantasmallink/")) return {81,0};
        return {}; // Unknown/modded item remains selectable with its native name.
    }
    inline bool SameOrder(const std::vector<Item>& a,const std::vector<Item>& b)
    {
        if(a.size()!=b.size()) return false;
        for(size_t i=0;i<a.size();++i)
            if(a[i].id!=b[i].id || a[i].primary!=b[i].primary || a[i].secondary!=b[i].secondary) return false;
        return true;
    }
    // Counts/names may change live. A changed layout needs a fresh deliberate
    // selection, so releasing a held direction cannot equip a replacement item.
    inline void Reconcile(const std::vector<Item>& oldItems,const std::vector<Item>& newItems,int& page,int& pick,bool& rearm)
    {
        page=0; // All available items share one ring.
        if(!SameOrder(oldItems,newItems)) { pick=-1;rearm=true; }
        if(pick>=(int)newItems.size()) pick=-1;
    }
}
