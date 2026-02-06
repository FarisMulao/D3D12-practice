#include "D3D12App.h"
#include <iostream>
#include <windows.h>

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance,
                   _In_ LPSTR lpCmdLine, _In_ int nCmdShow) {
  UNREFERENCED_PARAMETER(hPrevInstance);
  UNREFERENCED_PARAMETER(lpCmdLine);
  try {
    D3D12App app(hInstance, nCmdShow);
    app.Run();
  } catch (const std::exception &e) {
    std::cerr << "Exception: " << e.what() << std::endl;
    MessageBoxA(nullptr, e.what(), "Error", MB_ICONERROR | MB_OK);
    return -1;
  }

  return 0;
}
