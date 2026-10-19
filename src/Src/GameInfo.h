#pragma once
#include <cstdint>
#include <string>

//! Facts about the host executable. Every hook in the mod sits at a fixed offset of ONE build of Dishonored2.exe;
//! on any other build those offsets point into unrelated code, so the mod must not touch the game at all there.
namespace GameInfo
{
    // The Steam build. Its file version still reads 1.77.9.0, but the exe was rebuilt on 2025-02-06 (the PE time
    // stamp below), and the GOG build also calls itself 1.77.9 with a different exe (42,591,232 bytes): the version
    // string alone does not identify the build, the link time stamp and the size do.
    constexpr const char* kSupportedVersion = "1.77.9.0";
    constexpr uint64_t kSupportedExeSize = 41134632;
    constexpr uint32_t kSupportedTimeStamp = 0x67A50CDE;

    void Init();
    uintptr_t Base();                //!< Dishonored2.exe image base
    const std::string& Version();    //!< FileVersion string
    bool IsSupportedBuild();         //!< version + size + link time stamp match
    const std::string& Mismatch();   //!< why not (empty when supported), for the log and the message box
    const std::string& ExeName();    //!< file name of the host executable
    const std::wstring& GameDir();   //!< folder containing the exe
}
