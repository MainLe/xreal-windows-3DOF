#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include "tracking.hpp"
namespace xreal {
template<class T> using Com=Microsoft::WRL::ComPtr<T>;
void check(HRESULT hr,const char* what);
struct Monitor {HMONITOR monitor=nullptr;RECT rect{};std::wstring name;Com<IDXGIAdapter1> adapter;Com<IDXGIOutput> output;bool primary=false,glasses=false;};
std::vector<Monitor> monitors();
struct GraphicsStats {uint64_t rendered=0,captured=0,timeouts=0,restarts=0;double lastCaptureAge=-1;std::wstring capture=L"未开启桌面采集";};
class Graphics {
    HWND window_=nullptr;Monitor source_;
    Com<ID3D11Device> device_;Com<ID3D11DeviceContext> context_;
    Com<IDXGISwapChain1> swap_;Com<ID3D11RenderTargetView> target_;
    Com<ID3D11VertexShader> vs_;Com<ID3D11PixelShader> ps_;Com<ID3D11InputLayout> layout_;
    Com<ID3D11Buffer> vertices_,constants_;Com<ID3D11SamplerState> sampler_;
    Com<ID3D11RasterizerState> raster_;Com<IDXGIOutputDuplication> duplication_;
    Com<ID3D11Texture2D> capture_,checker_,cursorTexture_;Com<ID3D11ShaderResourceView> captureView_,checkerView_,cursorView_;
    DXGI_MODE_ROTATION rotation_=DXGI_MODE_ROTATION_IDENTITY;unsigned sourceW_=1920,sourceH_=1080,width_=0,height_=0;
    uint64_t lastCapture_=0;ULONGLONG nextRecovery_=0;bool grid_=false,frameHeld_=false;
    HCURSOR lastCursor_=nullptr;POINT hotSpot_{};unsigned cursorW_=0,cursorH_=0;bool cursorVisible_=false,separateCursor_=true;
    float cursorX_=0,cursorY_=0;std::vector<uint8_t> cursorPixels_;std::wstring screenshot_;
    void shaders();void duplicate();void acquire();void releaseFrame();void updateCursor();
public:
    GraphicsStats stats;
    Graphics(HWND window,const Monitor& source,bool warp=false);
    ~Graphics(){releaseFrame();}
    void grid(bool enabled){grid_=enabled;}
    void screenshot(std::wstring path){screenshot_=std::move(path);}
    void render(Quat anchor,Quat head,bool tracked,float scale,float distance,float fov,bool stereo);
    void saveFrame(const std::wstring& path);
};
}
