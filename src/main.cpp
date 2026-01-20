#include <windows.h>
#include "D3D12App.h"
#include <iostream>

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow)
{
    try
    {
        D3D12App app(hInstance, nCmdShow);
        app.Run();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}
