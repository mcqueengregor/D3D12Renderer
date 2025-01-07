// Tutorial source: https://www.3dgep.com/learning-directx-12-1/

#include "WinIncludes.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>

#include "Dx12Headers/d3dx12.h"   // https://github.com/microsoft/DirectX-Headers

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

HWND                              g_hWnd;         // Handle to OS window used to display the back buffer.
RECT                              g_windowRect;   // Used to store previous window dimensions when toggling between windowed and fullscreen modes.

ComPtr<ID3D12Device2>             g_device;
ComPtr<ID3D12CommandQueue>        g_commandQueue;
ComPtr<IDXGISwapChain4>           g_swapChain;
ComPtr<ID3D12Resource>            g_backBuffers[g_numFrames];         // Pointers to swapchain's back buffer resources
ComPtr<ID3D12GraphicsCommandList> g_commandList;                      // Used to record GPU commands (like Vulkan's command pool?)
ComPtr<ID3D12CommandAllocator>    g_commandAllocators[g_numFrames];   // Backing memory for recording GPU commands into command list, one per frame in flight is required.
ComPtr<ID3D12DescriptorHeap>      g_RTVDescriptorHeap;                // Render target view (RTV) object to describe properties of back buffers. (Descriptor heaps are essentially descriptor sets.)
UINT                              g_RTVDescriptorSize;                // Size of a single RTV descriptor, used to correctly index into the descriptor heap.
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

ComPtr<ID3D12Device2> CreateDevice(ComPtr <IDXGIAdapter4> adapter)
{
  ComPtr<ID3D12Device2> d3d12Device2;
  DX12_CHECK(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d3d12Device2)));

#if defined(_DEBUG)
  // Enable debug messages if in debug mode:
  ComPtr<ID3D12InfoQueue> pInfoQueue;
  if (SUCCEEDED(d3d12Device2.As(&pInfoQueue)))
  {
    pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
    pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
    pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);

    D3D12_MESSAGE_SEVERITY severities[] = {
      D3D12_MESSAGE_SEVERITY_INFO,
    };

    D3D12_MESSAGE_ID denyIDs[] = {
      D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
      D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,
      D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,
    };

    D3D12_INFO_QUEUE_FILTER newFilter = {};
    newFilter.DenyList.NumSeverities = _countof(severities);
    newFilter.DenyList.pSeverityList = severities;
    newFilter.DenyList.NumIDs = _countof(denyIDs);
    newFilter.DenyList.pIDList = denyIDs;
    
    DX12_CHECK(pInfoQueue->PushStorageFilter(&newFilter));
  }
#endif

  return d3d12Device2;
}

ComPtr<ID3D12CommandQueue> CreateCommandQueue(ComPtr<ID3D12Device2> device, D3D12_COMMAND_LIST_TYPE type)
{
  ComPtr<ID3D12CommandQueue> d3d12CommandQueue;

  D3D12_COMMAND_QUEUE_DESC desc = {};
  desc.Type = type;
  desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
  desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  desc.NodeMask = 0;

  DX12_CHECK(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&d3d12CommandQueue)));

  return d3d12CommandQueue;
}

bool CheckTearingSupport()
{
  BOOL allowTearing = FALSE;

  ComPtr<IDXGIFactory4> factory4;
  if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory4))))
  {
    ComPtr<IDXGIFactory5> factory5;

    if (SUCCEEDED(factory4.As(&factory5)))
    {
      if (FAILED(factory5->CheckFeatureSupport(
        DXGI_FEATURE_PRESENT_ALLOW_TEARING,
        &allowTearing, sizeof(allowTearing))))
      {
        allowTearing = FALSE;
      }
    }
  }

  return allowTearing == TRUE;
}

