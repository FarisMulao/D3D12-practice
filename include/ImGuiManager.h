#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

class ImGuiManager {
public:
  ImGuiManager() = default;
  ~ImGuiManager();

  void Initialize(HWND hwnd, ID3D12Device *device, int numFramesInFlight,
                  DXGI_FORMAT rtvFormat);
  void Shutdown();

  void BeginFrame();
  void EndFrame(ID3D12GraphicsCommandList *commandList);

  // UI State - public so D3DRenderer can read these
  struct UIState {
    int bounceCount = 3;
    float lightPos[3] = {10.0f, 10.0f, -10.0f};
    float emissiveIntensity = 2.0f;
    float animationSpeed = 1.0f;
    bool animationEnabled = true;
    bool showUI = true;
  };

  UIState &GetState() { return m_state; }

private:
  ComPtr<ID3D12DescriptorHeap> m_srvHeap;
  UIState m_state;
  bool m_initialized = false;
};
