#define NOMINMAX
#include <Windows.h>
#include <d3dcompiler.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <wrl/client.h>

#include "../include/D3DRenderer.h"
#include "../shaders/RayTracingHlslCompat.h"

#pragma comment(lib, "d3dcompiler.lib")

using namespace DirectX;
using Microsoft::WRL::ComPtr;

D3DRenderer::D3DRenderer(HWND hwnd)
    : m_hwnd(hwnd), m_width(0), m_height(0), m_fenceValue(0),
      m_fenceEvent(nullptr), m_frameIndex(0), m_rtvDescriptorSize(0),
      m_constantBufferData(nullptr), m_indexCount(0), m_rotationAngle(0.0f),
      m_cameraPos({0, 5, -10}), m_cameraYaw(0), m_cameraPitch(0) {
  RECT rect;
  GetClientRect(hwnd, &rect);
  m_width = rect.right - rect.left;
  m_height = rect.bottom - rect.top;

  InitializeD3D12();
  CreateResources();
  InitRayTracing();

  // Initialize ImGui
  m_imgui.Initialize(hwnd, m_device.Get(), FrameCount,
                     DXGI_FORMAT_R8G8B8A8_UNORM);
}

D3DRenderer::~D3DRenderer() {
  WaitForPreviousFrame();

  if (m_constantBuffer && m_constantBufferData) {
    D3D12_RANGE range = {0, 0};
    m_constantBuffer->Unmap(0, &range);
    m_constantBufferData = nullptr;
  }

  if (m_fenceEvent) {
    CloseHandle(m_fenceEvent);
    m_fenceEvent = nullptr;
  }
}

void D3DRenderer::InitializeD3D12() {
  UINT dxgiFactoryFlags = 0;

#ifdef _DEBUG
  {
    ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
      debugController->EnableDebugLayer();
      dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }
  }
#endif

  if (FAILED(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&m_factory)))) {
    throw std::runtime_error("Failed to create DXGI factory");
  }

  ComPtr<IDXGIAdapter1> adapter;
  for (UINT adapterIndex = 0;
       DXGI_ERROR_NOT_FOUND != m_factory->EnumAdapters1(adapterIndex, &adapter);
       ++adapterIndex) {
    DXGI_ADAPTER_DESC1 desc;
    adapter->GetDesc1(&desc);

    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
      continue;
    }

    if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                    _uuidof(ID3D12Device), nullptr))) {
      break;
    }
  }

  if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                               IID_PPV_ARGS(&m_device)))) {
    throw std::runtime_error("Failed to create D3D12 device");
  }

  D3D12_COMMAND_QUEUE_DESC queueDesc = {};
  queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

  if (FAILED(m_device->CreateCommandQueue(&queueDesc,
                                          IID_PPV_ARGS(&m_commandQueue)))) {
    throw std::runtime_error("Failed to create command queue");
  }

  DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
  swapChainDesc.BufferCount = FrameCount;
  swapChainDesc.Width = m_width;
  swapChainDesc.Height = m_height;
  swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  swapChainDesc.SampleDesc.Count = 1;

  ComPtr<IDXGISwapChain1> swapChain1;
  if (FAILED(m_factory->CreateSwapChainForHwnd(m_commandQueue.Get(), m_hwnd,
                                               &swapChainDesc, nullptr, nullptr,
                                               &swapChain1))) {
    throw std::runtime_error("Failed to create swap chain");
  }

  if (FAILED(swapChain1.As(&m_swapChain))) {
    throw std::runtime_error("Failed to query swap chain");
  }

  m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

  D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
  rtvHeapDesc.NumDescriptors = FrameCount;
  rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

  if (FAILED(m_device->CreateDescriptorHeap(&rtvHeapDesc,
                                            IID_PPV_ARGS(&m_rtvHeap)))) {
    throw std::runtime_error("Failed to create RTV descriptor heap");
  }

  m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

  D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
      m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
  for (UINT n = 0; n < FrameCount; n++) {
    if (FAILED(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])))) {
      throw std::runtime_error("Failed to get swap chain buffer");
    }
    m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr,
                                     rtvHandle);
    rtvHandle.ptr += m_rtvDescriptorSize;
  }

  if (FAILED(m_device->CreateCommandAllocator(
          D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)))) {
    throw std::runtime_error("Failed to create command allocator");
  }

  if (FAILED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         m_commandAllocator.Get(), nullptr,
                                         IID_PPV_ARGS(&m_commandList)))) {
    throw std::runtime_error("Failed to create command list");
  }

  m_commandList->Close();

  if (FAILED(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                   IID_PPV_ARGS(&m_fence)))) {
    throw std::runtime_error("Failed to create fence");
  }

  m_fenceValue = 1;
  m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
  if (m_fenceEvent == nullptr) {
    throw std::runtime_error("Failed to create fence event");
  }
}

