#pragma once
#include <cmath>
#include <cstring>

//! Small helpers shared by the camera hooks.
//! OpenXR: +X right, +Y up, -Z forward (meters).   id Tech: +X forward, +Y left, +Z up (Void: meters).
namespace VRMath
{
    inline void XrToId(const float v[3], float out[3]) { out[0] = -v[2]; out[1] = -v[0]; out[2] = v[1]; }

    //! Basis vectors of a quaternion (x,y,z,w) in XR space: fwd(-Z), right(+X), up(+Y).
    inline void QuatBasis(const float q[4], float fwd[3], float right[3], float up[3])
    {
        float x = q[0], y = q[1], z = q[2], w = q[3];
        float m00 = 1 - 2 * (y * y + z * z), m01 = 2 * (x * y - z * w), m02 = 2 * (x * z + y * w);
        float m10 = 2 * (x * y + z * w), m11 = 1 - 2 * (x * x + z * z), m12 = 2 * (y * z - x * w);
        float m20 = 2 * (x * z - y * w), m21 = 2 * (y * z + x * w), m22 = 1 - 2 * (x * x + y * y);
        right[0] = m00; right[1] = m10; right[2] = m20;
        up[0] = m01;    up[1] = m11;    up[2] = m21;
        fwd[0] = -m02;  fwd[1] = -m12;  fwd[2] = -m22;
    }

    //! Composes the game's (body) yaw taken from `axis` with an HMD pose:
    //! axis <- yaw(body) * hmdRotation ; org <- org + yaw(body) * (hmdPos - refPos) * scale.
    //! axis is an idMat3 (rows fwd/left/up). hmdPos/refPos/quat are XR-space.
    inline void ComposeHead(const float hmdPos[3], const float hmdQuat[4], const float refPos[3], float scale,
                            float org[3], float axis[9])
    {
        float yaw = atan2f(axis[1], axis[0]);
        float cy = cosf(yaw), sy = sinf(yaw);
        auto toWorld = [&](const float l[3], float out[3]) {
            out[0] = l[0] * cy - l[1] * sy; out[1] = l[0] * sy + l[1] * cy; out[2] = l[2]; };
        float fx[3], rx[3], ux[3]; QuatBasis(hmdQuat, fx, rx, ux);
        float fl[3], rl[3], ul[3]; XrToId(fx, fl); XrToId(rx, rl); XrToId(ux, ul);
        float fw[3], rw[3], uw[3]; toWorld(fl, fw); toWorld(rl, rw); toWorld(ul, uw);
        axis[0] = fw[0]; axis[1] = fw[1]; axis[2] = fw[2];
        axis[3] = -rw[0]; axis[4] = -rw[1]; axis[5] = -rw[2];
        axis[6] = uw[0]; axis[7] = uw[1]; axis[8] = uw[2];
        float dxr[3] = { hmdPos[0] - refPos[0], hmdPos[1] - refPos[1], hmdPos[2] - refPos[2] };
        float dl[3]; XrToId(dxr, dl); float dw[3]; toWorld(dl, dw);
        org[0] += dw[0] * scale; org[1] += dw[1] * scale; org[2] += dw[2] * scale;
    }
}