ComPtr<IDXGISwapChain4> CreateSwapChain(HWND hWnd, ComPtr<ID3D12CommandQueue> commandQueue,
  uint32_t width, uint32_t height, uint32_t bufferCount)
{
  ComPtr<IDXGISwapChain4> dxgiSwapChain4;
  ComPtr<IDXGIFactory4> dxgiFactory4;
  UINT createFactoryFlags = 0;

#if defined (_DEBUG)
  createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif

  DX12_CHECK(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory4)));

  DXGI_SWAP_CHAIN_DESC1 desc = {};
  desc.Width = width;                                   // Resolution width. If 0 is specified, then output window's width is automatically used.
  desc.Height = height;                                 // As above, but for height.
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;             // Display format.
  desc.Stereo = FALSE;                                  // Whether or not the fullscreen-display mode or swapchain back buffer is stereo (what does this mean).
  desc.SampleDesc = { 1, 0 };                           // Multisampling parameters. When using a "flip" model swapchain, { 1, 0 } is required.
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;   // Surface usage and CPU access options for the back buffer. Could specify as DXGI_USAGE_SHADER_INPUT as well.
  desc.BufferCount = bufferCount;                       // Number of buffers in the swapchain.
  desc.Scaling = DXGI_SCALING_STRETCH;                  // Specifies behaviour upon window resize, could also be SCALING_NONE or SCALING_ASPECT_RATIO_STRETCH.
  desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;      // Defines flip model, could also be EFFECT_FLIP_SEQUENTIAL.
  desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;         // Transparency behaviour of the back buffer, could also be PREMULTIPLIED (additive), STRAIGHT or IGNORE.
  desc.Flags = CheckTearingSupport() ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;  // ALLOW_TEARING should be specified if tearing support is available!

  ComPtr<IDXGISwapChain1> swapChain1;

  DX12_CHECK(dxgiFactory4->CreateSwapChainForHwnd(commandQueue.Get(), hWnd, &desc, NULL, NULL, &swapChain1));
  DX12_CHECK(dxgiFactory4->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER)); // Disable Alt+Enter fullscreen toggle.
  DX12_CHECK(swapChain1.As(&dxgiSwapChain4));

  return dxgiSwapChain4;
}

ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(ComPtr<ID3D12Device2> device, 
  D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors)
{
  ComPtr<ID3D12DescriptorHeap> descriptorHeap;

  D3D12_DESCRIPTOR_HEAP_DESC desc = {};
  desc.NumDescriptors = numDescriptors;
  desc.Type = type;

  DX12_CHECK(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descriptorHeap)));

  return descriptorHeap;
}

void UpdateRenderTargetViews(ComPtr<ID3D12Device2> device, ComPtr<IDXGISwapChain4> swapChain,
  ComPtr<ID3D12DescriptorHeap> descriptorHeap)
{
  UINT rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

  CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(descriptorHeap->GetCPUDescriptorHandleForHeapStart());

  for (int i = 0; i < g_numFrames; ++i)
  {
    ComPtr<ID3D12Resource> backBuffer;
    DX12_CHECK(swapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));
    device->CreateRenderTargetView(backBuffer.Get(), nullptr, rtvHandle);
    g_backBuffers[i] = backBuffer;
    rtvHandle.Offset(rtvDescriptorSize);
  }
}

ComPtr<ID3D12CommandAllocator> CreateCommandAllocator(ComPtr<ID3D12Device2> device, D3D12_COMMAND_LIST_TYPE type)
{
  ComPtr<ID3D12CommandAllocator> commandAllocator;
  DX12_CHECK(device->CreateCommandAllocator(type, IID_PPV_ARGS(&commandAllocator)));

  return commandAllocator;
}

ComPtr<ID3D12GraphicsCommandList> CreateCommandList(ComPtr<ID3D12Device2> device,
  ComPtr<ID3D12CommandAllocator> commandAllocator, D3D12_COMMAND_LIST_TYPE type)
{
  ComPtr<ID3D12GraphicsCommandList> commandList;
  DX12_CHECK(device->CreateCommandList(0, type, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList)));
  DX12_CHECK(commandList->Close());

  return commandList;
}

ComPtr<ID3D12Fence> CreateFence(ComPtr<ID3D12Device2> device)
{
  ComPtr<ID3D12Fence> fence;
  DX12_CHECK(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
  return fence;
}

HANDLE CreateEventHandle()
{
  HANDLE fenceEvent;
  fenceEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);
  assert(fenceEvent && "Failed to create fence event!");
  return fenceEvent;
}

uint64_t Signal(ComPtr<ID3D12CommandQueue> commandQueue, ComPtr<ID3D12Fence> fence, uint64_t& fenceVal)
{
  uint64_t fenceValForSignal = ++fenceVal;
  DX12_CHECK(commandQueue->Signal(fence.Get(), fenceValForSignal));
  return fenceValForSignal;
}

void WaitForFenceValue(ComPtr<ID3D12Fence> fence, uint64_t fenceVal, HANDLE fenceEvent,
  std::chrono::milliseconds duration = std::chrono::milliseconds::max())
{
  if (fence->GetCompletedValue() < fenceVal)
  {
    DX12_CHECK(fence->SetEventOnCompletion(fenceVal, fenceEvent));
    ::WaitForSingleObject(fenceEvent, static_cast<DWORD>(duration.count()));
  }
}