void D3DRenderer::CreateResources() {
  // Define geometry
  std::vector<Vertex> vertices;
  std::vector<UINT> indices;

  // Sphere
  m_sphereVertexOffset = 0;
  m_sphereIndexOffset = 0;
  CreateSphere(vertices, indices, 0.5f, 32, 32);
  m_sphereIndexCount = (UINT)indices.size();

  // Plane
  m_planeVertexOffset = (UINT)vertices.size();
  m_planeIndexOffset = (UINT)indices.size();
  CreatePlane(vertices, indices, 20.0f, 20.0f);
  m_planeIndexCount = (UINT)indices.size() - m_planeIndexOffset;

  // Update totals
  m_vertexCount = (UINT)vertices.size();
  m_indexCount = (UINT)indices.size();

  m_sphereIndexCount =
      m_planeIndexOffset; // Correction: indices.size() includes sphere indices
  // Actually simpler:
  // sphereIndices = current indices size (before plane)
  // planeIndices = total - sphere
  // Let's rewrite carefully.

  // Re-clearing for safety from earlier thought
  vertices.clear();
  indices.clear();

  // 1. Sphere
  m_sphereVertexOffset = 0;
  m_sphereIndexOffset = 0;
  CreateSphere(vertices, indices, 0.5f, 32, 32);
  m_sphereIndexCount = (UINT)indices.size();

  // 2. Plane
  m_planeVertexOffset = (UINT)vertices.size();
  m_planeIndexOffset = (UINT)indices.size();
  CreatePlane(vertices, indices, 20.0f, 20.0f); // Adds to end
  m_planeIndexCount = (UINT)indices.size() - m_planeIndexOffset;

  UINT vertexBufferSize = (UINT)(vertices.size() * sizeof(Vertex));
  D3D12_HEAP_PROPERTIES heapProps = {};
  heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

  D3D12_RESOURCE_DESC vertexBufferDesc = {};
  vertexBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  vertexBufferDesc.Width = vertexBufferSize;
  vertexBufferDesc.Height = 1;
  vertexBufferDesc.DepthOrArraySize = 1;
  vertexBufferDesc.MipLevels = 1;
  vertexBufferDesc.Format = DXGI_FORMAT_UNKNOWN;
  vertexBufferDesc.SampleDesc.Count = 1;
  vertexBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

  if (FAILED(m_device->CreateCommittedResource(
          &heapProps, D3D12_HEAP_FLAG_NONE, &vertexBufferDesc,
          D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
          IID_PPV_ARGS(&m_vertexBuffer)))) {
    throw std::runtime_error("Failed to create vertex buffer");
  }

  void *pVertexDataBegin;
  m_vertexBuffer->Map(0, nullptr, &pVertexDataBegin);
  memcpy(pVertexDataBegin, vertices.data(), vertexBufferSize);
  m_vertexBuffer->Unmap(0, nullptr);

  m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
  m_vertexBufferView.SizeInBytes = vertexBufferSize;
  m_vertexBufferView.StrideInBytes = sizeof(Vertex);

  UINT indexBufferSize = (UINT)(indices.size() * sizeof(UINT));
  D3D12_RESOURCE_DESC indexBufferDesc = vertexBufferDesc;
  indexBufferDesc.Width = indexBufferSize;

  if (FAILED(m_device->CreateCommittedResource(
          &heapProps, D3D12_HEAP_FLAG_NONE, &indexBufferDesc,
          D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
          IID_PPV_ARGS(&m_indexBuffer)))) {
    throw std::runtime_error("Failed to create index buffer");
  }

  void *pIndexDataBegin;
  m_indexBuffer->Map(0, nullptr, &pIndexDataBegin);
  memcpy(pIndexDataBegin, indices.data(), indexBufferSize);
  m_indexBuffer->Unmap(0, nullptr);

  m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
  m_indexBufferView.SizeInBytes = indexBufferSize;
  m_indexBufferView.Format = DXGI_FORMAT_R32_UINT;

  UINT constantBufferSize = (sizeof(ShaderParams) + 255) & ~255;
  D3D12_RESOURCE_DESC constantBufferDesc = vertexBufferDesc;
  constantBufferDesc.Width = constantBufferSize;

  if (FAILED(m_device->CreateCommittedResource(
          &heapProps, D3D12_HEAP_FLAG_NONE, &constantBufferDesc,
          D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
          IID_PPV_ARGS(&m_constantBuffer)))) {
    throw std::runtime_error("Failed to create constant buffer");
  }

  m_constantBuffer->Map(0, nullptr, &m_constantBufferData);

  D3D12_ROOT_PARAMETER rootParameter = {};
  rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
  rootParameter.Constants.ShaderRegister = 0;
  rootParameter.Constants.RegisterSpace = 0;
  rootParameter.Constants.Num32BitValues = 16;

  D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
  rootSignatureDesc.NumParameters = 1;
  rootSignatureDesc.pParameters = &rootParameter;
  rootSignatureDesc.Flags =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

  ComPtr<ID3DBlob> signatureBlob;
  ComPtr<ID3DBlob> errorBlob;
  if (FAILED(D3D12SerializeRootSignature(&rootSignatureDesc,
                                         D3D_ROOT_SIGNATURE_VERSION_1,
                                         &signatureBlob, &errorBlob))) {
    if (errorBlob) {
      std::cerr << "Root signature serialization failed: "
                << (char *)errorBlob->GetBufferPointer() << std::endl;
    }
    throw std::runtime_error("Failed to serialize root signature");
  }

  if (FAILED(m_device->CreateRootSignature(0, signatureBlob->GetBufferPointer(),
                                           signatureBlob->GetBufferSize(),
                                           IID_PPV_ARGS(&m_rootSignature)))) {
    throw std::runtime_error("Failed to create root signature");
  }

  ComPtr<ID3DBlob> vertexShader;
  ComPtr<ID3DBlob> pixelShader;

#ifdef _DEBUG
  UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
  UINT compileFlags = 0;
#endif

  if (FAILED(D3DCompileFromFile(
          L"shaders/shader.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
          "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, &errorBlob))) {
    if (errorBlob) {
      std::cerr << "Vertex shader compilation failed: "
                << (char *)errorBlob->GetBufferPointer() << std::endl;
    }
    throw std::runtime_error("Failed to compile vertex shader");
  }
  if (FAILED(D3DCompileFromFile(
          L"shaders/shader.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
          "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, &errorBlob))) {
    if (errorBlob) {
      std::cerr << "Pixel shader compilation failed: "
                << (char *)errorBlob->GetBufferPointer() << std::endl;
    }
    throw std::runtime_error("Failed to compile pixel shader");
  }

  D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};

  D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.InputLayout = {inputElementDescs, _countof(inputElementDescs)};
  psoDesc.pRootSignature = m_rootSignature.Get();
  psoDesc.VS = {vertexShader->GetBufferPointer(),
                vertexShader->GetBufferSize()};
  psoDesc.PS = {pixelShader->GetBufferPointer(), pixelShader->GetBufferSize()};
  psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
  psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask =
      D3D12_COLOR_WRITE_ENABLE_ALL;
  psoDesc.DepthStencilState.DepthEnable = FALSE;
  psoDesc.DepthStencilState.StencilEnable = FALSE;
  psoDesc.SampleMask = UINT_MAX;
  psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.NumRenderTargets = 1;
  psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  psoDesc.SampleDesc.Count = 1;

  if (FAILED(m_device->CreateGraphicsPipelineState(
          &psoDesc, IID_PPV_ARGS(&m_pipelineState)))) {
    throw std::runtime_error("Failed to create graphics pipeline state");
  }
  CreateConstantBuffer();
}

void D3DRenderer::Render() {
  // Start ImGui frame
  m_imgui.BeginFrame();

  // Animation scaling
  auto &ui = m_imgui.GetState();
  if (ui.animationEnabled) {
    m_animationTime += 0.016f * ui.animationSpeed; // ~60fps
  }

  UpdateCamera();
  PopulateCommandList();
  Present();
  WaitForPreviousFrame();
}

void D3DRenderer::PopulateCommandList() {
  m_commandAllocator->Reset();

  // 1. Rebuild TLAS for animation
  m_dxrCommandList->Reset(m_commandAllocator.Get(), nullptr);
  CreateTopLevelAS(m_dxrCommandList.Get());
  m_dxrCommandList->Close();
  ID3D12CommandList *ppCommandLists[] = {m_dxrCommandList.Get()};
  m_commandQueue->ExecuteCommandLists(1, ppCommandLists);
  WaitForPreviousFrame();

  // 2. Main Ray Tracing Pass
  m_commandList->Reset(m_commandAllocator.Get(), nullptr);

  // Set Descriptor Heaps
  ID3D12DescriptorHeap *heaps[] = {m_srvUavHeap.Get()};
  m_commandList->SetDescriptorHeaps(1, heaps);

  // Transition Output to UAV
  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = m_outputResource.Get();
  barrier.Transition.StateBefore =
      D3D12_RESOURCE_STATE_COPY_SOURCE; // Assumed state from previous frame
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  m_commandList->ResourceBarrier(1, &barrier);

  // Dispatch Rays
  D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};
  dispatchDesc.RayGenerationShaderRecord.StartAddress =
      m_shaderTable->GetGPUVirtualAddress();
  dispatchDesc.RayGenerationShaderRecord.SizeInBytes = m_shaderTableEntrySize;

  dispatchDesc.MissShaderTable.StartAddress =
      m_shaderTable->GetGPUVirtualAddress() + m_shaderTableEntrySize;
  dispatchDesc.MissShaderTable.SizeInBytes = m_shaderTableEntrySize;
  dispatchDesc.MissShaderTable.StrideInBytes = m_shaderTableEntrySize;

  dispatchDesc.HitGroupTable.StartAddress =
      m_shaderTable->GetGPUVirtualAddress() + m_shaderTableEntrySize * 2;
  dispatchDesc.HitGroupTable.SizeInBytes = m_shaderTableEntrySize;
  dispatchDesc.HitGroupTable.StrideInBytes = m_shaderTableEntrySize;

  dispatchDesc.Width = m_width;
  dispatchDesc.Height = m_height;
  dispatchDesc.Depth = 1;

  m_dxrCommandList->SetPipelineState1(m_dxrStateObject.Get());
  m_dxrCommandList->SetComputeRootSignature(m_dxrGlobalRootSignature.Get());

  // Bind Acceleration Structure (t0 - Root Parameter 0)
  m_dxrCommandList->SetComputeRootShaderResourceView(
      0, m_topLevelAS->GetGPUVirtualAddress());

  // Bind Output UAV (u0 - Root Parameter 1 - Descriptor Table)
  D3D12_GPU_DESCRIPTOR_HANDLE uavHandle =
      m_srvUavHeap->GetGPUDescriptorHandleForHeapStart();
  uavHandle.ptr += m_device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); // Index 1
  m_dxrCommandList->SetComputeRootDescriptorTable(1, uavHandle);

  // Bind Constants
  if (m_cameraBuffer) {
    m_dxrCommandList->SetComputeRootConstantBufferView(
        2, m_cameraBuffer->GetGPUVirtualAddress());
  }

  m_dxrCommandList->DispatchRays(&dispatchDesc);

  // Transition Output to Copy Source
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  m_commandList->ResourceBarrier(1, &barrier);

  // Transition Backbuffer to Copy Dest
  barrier.Transition.pResource = m_renderTargets[m_frameIndex].Get();
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
  m_commandList->ResourceBarrier(1, &barrier);

  // Copy
  m_commandList->CopyResource(m_renderTargets[m_frameIndex].Get(),
                              m_outputResource.Get());

  // Transition Backbuffer to Render Target for ImGui
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
  m_commandList->ResourceBarrier(1, &barrier);

  // Render ImGui
  D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
      m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
  rtvHandle.ptr += (m_frameIndex * m_rtvDescriptorSize);
  m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

  m_imgui.EndFrame(m_commandList.Get());

  // Transition Backbuffer to Present
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
  m_commandList->ResourceBarrier(1, &barrier);

  m_commandList->Close();
}

void D3DRenderer::Present() {
  ID3D12CommandList *ppCommandLists[] = {m_commandList.Get()};
  m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

  m_swapChain->Present(1, 0);
}

void D3DRenderer::WaitForPreviousFrame() {
  const UINT64 fence = m_fenceValue;
  m_commandQueue->Signal(m_fence.Get(), fence);
  m_fenceValue++;

  if (m_fence->GetCompletedValue() < fence) {
    m_fence->SetEventOnCompletion(fence, m_fenceEvent);
    WaitForSingleObject(m_fenceEvent, INFINITE);
  }

  m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

// ------------------------------------------------------------------------------------------------
// Ray Tracing Implementation
// ------------------------------------------------------------------------------------------------

void D3DRenderer::InitRayTracing() {
  // 1. Query DXR Device
  if (FAILED(m_device->QueryInterface(IID_PPV_ARGS(&m_dxrDevice)))) {
    throw std::runtime_error("DXR is not supported on this device/system "
                             "(failed to query ID3D12Device5).");
  }

  // 2. Query DXR Command List
  // We can cast the existing command list if it was created on a compute/direct
  // queue that supports it. Our m_commandList is DIRECT, so it should support
  // DXR.
  if (FAILED(m_commandList->QueryInterface(IID_PPV_ARGS(&m_dxrCommandList)))) {
    throw std::runtime_error("Failed to query ID3D12GraphicsCommandList4.");
  }

  D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
  if (SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5,
                                              &options5, sizeof(options5)))) {
    if (options5.RaytracingTier < D3D12_RAYTRACING_TIER_1_0) {
      throw std::runtime_error("Device does not support Ray Tracing Tier 1.0.");
    }
  } else {
    throw std::runtime_error("Failed to check DXR feature support.");
  }

  // Create SRV/UAV/CBV Descriptor Heap
  D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
  heapDesc.NumDescriptors = 10; // TLAS, Output, TopLevelAS, etc.
  heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if (FAILED(m_device->CreateDescriptorHeap(&heapDesc,
                                            IID_PPV_ARGS(&m_srvUavHeap)))) {
    throw std::runtime_error("Failed to create SRV/UAV descriptor heap");
  }

  CreateAccelerationStructures();
  CreateRayTracingOutputResource();
  CreateRayTracingPipeline();
  CreateShaderTables();
}

