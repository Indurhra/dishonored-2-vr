#pragma once
#include <string>

//! Records where the process dies. A mod that hooks the renderer and the game's entity system gets blamed for
//! every crash, and "it closed the game" is not something that can be debugged from a screenshot -- so write the
//! faulting instruction, the faulting address and a backtrace (module + RVA per frame) into DishonoredVR.log.
//! Two handlers, because they catch different things:
//!   - a vectored handler sees the exception FIRST, before any __except in the engine, which is the only way to see
//!     a fault the game swallows; it only logs and always continues the search.
//!   - the unhandled filter sees the one that actually kills the process, including faults raised on a thread the
//!     vectored handler was rate-limited on.
namespace CrashLog
{
    void Install();
    //! Note what the mod is doing, so the log line before the fault says which subsystem was live.
    void SetPhase(const char* phase);
    //! "<module>+0x<rva>" for an address (or the absolute address outside any module)
    std::string Where(const void* addr);
}
