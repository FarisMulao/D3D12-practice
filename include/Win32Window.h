#pragma once

#include <windows.h>
#include <string>

class D3D12App;

class Win32Window
{
public:
    Win32Window(HINSTANCE hInstance, int nCmdShow, const wchar_t* title, int width, int height);
    ~Win32Window();

    HWND GetHWND() const { return m_hwnd; }
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT OnMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

private:
    HWND m_hwnd;
    HINSTANCE m_hInstance;
    std::wstring m_windowTitle;
};
