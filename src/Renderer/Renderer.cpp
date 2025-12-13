#include "Renderer.h"
#define STB_PERLIN_IMPLEMENTATION
#include "stb_perlin.h"
#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_win32.h"
#include "imgui/backends/imgui_impl_dx12.h"

const int gNumFrameResources = 3;

// Simple free list based allocator
struct ExampleDescriptorHeapAllocator
{
	ID3D12DescriptorHeap* Heap = nullptr;
	D3D12_DESCRIPTOR_HEAP_TYPE  HeapType = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
	D3D12_CPU_DESCRIPTOR_HANDLE HeapStartCpu;
	D3D12_GPU_DESCRIPTOR_HANDLE HeapStartGpu;
	UINT                        HeapHandleIncrement;
	ImVector<int>               FreeIndices;

	void Create(ID3D12Device* device, ID3D12DescriptorHeap* heap)
	{
		IM_ASSERT(Heap == nullptr && FreeIndices.empty());
		Heap = heap;
		D3D12_DESCRIPTOR_HEAP_DESC desc = heap->GetDesc();
		HeapType = desc.Type;
		HeapStartCpu = Heap->GetCPUDescriptorHandleForHeapStart();
		HeapStartGpu = Heap->GetGPUDescriptorHandleForHeapStart();
		HeapHandleIncrement = device->GetDescriptorHandleIncrementSize(HeapType);
		FreeIndices.reserve((int)desc.NumDescriptors);
		for (int n = desc.NumDescriptors; n > 0; n--)
			FreeIndices.push_back(n - 1);
	}
	void Destroy()
	{
		Heap = nullptr;
		FreeIndices.clear();
	}
	void Alloc(D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_desc_handle)
	{
		IM_ASSERT(FreeIndices.Size > 0);
		int idx = FreeIndices.back();
		FreeIndices.pop_back();
		out_cpu_desc_handle->ptr = HeapStartCpu.ptr + (idx * HeapHandleIncrement);
		out_gpu_desc_handle->ptr = HeapStartGpu.ptr + (idx * HeapHandleIncrement);
	}
	void Free(D3D12_CPU_DESCRIPTOR_HANDLE out_cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE out_gpu_desc_handle)
	{
		int cpu_idx = (int)((out_cpu_desc_handle.ptr - HeapStartCpu.ptr) / HeapHandleIncrement);
		int gpu_idx = (int)((out_gpu_desc_handle.ptr - HeapStartGpu.ptr) / HeapHandleIncrement);
		IM_ASSERT(cpu_idx == gpu_idx);
		FreeIndices.push_back(cpu_idx);
	}
};

static ExampleDescriptorHeapAllocator g_pd3dSrvDescHeapAlloc;

Renderer::Renderer(HWND& windowHandle, UINT width, UINT height, Camera& cam)
	:m_Hwnd(windowHandle),
	m_ClientWidth(width),
	m_ClientHeight(height),
	m_Camera(cam)
{
	m_Hwnd = windowHandle;
	InitializeD3D12(m_Hwnd);
}

bool Renderer::InitializeD3D12(HWND& windowHandle)
{
#if defined(DEBUG) || defined(_DEBUG)
	CreateDebugController();
#endif

	ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&m_DxgiFactory)));

	m_Camera.SetPosition(0.0f, 180.0f, -250.0f);
	m_Camera.UpdateViewMatrix();
	m_View = m_Camera.GetView4x4f();
	m_Proj = m_Camera.GetProj4x4f();

	CreateDevice();

	CreateFence();

	GetDescriptorSizes();

	CheckMSAAQuality();

	CreateCommandObjects();

	m_BackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

	CreateSwapChain(windowHandle);

	CreateRtvAndDsvDescriptorHeaps();

	CreateRenderTargetView();

	CreateDepthStencilView();

	//m_Camera.SetLens(0.25f * MathHelper::Pi, static_cast<float>(m_ClientWidth) / m_ClientHeight, 1.0f, 1000.0f);
	//m_Camera.UpdateViewMatrix();
	//XMStoreFloat4x4(&m_Proj, m_Camera.GetProj());
	//XMStoreFloat4x4(&m_View, m_Camera.GetView());

	m_CbvSrvDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	m_Waves = std::make_unique<Waves>(128, 128, 1.0f, 0.03f, 4.0f, 0.2f);

	HeightMap hm = GeneratePerlinHeightmap(m_TerrainWidth, m_TerrainHeight, m_TerrainHeightScale, m_TerrainNoiseOctaves, m_TerrainNoisePersistance, m_TerrainNoiseSeed);
	CreateHeightMapTexture(hm);

	//	CreateCbvDescriptorHeaps();
	LoadTextures();
	createSrvDescriptorHeaps();
	CreateTextureSrvDescriptors();
	CreateOpaqueRootSignature();
	CreateTransparentRootSignature();

	BuildShadersAndInputLayout();
	BuildShapeGeometry();
	BuildLandGeometry(hm.width, hm.height);
	BuildSkullGeometry();
	BuildMaterials();
	BuildWavesGeometry();
	BuildRenderItems();
	BuildFrameResources();

	BuildPSOs();

	CreateImGuiDescriptorHeap();
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
	//	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // IF using Docking Branch

	ImGui_ImplDX12_InitInfo init_info = {};
	init_info.Device = m_Device.Get();
	init_info.CommandQueue = m_CommandQueue.Get();
	init_info.NumFramesInFlight = SwapChainBufferCount;
	init_info.RTVFormat = m_BackBufferFormat; // Or your render target format.

	//Allocating SRV descriptors (for textures) is up to the application, so we provide callbacks.
	//The example_win32_directx12/main.cpp application include a simple free-list based allocator.
	g_pd3dSrvDescHeapAlloc.Create(m_Device.Get(), m_ImGuiSrvHeap.Get());
	init_info.SrvDescriptorHeap = m_ImGuiSrvHeap.Get();
	init_info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_handle) { return g_pd3dSrvDescHeapAlloc.Alloc(out_cpu_handle, out_gpu_handle); };
	init_info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle) { return g_pd3dSrvDescHeapAlloc.Free(cpu_handle, gpu_handle);  };

	imguiCpuStart = m_ImGuiSrvHeap->GetCPUDescriptorHandleForHeapStart();
	imguiGpuStart = m_ImGuiSrvHeap->GetGPUDescriptorHandleForHeapStart();

	ImGui_ImplDX12_Init(&init_info);
	ImGui_ImplWin32_Init(m_Hwnd);


	ThrowIfFailed(m_CommandList->Close());
	ID3D12CommandList* cmdLists[] = { m_CommandList.Get() };
	m_CommandQueue->ExecuteCommandLists(_countof(cmdLists), cmdLists);

	return true;
}