void Flush(ComPtr<ID3D12CommandQueue> commandQueue, ComPtr<ID3D12Fence> fence,
  uint64_t& fenceVal, HANDLE fenceEvent)
{
  uint64_t fenceValForSignal = Signal(commandQueue, fence, fenceVal);
  WaitForFenceValue(fence, fenceValForSignal, fenceEvent);
}

void Update()
{
  static uint64_t frameCount = 0;
  static double elapsedSecs = 0.0;
  static std::chrono::high_resolution_clock clock;
  static auto t0 = clock.now();

  frameCount++;
  auto t1 = clock.now();
  auto dt = t1 - t0;
  t0 = t1;

  elapsedSecs += dt.count() * 1e-9; // Convert ns to secs

  if (elapsedSecs > 1.0)
  {
    char buffer[500];
    auto fps = frameCount / elapsedSecs;
    sprintf_s(buffer, 500, "FPS: %f\n", fps);
    OutputDebugString((LPCWSTR)buffer);
  }
}

void Render()
{
  auto& commandAllocator = g_commandAllocators[g_currentBackBufferIndex];
  auto& backBuffer = g_backBuffers[g_currentBackBufferIndex];

  commandAllocator->Reset();
  g_commandList->Reset(commandAllocator.Get(), nullptr);

  // Clear render target:
  {
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
      backBuffer.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

    g_commandList->ResourceBarrier(1, &barrier);

    FLOAT clearColour[] = { 0.2f, 0.3f, 0.3f, 1.0f };
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(g_RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
      g_currentBackBufferIndex, g_RTVDescriptorSize);

    g_commandList->ClearRenderTargetView(rtv, clearColour, 0, nullptr);
  }

  // Present:
  {
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
      backBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

    g_commandList->ResourceBarrier(1, &barrier);

    DX12_CHECK(g_commandList->Close());
    ID3D12CommandList* const commandLists[] = {
      g_commandList.Get(),
    };

    g_commandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);

    UINT syncInterval = g_useVsync ? 1 : 0;
    UINT presentFlags = g_tearingSupported && !g_useVsync ? DXGI_PRESENT_ALLOW_TEARING : 0;

    DX12_CHECK(g_swapChain->Present(syncInterval, presentFlags));

    g_frameFenceValues[g_currentBackBufferIndex] = Signal(g_commandQueue, g_fence, g_fenceValue);

    g_currentBackBufferIndex = g_swapChain->GetCurrentBackBufferIndex();
    WaitForFenceValue(g_fence, g_frameFenceValues[g_currentBackBufferIndex], g_fenceEvent);
  }
}

void Resize(uint32_t width, uint32_t height)
{
  if (g_windowWidth != width || g_windowHeight != height)
  {
    g_windowWidth = std::max(width, 1u);
    g_windowHeight= std::max(height, 1u);

    Flush(g_commandQueue, g_fence, g_fenceValue, g_fenceEvent);

    for (int i = 0; i < g_numFrames; ++i)
    {
      // Release all back buffer references before resizing swapchain:
      g_backBuffers[i].Reset();
      g_frameFenceValues[i] = g_frameFenceValues[g_currentBackBufferIndex];
    }

    DXGI_SWAP_CHAIN_DESC desc = {};
    DX12_CHECK(g_swapChain->GetDesc(&desc));
    DX12_CHECK(g_swapChain->ResizeBuffers(g_numFrames, g_windowWidth, g_windowHeight, 
      desc.BufferDesc.Format, desc.Flags));

    g_currentBackBufferIndex = g_swapChain->GetCurrentBackBufferIndex();
    UpdateRenderTargetViews(g_device, g_swapChain, g_RTVDescriptorHeap);
  }
}

