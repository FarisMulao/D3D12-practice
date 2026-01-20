#include "D3DRenderer.h"
#include <stdexcept>
#include <iostream>
#include <d3dcompiler.h>

#pragma comment(lib, "d3dcompiler.lib")

D3DRenderer::D3DRenderer(HWND hwnd) :
    m_hwnd(hwnd),
    m_width(0),
    m_height(0),
    m_fenceValue(0),
    m_fenceEvent(nullptr),
    m_frameIndex(0),
    m_rtvDescriptorSize(0),
    m_constantBufferData(nullptr),
    m_indexCount(0),
    m_rotationAngle(0.0f)
{
    RECT rect;
    GetClientRect(hwnd, &rect);
    m_width = rect.right - rect.left;
    m_height = rect.bottom - rect.top;

    InitializeD3D12();
    CreateResources();
}

D3DRenderer::~D3DRenderer()
{
    WaitForPreviousFrame();

    if (m_constantBuffer && m_constantBufferData)
    {
        D3D12_RANGE range = { 0, 0 };
        m_constantBuffer->Unmap(0, &range);
        m_constantBufferData = nullptr;
    }

    if (m_fenceEvent)
    {
        CloseHandle(m_fenceEvent);
        m_fenceEvent = nullptr;
    }
}

void D3DRenderer::InitializeD3D12()
{
    UINT dxgiFactoryFlags = 0;

#ifdef _DEBUG
    {
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
        {
            debugController->EnableDebugLayer();
            dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }
#endif

    if (FAILED(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&m_factory))))
    {
        throw std::runtime_error("Failed to create DXGI factory");
    }

    ComPtr<IDXGIAdapter1> adapter;
    for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != m_factory->EnumAdapters1(adapterIndex, &adapter); ++adapterIndex)
    {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);

        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
        {
            continue;
        }

        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
        {
            break;
        }
    }

    if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device))))
    {
        throw std::runtime_error("Failed to create D3D12 device");
    }

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    if (FAILED(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue))))
    {
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
    if (FAILED(m_factory->CreateSwapChainForHwnd(
        m_commandQueue.Get(),
        m_hwnd,
        &swapChainDesc,
        nullptr,
        nullptr,
        &swapChain1)))
    {
        throw std::runtime_error("Failed to create swap chain");
    }

    if (FAILED(swapChain1.As(&m_swapChain)))
    {
        throw std::runtime_error("Failed to query swap chain");
    }

    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = FrameCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    if (FAILED(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap))))
    {
        throw std::runtime_error("Failed to create RTV descriptor heap");
    }

    m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT n = 0; n < FrameCount; n++)
    {
        if (FAILED(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n]))))
        {
            throw std::runtime_error("Failed to get swap chain buffer");
        }
        m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
        rtvHandle.ptr += m_rtvDescriptorSize;
    }

    if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator))))
    {
        throw std::runtime_error("Failed to create command allocator");
    }

    if (FAILED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), nullptr, IID_PPV_ARGS(&m_commandList))))
    {
        throw std::runtime_error("Failed to create command list");
    }

    m_commandList->Close();

    if (FAILED(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence))))
    {
        throw std::runtime_error("Failed to create fence");
    }

    m_fenceValue = 1;
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (m_fenceEvent == nullptr)
    {
        throw std::runtime_error("Failed to create fence event");
    }
}

void D3DRenderer::CreateResources()
{
    // Define cube vertices
    Vertex vertices[] = {
        // Front face
        { XMFLOAT3(-0.5f, -0.5f,  0.5f), XMFLOAT3(1.0f, 0.0f, 0.0f) },
        { XMFLOAT3( 0.5f, -0.5f,  0.5f), XMFLOAT3(0.0f, 1.0f, 0.0f) },
        { XMFLOAT3( 0.5f,  0.5f,  0.5f), XMFLOAT3(0.0f, 0.0f, 1.0f) },
        { XMFLOAT3(-0.5f,  0.5f,  0.5f), XMFLOAT3(1.0f, 1.0f, 0.0f) },
        // Back face
        { XMFLOAT3(-0.5f, -0.5f, -0.5f), XMFLOAT3(1.0f, 0.0f, 1.0f) },
        { XMFLOAT3( 0.5f, -0.5f, -0.5f), XMFLOAT3(0.0f, 1.0f, 1.0f) },
        { XMFLOAT3( 0.5f,  0.5f, -0.5f), XMFLOAT3(1.0f, 1.0f, 1.0f) },
        { XMFLOAT3(-0.5f,  0.5f, -0.5f), XMFLOAT3(0.5f, 0.5f, 0.5f) },
    };

    // Define cube indices
    UINT indices[] = {
        // Front face
        0, 1, 2,  2, 3, 0,
        // Back face
        4, 6, 5,  6, 4, 7,
        // Left face
        4, 0, 3,  3, 7, 4,
        // Right face
        1, 5, 6,  6, 2, 1,
        // Top face
        3, 2, 6,  6, 7, 3,
        // Bottom face
        4, 5, 1,  1, 0, 4
    };

    m_indexCount = _countof(indices);

    UINT vertexBufferSize = sizeof(vertices);
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

    if (FAILED(m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &vertexBufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_vertexBuffer)))) {
        throw std::runtime_error("Failed to create vertex buffer");
    }

    void* pVertexDataBegin;
    m_vertexBuffer->Map(0, nullptr, &pVertexDataBegin);
    memcpy(pVertexDataBegin, vertices, vertexBufferSize);
    m_vertexBuffer->Unmap(0, nullptr);

    m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vertexBufferView.SizeInBytes = vertexBufferSize;
    m_vertexBufferView.StrideInBytes = sizeof(Vertex);

    UINT indexBufferSize = sizeof(indices);
    D3D12_RESOURCE_DESC indexBufferDesc = vertexBufferDesc;
    indexBufferDesc.Width = indexBufferSize;

    if (FAILED(m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &indexBufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_indexBuffer)))) {
        throw std::runtime_error("Failed to create index buffer");
    }

    void* pIndexDataBegin;
    m_indexBuffer->Map(0, nullptr, &pIndexDataBegin);
    memcpy(pIndexDataBegin, indices, indexBufferSize);
    m_indexBuffer->Unmap(0, nullptr);

    m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
    m_indexBufferView.SizeInBytes = indexBufferSize;
    m_indexBufferView.Format = DXGI_FORMAT_R32_UINT;

    UINT constantBufferSize = (sizeof(ConstantBuffer) + 255) & ~255;
    D3D12_RESOURCE_DESC constantBufferDesc = vertexBufferDesc;
    constantBufferDesc.Width = constantBufferSize;

    if (FAILED(m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &constantBufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_constantBuffer)))) {
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
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> signatureBlob;
    ComPtr<ID3DBlob> errorBlob;
    if (FAILED(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob))) {
        if (errorBlob) {
            std::cerr << "Root signature serialization failed: " << (char*)errorBlob->GetBufferPointer() << std::endl;
        }
        throw std::runtime_error("Failed to serialize root signature");
    }

    if (FAILED(m_device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)))) {
        throw std::runtime_error("Failed to create root signature");
    }

    ComPtr<ID3DBlob> vertexShader;
    ComPtr<ID3DBlob> pixelShader;