void Renderer::Update(GameTimer& gt, Camera& cam)
{
	m_CurrentFrameResourceIndex = (m_CurrentFrameResourceIndex + 1) % NumFrameResources;
	m_CurrentFrameResource = m_FrameResources[m_CurrentFrameResourceIndex].get();

	if (m_CurrentFrameResource->Fence != 0 && m_Fence->GetCompletedValue() < m_CurrentFrameResource->Fence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(m_Fence->SetEventOnCompletion(m_CurrentFrameResource->Fence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}

	cam.UpdateViewMatrix();
	XMStoreFloat4x4(&m_View, cam.GetView());
	XMStoreFloat4x4(&m_Proj, cam.GetProj());
	m_EyePos = cam.GetPosition3f();


	UpdateObjectCBs();
	UpdateMainPassCB();
	UpdateMaterialCBs();
	UpdateWaves(gt);
	UpdateWaterCB(gt);
}

void Renderer::Draw()
{
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
	ShowImGUIEnvironmentControl();

	showImgui = true;
	ImGui::Render();

	auto cmdListAlloc = m_CurrentFrameResource->CmdListAlloc;

	ThrowIfFailed(cmdListAlloc->Reset());

	ThrowIfFailed(m_CommandList->Reset(cmdListAlloc.Get(), m_PipelineStateObjects["opaque"].Get()));

	if (m_NeedRegen)
	{
		FlushCommandQueue();
		m_TerrainConstantsCB.gTerrainSize = XMFLOAT2(m_TerrainWidth, m_TerrainHeight);
		m_TerrainConstantsCPU.gHeightScale = m_TerrainHeightScale;
		RegenerateHeightMap();
		UpdateHeightMapTexture();
		UpdateHeightMapSrv();
		RebuildLandGeometry(m_TerrainConstantsCB.gTerrainSize.x, m_TerrainConstantsCB.gTerrainSize.y);
		RebuildLandRenderItem();
		m_NeedRegen = false;
	}
	UpdateTerrainCB();

	D3D12_VIEWPORT vp;
	vp.TopLeftX = 0.0f;
	vp.TopLeftY = 0.0f;
	vp.Width = m_ClientWidth;
	vp.Height = m_ClientHeight;
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;

	m_CommandList->RSSetViewports(1, &vp);

	m_ScissorRect = { 0, 0, static_cast<long>(m_ClientWidth), static_cast<long>(m_ClientHeight) };
	m_CommandList->RSSetScissorRects(1, &m_ScissorRect);

	m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	m_CommandList->ClearRenderTargetView(CurrentBackBufferView(), DirectX::Colors::Fuchsia, 0, nullptr);
	m_CommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	m_CommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	if (m_WireframeMode)
	{
		m_CommandList->SetPipelineState(m_PipelineStateObjects["wireframe"].Get());
	}
	else
	{
		m_CommandList->SetPipelineState(m_PipelineStateObjects["opaque"].Get());
	}
	m_CommandList->SetGraphicsRootSignature(m_OpaqueRootSignature.Get());


	auto passCB = m_CurrentFrameResource->PassCB->Resource();
	m_CommandList->SetGraphicsRootConstantBufferView(3, passCB->GetGPUVirtualAddress());
	auto terrainCB = m_CurrentFrameResource->TerrainCB->Resource();
	m_CommandList->SetGraphicsRootConstantBufferView(4, terrainCB->GetGPUVirtualAddress());

	ID3D12DescriptorHeap* descriptorHeaps[] = { m_TexSrvHeap.Get() };
	m_CommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
	D3D12_GPU_DESCRIPTOR_HANDLE tex = m_TexSrvHeap->GetGPUDescriptorHandleForHeapStart();
	m_CommandList->SetGraphicsRootDescriptorTable(0, tex);


	DrawRenderItems(m_CommandList.Get(), m_OpaqueRenderItems);

	m_CommandList->SetPipelineState(m_PipelineStateObjects["sky"].Get());
	m_CommandList->SetGraphicsRootSignature(m_OpaqueRootSignature.Get());
	m_CommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
	passCB = m_CurrentFrameResource->PassCB->Resource();
	m_CommandList->SetGraphicsRootConstantBufferView(3, passCB->GetGPUVirtualAddress());
	tex = m_TexSrvHeap->GetGPUDescriptorHandleForHeapStart();
	m_CommandList->SetGraphicsRootDescriptorTable(0, tex);

	DrawRenderItems(m_CommandList.Get(), m_SkyRenderItems);

	m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_DepthStencilBuffer.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
	m_CommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	m_CommandList->SetPipelineState(m_PipelineStateObjects["water"].Get());
	m_CommandList->SetGraphicsRootSignature(m_TransparentRootSignature.Get());
	ID3D12DescriptorHeap* descriptorHeaps2[] = { m_SrvHeap.Get() };
	m_CommandList->SetDescriptorHeaps(_countof(descriptorHeaps2), descriptorHeaps2);

	passCB = m_CurrentFrameResource->PassCB->Resource();
	m_CommandList->SetGraphicsRootConstantBufferView(3, passCB->GetGPUVirtualAddress());
	auto waterCB = m_CurrentFrameResource->WaterCB->Resource();
	m_CommandList->SetGraphicsRootConstantBufferView(4, waterCB->GetGPUVirtualAddress());
	tex = m_SrvHeap->GetGPUDescriptorHandleForHeapStart();
	m_CommandList->SetGraphicsRootDescriptorTable(0, tex);

	DrawRenderItems(m_CommandList.Get(), m_TransparentRenderItems);
	m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_DepthStencilBuffer.Get(), D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE));

	m_CommandList->SetDescriptorHeaps(1, m_ImGuiSrvHeap.GetAddressOf());
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_CommandList.Get());


	m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));
	ThrowIfFailed(m_CommandList->Close());

	ID3D12CommandList* cmdLists[] = { m_CommandList.Get() };
	m_CommandQueue->ExecuteCommandLists(_countof(cmdLists), cmdLists);

	ThrowIfFailed(m_SwapChain->Present(0, 0));
	m_CurrentBackBuffer = (m_CurrentBackBuffer + 1) % SwapChainBufferCount;

	m_CurrentFrameResource->Fence = ++m_CurrentFence;

	m_CommandQueue->Signal(m_Fence.Get(), m_CurrentFence);

	//	ThrowIfFailed(cmdListAlloc->Reset());


}
void Renderer::CreateDebugController()
{
	ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&m_DebugController)));
	m_DebugController->EnableDebugLayer();
}

void Renderer::CreateDevice()
{
	HRESULT hardwareResult = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_2, IID_PPV_ARGS(&m_Device));

	if (FAILED(hardwareResult))
	{
		ThrowIfFailed(m_DxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&m_WarpAdapter)));

		ThrowIfFailed(D3D12CreateDevice(m_WarpAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_Device)));
	}
}

void Renderer::CreateFence()
{
	ThrowIfFailed(m_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_Fence)));
}

void Renderer::GetDescriptorSizes()
{
	m_RtvDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	m_DsvDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	m_CbvSrvUavDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void Renderer::CheckMSAAQuality()
{
	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msQualityLevels;
	msQualityLevels.Format = m_BackBufferFormat;
	msQualityLevels.SampleCount = 4;
	msQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
	msQualityLevels.NumQualityLevels = 0;

	ThrowIfFailed(m_Device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &msQualityLevels, sizeof(msQualityLevels)));

	m_4xMsaaQuality = msQualityLevels.NumQualityLevels;

	assert(m_4xMsaaQuality > 0 && "Unexpected MSAA quality level.");
}

void Renderer::CreateCommandObjects()
{
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

	ThrowIfFailed(m_Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_CommandQueue)));

	ThrowIfFailed(m_Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(m_CommandAllocator.GetAddressOf())));

	ThrowIfFailed(m_Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_CommandAllocator.Get(), nullptr, IID_PPV_ARGS(m_CommandList.GetAddressOf())));

	//	m_CommandList->Close();
}

void Renderer::CreateSwapChain(HWND& windowHandle)
{
	m_SwapChain.Reset();

	DXGI_SWAP_CHAIN_DESC swapChainDesc;
	swapChainDesc.BufferDesc.Width = m_ClientWidth;
	swapChainDesc.BufferDesc.Height = m_ClientHeight;
	swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
	swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
	swapChainDesc.BufferDesc.Format = m_BackBufferFormat;
	swapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	swapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.SampleDesc.Quality = 0;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = SwapChainBufferCount;
	swapChainDesc.OutputWindow = m_Hwnd;
	swapChainDesc.Windowed = true;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	ThrowIfFailed(m_DxgiFactory->CreateSwapChain(m_CommandQueue.Get(), &swapChainDesc, m_SwapChain.GetAddressOf()));
}

void Renderer::CreateRtvAndDsvDescriptorHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
	rtvHeapDesc.NumDescriptors = SwapChainBufferCount;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.NodeMask = 0;

	ThrowIfFailed(m_Device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(m_RtvHeap.GetAddressOf())));

	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
	dsvHeapDesc.NumDescriptors = 1;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvHeapDesc.NodeMask = 0;

	ThrowIfFailed(m_Device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(m_DsvHeap.GetAddressOf())));
}

D3D12_CPU_DESCRIPTOR_HANDLE Renderer::CurrentBackBufferView() const
{
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(m_RtvHeap->GetCPUDescriptorHandleForHeapStart(), m_CurrentBackBuffer, m_RtvDescriptorSize);
}

D3D12_CPU_DESCRIPTOR_HANDLE Renderer::DepthStencilView() const
{
	return m_DsvHeap->GetCPUDescriptorHandleForHeapStart();
}

void Renderer::CreateRenderTargetView()
{
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(m_RtvHeap->GetCPUDescriptorHandleForHeapStart());

	for (UINT i = 0; i < SwapChainBufferCount; i++)
	{
		ThrowIfFailed(m_SwapChain->GetBuffer(i, IID_PPV_ARGS(&m_SwapChainBuffer[i])));

		m_Device->CreateRenderTargetView(m_SwapChainBuffer[i].Get(), nullptr, rtvHeapHandle);

		rtvHeapHandle.Offset(m_RtvDescriptorSize);
	}
}

