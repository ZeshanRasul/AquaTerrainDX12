#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "../Utils/d3dUtil.h"
#include "../Utils/GeometryGenerator.h"
#include "../Utils/Waves.h"
#include "UploadBuffer.h"
#include "FrameResource.h"
#include "../Camera.h"
#include "../Utils/GameTimer.h"
#include "../Simulation/StableFluids.h"
#include "../Simulation/StableFluids3D.h"
#include "../Simulation/SmokeSolver3.h"
#include "../Simulation/SmokeBenchmark.h"

using namespace DirectX;

enum class RenderLayer : int
{
	Opaque = 0,
	Count
};

enum class FluidDemoMode : int
{
	Off = 0,
	Fluid2D,
	Fluid3D,
	Smoke3D
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
	D3D12_CPU_DESCRIPTOR_HANDLE ReadOnlyDepthStencilView() const;
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
	void CreateWaterComputeRootSignature();
	void CreateSmokeRootSignature();
	void CreateSmokeResources();
	void UploadSmokeDensity(ID3D12GraphicsCommandList* commandList);
	void DrawSmokeVolume(ID3D12GraphicsCommandList* commandList);

	void BuildShadersAndInputLayout();

	void BuildPSOs();

	void BuildMaterials();
	void BuildShapeGeometry();
	void BuildSkullGeometry();
	void BuildLandGeometry(float width, float height);
	void RebuildLandGeometry(float width, float height);
	void BuildWavesGeometry();
	float GetHillsHeight(float x, float z);
	XMFLOAT3 GetHillsNormal(float x, float z);
	void BuildRenderItems();
	void RebuildLandRenderItem();
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& riItems);
	void DrawRenderItemsWater(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& riItems);

	void BuildFrameResources();
	void RebuildFrameResources();
	void UpdateObjectCBs();
	void UpdateMaterialCBs();
	void UpdateMainPassCB();
	void UpdateWaterCB(GameTimer& dt);
	void UpdateTerrainCB();
	void UpdateWaves(GameTimer& dt);

	void LoadTextures();
	void BuildDescriptorHeaps();
	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();


	void FlushCommandQueue();

	ID3D12Resource* CurrentBackBuffer() const;

	void ShowImGUIEnvironmentControl();
	void ShowImGUICameraControl();
	void ShowImGUILightControl();
	void ShowImGUITerrainControl();
	void UpdateHeightMapSrv();

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
	UINT mCbvSrvDescriptorSize = 0;

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
	D3D12_VIEWPORT vp;

	Microsoft::WRL::ComPtr<ID3DBlob> m_VsByteCode;
	Microsoft::WRL::ComPtr<ID3DBlob> m_PsByteCode;
	Microsoft::WRL::ComPtr<ID3DBlob> m_VsByteCodeWater;
	Microsoft::WRL::ComPtr<ID3DBlob> m_PsByteCodeWater;
	Microsoft::WRL::ComPtr<ID3DBlob> m_VsByteCodeSky;
	Microsoft::WRL::ComPtr<ID3DBlob> m_PsByteCodeSky;
	Microsoft::WRL::ComPtr<ID3DBlob> m_CsByteCodeWaveUpdate;
	Microsoft::WRL::ComPtr<ID3DBlob> m_CsByteCodeWaveDisturb;
	Microsoft::WRL::ComPtr<ID3DBlob> m_CsByteCodeWaveNormals;
	Microsoft::WRL::ComPtr<ID3DBlob> m_VsByteCodeSmoke;
	Microsoft::WRL::ComPtr<ID3DBlob> m_PsByteCodeSmoke;
	std::vector<D3D12_INPUT_ELEMENT_DESC> m_InputLayoutDescs;

	XMFLOAT4X4 m_World = MathHelper::Identity4x4();
	XMFLOAT4X4 m_View = MathHelper::Identity4x4();
	XMFLOAT4X4 m_Proj = MathHelper::Identity4x4();

	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_OpaqueRootSignature;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_TransparentRootSignature;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_ComputeRootSignature;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_SmokeRootSignature;
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
	bool m_WireframeMode = false;

	PassConstants m_MainPassCB;
	WaterConstants m_WaterConstantsCB;
	TerrainConstants m_TerrainConstantsCB;
	INT m_TerrainWorldWidth = 1200;
	INT m_TerrainWorldHeight = 1200;
	INT m_HeightMapWidth = 1024;
	INT m_HeightMapHeight = 1024;
	float m_TerrainHeightScale = 360.0f;
	float m_TerrainNoiseFrequency = 0.014f;
	float m_TerrainNoiseOctaves = 5.0f;
	float m_TerrainNoisePersistance = 0.35f;
	float m_TerrainNoiseAmplitude = 0.25f;
	float m_TerrainNoiseValue = 0.0f;
	int m_TerrainNoiseSeed = 1442;
	float m_WaterHeight[3] = { 0.0f, 150.0f, 0.0f };
	float m_WaterScale[3] = { 10.0f, 25.0f, 10.0f };
	float m_WaterWaveSpeed = 0.15f;
	float m_WaterWaveAmplitude = 0.1f;
	float m_WaterWaveFrequency = 0.25f;
	float m_mudStartFrac = 0.15f;
	float m_grassStartFrac = 0.35f;
	float m_rockStartFrac = 0.65f;
	float m_blendFrac = 0.06f;
	float m_mudRepeatSize = 16.0f;
	float m_grassRepeatSize = 8.0f;
	float m_rockRepeatSize = 12.0f;
	float m_MudSlopeBias = 0.25f;
	float m_MudSlopePower = 2.0f;
	float m_RockSlopeBias = 0.55f;
	float m_RockSlopePower = 3.5f;
	float m_MudStartHeight = 164.0f;
	float m_GrassStartHeight = 190.0f;
	float m_RockStartHeight = 220.0f;

	/*XMFLOAT2 m_TerrainSize;
	float m_HeightScale;
	float m_HeightOffset;
	float m_MudStartHeight;
	float m_GrassStartHeight;
	float m_RockStartHeight;
	float m_HeightBlendRange;*/

	TerrainConstants m_TerrainConstantsCPU;

	Camera& m_Camera;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_SrvHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_TexSrvHeap;
	UINT m_SkyTexHeapIndex = 1;

	UINT m_CbvSrvDescriptorSize;

	Microsoft::WRL::ComPtr<ID3D12Resource> m_DepthSRV;

	Microsoft::WRL::ComPtr<ID3D12Resource> m_HeightMapTex = nullptr;
	D3D12_GPU_DESCRIPTOR_HANDLE m_HeightMapSrvGpuHandle = {};
	Microsoft::WRL::ComPtr<ID3D12Resource> m_HeightMapUpload;
	std::vector<float> m_HeightMapData;
	float m_HeightMapScale = 0;
	int m_HeightMapSeed = 0;
	int m_HeightMapOctaves = 0;
	float m_HeightMapPersistance = 0.0f;
	bool m_NeedRegen = false;
	HeightMap m_CpuHeightMap;
	void RegenerateHeightMap();
	void UpdateHeightMapTexture();

	HeightMap GeneratePerlinHeightmap_Simple(UINT width, UINT height, float scale, int seed);

	HeightMap GeneratePerlinHeightmap(UINT width, UINT height, float scale, int octaves, float persistence, int seed);

	void CreateHeightMapTexture(const HeightMap& hm);

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_ImGuiSrvHeap;
	D3D12_GPU_DESCRIPTOR_HANDLE imguiGpuStart;
	D3D12_CPU_DESCRIPTOR_HANDLE	imguiCpuStart;
	bool showImgui = true;

	void CreateImGuiDescriptorHeap()
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.NumDescriptors = 1;
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		desc.NodeMask = 0;

		ThrowIfFailed(m_Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(m_ImGuiSrvHeap.GetAddressOf())));

	}

	Microsoft::WRL::ComPtr<ID3D12Resource> m_WaterHeightPrev = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_WaterHeightPrevUav = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_WaterHeightCurrent = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_WaterHeightCurrentUav = nullptr;
	bool m_UsePrevAsWaterHeightSrv = false;
	bool m_PendingDisturb = false;
	float m_DisturbX = 0.0f;
	float m_DisturbY = 0.0f;
	float m_DisturbRadius = 8.0f;
	float m_DisturbStrength = 0.15f;
	float m_DisturbAccumTime = 0.0f;

	void CreateWaterSimTextures();

	void DrawFluidDemoSelector();
	void DrawFluidDebug(StableFluids& fluid);
	void DrawFluid3DDebug(StableFluids3D& fluid);
	void DrawSmoke3DDebug(SmokeSolver3& smokeSolver);

	FluidDemoMode m_FluidDemoMode = FluidDemoMode::Fluid2D;
	bool m_ShowFluid3DSliceViewer = true;
	bool m_ShowSmokeVolume = true;

	StableFluids m_Fluid{};
	float m_FluidAccumulator = 0.0f;
	bool m_FluidEmitterEnabled = true;
	bool m_FluidPaused = false;
	bool m_FluidSingleStepRequested = false;

	StableFluids3D m_Fluid3D{ StableFluids3D::GridSize,
		StableFluids3D::GridSize, StableFluids3D::GridSize };
	float m_Fluid3DAccumulator = 0.0f;
	bool m_Fluid3DEmitterEnabled = true;
	bool m_Fluid3DPaused = false;
	bool m_Fluid3DSingleStepRequested = false;

	SmokeSolver3 m_SmokeSolver{ { 32, 32, 32 }, { 1.0 / 32.0, 1.0 / 32.0, 1.0 / 32.0 },  { -0.5, -0.5, -0.5 } };
	float m_Smoke3DAccumulator = 0.0f;
	bool m_Smoke3DEmitterEnabled = true;
	bool m_Smoke3DPaused = false;
	bool m_Smoke3DSingleStepRequested = false;
	SmokeBenchmarkRecorder m_SmokeBenchmark;
	int m_SmokeBenchmarkTotalSteps = 480;
	int m_SmokeBenchmarkEmitterSteps = 240;
	int m_SmokeBenchmarkWarmupSteps = 10;
	char m_SmokeBenchmarkRunLabel[64] = "baseline";
	bool m_SmokeBenchmarkSimulationOnly = true;
	bool m_SmokeBenchmarkRestoreSmokeVolume = true;
	bool m_SmokeBenchmarkRestoreSliceViewer = true;
	float deltaTime = 0.0f;

	Microsoft::WRL::ComPtr<ID3D12Resource> m_SmokeDensityTexture;
	std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, NumFrameResources>
		m_SmokeUploadBuffers;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_SmokeSrvHeap;
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT m_SmokeUploadFootprint = {};
	UINT m_SmokeUploadRowCount = 0;
	UINT64 m_SmokeUploadRowSize = 0;
	UINT64 m_SmokeUploadBufferSize = 0;
	bool m_SmokeDensityDirty = true;

	float m_SmokePosition[3] = { -120.0f, 210.0f, 35.0f };
	float m_SmokeSize[3] = { 80.0f, 120.0f, 80.0f };
	float m_SmokeColour[3] = { 0.72f, 0.78f, 0.86f };
	float m_SmokeDensityScale = 0.12f;
	float m_SmokeAbsorption = 3.0f;
	float m_SmokeStepScale = 0.75f;

	struct SmokeGpuTexture
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> resource;

		D3D12_GPU_DESCRIPTOR_HANDLE srv = {};
		D3D12_GPU_DESCRIPTOR_HANDLE uav = {};

		D3D12_RESOURCE_STATES state =
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	};

	std::array<SmokeGpuTexture, 2> m_GpuDensity;
	std::array<SmokeGpuTexture, 2> m_GpuTemperature;
	std::array<SmokeGpuTexture, 2> m_GpuU;
	std::array<SmokeGpuTexture, 2> m_GpuV;
	std::array<SmokeGpuTexture, 2> m_GpuW;
	std::array<SmokeGpuTexture, 2> m_GpuPressure;

	SmokeGpuTexture m_GpuDivergence;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_SmokeGpuDescriptorHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_SmokeCpuDescriptorHeap;

	void CreateSmokeGpuResources();
	void CreateSmokeGpuDescriptorHeap();

	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_SmokeBindingRootSignature;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_SmokeClearPSO;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_SmokeInjectPSO;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_SmokeApplyBuoyancyPSO;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_SmokeApplyDivergencePSO;

	void CreateSmokeBindingRootSignature();
	void CreateSmokeBindingPSOs();

	bool m_SmokeGpuResetRequested = true;
    bool m_SmokeGpuStepRequested = false;
    unsigned int m_SmokeGpuInjectionCount = 0;
    void DispatchSmokeSourceTest(ID3D12GraphicsCommandList* commandList);
};

struct SmokeBindingConstants
{
	std::uint32_t gridResolution[3];
	float dt;
	std::uint32_t sourceCell[3];
	float densityRate;
	float temperatureRate;
	float ambientTemperature = 0.0f;
	float temperatureBuoyancy = 0.5f;
	float smokeWeight = 0.05f;
	float hx;
	float hy;
	float hz;
	float pad;
};

static_assert(sizeof(SmokeBindingConstants) == 64);

enum SmokeBindingRootParameter : UINT
{
	SmokeBindingConstantsRoot = 0,
	SmokeBindingOutputRoot,    // u0, u1: density and temperature
	SmokeBindingInputRoot,     // t0, t1: density and temperature
	SmokeBindingVelocityRoot,  // u2, u3, u4: U, V, W velocity
	SmokeBindingDivergenceRoot, // u5: divergence
	SmokeBindingPressureRoot,  // u6, u7: pressure
	SmokeBindingRootCount
};
