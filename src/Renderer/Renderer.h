#pragma once
#include "../Utils/d3dUtil.h"
#pragma comment(lib,"d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")
#pragma comment(lib, "dxgi.lib")

class Renderer {
public:
	Renderer(HWND& windowHandle);
	~Renderer() = default;

	bool InitializeD3D12(HWND& windowHandle);
	bool Shutdown();

private:
	void CreateDebugController();
	void CreateDevice();
	void CreateFence();
	void GetDescriptorSizes();
	void CheckMSAAQuality();
	void CreateCommandObjects();
	void CreateSwapChain(HWND& hwnd);

	Microsoft::WRL::ComPtr<ID3D12Device> m_Device;
	Microsoft::WRL::ComPtr<IDXGIAdapter> m_WarpAdapter;
	Microsoft::WRL::ComPtr<ID3D12Debug> m_DebugController;

	Microsoft::WRL::ComPtr<IDXGIFactory4> m_DxgiFactory;

	Microsoft::WRL::ComPtr<ID3D12Fence> m_Fence;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_CommandQueue;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_CommandAllocator;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_CommandList;

	Microsoft::WRL::ComPtr<IDXGISwapChain> m_SwapChain;
	UINT m_SwapChainBufferCount = 2;

	UINT m_RtvDescriptorSize = 0;
	UINT m_DsvDescriptorSize = 0;
	UINT m_CbvSrvDescriptorSize = 0;

	UINT m_4xMsaaQuality = 0;
	bool m_MsaaState = false;

	DXGI_FORMAT m_BackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

	HWND& m_Hwnd;


};