void Renderer::CreateDepthStencilView()
{
	D3D12_RESOURCE_DESC depthStencilDesc;
	depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthStencilDesc.Alignment = 0;
	depthStencilDesc.Width = m_ClientWidth;
	depthStencilDesc.Height = m_ClientHeight;
	depthStencilDesc.DepthOrArraySize = 1;
	depthStencilDesc.MipLevels = 1;
	depthStencilDesc.Format = m_DepthStencilFormat;
	depthStencilDesc.SampleDesc.Count = 1;
	depthStencilDesc.SampleDesc.Quality = 0;
	depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE optClear;
	optClear.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	optClear.DepthStencil.Depth = 1.0f;
	optClear.DepthStencil.Stencil = 0;

	ThrowIfFailed(m_Device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, &depthStencilDesc, D3D12_RESOURCE_STATE_COMMON, &optClear, IID_PPV_ARGS(m_DepthStencilBuffer.GetAddressOf())));

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.Texture2D.MipSlice = 0;

	m_Device->CreateDepthStencilView(m_DepthStencilBuffer.Get(), &dsvDesc, DepthStencilView());

	m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_DepthStencilBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE));
}

void Renderer::CreateIndexBufferView()
{
	m_IbView.BufferLocation = m_IndexBufferGPU->GetGPUVirtualAddress();
	m_IbView.Format = DXGI_FORMAT_R16_UINT;
	m_IbView.SizeInBytes = m_IbByteSize;

	D3D12_INDEX_BUFFER_VIEW indexBuffers[1] = { m_IbView };
	m_CommandList->IASetIndexBuffer(indexBuffers);
}

void Renderer::CreateCbvDescriptorHeaps()
{
	UINT objectCount = (UINT)m_OpaqueRenderItems.size() + (UINT)m_TransparentRenderItems.size();

	UINT numDescriptors = (objectCount + 1) * gNumFrameResources + (m_TransparentRenderItems.size() * 3);

	m_PassCbvOffset = objectCount * gNumFrameResources;

	m_WaterCbvOffset = m_PassCbvOffset + gNumFrameResources;

	D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
	cbvHeapDesc.NumDescriptors = numDescriptors;
	cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	cbvHeapDesc.NodeMask = 0;

	m_Device->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&m_CbvHeap));
}

void Renderer::CreateConstantBufferViews()
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

	UINT objectCount = (UINT)m_OpaqueRenderItems.size() + (UINT)m_TransparentRenderItems.size();

	for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex)
	{
		auto objectCB = m_FrameResources[frameIndex]->ObjectCB->Resource();

		for (UINT i = 0; i < objectCount; ++i)
		{
			D3D12_GPU_VIRTUAL_ADDRESS cBufAddress = objectCB->GetGPUVirtualAddress();

			cBufAddress += i * objCBByteSize;

			int heapIndex = frameIndex * objectCount + i;

			auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_CbvHeap->GetCPUDescriptorHandleForHeapStart());

			handle.Offset(heapIndex * m_CbvSrvUavDescriptorSize);

			D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
			cbvDesc.BufferLocation = cBufAddress;
			cbvDesc.SizeInBytes = objCBByteSize;

			m_Device->CreateConstantBufferView(&cbvDesc, handle);
		}
	}

	UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));

	for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex)
	{
		auto passCB = m_FrameResources[frameIndex]->PassCB->Resource();

		D3D12_GPU_VIRTUAL_ADDRESS passBufAddress = passCB->GetGPUVirtualAddress();

		int heapIndex = m_PassCbvOffset + frameIndex;

		auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_CbvHeap->GetCPUDescriptorHandleForHeapStart());
		handle.Offset(heapIndex, m_CbvSrvUavDescriptorSize);

		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
		cbvDesc.BufferLocation = passBufAddress;
		cbvDesc.SizeInBytes = passCBByteSize;

		m_Device->CreateConstantBufferView(&cbvDesc, handle);
	}

	UINT waterCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(WaterConstants));
	for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex)
	{
		for (size_t i = 0; i < m_TransparentRenderItems.size(); ++i)
		{
			auto waterCB = m_FrameResources[frameIndex]->WaterCB->Resource();
			D3D12_GPU_VIRTUAL_ADDRESS waterBufAddress = waterCB->GetGPUVirtualAddress();
			waterBufAddress += i * waterCBByteSize;
			int heapIndex = m_WaterCbvOffset + frameIndex * (UINT)m_TransparentRenderItems.size() + (UINT)i;
			auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_CbvHeap->GetCPUDescriptorHandleForHeapStart());
			handle.Offset(heapIndex, m_CbvSrvUavDescriptorSize);
			D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
			cbvDesc.BufferLocation = waterBufAddress;
			cbvDesc.SizeInBytes = waterCBByteSize;
			m_Device->CreateConstantBufferView(&cbvDesc, handle);
		}
	}
}

void Renderer::LoadTextures()
{
	auto grassTex = std::make_unique<Texture>();
	grassTex->Name = "grassTex";
	grassTex->Filename = L"../../Textures/Grass/grass4k.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(m_Device.Get(),
		m_CommandList.Get(), grassTex->Filename.c_str(),
		grassTex->Resource, grassTex->UploadHeap));

	m_Textures[grassTex->Name] = std::move(grassTex);

	auto skyCubeMap = std::make_unique<Texture>();
	skyCubeMap->Name = "skyCubeMap";
	skyCubeMap->Filename = L"../../Textures/grasscube1024.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(m_Device.Get(),
		m_CommandList.Get(), skyCubeMap->Filename.c_str(),
		skyCubeMap->Resource, skyCubeMap->UploadHeap));

	m_Textures[skyCubeMap->Name] = std::move(skyCubeMap);

	auto grassNorm = std::make_unique<Texture>();
	grassNorm->Name = "grassNorm";
	grassNorm->Filename = L"../../Textures/Grass/grassnorm4k.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(m_Device.Get(),
		m_CommandList.Get(), grassNorm->Filename.c_str(),
		grassNorm->Resource, grassNorm->UploadHeap));

	m_Textures[grassNorm->Name] = std::move(grassNorm);

	auto mud = std::make_unique<Texture>();
	mud->Name = "wetmud";
	mud->Filename = L"../../Textures/Mud/mud4k.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(m_Device.Get(),
		m_CommandList.Get(), mud->Filename.c_str(),
		mud->Resource, mud->UploadHeap));

	m_Textures[mud->Name] = std::move(mud);

	auto wetmudNorm = std::make_unique<Texture>();
	wetmudNorm->Name = "wetmud_norm";
	wetmudNorm->Filename = L"../../Textures/Mud/mudnorm4k.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(m_Device.Get(),
		m_CommandList.Get(), wetmudNorm->Filename.c_str(),
		wetmudNorm->Resource, wetmudNorm->UploadHeap));

	m_Textures[wetmudNorm->Name] = std::move(wetmudNorm);

	auto rock = std::make_unique<Texture>();
	rock->Name = "rock";
	rock->Filename = L"../../Textures/Rock/rock4k.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(m_Device.Get(),
		m_CommandList.Get(), rock->Filename.c_str(),
		rock->Resource, rock->UploadHeap));

	m_Textures[rock->Name] = std::move(rock);

	auto rockNorm = std::make_unique<Texture>();
	rockNorm->Name = "rockNorm";
	rockNorm->Filename = L"../../Textures/Rock/rocknorm4k.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(m_Device.Get(),
		m_CommandList.Get(), rockNorm->Filename.c_str(),
		rockNorm->Resource, rockNorm->UploadHeap));

	m_Textures[rockNorm->Name] = std::move(rockNorm);
}

void Renderer::createSrvDescriptorHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 1;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(m_Device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_SrvHeap)));

	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(m_SrvHeap->GetCPUDescriptorHandleForHeapStart());

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};



	srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	m_Device->CreateShaderResourceView(m_DepthStencilBuffer.Get(), &srvDesc, hDescriptor);

}

