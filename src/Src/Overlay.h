#pragma once
#include <dxgi.h>
#include <string>

//! ImGui debug overlay drawn onto the backbuffer right before Present.
//! Toggle with INSERT. While visible, it takes over mouse/keyboard input.
struct XRInputState;
class XRSession;
namespace Overlay
{
    void OnPresent(IDXGISwapChain* sc);   //!< render thread
    void OnResize(IDXGISwapChain* sc);    //!< release RTV before ResizeBuffers
    void Shutdown();
    bool Visible();
    bool CaptureMenu(const std::wstring& path); // explicit diagnostic capture only
    bool ControllerInput(const XRInputState& input, double now);
    void ControllerLost();
    void Panel(XRSession& xr, float degrees, float distance);
    HWND Window();                        //!< the game's window (the swapchain's), null before the first Present
    //! The game window's CLIENT size in physical pixels (= the backbuffer the game renders to). A Quest eye is taller
    //! than it is wide and the render now covers its whole fov (vr fov auto), but Windows caps a window at the desktop,
    //! so the game could only get 1902x973 -- 6 px per degree vertically. This lifts the cap (WM_GETMINMAXINFO) and
    //! resizes the window, which may then extend past the screen's bottom edge; the game follows with ResizeBuffers.
    //! 0 0 = drop the override and go back to the size before the first change.
    void SetWindowClient(int w, int h);
    std::string WindowInfo();
}
