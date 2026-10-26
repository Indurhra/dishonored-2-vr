#pragma once
#include "../Psvr2/Policy.hpp"
namespace Psvr2Triggers {
    void ObserveEquipment(void* player); // game thread, one current-item read
    dvrpsvr2::Profile Item();
    void Update(dvrpsvr2::Profile desired); // render thread, bounded IPC publish only
    void Stop();
    struct Frame {
        dvrpsvr2::Profile desired=dvrpsvr2::Profile::Off;
        ~Frame() { Update(desired); }
    };
}