void D3DRenderer::CreateAccelerationStructures() {
  // Reset command list for initialization work
  m_commandAllocator->Reset();
  m_commandList->Reset(m_commandAllocator.Get(), nullptr);

  // 1. Sphere BLAS
  D3D12_GPU_VIRTUAL_ADDRESS vbBase = m_vertexBuffer->GetGPUVirtualAddress();
  D3D12_GPU_VIRTUAL_ADDRESS ibBase = m_indexBuffer->GetGPUVirtualAddress();
  UINT stride = sizeof(Vertex);

  UINT sphereVertexCount = m_planeVertexOffset; // Since sphere started at 0
  D3D12_GPU_VIRTUAL_ADDRESS sphereVB = vbBase;  // Offset 0
  D3D12_GPU_VIRTUAL_ADDRESS sphereIB = ibBase;  // Offset 0

  m_sphereBLAS =
      CreateBottomLevelAS(m_dxrCommandList.Get(), sphereVB, stride,
                          sphereVertexCount, sphereIB, m_sphereIndexCount);

  // 2. Plane BLAS
  UINT planeVertexCount = m_vertexCount - m_planeVertexOffset;
  D3D12_GPU_VIRTUAL_ADDRESS planeVB = vbBase + m_planeVertexOffset * stride;
  D3D12_GPU_VIRTUAL_ADDRESS planeIB =
      ibBase + m_planeIndexOffset * sizeof(UINT);

  m_planeBLAS =
      CreateBottomLevelAS(m_dxrCommandList.Get(), planeVB, stride,
                          planeVertexCount, planeIB, m_planeIndexCount);

  // 3. TLAS
  CreateTopLevelAS(m_dxrCommandList.Get());

  // Close and execute
  m_commandList->Close();
  ID3D12CommandList *ppCommandLists[] = {m_commandList.Get()};
  m_commandQueue->ExecuteCommandLists(1, ppCommandLists);

  WaitForPreviousFrame(); // Wait for AS build to finish
}