#ifdef _DEBUG
    UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    UINT compileFlags = 0;
#endif

    if (FAILED(D3DCompileFromFile(L"shaders/shader.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, &errorBlob))) {
        if (errorBlob) {
            std::cerr << "Vertex shader compilation failed: " << (char*)errorBlob->GetBufferPointer() << std::endl;
        }
        throw std::runtime_error("Failed to compile vertex shader");
    }
    if (FAILED(D3DCompileFromFile(L"shaders/shader.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, &errorBlob))) {
        if (errorBlob) {
            std::cerr << "Pixel shader compilation failed: " << (char*)errorBlob->GetBufferPointer() << std::endl;
        }
        throw std::runtime_error("Failed to compile pixel shader");
    }

    D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
    psoDesc.pRootSignature = m_rootSignature.Get();
    psoDesc.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
    psoDesc.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;

    if (FAILED(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)))) {
        throw std::runtime_error("Failed to create graphics pipeline state");
    }
}

void D3DRenderer::Render()
{
    PopulateCommandList();
    Present();
    WaitForPreviousFrame();
}

void D3DRenderer::PopulateCommandList()
{
    m_commandAllocator->Reset();
    m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get());

    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());

    D3D12_VIEWPORT viewport = { 0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height), D3D12_MIN_DEPTH, D3D12_MAX_DEPTH };
    m_commandList->RSSetViewports(1, &viewport);

    D3D12_RECT scissorRect = { 0, 0, m_width, m_height };
    m_commandList->RSSetScissorRects(1, &scissorRect);

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_renderTargets[m_frameIndex].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    m_commandList->ResourceBarrier(1, &barrier);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtvHandle.ptr += m_frameIndex * m_rtvDescriptorSize;
    m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    const float clearColor[] = { 0.2f, 0.3f, 0.4f, 1.0f };
    m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
    m_commandList->IASetIndexBuffer(&m_indexBufferView);

    m_rotationAngle += 0.01f;
    if (m_rotationAngle > XM_2PI)
    {
        m_rotationAngle -= XM_2PI;
    }

    XMMATRIX world = XMMatrixRotationY(m_rotationAngle);
    XMMATRIX view = XMMatrixLookAtLH(XMVectorSet(0.0f, 2.0f, -5.0f, 0.0f), XMVectorZero(), XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
    XMMATRIX projection = XMMatrixPerspectiveFovLH(XM_PIDIV4, static_cast<float>(m_width) / static_cast<float>(m_height), 0.1f, 100.0f);
    XMMATRIX mvp = world * view * projection;

    ConstantBuffer cb;
    cb.mvp = XMMatrixTranspose(mvp);

    memcpy(m_constantBufferData, &cb, sizeof(cb));
    m_commandList->SetGraphicsRoot32BitConstants(0, 16, &cb, 0);

    m_commandList->DrawIndexedInstanced(m_indexCount, 1, 0, 0, 0);
    
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    m_commandList->ResourceBarrier(1, &barrier);

    m_commandList->Close();
}

void D3DRenderer::Present()
{
    ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    m_swapChain->Present(1, 0);
}

void D3DRenderer::WaitForPreviousFrame()
{
    const UINT64 fence = m_fenceValue;
    m_commandQueue->Signal(m_fence.Get(), fence);
    m_fenceValue++;

    if (m_fence->GetCompletedValue() < fence)
    {
        m_fence->SetEventOnCompletion(fence, m_fenceEvent);
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }

    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}
