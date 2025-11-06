#include "Renderer.h"

bool Renderer::InitializeD3D12()
{
#if defined(DEBUG) || defined(_DEBUG)
	{
		ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&m_debugController)));
		m_debugController->EnableDebugLayer();
	};
#endif

	ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&m_dxgiFactory)));

	HRESULT hardwareResult = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_2, IID_PPV_ARGS(&m_device));

	if (FAILED(hardwareResult))
	{
		ThrowIfFailed(m_dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&m_warpAdapter)));

		ThrowIfFailed(D3D12CreateDevice(m_warpAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_warpAdapter)));
	}

	return false;
}