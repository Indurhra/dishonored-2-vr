#pragma once
#include <dxgi.h>
#include <string>
struct XRInputState;

//! Drives the OpenXR session from the Present hook (render thread):
//! init once on the game's device, then per frame poll -> begin -> submit the
//! backbuffer as a flat frame (stage 1). Later stages feed real per-eye images.
namespace VRRuntime
{
    void OnPresent(IDXGISwapChain* sc);
    void Shutdown(bool processExit = false);
    //! The OpenXR frame thread (XRSession, the development notes): keep-alive frames while the game sends none, the render
    //! thread's bound on the wait for a frame, the new session for one the runtime stopped and did not offer back.
    void SetKeepAlive(int mode);
    void SetWaitBound(int ms);
    void SetKick(float seconds, bool anyRuntime = false);
    std::string PumpStatus();
    void SetD3DProtect(bool on);

    //! Game camera horizontal/vertical FOV in degrees used for the flat layer
    //! (until the camera hook supplies the real one). Any thread.
    void SetGameFov(float hDeg, float vDeg);

    //! Dumps the backbuffer and the last per-eye images to PNGs in the game
    //! dir on the next Present: DVR_<tag>_frame.png / DVR_<tag>_eyeN.png.
    void RequestCapture(const std::string& tag);
    //! `frames` consecutive frames (max 60) -> DVR_<tag>_bNN.png, copied on the GPU and written after the last one.
    void RequestBurst(int frames, const std::string& tag, int stride = 1);


    //! Latest located head pose (XR LOCAL space; midpoint of the eyes, eye-0 orientation).
    //! `gen` is the recentre generation the pose belongs to (XRSession::RecenterGeneration): when it changes, the pose
    //! jumped because the space moved, not because the head did.
    struct HeadPose { float pos[3] = {}; float quat[4] = { 0, 0, 0, 1 }; bool valid = false; unsigned gen = 0; };
    HeadPose GetHeadPose();

    //! Latest controller input snapshot (synced each frame). Returns false when
    //! the session/input isn't running (callers fall back to real devices).
    bool GetInput(XRInputState& out);
    //! A haptic pulse on a controller (0 left, 1 right). RENDER THREAD only (the Present path: KbmInject::Tick).
    bool Vibrate(int hand, float amplitude, float seconds, float hz = 0.f);
    unsigned long long HapticPulses();

    //! Stage 5: alternating-eye stereo. Each game frame is rendered from ONE eye's
    //! pose (ViewMatrixHook uses GetRenderEyePose()) and submitted to that eye only;
    //! eyes alternate every frame (half temporal rate per eye, full-rate head tracking).
    void SetStereo(bool on);
    bool Stereo();
    //! Eye index the frame currently being rendered must use (0/1), or -1 for mono.
    int  CurrentEye();
    //! Located pose of eye i (XR LOCAL space).
    HeadPose GetEyePose(int i);

    //! How the last frame was laid out for eye i: the game image's size, the content rect it was copied to inside the
    //! eye swapchain, the swapchain size and the eye's fov (radians L R U D). For BodyView. Any thread.
    struct Placement { bool valid = false; unsigned srcW = 0, srcH = 0; int rect[4] = {}; unsigned swap[2] = {}; float fov[4] = {}; };
    Placement GetPlacement(int i);
    //! 1 (default) = the flat frame is drawn at its true angular size in each eye; 0 = the old 1:1 pixel copy.
    void SetOrtho(bool on);
    bool Ortho();
    unsigned long long OrthoDraws();
    //! 1 (default) = the mono layer claims the pose each image was actually rendered from (ViewMatrixHook::RenderPoseXr);
    //! 0 = the head pose located at submit, as before.
    void SetTruePose(bool on);
    bool TruePose();
    void SetPoseSource(int s);   //!< research: 2 executed command lists (default), 1 draws, 0 latest view, 3 the view before the latest
    struct RenderPoseStats { unsigned long long used = 0, fromDraws = 0, gameAxis = 0, behindLatest = 0, phaseFix = 0, fromExec = 0; float lastDiff = 0, maxDiff = 0; };
    RenderPoseStats GetRenderPoseStats();

    struct Status
    {
        bool initialized = false, running = false;
        int state = 0;
        unsigned long long framesSubmitted = 0;
        unsigned eyeW = 0, eyeH = 0;
        std::string runtime, system, error;
    };
    Status GetStatus();
    //! Count of runtime recenters (XrEventDataReferenceSpaceChangePending). Any thread.
    unsigned RecenterEvents();
    unsigned long long LostFrames(int dev);   //!< frames a lost device's last good pose was held: 0 left, 1 right, 2 head
}
