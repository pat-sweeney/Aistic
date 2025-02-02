// Aistic.cpp : Defines the entry point for the application.
//

#include "framework.h"
#include "Aistic.h"
#include <d3d11.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include <vector>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#pragma comment(lib, "d3d11.lib")

using Microsoft::WRL::ComPtr;

#define MAX_LOADSTRING 100

// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name

// DirectX 11 Variables
ComPtr<IDXGISwapChain> g_pSwapChain;
ComPtr<ID3D11Device> g_pd3dDevice;
ComPtr<ID3D11DeviceContext> g_pImmediateContext;
ComPtr<ID3D11RenderTargetView> g_pRenderTargetView;
ComPtr<ID3D11Buffer> g_pVertexBuffer;
ComPtr<ID3D11Buffer> g_pIndexBuffer;
std::thread renderThread;
std::atomic<bool> running = true;

// Vertex Structure
struct Vertex {
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT2 texcoord;
};

// Load OBJ function
bool LoadOBJ(const std::string& filename, std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) {
    std::ifstream file(filename);
    if (!file.is_open()) return false;

    std::vector<DirectX::XMFLOAT3> positions;
    std::vector<DirectX::XMFLOAT3> normals;
    std::vector<DirectX::XMFLOAT2> texcoords;

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string prefix;
        iss >> prefix;
        if (prefix == "v") {
            DirectX::XMFLOAT3 pos;
            iss >> pos.x >> pos.y >> pos.z;
            positions.push_back(pos);
        }
        else if (prefix == "vn") {
            DirectX::XMFLOAT3 norm;
            iss >> norm.x >> norm.y >> norm.z;
            normals.push_back(norm);
        }
        else if (prefix == "vt") {
            DirectX::XMFLOAT2 tex;
            iss >> tex.x >> tex.y;
            texcoords.push_back(tex);
        }
        else if (prefix == "f") {
            std::vector<uint32_t> p, t, n;
            char slash;
            std::string vertexStr;
            while (iss >> vertexStr) {
                std::istringstream viss(vertexStr);
                std::string posIdxStr, texIdxStr, normIdxStr;
                uint32_t posIdx = 0, texIdx = 0, normIdx = 0;

                std::getline(viss, posIdxStr, '/');
                std::getline(viss, texIdxStr, '/');
                std::getline(viss, normIdxStr, '/');

                posIdx = std::stoi(posIdxStr) - 1;
                if (!texIdxStr.empty()) texIdx = std::stoi(texIdxStr) - 1;
                if (!normIdxStr.empty()) normIdx = std::stoi(normIdxStr) - 1;

                p.push_back(posIdx);
                t.push_back(texIdxStr.empty() ? 0 : texIdx); // Default to 0 if missing
                n.push_back(normIdxStr.empty() ? 0 : normIdx); // Default to 0 if missing
            }

            if (p.size() == 3) { // Triangle
                for (int i = 0; i < 3; ++i) {
                    Vertex vertex = { positions[p[i]], normals[n[i]], texcoords[t[i]] };
                    vertices.push_back(vertex);
                    indices.push_back((uint32_t)indices.size());
                }
            }
            else if (p.size() == 4) { // Quad
                // Split quad into two triangles
                for (int i = 0; i < 3; ++i) {
                    Vertex vertex = { positions[p[i]], normals[n[i]], texcoords[t[i]] };
                    vertices.push_back(vertex);
                    indices.push_back((uint32_t)indices.size());
                }
                for (int i : {0, 2, 3}) {
                    Vertex vertex = { positions[p[i]], normals[n[i]], texcoords[t[i]] };
                    vertices.push_back(vertex);
                    indices.push_back((uint32_t)indices.size());
                }
            }
        }
    }

    return true;
}
// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);
HRESULT             InitDirect3D(HWND hWnd);
void                CleanupDevice();
void                RenderLoop();
void                Render();

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // Initialize global strings
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_AISTIC, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // Perform application initialization:
    if (!InitInstance(hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_AISTIC));

    // Start the render thread
    renderThread = std::thread(RenderLoop);

    MSG msg;

    // Main message loop:
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    // Stop the render thread
    running = false;
    if (renderThread.joinable())
        renderThread.join();

    CleanupDevice();

    return (int)msg.wParam;
}

ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_AISTIC));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_AISTIC);
    wcex.lpszClassName = szWindowClass;
    wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    hInst = hInstance; // Store instance handle in our global variable

    HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);

    if (!hWnd)
    {
        return FALSE;
    }

    if (FAILED(InitDirect3D(hWnd)))
    {
        return FALSE;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    return TRUE;
}

