#pragma once
#include <sstream>
#include <string>
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;
namespace LiveWheel
{
    bool Install();
    bool Active();
    bool Enabled();
    bool Closed();
    void RequestOpen();
    void Cancel();
    // Returns true when the binding must be released (cancel/lost tracking).
    bool Input(float x, float y, bool held, bool cancel, bool tracked, int pageDelta);
    // Rendered in its own texture, never over the game's native wheel.
    bool Render(ID3D11Device* device, ID3D11DeviceContext* context);
    ID3D11ShaderResourceView* Texture();
    bool PanelPose(float pos[3], float quat[4]);
    bool Command(const std::string& what, std::istringstream& ss);
    std::string Status();
}
