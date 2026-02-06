#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../include/ImGuiManager.h"
#include <Windows.h>
#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>
#include <stdexcept>
#include <wrl/client.h>


using Microsoft::WRL::ComPtr;

ImGuiManager::~ImGuiManager() {
  if (m_initialized) {
    Shutdown();
  }
}

void ImGuiManager::Initialize(HWND hwnd, ID3D12Device *device,
                              int numFramesInFlight, DXGI_FORMAT rtvFormat) {
  // Create descriptor heap for ImGui
  D3D12_DESCRIPTOR_HEAP_DESC desc = {};
  desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  desc.NumDescriptors = 16;
  desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

  if (FAILED(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_srvHeap)))) {
    throw std::runtime_error("Failed to create ImGui descriptor heap");
  }

  // Initialize ImGui
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  // Setup style
  ImGui::StyleColorsDark();
  ImGuiStyle &style = ImGui::GetStyle();
  style.WindowRounding = 8.0f;
  style.FrameRounding = 4.0f;
  style.GrabRounding = 4.0f;
  style.Alpha = 0.95f;

  // Setup Platform/Renderer backends
  ImGui_ImplWin32_Init(hwnd);
  ImGui_ImplDX12_Init(device, numFramesInFlight, rtvFormat, m_srvHeap.Get(),
                      m_srvHeap->GetCPUDescriptorHandleForHeapStart(),
                      m_srvHeap->GetGPUDescriptorHandleForHeapStart());

  // Force font build by requesting pixel data
  unsigned char *pixels;
  int width, height;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

  m_initialized = true;
}

void ImGuiManager::Shutdown() {
  if (!m_initialized)
    return;

  ImGui_ImplDX12_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext();
  m_initialized = false;
}

void ImGuiManager::BeginFrame() {
  ImGui_ImplDX12_NewFrame();
  ImGui_ImplWin32_NewFrame();
  ImGui::NewFrame();

  // Toggle UI visibility with F1
  if (ImGui::IsKeyPressed(ImGuiKey_F1)) {
    m_state.showUI = !m_state.showUI;
  }

  if (m_state.showUI) {
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, 280), ImGuiCond_FirstUseEver);

    ImGui::Begin("Ray Tracing Controls", &m_state.showUI);

    ImGui::Text("Press F1 to toggle this panel");
    ImGui::Separator();

    // Bounce count
    ImGui::SliderInt("Bounce Count", &m_state.bounceCount, 1, 10);
    ImGui::SetItemTooltip("Number of ray bounces for reflections");

    ImGui::Separator();

    // Light position
    ImGui::Text("Light Position");
    ImGui::SliderFloat("Light X", &m_state.lightPos[0], -20.0f, 20.0f);
    ImGui::SliderFloat("Light Y", &m_state.lightPos[1], 1.0f, 30.0f);
    ImGui::SliderFloat("Light Z", &m_state.lightPos[2], -20.0f, 20.0f);

    ImGui::Separator();

    // Emissive
    ImGui::SliderFloat("Emissive Intensity", &m_state.emissiveIntensity, 0.0f,
                       10.0f);

    ImGui::Separator();

    // Animation
    ImGui::Checkbox("Enable Animation", &m_state.animationEnabled);
    if (m_state.animationEnabled) {
      ImGui::SliderFloat("Animation Speed", &m_state.animationSpeed, 0.1f,
                         5.0f);
    }

    ImGui::Separator();

    // Stats
    ImGui::Text("Performance");
    ImGui::Text("FPS: %.1f (%.2f ms)", ImGui::GetIO().Framerate,
                1000.0f / ImGui::GetIO().Framerate);

    ImGui::End();
  }
}

void ImGuiManager::EndFrame(ID3D12GraphicsCommandList *commandList) {
  ImGui::Render();

  ID3D12DescriptorHeap *heaps[] = {m_srvHeap.Get()};
  commandList->SetDescriptorHeaps(1, heaps);

  ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
}