void Renderer::CreateTextureSrvDescriptors()
{
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 8;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(m_Device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_TexSrvHeap)));

	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(m_TexSrvHeap->GetCPUDescriptorHandleForHeapStart());

	auto grassTex = m_Textures["grassTex"]->Resource;
	auto grassNorm = m_Textures["grassNorm"]->Resource;
	auto wetmud = m_Textures["wetmud"]->Resource;
	auto wetmud_norm = m_Textures["wetmud_norm"]->Resource;
	auto skyCubeMap = m_Textures["skyCubeMap"]->Resource;
	auto rock = m_Textures["rock"]->Resource;
	auto rockNorm = m_Textures["rockNorm"]->Resource;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = grassTex->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = grassTex->GetDesc().MipLevels;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	m_Device->CreateShaderResourceView(grassTex.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, m_CbvSrvUavDescriptorSize);

	srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = grassNorm->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = grassNorm->GetDesc().MipLevels;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	m_Device->CreateShaderResourceView(grassNorm.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, m_CbvSrvUavDescriptorSize);

	srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = wetmud->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = wetmud->GetDesc().MipLevels;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	m_Device->CreateShaderResourceView(wetmud.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, m_CbvSrvUavDescriptorSize);

	srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = wetmud_norm->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = wetmud_norm->GetDesc().MipLevels;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	m_Device->CreateShaderResourceView(wetmud_norm.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, m_CbvSrvUavDescriptorSize);
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
	srvDesc.TextureCube.MostDetailedMip = 0;
	srvDesc.TextureCube.MipLevels = skyCubeMap->GetDesc().MipLevels;
	srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
	srvDesc.Format = skyCubeMap->GetDesc().Format;
	m_Device->CreateShaderResourceView(skyCubeMap.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, m_CbvSrvUavDescriptorSize);

	srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;


	m_Device->CreateShaderResourceView(m_HeightMapTex.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, m_CbvSrvUavDescriptorSize);

	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.TextureCube.MostDetailedMip = 0;
	srvDesc.TextureCube.MipLevels = rock->GetDesc().MipLevels;
	srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
	srvDesc.Format = rock->GetDesc().Format;


	m_Device->CreateShaderResourceView(rock.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, m_CbvSrvUavDescriptorSize);

	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.TextureCube.MostDetailedMip = 0;
	srvDesc.TextureCube.MipLevels = rockNorm->GetDesc().MipLevels;
	srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
	srvDesc.Format = rockNorm->GetDesc().Format;



	m_Device->CreateShaderResourceView(rockNorm.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, m_CbvSrvUavDescriptorSize);


}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> Renderer::GetStaticSamplers()
{
	// Applications usually only need a handful of samplers.  So just define them all up front
	// and keep them available as part of the root signature.  

	const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
		0, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		1, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		2, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		3, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
		4, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
		0.0f,                             // mipLODBias
		8);                               // maxAnisotropy

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
		5, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
		0.0f,                              // mipLODBias
		8);                                // maxAnisotropy

	return {
		pointWrap, pointClamp,
		linearWrap, linearClamp,
		anisotropicWrap, anisotropicClamp };
}

void Renderer::CreateOpaqueRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable;
	texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 8, 0, 0, 0);

	CD3DX12_ROOT_PARAMETER slotRootParameter[5];

	slotRootParameter[0].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_ALL);
	slotRootParameter[1].InitAsConstantBufferView(0);
	slotRootParameter[2].InitAsConstantBufferView(1);
	slotRootParameter[3].InitAsConstantBufferView(2);
	slotRootParameter[4].InitAsConstantBufferView(3);

	auto staticSamplers = GetStaticSamplers();

	// A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(5, slotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSig = nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;

	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}

	ThrowIfFailed(hr);

	ThrowIfFailed(m_Device->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(m_OpaqueRootSignature.GetAddressOf())));
}

void Renderer::CreateTransparentRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE srvTable;
	srvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0);


	CD3DX12_ROOT_PARAMETER slotRootParameter[5];

	slotRootParameter[0].InitAsDescriptorTable(1, &srvTable, D3D12_SHADER_VISIBILITY_ALL);
	slotRootParameter[1].InitAsConstantBufferView(0);
	slotRootParameter[2].InitAsConstantBufferView(1);
	slotRootParameter[3].InitAsConstantBufferView(2);
	slotRootParameter[4].InitAsConstantBufferView(3);

	auto staticSamplers = GetStaticSamplers();


	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(5, slotRootParameter, (UINT)staticSamplers.size(), staticSamplers.data(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSig = nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;

	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}

	ThrowIfFailed(hr);

	ThrowIfFailed(m_Device->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(m_TransparentRootSignature.GetAddressOf())));
}

void Renderer::BuildShadersAndInputLayout()
{
	const D3D_SHADER_MACRO alphaTestDefines[] =
	{
		"ALPHA_TEST", "1",
		NULL, NULL
	};

	HRESULT hr = S_OK;

	m_VsByteCode = d3dUtil::CompileShader(L"Shaders\\vertex.hlsl", nullptr, "VS", "vs_5_0");
	m_PsByteCode = d3dUtil::CompileShader(L"Shaders\\pixel.hlsl", nullptr, "PS", "ps_5_0");
	m_VsByteCodeWater = d3dUtil::CompileShader(L"Shaders\\vertex_water.hlsl", nullptr, "VS", "vs_5_0");
	m_PsByteCodeWater = d3dUtil::CompileShader(L"Shaders\\pixel_water.hlsl", nullptr, "PS", "ps_5_0");
	m_VsByteCodeSky = d3dUtil::CompileShader(L"Shaders\\vertex_sky.hlsl", nullptr, "VS", "vs_5_0");
	m_PsByteCodeSky = d3dUtil::CompileShader(L"Shaders\\pixel_sky.hlsl", nullptr, "PS", "ps_5_0");



	m_InputLayoutDescs =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};


}
void Renderer::BuildMaterials()
{
	auto sky = std::make_unique<Material>();
	sky->Name = "sky";
	sky->MatCBIndex = 0;
	sky->DiffuseSrvHeapIndex = 1;
	sky->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	sky->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	sky->Roughness = 1.0f;


	auto grass = std::make_unique<Material>();
	grass->Name = "grass";
	grass->MatCBIndex = 1;
	grass->DiffuseSrvHeapIndex = 0;
	grass->DiffuseAlbedo = XMFLOAT4(0.5f, 0.6f, 0.1f, 1.0f);
	grass->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
	grass->Roughness = 0.125f;

	auto skullMat = std::make_unique<Material>();
	skullMat->Name = "skullMat";
	skullMat->MatCBIndex = 2;
	skullMat->DiffuseSrvHeapIndex = 2;
	skullMat->DiffuseAlbedo = XMFLOAT4(0.3f, 0.33f, 0.31f, 1.0f);
	skullMat->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
	skullMat->Roughness = 0.25f;

	auto water = std::make_unique<Material>();
	water->Name = "water";
	water->MatCBIndex = 3;
	water->DiffuseSrvHeapIndex = 3;
	water->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	water->FresnelR0 = XMFLOAT3(0.2f, 0.2f, 0.2f);
	water->Roughness = 0.0f;

	m_Materials["sky"] = std::move(sky);
	m_Materials["grass"] = std::move(grass);
	m_Materials["skullMat"] = std::move(skullMat);
	m_Materials["water"] = std::move(water);
}
void Renderer::BuildShapeGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.5f, 0.5f, 1.5f, 3);
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.0f, 30.0f, 60, 40);
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);

	//
	// We are concatenating all the geometry into one big vertex/index buffer.  So
	// define the regions in the buffer each submesh covers.
	//

	// Cache the vertex offsets to each object in the concatenated vertex buffer.
	UINT boxVertexOffset = 0;
	UINT gridVertexOffset = (UINT)box.Vertices.size();
	UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
	UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();

	// Cache the starting index for each object in the concatenated index buffer.
	UINT boxIndexOffset = 0;
	UINT gridIndexOffset = (UINT)box.Indices32.size();
	UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
	UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();

	// Define the SubmeshGeometry that cover different 
	// regions of the vertex/index buffers.

	SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = (UINT)box.Indices32.size();
	boxSubmesh.StartIndexLocation = boxIndexOffset;
	boxSubmesh.BaseVertexLocation = boxVertexOffset;

	SubmeshGeometry gridSubmesh;
	gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
	gridSubmesh.StartIndexLocation = gridIndexOffset;
	gridSubmesh.BaseVertexLocation = gridVertexOffset;

	SubmeshGeometry sphereSubmesh;
	sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
	sphereSubmesh.StartIndexLocation = sphereIndexOffset;
	sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

	SubmeshGeometry cylinderSubmesh;
	cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
	cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
	cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;

	//
	// Extract the vertex elements we are interested in and pack the
	// vertices of all the meshes into one vertex buffer.
	//

	auto totalVertexCount =
		box.Vertices.size() +
		grid.Vertices.size() +
		sphere.Vertices.size() +
		cylinder.Vertices.size();

	std::vector<Vertex> vertices(totalVertexCount);

	UINT k = 0;
	for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = box.Vertices[i].Position;
		vertices[k].Normal = box.Vertices[i].Normal;
	}

	for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = grid.Vertices[i].Position;
		vertices[k].Normal = grid.Vertices[i].Normal;
	}

	for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Normal = sphere.Vertices[i].Normal;
	}

	for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cylinder.Vertices[i].Position;
		vertices[k].Normal = cylinder.Vertices[i].Normal;
	}

	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
	indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
	indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
	indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "shapeGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(m_Device.Get(),
		m_CommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(m_Device.Get(),
		m_CommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	geo->DrawArgs["box"] = boxSubmesh;
	geo->DrawArgs["grid"] = gridSubmesh;
	geo->DrawArgs["sphere"] = sphereSubmesh;
	geo->DrawArgs["cylinder"] = cylinderSubmesh;

	m_Geometries[geo->Name] = std::move(geo);
}

