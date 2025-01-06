// Tutorial source: https://www.3dgep.com/learning-directx-12-1/

#include "WinIncludes.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>

// #include <d3dx12.h>   // https://github.com/microsoft/DirectX-Headers

#include <algorithm>
#include <cassert>
#include <chrono>

#include "Helpers.h"

const uint8_t                     g_numFrames = 3;        // Number of frames in flight
bool                              g_useWarp = false;      // Whether or not to use Windows Advanced Rasterization Platform (WARP) or not, i.e. software rasterisation. Using
                                                          // WARP gives the programmer access to the full set of advanced rendering features not always available in hardware.

bool                              g_isInitialised = false;

uint32_t                          g_windowWidth = 1280;
uint32_t                          g_windowHeight = 720;

HWND                              g_hWnd;
RECT                              g_windowRect;

ComPtr<ID3D12Device2>             g_device;
ComPtr<ID3D12CommandQueue>        g_commandQueue;
ComPtr<IDXGISwapChain4>           g_swapChain;
ComPtr<ID3D12Resource>            g_backBuffers[g_numFrames];
ComPtr<ID3D12GraphicsCommandList> g_commandList;
ComPtr<ID3D12CommandAllocator>    g_commandAllocators[g_numFrames];
ComPtr<ID3D12DescriptorHeap>      g_RTVDescriptorHeap;
UINT                              g_RTVDescriptorSize;
UINT                              g_currentBackBufferIndex;

ComPtr<ID3D12Fence>               g_fence;
uint64_t                          g_fenceValue = 0;
uint64_t                          g_frameFenceValues[g_numFrames] = {};
HANDLE                            g_fenceEvent;

bool                              g_useVsync = true;
bool                              g_tearingSupported = false;

bool                              g_isFullscreen = false;

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

void ParseCommandLineArguments()
{
  int argc;
  wchar_t** argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);

  for (size_t i = 0; i < argc; ++i)
  {
    if (::wcscmp(argv[i], L"-w") == 0 || ::wcscmp(argv[i], L"--width") == 0)
      g_windowWidth = ::wcstol(argv[++i], nullptr, 10);

    if (::wcscmp(argv[i], L"-h") == 0 || ::wcscmp(argv[i], L"--height") == 0)
      g_windowHeight = ::wcstol(argv[++i], nullptr, 10);

    if (::wcscmp(argv[i], L"-warp") == 0 || ::wcscmp(argv[i], L"--warp") == 0)
      g_useWarp = true;

    // Free memory allocated by CommandLineToArgvW:
    ::LocalFree(argv);
  }
}

void EnableDebugLayer()
{
#if defined(_DEBUG)
  // Enable debug layer before other Dx12 operations to ensure that
  // any possible errors can be caught during startup:
  ComPtr<ID3D12Debug> debugInterface;

  // IID_PPV_ARGS macro is used to 
  DX12_CHECK(D3D12GetDebugInterface(IID_PPV_ARGS(&debugInterface)));
  debugInterface->EnableDebugLayer();
#endif
}

void RegisterWindowClass(HINSTANCE hInst, const wchar_t* windowClassName)
{
  WNDCLASSEXW windowClass = {};

  windowClass.cbSize = sizeof(WNDCLASSEX);                  // Size of this structure, in bytes.
  windowClass.style = CS_HREDRAW | CS_VREDRAW;              // HREDRAW | VREDRAW specify that the window is redrawn on movement or size adjustment to width or height of client.
  windowClass.lpfnWndProc = &WndProc;                       // Function pointer to callback which handles windows events.
  windowClass.cbClsExtra = 0;                               // Extra bytes allocated following the window class structure (unneeded here).
  windowClass.cbWndExtra = 0;                               // As above, but for window instance (also unneeded here).
  windowClass.hInstance = hInst;                            // Handle to instance containing the window procedure for the class.
  windowClass.hIcon = ::LoadIcon(hInst, NULL);              // Handle to class icon (used in taskbar and top-left corner of window), passing NULL or nullptr forces default icon.
  windowClass.hCursor = ::LoadCursor(NULL, IDC_ARROW);      // As above, but for cursor handle.
  windowClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);   // Handle to class background brush (this is some weird shit).
  windowClass.lpszMenuName = NULL;                          // Pointer to null-terminated char string specifying the resource name of the class menu (nullptr 
  windowClass.lpszClassName = windowClassName;              // Pointer to null-terminated char string used to uniquely identify this window class.
  windowClass.hIconSm = ::LoadIcon(hInst, NULL);            // Handle to small icon associated with this window class, passing NULL or nullptr makes system search for hIcon's resource.

  static ATOM atom = ::RegisterClassExW(&windowClass);
  assert(atom > 0);
}

