#include "D3D12App.h"
#include "Win32Window.h"
#include "D3DRenderer.h"

D3D12App::D3D12App(HINSTANCE hInstance, int nCmdShow)
{
    m_window = std::make_unique<Win32Window>(hInstance, nCmdShow, L"D3D12 Practice", 1280, 720);
    m_renderer = std::make_unique<D3DRenderer>(m_window->GetHWND());
}

D3D12App::~D3D12App() = default;

void D3D12App::Run()
{
    MSG msg = {};
    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            m_renderer->Render();
        }
    }
}
