#pragma once

#include "../shaders/RayTracingHlslCompat.h"
#include "ImGuiManager.h"
#include <DirectXMath.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <vector>
#include <wrl/client.h>

class D3DRenderer {
public:
  struct Vertex {
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT3 color;
  };

  D3DRenderer(HWND hwnd);
  ~D3DRenderer();

  void Render();
  void WaitForPreviousFrame();

private:
  void InitializeD3D12();
  void CreateResources();
  void PopulateCommandList();
  void Present();

  HWND m_hwnd;
  int m_width;
  int m_height;

  // D3D12
  Microsoft::WRL::ComPtr<IDXGIFactory4> m_factory;
  Microsoft::WRL::ComPtr<ID3D12Device> m_device;
  Microsoft::WRL::ComPtr<IDXGISwapChain3> m_swapChain;
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_commandQueue;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
  Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_commandAllocator;
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;
  Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
  UINT64 m_fenceValue;
  HANDLE m_fenceEvent;

  UINT m_frameIndex;
  UINT m_rtvDescriptorSize;
  static const UINT FrameCount = 2;
  Microsoft::WRL::ComPtr<ID3D12Resource> m_renderTargets[FrameCount];

  Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipelineState;
  Microsoft::WRL::ComPtr<ID3D12Resource> m_vertexBuffer;
  Microsoft::WRL::ComPtr<ID3D12Resource> m_indexBuffer;
  Microsoft::WRL::ComPtr<ID3D12Resource> m_constantBuffer;
  D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
  D3D12_INDEX_BUFFER_VIEW m_indexBufferView;
  void *m_constantBufferData;
  UINT m_indexCount;
  UINT m_vertexCount;

  // Scene Geometry Offsets
  UINT m_sphereIndexCount;
  UINT m_sphereVertexOffset;
  UINT m_sphereIndexOffset;

  UINT m_planeIndexCount;
  UINT m_planeVertexOffset;
  UINT m_planeIndexOffset;
  float m_rotationAngle;
  void InitRayTracing();
  void CreateAccelerationStructures();
  void CreateRayTracingPipeline();
  void CreateRayTracingOutputResource();
  void CreateShaderTables();

  // DXR
  Microsoft::WRL::ComPtr<ID3D12Device5> m_dxrDevice;
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> m_dxrCommandList;
  Microsoft::WRL::ComPtr<ID3D12StateObject> m_dxrStateObject;
  Microsoft::WRL::ComPtr<ID3D12RootSignature> m_dxrGlobalRootSignature;
  Microsoft::WRL::ComPtr<ID3D12RootSignature> m_dxrLocalRootSignature;

  // Acceleration Structures
  Microsoft::WRL::ComPtr<ID3D12Resource> m_sphereBLAS;
  Microsoft::WRL::ComPtr<ID3D12Resource> m_planeBLAS;
  Microsoft::WRL::ComPtr<ID3D12Resource> m_topLevelAS;
  Microsoft::WRL::ComPtr<ID3D12Resource> m_scratchResource;
  Microsoft::WRL::ComPtr<ID3D12Resource> m_scratchTLAS;
  Microsoft::WRL::ComPtr<ID3D12Resource> m_instanceDescs;

  // Shader Tables
  Microsoft::WRL::ComPtr<ID3D12Resource> m_shaderTable;
  UINT m_shaderTableEntrySize;

  // Output
  Microsoft::WRL::ComPtr<ID3D12Resource> m_outputResource;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvUavHeap;

  // Camera
  DirectX::XMFLOAT3 m_cameraPos;
  float m_cameraYaw;
  float m_cameraPitch;
  POINT m_lastMousePos;
  Microsoft::WRL::ComPtr<ID3D12Resource> m_cameraBuffer;
  void *m_cameraMappedData;

  void UpdateCamera();
  void CreateConstantBuffer();

  // ImGui
  ImGuiManager m_imgui;

  // Animation
  float m_animationTime = 0.0f;

  // Helpers
  Microsoft::WRL::ComPtr<ID3D12Resource>
  CreateBottomLevelAS(ID3D12GraphicsCommandList4 *commandList,
                      D3D12_GPU_VIRTUAL_ADDRESS vbAddress, UINT vbStride,
                      UINT vertexCount, D3D12_GPU_VIRTUAL_ADDRESS ibAddress,
                      UINT indexCount);
  void CreateTopLevelAS(ID3D12GraphicsCommandList4 *commandList);
  void UpdateShaderTable();
  void CreatePlane(std::vector<Vertex> &vertices, std::vector<UINT> &indices,
                   float width, float depth);
  void CreateSphere(std::vector<Vertex> &vertices, std::vector<UINT> &indices,
                    float radius, int sliceCount, int stackCount);
};
