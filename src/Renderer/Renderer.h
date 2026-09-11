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
	Smoke3D,
	Smoke3DGPU
};

struct HeightMap
{
	std::vector<float> data;
	UINT width;
	UINT height;
};

enum SmokeTimestamp : UINT
{
	SmokeTimestampStepBegin = 0,
	SmokeTimestampSourceEnd,
	SmokeTimestampVelocityAdvectionEnd,
	SmokeTimestampBuoyancyEnd,
	SmokeTimestampDivergenceEnd,
	SmokeTimestampPressureClearEnd,
	SmokeTimestampPressureSolveEnd,
	SmokeTimestampPressureGradientEnd,
	SmokeTimestampStepEnd,
	SmokeTimestampDiagnosticsEnd,
	SmokeTimestampCount
};

constexpr UINT SmokeTimestampSlotsPerFrame = 2;

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
	float m_SmokeDensityScale = 0.62f;
	float m_SmokeAbsorption = 6.0f;
	float m_SmokeStepScale = 0.25f;

	struct SmokeGpuTexture
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> resource;

		D3D12_GPU_DESCRIPTOR_HANDLE srv = {};
		D3D12_GPU_DESCRIPTOR_HANDLE uav = {};

		D3D12_RESOURCE_STATES state =
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	};

	std::array<SmokeGpuTexture, 2> m_GpuDensity;
	std::array<SmokeGpuTexture, 2> m_GpuDensityHat;
	std::array<SmokeGpuTexture, 2> m_GpuDensityBar;
	std::array<SmokeGpuTexture, 2> m_GpuTemperature;
	std::array<SmokeGpuTexture, 2> m_GpuTemperatureHat;
	std::array<SmokeGpuTexture, 2> m_GpuTemperatureBar;
	std::array<SmokeGpuTexture, 2> m_GpuU;
	std::array<SmokeGpuTexture, 2> m_GpuV;
	std::array<SmokeGpuTexture, 2> m_GpuW;
	std::array<SmokeGpuTexture, 2> m_GpuPressure;

	std::array<SmokeGpuTexture, 1> m_GpuVorticityX;
	std::array<SmokeGpuTexture, 1> m_GpuVorticityY;
	std::array<SmokeGpuTexture, 1> m_GpuVorticityZ;

	SmokeGpuTexture m_GpuDivergence;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_SmokeGpuDiagnosticBuffer;
	D3D12_GPU_DESCRIPTOR_HANDLE m_SmokeGpuDiagnosticUav = {};
	D3D12_RESOURCE_STATES m_SmokeGpuDiagnosticState =
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_SmokeGpuDescriptorHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_SmokeCpuDescriptorHeap;

	void CreateSmokeGpuResources();
	void CreateSmokeGpuDescriptorHeap();

	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_SmokeBindingRootSignature;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_SmokeClearPSO;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_SmokeClearPressurePSO;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_SmokeInjectPSO;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_SmokeApplyBuoyancyPSO;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_SmokeApplyDivergencePSO;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_SmokeApplyPressurePSO;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_SmokeSubtractPressureGradientPSO;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_SmokeAdvectScalarsPSO;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_SmokeAdvectVelocityPSO;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_SmokeReduceDivergenceBeforePSO;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_SmokeReduceDivergenceAfterPSO;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_SmokeReduceVelocityPSO;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_SmokeAdvectScalarsRawPSO;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_SmokeMacCormackScalarsPSO;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_SmokeComputeVorticityPSO;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_SmokeApplyConfinementForcePSO;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_SmokeReduceEnstrophyPSO;

	void CreateSmokeBindingRootSignature();
	void CreateSmokeBindingPSOs();

	bool m_SmokeGpuResetRequested = true;
    bool m_SmokeGpuPaused = false;
    bool m_SmokeGpuEmitterEnabled = true;
    bool m_SmokeGpuSphereEnabled = true;
    float m_SmokeGpuSphereRadius = 0.085f;
	bool m_SmokeGpuOpenTopEnabled = false;
    DirectX::XMFLOAT3 m_SmokeGpuSphereCentre = { 0.0f, 0.1f, 0.0f };
	DirectX::XMFLOAT3 m_SmokeGpuSphereInitialCentre = { 0.0f, 0.1f, 0.0f };
	float m_SphereObstacleAmplitude = 0.05f;
	float m_SphereAngularFrequency = 0.5f;
	bool m_SphereTranslationEnabled = false;

    void CreateSmokeObstaclePipeline();
    void DrawSmokeObstacle(ID3D12GraphicsCommandList* commandList);
    DirectX::XMFLOAT3 SmokeObstacleWorldRadii() const;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_SmokeObstacleRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_SmokeObstaclePSO;
    float m_SmokeGpuAccumulator = 0.0f;
    unsigned m_SmokeGpuPendingSteps = 0;
    int m_SmokeGpuPressureIterations = 20;
	float m_SmokeGpuVorticityEpsilon = 0.0f;
	float m_SmokeGpuBenchmarkVorticityEpsilon = 0.0f;
	SmokeLimiterMode m_SmokeGpuLimiterMode = SmokeLimiterMode::Clamp;
	int m_SmokeGpuBenchmarkLimiterMode = 0;
    double m_SmokeGpuLastMilliseconds = 0.0;
    double m_SmokeGpuLastPressureMilliseconds = 0.0;
	unsigned m_SmokeGpuLastPressureIterations = 0;
	SmokeAdvectionMode m_SmokeGpuAdvectionMode = SmokeAdvectionMode::SemiLagrangian;
	double m_SmokeGpuAverageIterationMicroSeconds = 0.0;
	double m_SmokeGpuPressureFraction = 0.0;
	std::array<double, SmokeTimestampCount - 1> m_SmokeGpuLastStageMilliseconds{};
	double m_SmokeGpuLastDivergenceBeforeRms = 0.0;
	double m_SmokeGpuLastDivergenceBeforeMax = 0.0;
	double m_SmokeGpuLastDivergenceAfterRms = 0.0;
	double m_SmokeGpuLastDivergenceAfterMax = 0.0;
	double m_SmokeGpuLastKineticEnergy = 0.0;
	double m_SmokeGpuLastRmsSpeed = 0.0;
	double m_SmokeGpuLastMaxSpeed = 0.0;
	unsigned m_SmokeGpuLastDiagnosticNonfinite = 0;
    void DrawSmokeGpuDebug();
    void CreateSmokeGpuDiagnostics();
    void CollectSmokeGpuDiagnostics();
    void SaveSmokeGpuBenchmark();
    void StartSmokeMassAudit(bool automatic = false);
    void CreateSmokeMassAudit();
    void CaptureSmokeMassAudit(ID3D12GraphicsCommandList* commands, SmokeGpuTexture& texture, unsigned stage);
    void CollectSmokeMassAudit(unsigned frame, unsigned step, bool emit, double densitySum, std::uint64_t densityHash);
    void SaveSmokeMassAudit();
    bool m_SmokeMassAudit = false;
    bool m_SmokeMassAuditAutomatic = false;
    bool m_SmokeAuditProbes = true;
    static constexpr unsigned SmokeAuditStages = 8; // before/after source, pre-advection, hat, bar, constant, impulse, final
    UINT64 m_SmokeAuditTextureStride = 0;
    UINT64 m_SmokeAuditCellsOffset = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT m_SmokeAuditFootprint{};
    Microsoft::WRL::ComPtr<ID3D12Resource> m_SmokeAuditCells;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, NumFrameResources> m_SmokeAuditReadbacks;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_SmokeAuditSLPSO, m_SmokeAuditMCPSO, m_SmokeAuditPatternPSO;
    std::vector<std::string> m_SmokeAuditRows;
    std::filesystem::path m_SmokeAuditOutput;
    unsigned m_SmokeAuditFailures = 0;

    // Passive-advection reference harness (SmokeAdvectionReference.cpp).
    void StartSmokeAdvectionReference(SmokeReferenceCase referenceCase, bool automatic = false);
    void DispatchSmokeReferenceStep(ID3D12GraphicsCommandList* commandList, UINT timestampSlot = 0);
    void RecordReferenceStep(const void* mapped, unsigned step);
    void SaveSmokeAdvectionReference();
    SmokeReferenceCase m_SmokeReferenceCase = SmokeReferenceCase::None;
    bool m_SmokeReferenceAutomatic = false;
    int m_SmokeReferencePeriodic = 0;
    int m_SmokeReferenceVelocityMode = 0;   // 0 zero, 1 translation +x, 2 rotation about z
    float m_SmokeReferenceSpeed = 0.0f;      // cells/step (translation) or rad/step (rotation)
    float m_SmokeReferenceBlobSigma = 0.0f;  // cells
    float m_SmokeReferenceBlobCentre[3]{};   // cells
    std::vector<std::string> m_SmokeReferenceRows;
    std::filesystem::path m_SmokeReferenceOutput;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_SmokeReferenceBlobPSO;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_SmokeReferenceVelocityPSO;
    // Optimized advection PSOs for timing runs ONLY (production/mass-audit paths
    // keep their SKIP_OPTIMIZATION build). Correctness runs use the production PSOs.
    void CreateReferenceTimingPSOs();
    bool m_SmokeReferenceTiming = false;
    // Independent one-step projection sensitivity trials, sharing reference dispatch.
    void ConfigureProjectionExperiment();
    void DispatchProjectionExperiment(ID3D12GraphicsCommandList*, const struct SmokeBindingConstants&, UINT);
    void CaptureProjectionExperiment(ID3D12GraphicsCommandList*, const struct SmokeBindingConstants&);
    void CaptureProjectionInitialDensity(ID3D12GraphicsCommandList*);
    void RecordProjectionExperiment(const void*, unsigned);
    void SaveProjectionExperiment();
    bool m_ProjectionExperiment = false;
    bool m_ProjectionSharp = false;
    int m_ProjectionIterations = 0; // -1: prescribed divergence-free control
    unsigned m_ProjectionAdvectionSteps = 1;
    float m_ProjectionShiftCells = 0.0f;
    double m_ProjectionDtScale = 1.0;
    void CreateTransportProbe();
    void DispatchTransportProbe(ID3D12GraphicsCommandList*, unsigned, bool);
    void RecordTransportProbe(unsigned);
    bool m_TransportProbeEnabled = false;
    bool m_DensitySamplingFloat = false;
    UINT64 m_TransportProbeBytes = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_TransportProbeBuffer;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, NumFrameResources> m_TransportProbeReadbacks;
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, 3> m_TransportProbePSOs;
    std::array<std::uint64_t, 2> m_TransportProbeHashes{};
    std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, 5> m_ProjectionPSOs;
    std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, 7> m_ProjectionFootprints{};
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, NumFrameResources> m_ProjectionReadbacks;
    std::vector<std::string> m_ProjectionRows;
    std::array<std::uint64_t, 8> m_ProjectionFirstHashes{};
    unsigned m_ProjectionFailures = 0;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_SmokeRefOptAdvectScalarsPSO;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_SmokeRefOptAdvectScalarsRawPSO;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_SmokeRefOptMacCormackScalarsPSO;

    struct GpuSmokeSample
    {
        unsigned step = 0;
        bool emit = false;
        double milliseconds = 0.0;
        double densitySum = 0.0, densityMin = 0.0, densityMax = 0.0;
        Vector3 centre{};
        unsigned nonfinite = 0;
        std::uint64_t densityHash = 14695981039346656037ull;
		unsigned pressureIterations = 0;
		double sourceMilliseconds = 0.0;
		double velocityAdvectionMilliseconds = 0.0;
		double buoyancyMilliseconds = 0.0;
		double divergenceMilliseconds = 0.0;
		double pressureClearMilliseconds = 0.0;
		double pressureMilliseconds = 0.0;
		double pressureGradientMilliseconds = 0.0;
		double scalarAdvectionMilliseconds = 0.0;
		double diagnosticsMilliseconds = 0.0;
		double divergenceBeforeRms = 0.0;
		double divergenceBeforeMax = 0.0;
		double divergenceAfterRms = 0.0;
		double divergenceAfterMax = 0.0;
		double kineticEnergy = 0.0;
		double rmsSpeed = 0.0;
		double maxSpeed = 0.0;
		double enstrophy = 0.0;
		double maxVorticity = 0.0;
		unsigned divergenceBeforeNonfinite = 0;
		unsigned divergenceAfterNonfinite = 0;
		unsigned velocityNonfinite = 0;
		unsigned diagnosticNonfinite = 0;
    };

    struct GpuSmokeReadback
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
        bool pending = false;
        bool benchmark = false;
		UINT64 timestampOffset = 0;
        GpuSmokeSample sample;
    };

    std::array<GpuSmokeReadback, NumFrameResources> m_SmokeGpuReadbacks;
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> m_SmokeGpuQueries;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT m_SmokeGpuReadbackFootprint{};
	UINT64 m_SmokeGpuDiagnosticsReadbackOffset = 0;
    UINT64 m_SmokeGpuTimestampFrequency = 0;
    bool m_SmokeGpuBenchmarkRunning = false;
    bool m_SmokeGpuBenchmarkStopping = false;
    unsigned m_SmokeGpuBenchmarkSubmitted = 0;
    SmokeBenchmarkConfig m_SmokeGpuBenchmarkConfig;
    SmokePhysicsParameters m_SmokeGpuBenchmarkPhysics;
    int m_SmokeGpuBenchmarkIterations = 20;
    bool m_SmokeGpuRestoreVolume = true;
    std::vector<GpuSmokeSample> m_SmokeGpuBenchmarkSamples;
    std::string m_SmokeGpuBenchmarkStatus = "No GPU benchmark recorded yet.";
    bool m_SmokeGpuStepRequested = false;
    unsigned int m_SmokeGpuInjectionCount = 0;
    void DispatchSmokeSourceTest(
		ID3D12GraphicsCommandList* commandList,
		UINT timestampSlot = 0);

	std::uint32_t m_GpuScalarReadIndex = 0;
	std::uint32_t m_GpuScalarWriteIndex = 1;
	std::uint32_t m_GpuVelocityReadIndex = 0;
	std::uint32_t m_GpuVelocityWriteIndex = 1;
};