void Renderer::BuildSkullGeometry()
{
	std::ifstream fin("Models/skull.txt");

	if (!fin)
	{
		MessageBox(0, L"Models/skull.txt not found.", 0, 0);
		return;
	}

	UINT vcount = 0;
	UINT tcount = 0;
	std::string ignore;

	fin >> ignore >> vcount;
	fin >> ignore >> tcount;
	fin >> ignore >> ignore >> ignore >> ignore;

	std::vector<Vertex> vertices(vcount);
	for (UINT i = 0; i < vcount; ++i)
	{
		fin >> vertices[i].Pos.x >> vertices[i].Pos.y >> vertices[i].Pos.z;
		fin >> vertices[i].Normal.x >> vertices[i].Normal.y >> vertices[i].Normal.z;
	}

	fin >> ignore;
	fin >> ignore;
	fin >> ignore;

	std::vector<std::int32_t> indices(3 * tcount);
	for (UINT i = 0; i < tcount; ++i)
	{
		fin >> indices[i * 3 + 0] >> indices[i * 3 + 1] >> indices[i * 3 + 2];
	}

	fin.close();

	//
	// Pack the indices of all the meshes into one index buffer.
	//

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::int32_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "skullGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(m_Device.Get(),
		m_CommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(m_Device.Get(),
		m_CommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R32_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["skull"] = submesh;

	m_Geometries[geo->Name] = std::move(geo);
}

void Renderer::BuildLandGeometry(float width, float height)
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(460.0f, 460.0f, 50, 50);

	std::vector<Vertex> vertices(grid.Vertices.size());
	for (size_t i = 0; i < grid.Vertices.size(); ++i)
	{
		auto& p = grid.Vertices[i].Position;
		vertices[i].Pos = p;
		vertices[i].Pos.y = GetHillsHeight(p.x, p.z);
		XMFLOAT3 n = GetHillsNormal(p.x, p.z);
		vertices[i].Normal = n;
		vertices[i].TexCoord = grid.Vertices[i].TexC;
	}
	//for (size_t i = 0; i < grid.Vertices.size(); ++i)
	//{
	//	auto& p = grid.Vertices[i].Position;
	//	vertices[i].Pos = p;
	//	vertices[i].Pos.y = 1.0f;
	//	XMFLOAT3 n = { 0.0f, 1.0f, 0.0f };
	//	vertices[i].Normal = n;
	//	vertices[i].TexCoord = grid.Vertices[i].TexC;
	//}

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

	std::vector<std::uint16_t> indices = grid.GetIndices16();
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "landGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(m_Device.Get(),
		m_CommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(m_Device.Get(),
		m_CommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["grid"] = submesh;

	m_Geometries["landGeo"] = std::move(geo);
}

void Renderer::RebuildLandGeometry(float width, float height)
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(m_TerrainConstantsCPU.gTerrainSize.x, m_TerrainConstantsCPU.gTerrainSize.y, 50, 50);

	std::vector<Vertex> vertices(grid.Vertices.size());
	for (size_t i = 0; i < grid.Vertices.size(); ++i)
	{
		auto& p = grid.Vertices[i].Position;
		vertices[i].Pos = p;
		vertices[i].Pos.y = GetHillsHeight(p.x, p.z);
		XMFLOAT3 n = GetHillsNormal(p.x, p.z);
		vertices[i].Normal = n;
		vertices[i].TexCoord = grid.Vertices[i].TexC;
	}
	//for (size_t i = 0; i < grid.Vertices.size(); ++i)
	//{
	//	auto& p = grid.Vertices[i].Position;
	//	vertices[i].Pos = p;
	//	vertices[i].Pos.y = 1.0f;
	//	XMFLOAT3 n = { 0.0f, 1.0f, 0.0f };
	//	vertices[i].Normal = n;
	//	vertices[i].TexCoord = grid.Vertices[i].TexC;
	//}

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

	std::vector<std::uint16_t> indices = grid.GetIndices16();
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "landGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(m_Device.Get(),
		m_CommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(m_Device.Get(),
		m_CommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["grid"] = submesh;

	m_Geometries["landGeo"] = std::move(geo);
}


void Renderer::BuildWavesGeometry()
{
	std::vector<std::uint16_t> indices(3 * m_Waves->TriangleCount());
	assert(m_Waves->VertexCount() < 0x0000ffff);

	int m = m_Waves->RowCount();
	int n = m_Waves->ColumnCount();
	int k = 0;
	for (int i = 0; i < m - 1; ++i)
	{
		for (int j = 0; j < n - 1; ++j)
		{
			indices[k] = i * n + j;
			indices[k + 1] = i * n + j + 1;
			indices[k + 2] = (i + 1) * n + j;

			indices[k + 3] = (i + 1) * n + j;
			indices[k + 4] = i * n + j + 1;
			indices[k + 5] = (i + 1) * n + j + 1;

			k += 6;
		}
	}

	UINT vbByteSize = m_Waves->VertexCount() * sizeof(Vertex);
	UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "waterGeo";

	geo->VertexBufferCPU = nullptr;
	geo->VertexBufferGPU = nullptr;

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(m_Device.Get(),
		m_CommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["grid"] = submesh;

	m_Geometries["waterGeo"] = std::move(geo);
}

float Renderer::GetHillsHeight(float x, float z)
{
	return 0.3f * (z * sinf(0.1f * x) + x * cosf(0.1f * z));
}

XMFLOAT3 Renderer::GetHillsNormal(float x, float z)
{
	XMFLOAT3 n(
		-0.03f * z * cosf(0.1f * x) - 0.3f * cosf(0.1f * z),
		1.0f,
		-0.3f * sinf(0.1f * x) + 0.03f * x * sinf(0.1f * z));

	XMVECTOR unitNormal = XMVector3Normalize(XMLoadFloat3(&n));
	XMStoreFloat3(&n, unitNormal);

	return n;
}

void Renderer::RebuildLandRenderItem()
{
	auto isLand = [](RenderItem* ri) { return ri->ObjCBIndex == 1; };

	auto itOpaque = std::remove_if(m_OpaqueRenderItems.begin(), m_OpaqueRenderItems.end(), isLand);
	m_OpaqueRenderItems.erase(itOpaque, m_OpaqueRenderItems.end());

	auto itAll = std::remove_if(m_AllRenderItems.begin(), m_AllRenderItems.end(), isLand);
	m_AllRenderItems.erase(itAll, m_AllRenderItems.end());

	auto landRitem = new RenderItem();
	landRitem->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&landRitem->TexTransform, XMMatrixScaling(8.0f, 8.0f, 10.0f));

	landRitem->ObjCBIndex = 1;
	landRitem->NumFramesDirty = NumFrameResources;
	landRitem->Mat = m_Materials["grass"].get();
	landRitem->Geo = m_Geometries["landGeo"].get();
	landRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	const auto& gridSubmesh = landRitem->Geo->DrawArgs["grid"];
	landRitem->IndexCount = gridSubmesh.IndexCount;
	landRitem->StartIndexLocation = gridSubmesh.StartIndexLocation;
	landRitem->BaseVertexLocation = gridSubmesh.BaseVertexLocation;

	m_OpaqueRenderItems.push_back(landRitem);
	m_AllRenderItems.push_back(landRitem);

}

void Renderer::RebuildFrameResources()
{
	m_FrameResources.clear();

	UINT passCount = gNumFrameResources;
	UINT objectCount = (UINT)m_AllRenderItems.size();
	UINT materialCount = (UINT)m_Materials.size();
	UINT opaqueObjectCount = (UINT)m_OpaqueRenderItems.size();
	UINT transparentObjectCount = (UINT)m_TransparentRenderItems.size();
	UINT skyObjectCount = (UINT)m_SkyRenderItems.size();

	for (int i = 0; i < gNumFrameResources; ++i)
	{
		m_FrameResources.push_back(
			std::make_unique<FrameResource>(
				m_Device.Get(), passCount, opaqueObjectCount, transparentObjectCount, skyObjectCount, materialCount, m_Waves->VertexCount()));
	}

	m_CurrentFrameResourceIndex = 0;
	m_CurrentFrameResource = m_FrameResources[0].get();
}

void Renderer::BuildRenderItems()
{
	auto skyRitem = new RenderItem();
	XMStoreFloat4x4(&skyRitem->World, XMMatrixScaling(5000.0f, 5000.0f, 5000.0f));
	skyRitem->TexTransform = MathHelper::Identity4x4();
	skyRitem->ObjCBIndex = 0;
	skyRitem->Mat = m_Materials["sky"].get();
	skyRitem->Geo = m_Geometries["shapeGeo"].get();
	skyRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	skyRitem->IndexCount = skyRitem->Geo->DrawArgs["sphere"].IndexCount;
	skyRitem->StartIndexLocation = skyRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
	skyRitem->BaseVertexLocation = skyRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;
	m_SkyRenderItems.push_back(std::move(skyRitem));

	auto gridRitem = new RenderItem();
	gridRitem->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&gridRitem->TexTransform, XMMatrixScaling(8.0f, 8.0f, 10.0f));
	gridRitem->ObjCBIndex = 1;
	gridRitem->Mat = m_Materials["grass"].get();
	gridRitem->Geo = m_Geometries["landGeo"].get();
	gridRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
	gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
	m_OpaqueRenderItems.push_back(std::move(gridRitem));

	auto skullRitem = new RenderItem();
	XMStoreFloat4x4(&skullRitem->TexTransform, XMMatrixScaling(8.0f, 8.0f, 1.0f));
	skullRitem->ObjCBIndex = 2;
	skullRitem->Mat = m_Materials["skullMat"].get();
	skullRitem->Geo = m_Geometries["skullGeo"].get();
	skullRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	skullRitem->IndexCount = skullRitem->Geo->DrawArgs["skull"].IndexCount;
	skullRitem->StartIndexLocation = skullRitem->Geo->DrawArgs["skull"].StartIndexLocation;
	skullRitem->BaseVertexLocation = skullRitem->Geo->DrawArgs["skull"].BaseVertexLocation;
	m_OpaqueRenderItems.push_back(std::move(skullRitem));

	auto wavesRitem = new RenderItem();
	XMStoreFloat4x4(&wavesRitem->World, XMMatrixScaling(10.0f, 1.0f, 10.0f) * XMMatrixTranslation(0.0f, 50.0f, 0.0f));
	XMStoreFloat4x4(&wavesRitem->TexTransform, XMMatrixScaling(5.0f, 5.0f, 1.0f));
	wavesRitem->ObjCBIndex = 3;
	wavesRitem->Mat = m_Materials["water"].get();
	wavesRitem->Geo = m_Geometries["waterGeo"].get();
	wavesRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wavesRitem->IndexCount = wavesRitem->Geo->DrawArgs["grid"].IndexCount;
	wavesRitem->StartIndexLocation = wavesRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	wavesRitem->BaseVertexLocation = wavesRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

	m_WavesRitem = wavesRitem;
	m_TransparentRenderItems.push_back(std::move(wavesRitem));

	for (auto& e : m_SkyRenderItems)
		m_AllRenderItems.push_back(e);

	for (auto& e : m_OpaqueRenderItems)
		m_AllRenderItems.push_back(e);

	for (auto& e : m_TransparentRenderItems)
		m_AllRenderItems.push_back(e);
}

