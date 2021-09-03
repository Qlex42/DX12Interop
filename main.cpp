// Simple example of DX12->DX11->GL interop

#include <initguid.h>
#include <d3d12.h> /* install Plateform SDK 10.0.14.393 with Visual installer and relauch CMake if this file is missing */
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include "d3dx12.h" // for CD3DX12_VIEWPORT CD3DX12_CPU_DESCRIPTOR_HANDLE CD3DX12_DESCRIPTOR_RANGE1 CD3DX12_ROOT_PARAMETER1 D3DX12SerializeVersionedRootSignature...
#include <tchar.h> // for _T
#include <d3d11on12.h>
#include <DirectXMath.h>
#include <vector>

typedef HANDLE(WINAPI* PFNWGLDXOPENDEVICENVPROC)				(void* dxDevice);
PFNWGLDXOPENDEVICENVPROC				wglDXOpenDeviceNV;

typedef HRESULT(WINAPI* PFN_D3D12_CREATE_DEVICE)(
  _In_opt_ IUnknown* pAdapter,
  D3D_FEATURE_LEVEL MinimumFeatureLevel,
  _In_ REFIID riid, // Expected: ID3D12Device
  _COM_Outptr_opt_ void** ppDevice);


static ID3D12Device* deviceDX12 = NULL;
static HWND hwnd = NULL;

bool ImGui_ImplDX12_Init();

// Win32 message handler
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{ return ::DefWindowProc(hWnd, msg, wParam, lParam); }

// Main code
int  WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
  // Create application window
  WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, _T("ImGui Example"), NULL };
  ::RegisterClassEx(&wc);
  hwnd = ::CreateWindow(wc.lpszClassName, _T("SmodePlayer DirectX12"), WS_OVERLAPPEDWINDOW, 100, 100, 1280, 800, NULL, NULL, wc.hInstance, NULL);

  // Create DX12 device
  ImGui_ImplDX12_Init();

  // DX12->DX11 interop
  ID3D11Device* device11;
  HRESULT hr = D3D11On12CreateDevice(
    deviceDX12,
    D3D11_CREATE_DEVICE_SINGLETHREADED,
    nullptr,
    0,
    nullptr,
    0,
    0,
    &device11,
    nullptr,
    nullptr);

  // Creation of an OpenGL context
  HDC hdc = GetDC(hwnd);
  PIXELFORMATDESCRIPTOR pfd;
  memset(&pfd, 0, sizeof(pfd));
  pfd.nSize = sizeof(pfd);
  pfd.nVersion = 1;
  pfd.dwFlags = PFD_SUPPORT_OPENGL | PFD_DRAW_TO_WINDOW | PFD_DOUBLEBUFFER;
  pfd.dwFlags |= PFD_SWAP_EXCHANGE ;
  pfd.iPixelType = PFD_TYPE_RGBA;
  pfd.iLayerType = PFD_MAIN_PLANE;
  pfd.cColorBits = (BYTE)(24);
  pfd.cRedBits = (BYTE)8;
  pfd.cGreenBits = (BYTE)8;
  pfd.cBlueBits = (BYTE)8;
  pfd.cAlphaBits = (BYTE)8;
  pfd.cDepthBits = (BYTE)16;
  int pixelFormat = ChoosePixelFormat(hdc, &pfd);
  SetPixelFormat(hdc, pixelFormat, &pfd);
  HGLRC hRC = wglCreateContext(hdc);
  wglMakeCurrent(hdc, hRC);

  // DX11->GL interop
  wglDXOpenDeviceNV = (PFNWGLDXOPENDEVICENVPROC)wglGetProcAddress("wglDXOpenDeviceNV");
  SetLastError(0);
  HANDLE deviceHandle = wglDXOpenDeviceNV(device11);
  DWORD error = GetLastError();

  assert(deviceHandle); // CRASH HERE

  wglMakeCurrent(nullptr, nullptr);
  wglDeleteContext(hRC);

  return 0;
}




// DirectX12 
#ifdef _MSC_VER
#pragma comment(lib, "d3dcompiler") // Automatically link with d3dcompiler.lib as we are using D3DCompile() below.
#endif

inline void ThrowIfFailed(HRESULT hr)
{
  if (FAILED(hr))
  {
    assert(false);
    return;
  }
}

static const UINT FrameCount = 2;
static bool m_useWarpDevice = false;
static UINT m_width = 800;
static UINT m_height = 800;

struct Vertex
{
  DirectX::XMFLOAT3 position;
};

// Pipeline objects.
static CD3DX12_VIEWPORT m_viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height));
static CD3DX12_RECT m_scissorRect = CD3DX12_RECT(0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height));

static IDXGISwapChain3* m_swapChain = NULL;

static ID3D12Resource* m_renderTargets[FrameCount];
static ID3D12CommandAllocator* m_commandAllocator = NULL;
static ID3D12CommandQueue* m_commandQueue = NULL;
static ID3D12RootSignature* m_rootSignature = NULL;
static ID3D12DescriptorHeap* m_rtvHeap = NULL;
static ID3D12DescriptorHeap* m_srvHeap = NULL;
static ID3D12PipelineState* m_pipelineState = NULL;
static ID3D12GraphicsCommandList* m_commandList = NULL;
static UINT m_rtvDescriptorSize = 0;