ComPtr<ID3D12Resource> D3DRenderer::CreateBottomLevelAS(
    ID3D12GraphicsCommandList4 *commandList,
    D3D12_GPU_VIRTUAL_ADDRESS vbAddress, UINT vbStride, UINT vertexCount,
    D3D12_GPU_VIRTUAL_ADDRESS ibAddress, UINT indexCount) {

  // Geometry definition
  D3D12_RAYTRACING_GEOMETRY_DESC geomDesc = {};
  geomDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
  geomDesc.Triangles.VertexBuffer.StartAddress = vbAddress;
  geomDesc.Triangles.VertexBuffer.StrideInBytes = vbStride;
  geomDesc.Triangles.VertexCount = vertexCount;
  geomDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
  geomDesc.Triangles.IndexBuffer = ibAddress;
  geomDesc.Triangles.IndexCount = indexCount;
  geomDesc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
  geomDesc.Triangles.Transform3x4 = 0;
  geomDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

  // Build inputs
  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
  inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
  inputs.Flags =
      D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
  inputs.NumDescs = 1;
  inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  inputs.pGeometryDescs = &geomDesc;

  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
  m_dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

  // Scratch Buffer
  D3D12_RESOURCE_DESC scratchDesc = {};
  scratchDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  scratchDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  scratchDesc.Width = info.ScratchDataSizeInBytes;
  scratchDesc.Height = 1;
  scratchDesc.DepthOrArraySize = 1;
  scratchDesc.MipLevels = 1;
  scratchDesc.Format = DXGI_FORMAT_UNKNOWN;
  scratchDesc.SampleDesc.Count = 1;
  scratchDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  scratchDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

  D3D12_HEAP_PROPERTIES defaultHeapProps = {};
  defaultHeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
  defaultHeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
  defaultHeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
  defaultHeapProps.CreationNodeMask = 1;
  defaultHeapProps.VisibleNodeMask = 1;

  // Reuse or create scratch buffer
  if (!m_scratchResource ||
      m_scratchResource->GetDesc().Width < info.ScratchDataSizeInBytes) {
    m_scratchResource.Reset();
    // Add margin to avoid frequent reallocations if sizes slightly differ
    scratchDesc.Width =
        (std::max)(scratchDesc.Width,
                   (UINT64)
                       D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
    m_device->CreateCommittedResource(
        &defaultHeapProps, D3D12_HEAP_FLAG_NONE, &scratchDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
        IID_PPV_ARGS(&m_scratchResource));
  }

  // Result (BLAS)
  D3D12_RESOURCE_DESC asDesc = scratchDesc; // Copy props
  asDesc.Width = info.ResultDataMaxSizeInBytes;
  asDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

  ComPtr<ID3D12Resource> blas;
  m_device->CreateCommittedResource(
      &defaultHeapProps, D3D12_HEAP_FLAG_NONE, &asDesc,
      D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr,
      IID_PPV_ARGS(&blas));

  // Build
  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
  buildDesc.Inputs = inputs;
  buildDesc.DestAccelerationStructureData = blas->GetGPUVirtualAddress();
  buildDesc.ScratchAccelerationStructureData =
      m_scratchResource->GetGPUVirtualAddress();

  commandList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

  // Barrier
  D3D12_RESOURCE_BARRIER uavBarrier = {};
  uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  uavBarrier.UAV.pResource = blas.Get();
  commandList->ResourceBarrier(1, &uavBarrier);

  return blas;
}

void D3DRenderer::CreateTopLevelAS(ID3D12GraphicsCommandList4 *commandList) {
  std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instances;

  // 1. Plane (ID 0)
  {
    D3D12_RAYTRACING_INSTANCE_DESC desc = {};
    desc.Transform[0][0] = desc.Transform[1][1] = desc.Transform[2][2] = 1;
    desc.InstanceID = 0;
    desc.InstanceMask = 0xFF;
    desc.InstanceContributionToHitGroupIndex = 0;
    desc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
    desc.AccelerationStructure = m_planeBLAS->GetGPUVirtualAddress();
    instances.push_back(desc);
  }

  // 2. Main Mirror Sphere (ID 1)
  {
    D3D12_RAYTRACING_INSTANCE_DESC desc = {};
    // Scale 1.5, Pos (0, 1.5, 0)
    desc.Transform[0][0] = 1.5f;
    desc.Transform[0][3] = 0.0f;
    desc.Transform[1][1] = 1.5f;
    desc.Transform[1][3] = 1.5f;
    desc.Transform[2][2] = 1.5f;
    desc.Transform[2][3] = 0.0f;

    desc.InstanceID = 1;
    desc.InstanceMask = 0xFF;
    desc.InstanceContributionToHitGroupIndex = 0;
    desc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
    desc.AccelerationStructure = m_sphereBLAS->GetGPUVirtualAddress();
    instances.push_back(desc);
  }

  // 3. Pastel Balls (ID 2+)
  auto &ui = m_imgui.GetState();
  int numBalls = 50;
  for (int i = 0; i < numBalls; ++i) {
    float angle = ((float)i / numBalls * 6.28f * 2.0f) +
                  (m_animationTime * ui.animationSpeed * 0.5f);
    float orbitRadius = 4.0f + (i % 5) * 2.0f;
    float x = cos(angle) * orbitRadius;
    float z = sin(angle) * orbitRadius;
    float bounce =
        abs(sin(m_animationTime * ui.animationSpeed * 2.0f + i)) * 2.0f;
    float scale = 0.3f + ((i % 3) * 0.1f);

    D3D12_RAYTRACING_INSTANCE_DESC desc = {};
    desc.Transform[0][0] = scale;
    desc.Transform[0][3] = x;
    desc.Transform[1][1] = scale;
    desc.Transform[1][3] = scale + bounce; // Bouncing
    desc.Transform[2][2] = scale;
    desc.Transform[2][3] = z;

    desc.InstanceID = 2 + i;
    desc.InstanceMask = 0xFF;
    desc.InstanceContributionToHitGroupIndex = 0;
    desc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
    desc.AccelerationStructure = m_sphereBLAS->GetGPUVirtualAddress();
    instances.push_back(desc);
  }

  // Upload Instance Descs
  UINT instanceDescSize =
      (UINT)instances.size() * sizeof(D3D12_RAYTRACING_INSTANCE_DESC);

  D3D12_HEAP_PROPERTIES uploadHeapProps = {};
  uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
  uploadHeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
  uploadHeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
  uploadHeapProps.CreationNodeMask = 1;
  uploadHeapProps.VisibleNodeMask = 1;

  D3D12_RESOURCE_DESC bufferDesc = {};
  bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  bufferDesc.Alignment = 0;
  bufferDesc.Width = instanceDescSize;
  bufferDesc.Height = 1;
  bufferDesc.DepthOrArraySize = 1;
  bufferDesc.MipLevels = 1;
  bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
  bufferDesc.SampleDesc.Count = 1;
  bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

  // Release old resource if exists? Or just create new one directly?
  // CreateCommittedResource will overwrite pointer but leak if not released?
  // ComPtr handles this.
  if (FAILED(m_device->CreateCommittedResource(
          &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
          D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
          IID_PPV_ARGS(&m_instanceDescs)))) {
    throw std::runtime_error("Failed to create instance desc buffer");
  }

  void *pData;
  m_instanceDescs->Map(0, nullptr, &pData);
  memcpy(pData, instances.data(), instanceDescSize);
  m_instanceDescs->Unmap(0, nullptr);

  // TLAS Inputs
  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
  inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
  inputs.Flags =
      D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
  inputs.NumDescs = (UINT)instances.size();
  inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  inputs.InstanceDescs = m_instanceDescs->GetGPUVirtualAddress();

  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
  m_dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

  // Re-create scratch if needed (simplified: always create new, or check size)
  D3D12_RESOURCE_DESC scratchDesc = {};
  scratchDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  scratchDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  scratchDesc.Width =
      info.ScratchDataSizeInBytes > 0 ? info.ScratchDataSizeInBytes : 1024;
  scratchDesc.Height = 1;
  scratchDesc.DepthOrArraySize = 1;
  scratchDesc.MipLevels = 1;
  scratchDesc.Format = DXGI_FORMAT_UNKNOWN;
  scratchDesc.SampleDesc.Count = 1;
  scratchDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  scratchDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

  D3D12_HEAP_PROPERTIES defaultHeapProps = {};
  defaultHeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
  defaultHeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
  defaultHeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
  defaultHeapProps.CreationNodeMask = 1;
  defaultHeapProps.VisibleNodeMask = 1;

  m_device->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE,
                                    &scratchDesc,
                                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                    nullptr, IID_PPV_ARGS(&m_scratchTLAS));

  // TLAS Resource
  D3D12_RESOURCE_DESC asDesc = scratchDesc;
  asDesc.Width =
      info.ResultDataMaxSizeInBytes > 0 ? info.ResultDataMaxSizeInBytes : 1024;
  asDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

  m_device->CreateCommittedResource(
      &defaultHeapProps, D3D12_HEAP_FLAG_NONE, &asDesc,
      D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr,
      IID_PPV_ARGS(&m_topLevelAS));

  // Build
  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
  buildDesc.Inputs = inputs;
  buildDesc.DestAccelerationStructureData =
      m_topLevelAS->GetGPUVirtualAddress();
  buildDesc.ScratchAccelerationStructureData =
      m_scratchTLAS->GetGPUVirtualAddress();

  commandList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

  // UAV Barrier for TLAS
  D3D12_RESOURCE_BARRIER uavBarrier = {};
  uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  uavBarrier.UAV.pResource = m_topLevelAS.Get();
  commandList->ResourceBarrier(1, &uavBarrier);
}