struct SmokeSphereObstacle
{
	float centre[3];
	float radius;
	std::uint32_t enabled = 1;
	float velocity[3];
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
	float vorticityEpsilon;

	float GridSpacing[3];
	float FluidDensity;

	float JacobiWeight; // Start with 2.0 / 3.0.
	float Padding[2];
	int sphereMovementEnabled;

	float origin[3];
	int openTopEnabled;

	SmokeSphereObstacle sphereObstacle;

	int limiterMode; // 0 = clamp, 1 = revert, 2 = adaptive (MacCormack combine)

	// Passive-advection reference harness (inert in normal runs; all zero).
	int periodicDomain;            // 1 = wrap density sampling + limiter stencil
	int referenceVelocityMode;     // 0 zero, 1 translation +x, 2 rotation about z
	float referenceSpeed;          // cells/step (translation) or rad/step (rotation)
	float referenceBlobSigma;      // cells
	float referenceBlobCentre[3];  // cells
};


static_assert(sizeof(SmokeSphereObstacle) == 32);
static_assert(offsetof(SmokeBindingConstants, sphereObstacle) == 112);
static_assert(sizeof(SmokeBindingConstants) == 176);

constexpr UINT SmokeConstantCount =
static_cast<UINT>(
	sizeof(SmokeBindingConstants) / sizeof(std::uint32_t));

enum SmokeBindingRootParameter : UINT
{
	SmokeBindingConstantsRoot = 0,
	SmokeBindingOutputRoot,    // u0, u1: density and temperature
	SmokeBindingInputRoot,     // t0, t1: density and temperature
	SmokeBindingVelocityRoot,  // u2, u3, u4: U, V, W velocity
	SmokeBindingVelocityInputRoot, // t2, t3, t4: U, V, W velocity
	SmokeBindingDivergenceRoot, // u5: divergence
	SmokeBindingPressureReadRoot,  // u6: pressure
	SmokeBindingPressureWriteRoot, //u7: pressure
	SmokeBindingPressureReadInputRoot,  // u8: pressure
	SmokeBindingDivergenceReadRoot, 
	SmokeBindingDiagnosticsRoot, // u9: three float4 reduction records
	SmokeBindingHatBarRoot, // t7..t10: density-hat, temperature-hat, density-bar, temperature-bar
	SmokeBindingVorticityRoot,     // u12,u13,u14 : write omega
	SmokeBindingVorticityReadRoot, // t11,t12,t13 : read omega
    SmokeBindingAuditRoot, // u15: per-cell audit values (audit shader variants only)
	SmokeBindingRootCount
};
