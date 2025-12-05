#pragma once
#include "../Utils/d3dUtil.h"
#include "../Utils/GeometryGenerator.h"
#include "../Utils/Waves.h"
#include "UploadBuffer.h"
#include "FrameResource.h"
#include "../Camera.h"
#include "../Utils/GameTimer.h"



using namespace DirectX;

enum class RenderLayer : int
{
	Opaque = 0,
	Count
};

struct HeightMap
{
	std::vector<float> data;
	UINT width;
	UINT height;
};

class Renderer {
public:
	Renderer(HWND& windowHandle, UINT width, UINT height, Camera& cam);
	~Renderer() = default;

	bool InitializeD3D12(HWND& windowHandle);
	bool Shutdown();
	void Update(GameTimer& dt, Camera& cam);
	void Draw();

private:
	void CreateDebugController();
	void CreateDevice();
	void CreateFence();
	void GetDescriptorSizes();
	void CheckMSAAQuality();
	void CreateCommandObjects();
	void CreateSwapChain(HWND& hwnd);
	void CreateRtvAndDsvDescriptorHeaps();
	D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferView() const;
	D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView() const;
	void CreateRenderTargetView();
	void CreateDepthStencilView();

	void CreateVertexBuffer();
	void CreateVertexBufferView();

	void CreateIndexBuffer();
	void CreateIndexBufferView();

	void CreateCbvDescriptorHeaps();
	void CreateConstantBufferViews();
	void createSrvDescriptorHeaps();
	void CreateTextureSrvDescriptors();
	void CreateOpaqueRootSignature();
	void CreateTransparentRootSignature();

	void BuildShadersAndInputLayout();
	
	void BuildPSOs();