// App resources.
static ID3D12Resource* m_vertexBuffer = NULL;
static D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
static ID3D12Resource* m_texture = NULL;
static ID3D12Resource* m_depthTexture = NULL;

static D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
static ID3D12Resource* textureUploadHeap = NULL;
static D3D12_RESOURCE_DESC textureDesc = {};
static D3D12_RESOURCE_DESC depthTextureDesc = {};

// Synchronization objects.
static UINT m_frameIndex = 0;
static HANDLE m_fenceEvent = NULL;
static ID3D12Fence* m_fence = NULL;
static UINT64 m_fenceValue = NULL;


void LoadPipeline();
void LoadAssets();
template<typename T>
static void SafeRelease(T*& res)
{
  if (res)
    res->Release();
  res = NULL;
}

HINSTANCE ImGui_GetD3d12Dll()
{
  static HINSTANCE d3d12_dll = ::GetModuleHandleA("d3d12.dll");
  if (d3d12_dll == NULL)
  {
    // Attempt to load d3d12.dll from local directories. This will only succeed if
    // (1) the current OS is Windows 7, and
    // (2) there exists a version of d3d12.dll for Windows 7 (D3D12On7) in one of the following directories.
    // See https://github.com/ocornut/imgui/pull/3696 for details.
    const char* localD3d12Paths[] = { ".\\d3d12.dll", ".\\d3d12on7\\d3d12.dll", ".\\12on7\\d3d12.dll" }; // A. current directory, B. used by some games, C. used in Microsoft D3D12On7 sample
    for (int i = 0; i < sizeof(localD3d12Paths) / sizeof(*localD3d12Paths); i++)
      if ((d3d12_dll = ::LoadLibraryA(localD3d12Paths[i])) != NULL)
        break;

    // If failed, we are on Windows >= 10.
    if (d3d12_dll == NULL)
      d3d12_dll = ::LoadLibraryA("d3d12.dll");

    if (d3d12_dll == NULL)
      return nullptr;
  }
  return d3d12_dll;
}

bool ImGui_ImplDX12_Init()
{
  if (false) // debug layer
  {
    ID3D12Debug* debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
    {
      debugController->EnableDebugLayer();
    }
  }
  LoadPipeline();
  LoadAssets();

  return true;
}

void ImGui_ImplDX12_Shutdown()
{
  CloseHandle(m_fenceEvent);
  SafeRelease(m_swapChain);
  SafeRelease(m_rootSignature);
  for (int i = 0; i < FrameCount; i++)
    SafeRelease(m_renderTargets[i]);
  SafeRelease(deviceDX12);
  SafeRelease(m_texture);
  SafeRelease(m_depthTexture);
}

// Load the rendering pipeline dependencies.
void LoadPipeline()
{
  UINT dxgiFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
  IDXGIFactory2* factory;
  ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));
  //ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
  HINSTANCE d3d12_dll = ImGui_GetD3d12Dll();
  if (!d3d12_dll)
    return;
  PFN_D3D12_CREATE_DEVICE D3D12CreateDevice = (PFN_D3D12_CREATE_DEVICE)::GetProcAddress(d3d12_dll, "D3D12CreateDevice");
  if (D3D12CreateDevice == NULL)
    return;
  // Create device
  D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_12_1;

  ThrowIfFailed(D3D12CreateDevice(NULL, featureLevel, IID_PPV_ARGS(&deviceDX12)));

  // Describe and create the command queue.
  D3D12_COMMAND_QUEUE_DESC queueDesc = {};
  queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

  ThrowIfFailed(deviceDX12->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));

  // Describe and create the swap chain.
  DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
  swapChainDesc.BufferCount = FrameCount;
  swapChainDesc.Width = m_width;
  swapChainDesc.Height = m_height;
  swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  swapChainDesc.SampleDesc.Count = 1;

  IDXGISwapChain1* swapChain;
  ThrowIfFailed(factory->CreateSwapChainForHwnd(
    m_commandQueue,        // Swap chain needs the queue so that it can force a flush on it.
    hwnd,
    &swapChainDesc,
    nullptr,
    nullptr,
    &swapChain
  ));

  // This sample does not support fullscreen transitions.
  ThrowIfFailed(factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER));
  SafeRelease(factory);
  m_swapChain = (IDXGISwapChain3*)swapChain;
  m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

  // Create descriptor heaps.
  {
    // Describe and create a render target view (RTV) descriptor heap.
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = FrameCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(deviceDX12->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

    // Describe and create a shader resource view (SRV) heap for the texture.
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = 1;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(deviceDX12->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_srvHeap)));

    m_rtvDescriptorSize = deviceDX12->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  }

  // Create frame resources.
  {
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

    // Create a RTV for each frame.
    for (UINT n = 0; n < FrameCount; n++)
    {
      ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
      deviceDX12->CreateRenderTargetView(m_renderTargets[n], nullptr, rtvHandle);
      rtvHandle.Offset(1, m_rtvDescriptorSize);
    }
  }

  ThrowIfFailed(deviceDX12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)));
}