void D3DRenderer::CreateRayTracingOutputResource() {
  D3D12_RESOURCE_DESC resDesc = {};
  resDesc.DepthOrArraySize = 1;
  resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  resDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  resDesc.Width = m_width;
  resDesc.Height = m_height;
  resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  resDesc.MipLevels = 1;
  resDesc.SampleDesc.Count = 1;

  D3D12_HEAP_PROPERTIES heapProps = {};
  heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
  heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
  heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
  heapProps.CreationNodeMask = 1;
  heapProps.VisibleNodeMask = 1;

  if (FAILED(m_device->CreateCommittedResource(
          &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
          D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr,
          IID_PPV_ARGS(&m_outputResource)))) {
    throw std::runtime_error("Failed to create ray tracing output resource");
  }

  // Create UAV
  D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
  uavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  uavDesc.Texture2D.MipSlice = 0;
  uavDesc.Texture2D.PlaneSlice = 0;

  D3D12_CPU_DESCRIPTOR_HANDLE uavHeapHandle =
      m_srvUavHeap->GetCPUDescriptorHandleForHeapStart();
  // Index 1 (Index 0 reserved for TLAS)
  uavHeapHandle.ptr += m_device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  m_device->CreateUnorderedAccessView(m_outputResource.Get(), nullptr, &uavDesc,
                                      uavHeapHandle);
}