	void BuildMaterials();
	void BuildShapeGeometry();
	void BuildSkullGeometry();
	void BuildLandGeometry();
	void BuildWavesGeometry();
	float GetHillsHeight(float x, float z);
	XMFLOAT3 GetHillsNormal(float x, float z);
	void BuildRenderItems();
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& riItems);
	void DrawRenderItemsWater(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& riItems);

	void BuildFrameResources();
	void UpdateObjectCBs();
	void UpdateMaterialCBs();
	void UpdateMainPassCB();
	void UpdateWaterCB(GameTimer& dt);
	void UpdateWaves(GameTimer& dt);

	void FlushCommandQueue();

	ID3D12Resource* CurrentBackBuffer() const;

	Microsoft::WRL::ComPtr<ID3D12Device> m_Device;
	Microsoft::WRL::ComPtr<IDXGIAdapter> m_WarpAdapter;
	Microsoft::WRL::ComPtr<ID3D12Debug> m_DebugController;

	Microsoft::WRL::ComPtr<IDXGIFactory4> m_DxgiFactory;

	Microsoft::WRL::ComPtr<ID3D12Fence> m_Fence;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_CommandQueue;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_CommandAllocator;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_CommandList;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_RtvHeap;;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_DsvHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_CbvHeap;

	Microsoft::WRL::ComPtr<IDXGISwapChain> m_SwapChain;
	static const int SwapChainBufferCount = 2;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_SwapChainBuffer[SwapChainBufferCount];
	Microsoft::WRL::ComPtr<ID3D12Resource> m_DepthStencilBuffer;
	int m_CurrentBackBuffer = 0;

	D3D12_RECT m_ScissorRect;

	Microsoft::WRL::ComPtr<ID3DBlob> m_VertexBufferCPU = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_VertexBufferGPU = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_VertexBufferUploader = nullptr;
	D3D12_VERTEX_BUFFER_VIEW m_VbView;
	UINT64 m_VbByteSize = 0;

	Microsoft::WRL::ComPtr<ID3DBlob> m_IndexBufferCPU = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_IndexBufferGPU = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_IndexBufferUploader = nullptr;
	D3D12_INDEX_BUFFER_VIEW m_IbView;
	UINT64 m_IbByteSize = 0;

	UINT m_CbufferElementByteSize = 0;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_UploadCBuffer = nullptr;
	std::unique_ptr<UploadBuffer<ObjectConstants>> m_ObjectCB = nullptr;
	UINT m_PassCbvOffset;
	UINT m_WaterCbvOffset;

	UINT m_RtvDescriptorSize = 0;
	UINT m_DsvDescriptorSize = 0;
	UINT m_CbvSrvUavDescriptorSize = 0;

	UINT m_CurrentFence = 0;

	UINT m_4xMsaaQuality = 0;
	bool m_MsaaState = false;

	bool m_IsWireframe = false;

	DXGI_FORMAT m_BackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	DXGI_FORMAT m_DepthStencilFormat = DXGI_FORMAT_R24G8_TYPELESS;
	HWND& m_Hwnd;
	UINT m_ClientWidth;
	UINT m_ClientHeight;


	Microsoft::WRL::ComPtr<ID3DBlob> m_VsByteCode;
	Microsoft::WRL::ComPtr<ID3DBlob> m_PsByteCode;
	Microsoft::WRL::ComPtr<ID3DBlob> m_VsByteCodeWater;
	Microsoft::WRL::ComPtr<ID3DBlob> m_PsByteCodeWater;
	Microsoft::WRL::ComPtr<ID3DBlob> m_VsByteCodeSky;
	Microsoft::WRL::ComPtr<ID3DBlob> m_PsByteCodeSky;
	std::vector<D3D12_INPUT_ELEMENT_DESC> m_InputLayoutDescs;

	XMFLOAT4X4 m_World = MathHelper::Identity4x4();
	XMFLOAT4X4 m_View = MathHelper::Identity4x4();
	XMFLOAT4X4 m_Proj = MathHelper::Identity4x4();

	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_OpaqueRootSignature;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_TransparentRootSignature;
	std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12PipelineState>> m_PipelineStateObjects;

	float m_Theta = 1.5f * DirectX::XM_PI;
	float m_Phi = DirectX::XM_PIDIV4;
	float m_Radius = 5.0f;

	static const int NumFrameResources = 3;
	std::vector<std::unique_ptr<FrameResource>> m_FrameResources;
	FrameResource* m_CurrentFrameResource = nullptr;
	int m_CurrentFrameResourceIndex = 0;
	XMFLOAT3 m_EyePos;

	std::vector<RenderItem*> m_AllRenderItems;

	std::vector<RenderItem*> m_OpaqueRenderItems;
	std::vector<RenderItem*> m_TransparentRenderItems;
	std::vector<RenderItem*> m_SkyRenderItems;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> m_Geometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> m_Materials;
	std::unordered_map<std::string, std::unique_ptr<Texture>> m_Textures;
	
	std::unique_ptr<Waves> m_Waves;
	RenderItem* m_WavesRitem = nullptr;

	PassConstants m_MainPassCB;
	WaterConstants m_waterConstantsCB;
	Camera& m_Camera;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_SrvHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_TexSrvHeap;
	UINT m_SkyTexHeapIndex = 1;

	void LoadTextures();
	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();
	UINT m_CbvSrvDescriptorSize;

	Microsoft::WRL::ComPtr<ID3D12Resource> m_DepthSRV;

	Microsoft::WRL::ComPtr<ID3D12Resource> mHeightMapTex = nullptr;
	D3D12_GPU_DESCRIPTOR_HANDLE mHeightMapSrvGpuHandle = {};


	HeightMap GeneratePerlinHeightmap(UINT width, UINT height, float scale, int octaves, float persistence, int seed);

	void CreateHeightMapTexture(const HeightMap& hm)
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
			IID_PPV_ARGS(&mHeightMapTex)));

		const UINT64 uploadBufferSize = GetRequiredIntermediateSize(mHeightMapTex.Get(), 0, 1);

		Microsoft::WRL::ComPtr<ID3D12Resource> heightMapUpload;

		ThrowIfFailed(device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&heightMapUpload)));

		D3D12_SUBRESOURCE_DATA subresourceData = {};
		subresourceData.pData = hm.data.data();
		subresourceData.RowPitch = hm.width * sizeof(float);
		subresourceData.SlicePitch = subresourceData.RowPitch * hm.height;

		auto cmdList = m_CommandList.Get();

		UpdateSubresources(cmdList, mHeightMapTex.Get(), heightMapUpload.Get(), 0, 0, 1, &subresourceData);
		cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mHeightMapTex.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));


	}
};