std::vector<Vertex> vertices;
std::vector<uint32_t> indices;

HRESULT InitDirect3D(HWND hWnd)
{
    HRESULT hr = S_OK;

    // Create a swap chain description
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 1;
    sd.BufferDesc.Width = 800;
    sd.BufferDesc.Height = 600;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;

    // Create the device and swap chain
    hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &sd, g_pSwapChain.GetAddressOf(), g_pd3dDevice.GetAddressOf(), nullptr, g_pImmediateContext.GetAddressOf());
    if (FAILED(hr))
        return hr;

    // Create a render target view
    ComPtr<ID3D11Texture2D> pBackBuffer;
    hr = g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(pBackBuffer.GetAddressOf()));
    if (FAILED(hr))
        return hr;

    hr = g_pd3dDevice->CreateRenderTargetView(pBackBuffer.Get(), nullptr, g_pRenderTargetView.GetAddressOf());
    if (FAILED(hr))
        return hr;

    g_pImmediateContext->OMSetRenderTargets(1, g_pRenderTargetView.GetAddressOf(), nullptr);

    // Setup the viewport
    D3D11_VIEWPORT vp;
    vp.Width = (FLOAT)800;
    vp.Height = (FLOAT)600;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    g_pImmediateContext->RSSetViewports(1, &vp);

    // Load the OBJ file
    if (!LoadOBJ("ModelData\\bugatti.obj", vertices, indices)) {
        return E_FAIL;
    }

    // Create vertex buffer
    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof(Vertex) * (uint32_t)vertices.size();
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = vertices.data();
    hr = g_pd3dDevice->CreateBuffer(&bd, &initData, g_pVertexBuffer.GetAddressOf());
    if (FAILED(hr))
        return hr;

    // Create index buffer
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof(uint32_t) * (uint32_t)indices.size();
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    bd.CPUAccessFlags = 0;
    initData.pSysMem = indices.data();
    hr = g_pd3dDevice->CreateBuffer(&bd, &initData, g_pIndexBuffer.GetAddressOf());
    if (FAILED(hr))
        return hr;

    return S_OK;
}

void CleanupDevice()
{
    if (g_pImmediateContext) g_pImmediateContext->ClearState();
}

void RenderLoop()
{
    while (running)
    {
        Render();
        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // Approx. 60 FPS
    }
}

void Render()
{
    // Clear the back buffer
    float ClearColor[4] = { 0.0f, 0.125f, 0.3f, 1.0f }; // RGBA
    g_pImmediateContext->ClearRenderTargetView(g_pRenderTargetView.Get(), ClearColor);

    // Set vertex buffer
    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    g_pImmediateContext->IASetVertexBuffers(0, 1, g_pVertexBuffer.GetAddressOf(), &stride, &offset);

    // Set index buffer
    g_pImmediateContext->IASetIndexBuffer(g_pIndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

    // Set primitive topology
    g_pImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Draw the model
    g_pImmediateContext->DrawIndexed((uint32_t)indices.size(), 0, 0);

    // Present the information rendered to the back buffer to the front buffer (the screen)
    g_pSwapChain->Present(0, 0);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_COMMAND:
    {
        int wmId = LOWORD(wParam);
        // Parse the menu selections:
        switch (wmId)
        {
        case IDM_ABOUT:
            DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
            break;
        case IDM_EXIT:
            DestroyWindow(hWnd);
            break;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
    }
    break;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        EndPaint(hWnd, &ps);
    }
    break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}