void D3DRenderer::CreateRayTracingPipeline() {
  // 1. Create Global Root Signature
  D3D12_ROOT_PARAMETER rootParams[3] = {};

  // Slot 0: Acceleration Structure (SRV t0)
  rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  rootParams[0].Descriptor.ShaderRegister = 0;
  rootParams[0].Descriptor.RegisterSpace = 0;
  rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  // Slot 1: Output UAV (UAV u0)
  // For UAV, using a Descriptor Table is often required if not u0-u7 space
  // overlap issues or if binding as Root UAV. Root UAV works for
  // buffers/structured buffers, but for Texture2D UAV, we usually use
  // Descriptor Table.
  D3D12_DESCRIPTOR_RANGE uavRange = {};
  uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  uavRange.NumDescriptors = 1;
  uavRange.BaseShaderRegister = 0;
  uavRange.RegisterSpace = 0;
  uavRange.OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
  rootParams[1].DescriptorTable.pDescriptorRanges = &uavRange;
  rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  // Slot 2: Camera CBV (b0) - Use 32bit constants for simplicity or CBV
  // Let's use CBV Root Descriptor
  rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  rootParams[2].Descriptor.ShaderRegister = 0;
  rootParams[2].Descriptor.RegisterSpace = 0;
  rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
  rootSigDesc.NumParameters = 3;
  rootSigDesc.pParameters = rootParams;
  rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

  ComPtr<ID3DBlob> signatureBlob;
  ComPtr<ID3DBlob> errorBlob;
  if (FAILED(D3D12SerializeRootSignature(&rootSigDesc,
                                         D3D_ROOT_SIGNATURE_VERSION_1,
                                         &signatureBlob, &errorBlob))) {
    throw std::runtime_error("Failed to serialize DXR global root signature");
  }
  if (FAILED(m_device->CreateRootSignature(
          0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(),
          IID_PPV_ARGS(&m_dxrGlobalRootSignature)))) {
    throw std::runtime_error("Failed to create DXR global root signature");
  }

  // 2. Load DXIL Shader
  std::string shaderPath = "D:/repos/D3D12-practice/shaders/RayTracing.dxil";
  std::ifstream shaderFile(shaderPath, std::ios::binary);
  if (!shaderFile.is_open()) {
    throw std::runtime_error("Failed to open " + shaderPath);
  }
  std::vector<char> shaderData((std::istreambuf_iterator<char>(shaderFile)),
                               std::istreambuf_iterator<char>());

  // 3. Create State Object
  // We need to construct D3D12_STATE_OBJECT_DESC manually
  std::vector<D3D12_STATE_SUBOBJECT> subobjects;

  // DXIL Library
  D3D12_EXPORT_DESC exports[] = {
      {L"RayGen", nullptr, D3D12_EXPORT_FLAG_NONE},
      {L"Miss", nullptr, D3D12_EXPORT_FLAG_NONE},
      {L"ClosestHit", nullptr, D3D12_EXPORT_FLAG_NONE}};
  D3D12_DXIL_LIBRARY_DESC dxilLibDesc = {};
  dxilLibDesc.DXILLibrary.pShaderBytecode = shaderData.data();
  dxilLibDesc.DXILLibrary.BytecodeLength = shaderData.size();
  dxilLibDesc.NumExports = _countof(exports);
  dxilLibDesc.pExports = exports;

  D3D12_STATE_SUBOBJECT dxilLibSubObject = {};
  dxilLibSubObject.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
  dxilLibSubObject.pDesc = &dxilLibDesc;
  subobjects.push_back(dxilLibSubObject);

  // Hit Group
  D3D12_HIT_GROUP_DESC hitGroupDesc = {};
  hitGroupDesc.HitGroupExport = L"HitGroup";
  hitGroupDesc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
  hitGroupDesc.ClosestHitShaderImport = L"ClosestHit";

  D3D12_STATE_SUBOBJECT hitGroupSubObject = {};
  hitGroupSubObject.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
  hitGroupSubObject.pDesc = &hitGroupDesc;
  subobjects.push_back(hitGroupSubObject);

  // Shader Config
  D3D12_RAYTRACING_SHADER_CONFIG shaderConfigDesc = {};
  shaderConfigDesc.MaxPayloadSizeInBytes =
      48; // float4 + float3 + float3 + uint + bool
  shaderConfigDesc.MaxAttributeSizeInBytes = 8; // float2 barycentrics

  D3D12_STATE_SUBOBJECT shaderConfigSubObject = {};
  shaderConfigSubObject.Type =
      D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
  shaderConfigSubObject.pDesc = &shaderConfigDesc;
  subobjects.push_back(shaderConfigSubObject);

  // Pipeline Config
  D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfigDesc = {};
  pipelineConfigDesc.MaxTraceRecursionDepth = 5;

  D3D12_STATE_SUBOBJECT pipelineConfigSubObject = {};
  pipelineConfigSubObject.Type =
      D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
  pipelineConfigSubObject.pDesc = &pipelineConfigDesc;
  subobjects.push_back(pipelineConfigSubObject);

  // Global Root Signature
  D3D12_GLOBAL_ROOT_SIGNATURE globalRootSigDesc = {};
  globalRootSigDesc.pGlobalRootSignature = m_dxrGlobalRootSignature.Get();

  D3D12_STATE_SUBOBJECT globalRootSigSubObject = {};
  globalRootSigSubObject.Type =
      D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
  globalRootSigSubObject.pDesc = &globalRootSigDesc;
  subobjects.push_back(globalRootSigSubObject);

  // Create
  D3D12_STATE_OBJECT_DESC stateObjectDesc = {};
  stateObjectDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
  stateObjectDesc.NumSubobjects = static_cast<UINT>(subobjects.size());
  stateObjectDesc.pSubobjects = subobjects.data();

  if (FAILED(m_dxrDevice->CreateStateObject(&stateObjectDesc,
                                            IID_PPV_ARGS(&m_dxrStateObject)))) {
    throw std::runtime_error("Failed to create DXR State Object");
  }
}
void D3DRenderer::CreateShaderTables() {
  ComPtr<ID3D12StateObjectProperties> stateObjectProps;
  if (FAILED(m_dxrStateObject.As(&stateObjectProps))) {
    throw std::runtime_error("Failed to query ID3D12StateObjectProperties");
  }

  void *rayGenID = stateObjectProps->GetShaderIdentifier(L"RayGen");
  void *missID = stateObjectProps->GetShaderIdentifier(L"Miss");
  void *hitGroupID = stateObjectProps->GetShaderIdentifier(L"HitGroup");

  UINT shaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
  m_shaderTableEntrySize =
      64; // Align to 64 bytes for simplicity and compatibility with
          // StartAddress alignment requirements

  // Buffer size: 3 records
  UINT bufferSize = m_shaderTableEntrySize * 3;

  D3D12_HEAP_PROPERTIES uploadHeapProps = {};
  uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
  uploadHeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
  uploadHeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
  uploadHeapProps.CreationNodeMask = 1;
  uploadHeapProps.VisibleNodeMask = 1;

  D3D12_RESOURCE_DESC bufferDesc = {};
  bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  bufferDesc.Alignment = 0;
  bufferDesc.Width = bufferSize;
  bufferDesc.Height = 1;
  bufferDesc.DepthOrArraySize = 1;
  bufferDesc.MipLevels = 1;
  bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
  bufferDesc.SampleDesc.Count = 1;
  bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

  if (FAILED(m_device->CreateCommittedResource(
          &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
          D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
          IID_PPV_ARGS(&m_shaderTable)))) {
    throw std::runtime_error("Failed to create shader table buffer");
  }

  uint8_t *pData;
  m_shaderTable->Map(0, nullptr, reinterpret_cast<void **>(&pData));

  // 1. RayGen
  memcpy(pData, rayGenID, shaderIdentifierSize);

  // 2. Miss
  memcpy(pData + m_shaderTableEntrySize, missID, shaderIdentifierSize);

  // 3. HitGroup
  memcpy(pData + m_shaderTableEntrySize * 2, hitGroupID, shaderIdentifierSize);

  m_shaderTable->Unmap(0, nullptr);
}

