#pragma once
#include "WheelModel.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <filesystem>
#include <map>
#include <imgui.h>

// Shared by the game and the standalone D3D11 preview. No game hooks here.
class WheelRenderer
{
public:
    bool Initialize(ID3D11Device* device,const std::filesystem::path& assetRoot);
    void Draw(const std::vector<WheelModel::Item>& items,int page,int selected,int equippedId);
    bool Ready() const { return ready_; }
private:
    struct Texture { Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv; unsigned w=0,h=0; };
    std::map<int,Texture> textures_;
    bool attempted_=false,ready_=false;
    ImFont* font_=nullptr;
    bool Load(ID3D11Device* dev,const std::filesystem::path& file,int key);
    bool Image(int key,ImVec2 centre,ImVec2 bounds,ImU32 colour=IM_COL32_WHITE,float angle=0);
    void Text(const std::string& value,ImVec2 centre,float size,ImU32 colour,float wrap=0);
};