void LoadAssets()
{
  // Create the root signature.
  {
    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

    // This is the highest version the sample supports. If CheckFeatureSupport succeeds, the HighestVersion returned will not be greater than this.
    featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

    if (FAILED(deviceDX12->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
    {
      featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
    }

    CD3DX12_DESCRIPTOR_RANGE1 ranges[1];
    ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

    CD3DX12_ROOT_PARAMETER1 rootParameters[1];
    rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    sampler.MipLODBias = 0;
    sampler.MaxAnisotropy = 0;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ID3DBlob* signature;
    ID3DBlob* error;
    ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
    ThrowIfFailed(deviceDX12->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
  }

  // Create the pipeline state, which includes compiling and loading shaders.
  {
    ID3DBlob* vertexShader;
    ID3DBlob* pixelShader;
    UINT compileFlags = 0;

    static const char* vertexShaderStr =
      "     struct PSInput\
            {\
              float4 position : SV_POSITION;\
              float2 uv  : UV;\
            };\
            \
            PSInput VSMain(float4 position : POSITION)\
            {\
              PSInput result;\
              result.position = position;\
              result.uv = 0.5f * position.xy + float2(0.5f,0.5f);\
              return result;\
            }";

    if (!SUCCEEDED(D3DCompile(vertexShaderStr, strlen(vertexShaderStr), NULL, NULL, NULL, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, NULL)))
    {
      return; // NB: Pass ID3D10Blob* pErrorBlob to D3DCompile() to get error showing in (const char*)pErrorBlob->GetBufferPointer(). Make sure to Release() the blob!
    }
    static const char* pixelShaderStr =
      "Texture2D g_texture : register(t0);\
       SamplerState g_sampler : register(s0);\
          struct PSInput\
          {\
            float4 position : SV_POSITION;\
            float2 uv  : UV;\
          };\
          \
          float4 PSMain(PSInput input) : SV_TARGET\
          {\
            return g_texture.Sample(g_sampler, input.uv);\
          }";

    if (!SUCCEEDED(D3DCompile(pixelShaderStr, strlen(pixelShaderStr), NULL, NULL, NULL, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, NULL)))
    {
      vertexShader->Release();
      return; // NB: Pass ID3D10Blob* pErrorBlob to D3DCompile() to get error showing in (const char*)pErrorBlob->GetBufferPointer(). Make sure to Release() the blob!
    }

    // Define the vertex input layout.
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
    {
      { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    // Describe and create the graphics pipeline state object (PSO).
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
    psoDesc.pRootSignature = m_rootSignature;
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader);
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader);
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;
    if (!SUCCEEDED(deviceDX12->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState))))
      return;
  }

  // Create the command list.
  HRESULT hr = deviceDX12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator, m_pipelineState, IID_PPV_ARGS(&m_commandList));
  if (!SUCCEEDED(hr))
    return;

  // Create the vertex buffer.
  {
    Vertex triangleVertices[] =
    {
      { { -1.0f, 1.0f, 0.0f } },
      { { 1.0f, 1.0f, 0.0f } },
      { { -1.0f, -1.0f, 0.0f } },
      { { 1.0f, -1.0f, 0.0f } }
    };

    const UINT vertexBufferSize = sizeof(triangleVertices);

    // Note: using upload heaps to transfer static data like vert buffers is not 
    // recommended. Every time the GPU needs it, the upload heap will be marshalled 
    // over. Please read up on Default Heap usage. An upload heap is used here for 
    // code simplicity and because there are very few verts to actually transfer.
    ThrowIfFailed(deviceDX12->CreateCommittedResource(
      &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
      D3D12_HEAP_FLAG_NONE,
      &CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
      D3D12_RESOURCE_STATE_GENERIC_READ,
      nullptr,
      IID_PPV_ARGS(&m_vertexBuffer)));

    // Copy the triangle data to the vertex buffer.
    UINT8* pVertexDataBegin;
    CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
    ThrowIfFailed(m_vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)));
    memcpy(pVertexDataBegin, triangleVertices, sizeof(triangleVertices));
    m_vertexBuffer->Unmap(0, nullptr);

    // Initialize the vertex buffer view.
    m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vertexBufferView.StrideInBytes = sizeof(Vertex);
    m_vertexBufferView.SizeInBytes = vertexBufferSize;
  }

  // Close the command list and execute it to begin the initial GPU setup.
  ThrowIfFailed(m_commandList->Close());
  ID3D12CommandList* ppCommandLists[] = { m_commandList };
  m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

  // Create synchronization objects and wait until assets have been uploaded to the GPU.
  {
    ThrowIfFailed(deviceDX12->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
    m_fenceValue = 1;

    // Create an event handle to use for frame synchronization.
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (m_fenceEvent == nullptr)
    {
      ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
    }
  }
}
