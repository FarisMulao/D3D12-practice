#include "Win32Window.h"
#include "D3D12App.h"
#include <stdexcept>

Win32Window::Win32Window(HINSTANCE hInstance, int nCmdShow, const wchar_t* title, int width, int height)
    : m_hInstance(hInstance), m_windowTitle(title)
{
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"D3D12PracticeWindowClass";

    if (!RegisterClassExW(&wc))
    {
        throw std::runtime_error("Failed to register window class");
    }

    RECT rect = { 0, 0, width, height };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    m_hwnd = CreateWindowExW(
        0,
        L"D3D12PracticeWindowClass",
        m_windowTitle.c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left,
        rect.bottom - rect.top,
        nullptr,
        nullptr,
        hInstance,
        this
    );

    if (!m_hwnd)
    {
        throw std::runtime_error("Failed to create window");
    }

    ShowWindow(m_hwnd, nCmdShow);
    UpdateWindow(m_hwnd);
}

Win32Window::~Win32Window()
{
    DestroyWindow(m_hwnd);
}

LRESULT CALLBACK Win32Window::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    Win32Window* pThis = nullptr;
    if (uMsg == WM_NCCREATE)
    {
        CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
        pThis = reinterpret_cast<Win32Window*>(pCreate->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
    }
    else
    {
        pThis = reinterpret_cast<Win32Window*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (pThis)
    {
        return pThis->OnMessage(hwnd, uMsg, wParam, lParam);
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

LRESULT Win32Window::OnMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
