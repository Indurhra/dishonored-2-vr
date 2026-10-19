#pragma once
//! Headless test channel: polls <game>\DVR_cmd.txt (consumed after reading)
//! and writes <game>\DVR_heartbeat.txt once a second with the mod's state.
//! Commands (one per line): capture <tag> | set <cvar> <value> | log <text>
namespace Commands
{
    void OnPresent();   //!< call every frame (cheap; polls at ~10 Hz)
}