void Renderer::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& riItems)
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

	auto objectCB = m_CurrentFrameResource->ObjectCB->Resource();
	auto matCB = m_CurrentFrameResource->MaterialCB->Resource();

	for (size_t i = 0; i < riItems.size(); ++i)
	{
		auto ri = riItems[i];

		cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
		cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;
		D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex * matCBByteSize;

		if (ri->Geo->Name == "waterGeo")
		{

			cmdList->SetGraphicsRootConstantBufferView(1, objCBAddress);
			cmdList->SetGraphicsRootConstantBufferView(2, matCBAddress);
		}
		else
		{
			cmdList->SetGraphicsRootConstantBufferView(1, objCBAddress);
			cmdList->SetGraphicsRootConstantBufferView(2, matCBAddress);
		}

		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}

void Renderer::BuildPSOs()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;
	;
	ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	opaquePsoDesc.InputLayout = {
		m_InputLayoutDescs.data(), (UINT)m_InputLayoutDescs.size()
	};
	opaquePsoDesc.pRootSignature = m_OpaqueRootSignature.Get();
	opaquePsoDesc.VS = {
		reinterpret_cast<BYTE*>(m_VsByteCode->GetBufferPointer()),
		m_VsByteCode->GetBufferSize()
	};
	opaquePsoDesc.PS = {
		reinterpret_cast<BYTE*>(m_PsByteCode->GetBufferPointer()),
		m_PsByteCode->GetBufferSize()
	};
	opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	opaquePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	opaquePsoDesc.SampleMask = UINT_MAX;
	opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	opaquePsoDesc.NumRenderTargets = 1;
	opaquePsoDesc.RTVFormats[0] = m_BackBufferFormat;
	opaquePsoDesc.SampleDesc.Count = 1;
	opaquePsoDesc.SampleDesc.Quality = 0;
	opaquePsoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

	ThrowIfFailed(m_Device->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&m_PipelineStateObjects["opaque"])));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC wireframePsoDesc = opaquePsoDesc;
	wireframePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
	ThrowIfFailed(m_Device->CreateGraphicsPipelineState(&wireframePsoDesc, IID_PPV_ARGS(&m_PipelineStateObjects["wireframe"])));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC skyPsoDesc = opaquePsoDesc;
	skyPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	skyPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	skyPsoDesc.pRootSignature = m_OpaqueRootSignature.Get();
	skyPsoDesc.VS = {
		reinterpret_cast<BYTE*>(m_VsByteCodeSky->GetBufferPointer()),
		m_VsByteCodeSky->GetBufferSize()
	};
	skyPsoDesc.PS = {
		reinterpret_cast<BYTE*>(m_PsByteCodeSky->GetBufferPointer()),
		m_PsByteCodeSky->GetBufferSize()
	};

	ThrowIfFailed(m_Device->CreateGraphicsPipelineState(&skyPsoDesc, IID_PPV_ARGS(&m_PipelineStateObjects["sky"])));


	D3D12_GRAPHICS_PIPELINE_STATE_DESC waterPsoDesc = opaquePsoDesc;
	waterPsoDesc.pRootSignature = m_TransparentRootSignature.Get();
	waterPsoDesc.VS = {
		reinterpret_cast<BYTE*>(m_VsByteCodeWater->GetBufferPointer()),
		m_VsByteCodeWater->GetBufferSize()
	};
	waterPsoDesc.PS = {
		reinterpret_cast<BYTE*>(m_PsByteCodeWater->GetBufferPointer()),
		m_PsByteCodeWater->GetBufferSize()
	};
	waterPsoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
	waterPsoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	waterPsoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	waterPsoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	waterPsoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	waterPsoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
	waterPsoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	waterPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	waterPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	waterPsoDesc.DepthStencilState.DepthEnable = TRUE;
	waterPsoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	ThrowIfFailed(m_Device->CreateGraphicsPipelineState(&waterPsoDesc, IID_PPV_ARGS(&m_PipelineStateObjects["water"])));

}
void Renderer::BuildFrameResources()
{
	for (int i = 0; i < NumFrameResources; ++i)
	{
		m_FrameResources.push_back(std::make_unique<FrameResource>(m_Device.Get(), 1, (UINT)m_OpaqueRenderItems.size(), (UINT)m_TransparentRenderItems.size(), (UINT)m_SkyRenderItems.size(), (UINT)m_Materials.size(), m_Waves->VertexCount()));
	}
}
void Renderer::UpdateObjectCBs()
{
	auto currObjectCB = m_CurrentFrameResource->ObjectCB.get();

	for (auto& e : m_AllRenderItems)
	{
		if (e->NumFramesDirty)
		{
			XMMATRIX world = XMLoadFloat4x4(&e->World);
			XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));

			currObjectCB->CopyData(e->ObjCBIndex, objConstants);

			e->NumFramesDirty--;
		}
	}
}