void D3DRenderer::CreateSphere(std::vector<Vertex> &vertices,
                               std::vector<UINT> &indices, float radius,
                               int sliceCount, int stackCount) {
  Vertex topVertex = {
      {0.0f, radius, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}};
  Vertex bottomVertex = {
      {0.0f, -radius, 0.0f}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}};

  vertices.push_back(topVertex);

  float phiStep = XM_PI / stackCount;
  float thetaStep = 2.0f * XM_PI / sliceCount;

  for (int i = 1; i <= stackCount - 1; ++i) {
    float phi = i * phiStep;

    for (int j = 0; j <= sliceCount; ++j) {
      float theta = j * thetaStep;

      Vertex v;
      v.position.x = radius * sinf(phi) * cosf(theta);
      v.position.y = radius * cosf(phi);
      v.position.z = radius * sinf(phi) * sinf(theta);

      XMVECTOR p = XMLoadFloat3(&v.position);
      XMStoreFloat3(&v.normal, XMVector3Normalize(p));

      v.color = {1.0f, 1.0f, 1.0f};

      vertices.push_back(v);
    }
  }

  vertices.push_back(bottomVertex);

  for (int i = 1; i <= sliceCount; ++i) {
    indices.push_back(0);
    indices.push_back(i + 1);
    indices.push_back(i);
  }

  int baseIndex = 1;
  int ringVertexCount = sliceCount + 1;
  for (int i = 0; i < stackCount - 2; ++i) {
    for (int j = 0; j < sliceCount; ++j) {
      indices.push_back(baseIndex + i * ringVertexCount + j);
      indices.push_back(baseIndex + i * ringVertexCount + j + 1);
      indices.push_back(baseIndex + (i + 1) * ringVertexCount + j);

      indices.push_back(baseIndex + (i + 1) * ringVertexCount + j);
      indices.push_back(baseIndex + i * ringVertexCount + j + 1);
      indices.push_back(baseIndex + (i + 1) * ringVertexCount + j + 1);
    }
  }

  int southPoleIndex = (int)vertices.size() - 1;
  baseIndex = southPoleIndex - ringVertexCount;

  for (int i = 0; i < sliceCount; ++i) {
    indices.push_back(southPoleIndex);
    indices.push_back(baseIndex + i);
    indices.push_back(baseIndex + i + 1);
  }
}