void ToggleFullscreen()
{
  g_isFullscreen = !g_isFullscreen;

  // If going to fullscreen mode, cache windowed dimensions so they can be restored after toggling back:
  if (g_isFullscreen)
  {
    ::GetWindowRect(g_hWnd, &g_windowRect);

    // Set window style to borderless so client area fills the entire screen:
    UINT windowStyle = WS_OVERLAPPEDWINDOW & 
      ~(WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);

    ::SetWindowLongW(g_hWnd, GWL_STYLE, windowStyle);

    // Query name of nearest display device for the window, so that the correct dimensions
    // can be used to go fullscreen on multi-monitor setups:
    HMONITOR hMonitor = ::MonitorFromWindow(g_hWnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEX monitorInfo = {};
    monitorInfo.cbSize = sizeof(MONITORINFOEX);
    
    ::GetMonitorInfo(hMonitor, &monitorInfo);

    ::SetWindowPos(g_hWnd, HWND_TOP,
      monitorInfo.rcMonitor.left,
      monitorInfo.rcMonitor.top,
      monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
      monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top,
      SWP_FRAMECHANGED | SWP_NOACTIVATE);

    ::ShowWindow(g_hWnd, SW_MAXIMIZE);
  }
  else
  {
    ::SetWindowLong(g_hWnd, GWL_STYLE, WS_OVERLAPPEDWINDOW);
    ::SetWindowPos(g_hWnd, HWND_NOTOPMOST,
      g_windowRect.left,
      g_windowRect.top,
      g_windowRect.right - g_windowRect.left,
      g_windowRect.bottom - g_windowRect.top,
      SWP_FRAMECHANGED | SWP_NOACTIVATE);

    ::ShowWindow(g_hWnd, SW_NORMAL);
  }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  if (g_isInitialised)
  {
    switch (message)
    {
    case WM_PAINT:
      Update();
      Render();
      break;

    case WM_SYSKEYDOWN:
    case WM_KEYDOWN:
    {
      bool alt = (::GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

      switch (wParam)
      {
      case 'V':         // Toggle vsync usage on 'V' press.
        g_useVsync = !g_useVsync;
        break;
      case VK_ESCAPE:   // Quit on 'escape' press.
        ::PostQuitMessage(0);
        break;
      case VK_RETURN:
        if (alt)
        {
      case VK_F11:
        ToggleFullscreen();
        }
        break;
      }
    }
    break;

    case WM_SYSCHAR:  // Must be handled, Windows will play system notification sound
      break;          // when Alt+Enter is pressed otherwise.

    case WM_SIZE:
      {
        RECT clientRect = {};
        ::GetClientRect(g_hWnd, &clientRect);

        int width = clientRect.right - clientRect.left;
        int height = clientRect.bottom - clientRect.bottom;

        Resize(width, height);
      }
      break;

    case WM_DESTROY:
      ::PostQuitMessage(0);
      break;

    default:
      return ::DefWindowProcW(hwnd, message, wParam, lParam);
    }
  }
  else
    return ::DefWindowProcW(hwnd, message, wParam, lParam);

  return 0;
}

int CALLBACK wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nCmdShow)
{
  // Windows 10 Creators update added "Par Monitor V2 DPI awareness context, allowing the client area
  // of the window to achieve 100% scaling while still allowing non-client window content to be rendered
  // in a DPI-sensitive fashion:
  SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  const wchar_t* windowClassName = L"DX12WindowClass";
  ParseCommandLineArguments();
  EnableDebugLayer();

  // Register window class and create window + window rect:
  g_tearingSupported = CheckTearingSupport();
  RegisterWindowClass(hInstance, windowClassName);
  g_hWnd = CreateWindow(windowClassName, hInstance, L"3DGEP Dx12 Tutorial", g_windowWidth, g_windowHeight);
  ::GetWindowRect(g_hWnd, &g_windowRect);

  // Create Dx12 objects:
  ComPtr<IDXGIAdapter4> dxgiAdapter4 = GetAdapter(g_useWarp);
  g_device = CreateDevice(dxgiAdapter4);
  g_commandQueue = CreateCommandQueue(g_device, D3D12_COMMAND_LIST_TYPE_DIRECT);
  g_swapChain = CreateSwapChain(g_hWnd, g_commandQueue, g_windowWidth, g_windowHeight, g_numFrames);
  g_currentBackBufferIndex = g_swapChain->GetCurrentBackBufferIndex();
  g_RTVDescriptorHeap = CreateDescriptorHeap(g_device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, g_numFrames);
  g_RTVDescriptorSize = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

  UpdateRenderTargetViews(g_device, g_swapChain, g_RTVDescriptorHeap);

  for (int i = 0; i < g_numFrames; ++i)
    g_commandAllocators[i] = CreateCommandAllocator(g_device, D3D12_COMMAND_LIST_TYPE_DIRECT);

  g_commandList = CreateCommandList(g_device, g_commandAllocators[g_currentBackBufferIndex], D3D12_COMMAND_LIST_TYPE_DIRECT);
  g_fence = CreateFence(g_device);
  g_fenceEvent = CreateEventHandle();

  g_isInitialised = true;
  ::ShowWindow(g_hWnd, SW_SHOW);

  MSG msg = {};
  while (msg.message != WM_QUIT)
  {
    if (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
    {
      ::TranslateMessage(&msg);
      ::DispatchMessage(&msg);
    }
  }

  Flush(g_commandQueue, g_fence, g_fenceValue, g_fenceEvent);
  ::CloseHandle(g_fenceEvent);

  return 0;
}