void Renderer::UpdateMaterialCBs()
{
	auto curretMaterialCB = m_CurrentFrameResource->MaterialCB.get();

	for (auto& e : m_Materials)
	{
		Material* mat = e.second.get();

		if (mat->NumFramesDirty > 0)
		{
			XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

			MaterialConstants matConstants;
			matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
			matConstants.FresnelR0 = mat->FresnelR0;
			matConstants.Roughness = mat->Roughness;
			XMStoreFloat4x4(&matConstants.MatTransform, XMMatrixTranspose(matTransform));

			curretMaterialCB->CopyData(mat->MatCBIndex, matConstants);

			mat->NumFramesDirty--;
		}
	}
}
void Renderer::UpdateMainPassCB()
{
	XMMATRIX view = XMLoadFloat4x4(&m_View);
	XMMATRIX proj = XMLoadFloat4x4(&m_Proj);


	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
	XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

	XMStoreFloat4x4(&m_MainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&m_MainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&m_MainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&m_MainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&m_MainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&m_MainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));

	m_MainPassCB.EyePosW = m_Camera.GetPosition3f();
	m_MainPassCB.RenderTargetSize = XMFLOAT2((float)m_ClientWidth, (float)m_ClientHeight);
	m_MainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / m_ClientWidth, 1.0f / m_ClientHeight);
	m_MainPassCB.NearZ = 1.0f;
	m_MainPassCB.FarZ = 1000.0f;
	m_MainPassCB.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };
	m_MainPassCB.FogColor = { 0.7f, 0.7f, 0.7f, 1.0f };
	m_MainPassCB.FogStart = 5.0f;
	m_MainPassCB.FogRange = 150.0f;

	m_MainPassCB.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
	m_MainPassCB.Lights[0].Strength = { 0.6f, 0.6f, 0.6f };
	m_MainPassCB.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
	m_MainPassCB.Lights[1].Strength = { 0.3f, 0.3f, 0.3f };
	m_MainPassCB.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
	m_MainPassCB.Lights[2].Strength = { 0.15f, 0.15f, 0.15f };

	auto currPassCB = m_CurrentFrameResource->PassCB.get();
	currPassCB->CopyData(0, m_MainPassCB);
}

void Renderer::UpdateTerrainCB()
{
	// Height bands as fractions of [minH, maxH]
	const float mudStartFrac = 0.15f;
	const float grassStartFrac = 0.45f;
	const float rockStartFrac = 0.70f;
	const float mudRepeatSize = 16.0f;
	const float grassRepeatSize = 8.0f;
	const float rockRepeatSize = 12.0f;

	const float blendFrac = 0.06f;
	m_TerrainConstantsCPU.gHeightOffset = 0.0f;
	m_TerrainConstantsCPU.gHeightScale = m_TerrainHeightScale;

	float minH = m_TerrainConstantsCPU.gHeightOffset;
	;
	float maxH = m_TerrainConstantsCPU.gHeightOffset + m_TerrainConstantsCPU.gHeightScale;
	float rangeH = maxH - minH;

	m_TerrainConstantsCPU.gMudStartHeight = minH + mudStartFrac * rangeH;
	m_TerrainConstantsCPU.gGrassStartHeight = minH + grassStartFrac * rangeH;
	m_TerrainConstantsCPU.gRockStartHeight = minH + rockStartFrac * rangeH;
	m_TerrainConstantsCPU.gHeightBlendRange = blendFrac * rangeH;
	m_TerrainConstantsCPU.gMudSlopeBias = 0.15f;
	m_TerrainConstantsCPU.gMudSlopePower = 1.8f;
	m_TerrainConstantsCPU.gRockSlopeBias = 0.3f;
	m_TerrainConstantsCPU.gRockSlopePower = 3.0f;

	m_TerrainConstantsCPU.gMudTiling = std::max(1.0f, m_TerrainConstantsCPU.gTerrainSize.x / mudRepeatSize);
	m_TerrainConstantsCPU.gGrassTiling = std::max(1.0f, m_TerrainConstantsCPU.gTerrainSize.x / grassRepeatSize);
	m_TerrainConstantsCPU.gRockTiling = std::max(1.0f, m_TerrainConstantsCPU.gTerrainSize.x / rockRepeatSize);

	m_TerrainConstantsCB.gTerrainSize = m_TerrainConstantsCPU.gTerrainSize;
	m_TerrainConstantsCB.gHeightScale = m_TerrainConstantsCPU.gHeightScale;
	m_TerrainConstantsCB.gHeightOffset = m_TerrainConstantsCPU.gHeightOffset;

	m_TerrainConstantsCB.gMudStartHeight = m_TerrainConstantsCPU.gMudStartHeight;
	m_TerrainConstantsCB.gGrassStartHeight = m_TerrainConstantsCPU.gGrassStartHeight;

	m_TerrainConstantsCB.gRockStartHeight = m_TerrainConstantsCPU.gRockStartHeight;
	m_TerrainConstantsCB.gHeightBlendRange = m_TerrainConstantsCPU.gHeightBlendRange;

	m_TerrainConstantsCB.gMudSlopeBias = m_TerrainConstantsCPU.gMudSlopeBias;
	m_TerrainConstantsCB.gMudSlopePower = m_TerrainConstantsCPU.gMudSlopePower;

	m_TerrainConstantsCB.gRockSlopeBias = m_TerrainConstantsCPU.gRockSlopeBias;
	m_TerrainConstantsCB.gRockSlopePower = m_TerrainConstantsCPU.gRockSlopePower;

	m_TerrainConstantsCB.gMudTiling = m_TerrainConstantsCPU.gMudTiling;
	m_TerrainConstantsCB.gGrassTiling = m_TerrainConstantsCPU.gGrassTiling;

	m_TerrainConstantsCB.gRockTiling = m_TerrainConstantsCPU.gRockTiling;
	m_TerrainConstantsCB.gPad = m_TerrainConstantsCPU.gPad;

	auto currTerrainCB = m_CurrentFrameResource->TerrainCB.get();
	currTerrainCB->CopyData(0, m_TerrainConstantsCB);
}

void Renderer::UpdateWaterCB(GameTimer& dt)
{
	for (int i = 0; i < m_TransparentRenderItems.size(); ++i)
	{
		XMMATRIX world = XMMatrixIdentity() * XMMatrixTranslation(m_WaterHeight[0], m_WaterHeight[1], m_WaterHeight[2]);
		XMStoreFloat4x4(&m_WaterConstantsCB.gWorld, world);
		XMStoreFloat4x4(&m_WaterConstantsCB.gViewProj, XMMatrixMultiply(XMMatrixTranspose(XMLoadFloat4x4(&m_View)), XMMatrixTranspose(XMLoadFloat4x4(&m_Proj))));

		m_WaterConstantsCB.gCameraPos = m_EyePos;
		m_WaterConstantsCB.gTime = dt.TotalTime();
		m_WaterConstantsCB.gWaterColor = XMFLOAT3(0.65f, 0.75f, 0.90f);
		m_WaterConstantsCB.gPad0 = 0.0f;
		auto currWaterCB = m_CurrentFrameResource->WaterCB.get();
		currWaterCB->CopyData(i, m_WaterConstantsCB);
	}
}

void Renderer::UpdateWaves(GameTimer& gt)
{
	static float t_base = 0.0f;
	if ((gt.TotalTime() - t_base) >= 0.25f)
	{
		t_base += 0.25f;

		int i = MathHelper::Rand(4, m_Waves->RowCount() - 5);
		int j = MathHelper::Rand(4, m_Waves->ColumnCount() - 5);

		float r = MathHelper::RandF(0.2f, 0.5f);

		m_Waves->Disturb(i, j, r);
	}

	// Update the wave simulation.
	m_Waves->Update(gt.DeltaTime());

	// Update the wave vertex buffer with the new solution.
	auto currWavesVB = m_CurrentFrameResource->WavesVB.get();
	for (int i = 0; i < m_Waves->VertexCount(); ++i)
	{
		Vertex v;

		v.Pos = m_Waves->Position(i);
		v.Normal = m_Waves->Normal(i);

		// Derive tex-coords from position by 
		// mapping [-w/2,w/2] --> [0,1]
		v.TexCoord.x = 0.5f + v.Pos.x / m_Waves->Width();
		v.TexCoord.y = 0.5f - v.Pos.z / m_Waves->Depth();

		currWavesVB->CopyData(i, v);
	}

	// Set the dynamic VB of the wave renderitem to the current frame VB.
	m_WavesRitem->Geo->VertexBufferGPU = currWavesVB->Resource();
};