void D3DRenderer::CreatePlane(std::vector<Vertex> &vertices,
                              std::vector<UINT> &indices, float width,
                              float depth) {
  float hw = width * 0.5f;
  float hd = depth * 0.5f;

  Vertex v[4];
  v[0] = {{-hw, -0.5f, -hd}, {0.0f, 1.0f, 0.0f}, {0.5f, 0.5f, 0.5f}};
  v[1] = {{-hw, -0.5f, hd}, {0.0f, 1.0f, 0.0f}, {0.5f, 0.5f, 0.5f}};
  v[2] = {{hw, -0.5f, hd}, {0.0f, 1.0f, 0.0f}, {0.5f, 0.5f, 0.5f}};
  v[3] = {{hw, -0.5f, -hd}, {0.0f, 1.0f, 0.0f}, {0.5f, 0.5f, 0.5f}};

  int baseIndex = (int)vertices.size();
  for (auto &vert : v)
    vertices.push_back(vert);

  indices.push_back(baseIndex + 0);
  indices.push_back(baseIndex + 1);
  indices.push_back(baseIndex + 2);
  indices.push_back(baseIndex + 0);
  indices.push_back(baseIndex + 2);
  indices.push_back(baseIndex + 3);
}

void D3DRenderer::CreateConstantBuffer() {
  UINT bufferSize = (sizeof(ShaderParams) + 255) & ~255;

  D3D12_HEAP_PROPERTIES uploadHeapProps = {};
  uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
  uploadHeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
  uploadHeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
  uploadHeapProps.CreationNodeMask = 1;
  uploadHeapProps.VisibleNodeMask = 1;

  D3D12_RESOURCE_DESC bufferDesc = {};
  bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  bufferDesc.Alignment = 0;
  bufferDesc.Width = bufferSize;
  bufferDesc.Height = 1;
  bufferDesc.DepthOrArraySize = 1;
  bufferDesc.MipLevels = 1;
  bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
  bufferDesc.SampleDesc.Count = 1;
  bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

  if (FAILED(m_device->CreateCommittedResource(
          &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
          D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
          IID_PPV_ARGS(&m_cameraBuffer)))) {
    throw std::runtime_error("Failed to create camera constant buffer");
  }

  if (FAILED(m_cameraBuffer->Map(0, nullptr, &m_cameraMappedData))) {
    throw std::runtime_error("Failed to map camera constant buffer");
  }
}

void D3DRenderer::UpdateCamera() {
  float speed = 0.1f;
  if (GetAsyncKeyState(VK_SHIFT))
    speed *= 3.0f;

  // Rotation (Right Click)
  if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) {
    POINT curPos;
    GetCursorPos(&curPos);
    if (m_lastMousePos.x != 0 || m_lastMousePos.y != 0) {
      float dx = (float)(curPos.x - m_lastMousePos.x);
      float dy = (float)(curPos.y - m_lastMousePos.y);
      m_cameraYaw += dx * 0.005f;
      m_cameraPitch += dy * 0.005f;
    }
    m_lastMousePos = curPos;
  } else {
    m_lastMousePos = {0, 0};
  }

  // Move
  XMMATRIX rotation =
      XMMatrixRotationRollPitchYaw(m_cameraPitch, m_cameraYaw, 0);
  XMVECTOR forward = XMVector3TransformCoord(XMVectorSet(0, 0, 1, 0), rotation);
  XMVECTOR right = XMVector3TransformCoord(XMVectorSet(1, 0, 0, 0), rotation);
  XMVECTOR pos = XMLoadFloat3(&m_cameraPos);

  if (GetAsyncKeyState('W') & 0x8000)
    pos += forward * speed;
  if (GetAsyncKeyState('S') & 0x8000)
    pos -= forward * speed;
  if (GetAsyncKeyState('D') & 0x8000)
    pos += right * speed;
  if (GetAsyncKeyState('A') & 0x8000)
    pos -= right * speed;
  if (GetAsyncKeyState('E') & 0x8000)
    pos += XMVectorSet(0, 1, 0, 0) * speed;
  if (GetAsyncKeyState('Q') & 0x8000)
    pos -= XMVectorSet(0, 1, 0, 0) * speed;

  XMStoreFloat3(&m_cameraPos, pos);

  // Update Buffer
  XMVECTOR focus = pos + forward;
  XMVECTOR up = XMVectorSet(0, 1, 0, 0);
  XMMATRIX view = XMMatrixLookAtLH(pos, focus, up);
  XMMATRIX proj = XMMatrixPerspectiveFovLH(XM_PIDIV4, (float)m_width / m_height,
                                           0.1f, 1000.0f);

  ShaderParams cb;
  cb.viewInverse = XMMatrixInverse(nullptr, view);
  cb.projInverse = XMMatrixInverse(nullptr, proj);
  cb.cameraPos = {m_cameraPos.x, m_cameraPos.y, m_cameraPos.z, 1.0f};

  // UI state
  auto &ui = m_imgui.GetState();
  cb.lightPos = {ui.lightPos[0], ui.lightPos[1], ui.lightPos[2], 1.0f};
  cb.maxBounces = ui.bounceCount;
  cb.emissiveIntensity = ui.emissiveIntensity;
  cb.animationTime = m_animationTime;

  if (m_cameraMappedData) {
    memcpy(m_cameraMappedData, &cb, sizeof(ShaderParams));
  }
}