HWND CreateWindow(const wchar_t* windowClassName, HINSTANCE hInst, 
  const wchar_t* windowTitle, uint32_t width, uint32_t height)
{
  // Retrieve dimensions of primary display monitor, in pixels:
  int screenWidth = ::GetSystemMetrics(SM_CXSCREEN);
  int screenHeight = ::GetSystemMetrics(SM_CYSCREEN);

  RECT windowRect = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
  ::AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, false);

  int windowWidth = windowRect.right - windowRect.left;
  int windowHeight = windowRect.bottom - windowRect.top;
  
  int windowX = std::max<int>(0, (screenWidth - windowWidth) / 2);
  int windowY = std::max<int>(0, (screenHeight - windowHeight) / 2);

  // Create window instance:
  HWND hWnd = ::CreateWindowExW(
    NULL,                 // Extended window style of the window being created (possible values: https://learn.microsoft.com/en-us/windows/win32/winmsg/extended-window-styles?redirectedfrom=MSDN)
    windowClassName,      // Null-terminated char string created by previosu call to RegisterClass or RegisterClassEx function.
    windowTitle,          // Window name.
    WS_OVERLAPPEDWINDOW,  // Style of the window being created (possible values: https://learn.microsoft.com/en-us/windows/win32/winmsg/window-styles?redirectedfrom=MSDN)
    windowX,              // Initial horizontal position of the window, in screen coordinates.
    windowY,              // As above, but vertical position.
    windowWidth,          // Initial width, in device units.
    windowHeight,         // As above, but height.
    NULL,                 // Handle to parent window.
    NULL,                 // Handle to a menu or child-window identifier, depending on the window's style.
    hInst,                // Handle to instance of the module to be associated with this window.
    nullptr               // Pointer to value passed to the window via the CREATESTRUCT structure (fuck knows what this does exactly).
  );

  assert(hWnd && "Failed to create window!");

  return hWnd;
}

ComPtr<IDXGIAdapter4> GetAdapter(bool useWarp)
{
  ComPtr<IDXGIFactory4> dxgiFactory;
  UINT createFactoryFlags = 0;
#if defined (_DEBUG)
  createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif

  DX12_CHECK(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory)));

  ComPtr<IDXGIAdapter1> dxgiAdapter1;
  ComPtr<IDXGIAdapter4> dxgiAdapter4;

  if (useWarp)
  {
    DX12_CHECK(dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&dxgiAdapter1)));
    DX12_CHECK(dxgiAdapter1.As(&dxgiAdapter4));
  }
  else
  {
    SIZE_T maxDedicatedVideoMemory = 0;

    for (UINT i = 0; dxgiFactory->EnumAdapters1(i, &dxgiAdapter1) != DXGI_ERROR_NOT_FOUND; ++i)
    {
      // Enumerate available adapters to find ones capable of creating a D3D12 device,
      // keep track of the adapter with the largest dedicated video memory:
      DXGI_ADAPTER_DESC1 dxgiAdapterDesc1;
      dxgiAdapter1->GetDesc1(&dxgiAdapterDesc1);

      if ((dxgiAdapterDesc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0
        && SUCCEEDED(D3D12CreateDevice(dxgiAdapter1.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)) 
        && dxgiAdapterDesc1.DedicatedVideoMemory > maxDedicatedVideoMemory)
      {
        maxDedicatedVideoMemory = dxgiAdapterDesc1.DedicatedVideoMemory;
        DX12_CHECK(dxgiAdapter1.As(&dxgiAdapter4));
      }
    }
  }
  return dxgiAdapter4;
}