void Renderer::CreateVertexBufferView()
{
	m_VbView.BufferLocation = m_VertexBufferGPU->GetGPUVirtualAddress();
	m_VbView.StrideInBytes = sizeof(Vertex);
	m_VbView.SizeInBytes = m_VbByteSize;

	D3D12_VERTEX_BUFFER_VIEW vertexBuffers[1] = { m_VbView };
	m_CommandList->IASetVertexBuffers(0, 1, vertexBuffers);
}

void Renderer::FlushCommandQueue()
{
	m_CurrentFence++;

	ThrowIfFailed(m_CommandQueue->Signal(m_Fence.Get(), m_CurrentFence));

	if (m_Fence->GetCompletedValue() < m_CurrentFence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);

		ThrowIfFailed(m_Fence->SetEventOnCompletion(m_CurrentFence, eventHandle));

		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}
}

ID3D12Resource* Renderer::CurrentBackBuffer() const
{
	return m_SwapChainBuffer[m_CurrentBackBuffer].Get();
}

void Renderer::ShowImGUIEnvironmentControl()
{
	ImGui::Begin("Landscape Control");

	if (ImGui::CollapsingHeader("Water Settings"))
	{
		ImGui::SliderFloat3("Water Position", m_WaterHeight, -40.0f, 150.0f);
		ImGui::SliderFloat3("Water Scale", m_WaterScale, -50.0f, 50.0f);
		ImGui::SliderFloat("Wave Speed", &m_WaterWaveSpeed, 0.1f, 5.0f);
		ImGui::SliderFloat("Wave Amplitude", &m_WaterWaveAmplitude, 0.1f, 5.0f);
		ImGui::SliderFloat("Wave Frequency", &m_WaterWaveFrequency, 0.1f, 5.0f);
	}

	if (ImGui::CollapsingHeader("Terrain Settings"))
	{
		ImGui::SliderInt("Height", &m_TerrainHeight, 1, 1500);
		ImGui::SliderInt("Width", &m_TerrainWidth, 1, 1500);
		ImGui::SliderFloat("Scale", &m_TerrainHeightScale, 0.01f, 800.0f);
		ImGui::SliderFloat("Noise Frequency", &m_TerrainNoiseFrequency, 0.01f, 10.0f);
		ImGui::SliderFloat("Noise Octaves", &m_TerrainNoiseOctaves, 0.01f, 10.0f);
		ImGui::SliderFloat("Noise Amplitude", &m_TerrainNoiseAmplitude, 0.01f, 10.0f);
		ImGui::SliderFloat("Noise Value", &m_TerrainNoiseValue, 0.01f, 10.0f);
		ImGui::InputInt("Noise Seed", &m_TerrainNoiseSeed, 1, 2000);
		if (ImGui::Button("Regenerate"))
		{
			m_NeedRegen = true;
		}
	}


	ImGui::Checkbox("Wireframe", &m_WireframeMode);
	XMStoreFloat4x4(&m_TransparentRenderItems[0]->World, XMMatrixScaling(m_WaterScale[0], m_WaterScale[1], m_WaterScale[2]) * XMMatrixTranslation(m_WaterHeight[0], m_WaterHeight[1], m_WaterHeight[2]));
	m_TransparentRenderItems[0]->NumFramesDirty = NumFrameResources;
	ImGui::End();
}

HeightMap Renderer::GeneratePerlinHeightmap(UINT width, UINT height, float scale, int octaves, float persistence, int seed)
{
	HeightMap hm;
	hm.width = width;
	hm.height = height;
	hm.data.resize(width * height);

	// Guard against division by zero
	if (width <= 1 || height <= 1) return hm;

	for (UINT j = 0; j < height; ++j)
	{
		for (UINT i = 0; i < width; ++i)
		{
			float u = static_cast<float>(i) / static_cast<float>(width - 1);
			float v = static_cast<float>(j) / static_cast<float>(height - 1);

			float x = u * scale;
			float z = v * scale;

			float amplitude = m_TerrainNoisePersistance;
			float frequency = m_TerrainNoiseFrequency;
			float noiseValue = m_TerrainNoiseValue;

			for (int o = 0; o < octaves; ++o)
			{
				noiseValue += amplitude * stb_perlin_noise3_seed(x * frequency, z * frequency, 0.0f, 0, 0, 0, m_TerrainNoiseSeed);
				amplitude *= persistence;
				frequency *= 2.0f;
			}

			float h = 0.5f * (noiseValue + 1.0f);

			hm.data[j * width + i] = h;
		}
	}

	//float minVal = hm.data[0];
	//float maxVal = hm.data[0];
	//for (float v : hm.data)
	//{
	//	minVal = std::min(minVal, v);
	//	maxVal = std::max(maxVal, v);
	//}

	//float invRange = (maxVal - minVal) > 0.0f ? 1.0f / (maxVal - minVal) : 1.0f;
	//for (float& v : hm.data)
	//{
	//	v = (v - minVal) * invRange;
	//}

	return hm;
}

void Renderer::CreateHeightMapTexture(const HeightMap& hm)
{
	auto device = m_Device.Get();

	D3D12_RESOURCE_DESC texDesc = {};
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	texDesc.Width = hm.width;
	texDesc.Height = hm.height;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = DXGI_FORMAT_R32_FLOAT;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	ThrowIfFailed(device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&m_HeightMapTex)));

	const UINT64 uploadBufferSize = GetRequiredIntermediateSize(m_HeightMapTex.Get(), 0, 1);


	ThrowIfFailed(device->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&m_HeightMapUpload)));

	D3D12_SUBRESOURCE_DATA subresourceData = {};
	subresourceData.pData = hm.data.data();
	subresourceData.RowPitch = hm.width * sizeof(float);
	subresourceData.SlicePitch = subresourceData.RowPitch * hm.height;

	auto cmdList = m_CommandList.Get();

	UpdateSubresources(cmdList, m_HeightMapTex.Get(), m_HeightMapUpload.Get(), 0, 0, 1, &subresourceData);
	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_HeightMapTex.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

}

void Renderer::RegenerateHeightMap()
{
	m_CpuHeightMap = GeneratePerlinHeightmap(m_TerrainWidth, m_TerrainHeight, m_TerrainHeightScale, m_TerrainNoiseOctaves, m_TerrainNoisePersistance, m_TerrainNoiseSeed);

	m_HeightMapData = m_CpuHeightMap.data;
}

void Renderer::UpdateHeightMapTexture()
{
	m_HeightMapTex.Reset();
	m_HeightMapUpload.Reset();

	CreateHeightMapTexture(m_CpuHeightMap);

}

HeightMap Renderer::GeneratePerlinHeightmap_Simple(UINT width, UINT height, float scale, int seed)
{
	HeightMap hm;
	hm.width = width;
	hm.height = height;
	hm.data.resize(width * height);

	for (UINT j = 0; j < height; ++j)
	{
		for (UINT i = 0; i < width; ++i)
		{
			float u = static_cast<float>(i) / (width - 1);  // [0,1]
			float v = static_cast<float>(j) / (height - 1);  // [0,1]

			float x = u * scale;  // use X
			float z = v * scale;  // use Z

			// Single octave Perlin in [-1,1]
			float n = stb_perlin_noise3_seed(x, z, 0.0f, 0, 0, 0, seed);

			// Map [-1,1] -> [0,1]
			float h = 0.5f * (n + 1.0f);

			hm.data[j * width + i] = h;
		}
	}

	return hm;
}

void Renderer::UpdateHeightMapSrv()
{
	// heightmap SRV is descriptor #5 in m_TexSrvHeap
	CD3DX12_CPU_DESCRIPTOR_HANDLE h(m_TexSrvHeap->GetCPUDescriptorHandleForHeapStart());
	h.Offset(5, m_CbvSrvUavDescriptorSize);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	m_Device->CreateShaderResourceView(m_HeightMapTex.Get(), &srvDesc, h);
}