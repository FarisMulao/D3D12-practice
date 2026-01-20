#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <DirectXMath.h>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

class D3DRenderer
{
public:
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
    ComPtr<IDXGIFactory4> m_factory;
    ComPtr<ID3D12Device> m_device;
    ComPtr<IDXGISwapChain3> m_swapChain;
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    ComPtr<ID3D12CommandAllocator> m_commandAllocator;
    ComPtr<ID3D12GraphicsCommandList> m_commandList;
    ComPtr<ID3D12Fence> m_fence;
    UINT64 m_fenceValue;
    HANDLE m_fenceEvent;

    UINT m_frameIndex;
    UINT m_rtvDescriptorSize;
    static const UINT FrameCount = 2;
    ComPtr<ID3D12Resource> m_renderTargets[FrameCount];

    // Cube rendering
    struct Vertex {
        XMFLOAT3 position;
        XMFLOAT3 color;
    };

    struct ConstantBuffer {
        XMMATRIX mvp;
    };

    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pipelineState;
    ComPtr<ID3D12Resource> m_vertexBuffer;
    ComPtr<ID3D12Resource> m_indexBuffer;
    ComPtr<ID3D12Resource> m_constantBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
    D3D12_INDEX_BUFFER_VIEW m_indexBufferView;
    void* m_constantBufferData;
    UINT m_indexCount;
    float m_rotationAngle;
};
