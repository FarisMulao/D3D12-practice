#pragma once

#include <windows.h>
#include <memory>

class Win32Window;
class D3DRenderer;

class D3D12App
{
public:
    D3D12App(HINSTANCE hInstance, int nCmdShow);
    ~D3D12App();

    void Run();

private:
    std::unique_ptr<Win32Window> m_window;
    std::unique_ptr<D3DRenderer> m_renderer;
};
