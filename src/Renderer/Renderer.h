#pragma once
#include "../Utils/d3dUtil.h"


class Renderer {
	Renderer();
	~Renderer();

	bool InitializeD3D12();
	bool Shutdown();

	Microsoft::WRL::ComPtr<ID3D12Device> m_device;
	Microsoft::WRL::ComPtr<IDXGIAdapter> m_warpAdapter;
	Microsoft::WRL::ComPtr<ID3D12Debug> m_debugController;
	Microsoft::WRL::ComPtr<IDXGIFactory4> m_dxgiFactory;

};