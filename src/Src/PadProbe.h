#pragma once
//! Research: detours the engine's XInput device Poll (exe+0xE0220) to log what the
//! game parsed from our synthesized state (device struct fields, RTTI class name).
namespace PadProbe
{
    bool Install();
    void Uninstall();
}
