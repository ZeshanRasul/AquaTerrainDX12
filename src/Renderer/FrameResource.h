#pragma once
#include "../Utils/d3dUtil.h"
#include "../../include/MathHelper.h"
#include "../../include/UploadBuffer.h"

using namespace DirectX;

struct WaterConstants
{
	XMFLOAT4X4 gWorld = MathHelper::Identity4x4();
	XMFLOAT4X4 gViewProj = MathHelper::Identity4x4();
	XMFLOAT3 gCameraPos = { 0.0f, 0.0f, 0.0f };
	float gTime = 0.0f;
	XMFLOAT3 gWaterColor = { 0.65f, 0.75f, 0.90f };
	float gPad0 = 0.0f;

};

struct TerrainConstants
{
	XMFLOAT2 gTerrainSize = { 460.0f, 460.0f };
	float gHeightScale = 200.0f;
	float gHeightOffset = 0.0f;
	
	float gMudStartHeight = 80.0f;
	float gGrassStartHeight = 113.0f;
	
	float gRockStartHeight = 155.0f;
	float gHeightBlendRange = 3.0f;

	float gMudSlopeBias = 0.20f;
	float gMudSlopePower = 2.0f;

	float gRockSlopeBias = 0.35f;
	float gRockSlopePower = 3.0f;

	float gMudTiling = 2.0f;
	float gGrassTiling = 6.0f;
	
	float gRockTiling = 4.0f;
	float gPad = 0.0f;
};

struct ObjectConstants
{
	XMFLOAT4X4 World = MathHelper::Identity4x4();
	XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();
};

struct PassConstants
{
	XMFLOAT4X4 View = MathHelper::Identity4x4();
	XMFLOAT4X4 InvView = MathHelper::Identity4x4();
	XMFLOAT4X4 Proj = MathHelper::Identity4x4();
	XMFLOAT4X4 InvProj = MathHelper::Identity4x4();
	XMFLOAT4X4 ViewProj = MathHelper::Identity4x4();
	XMFLOAT4X4 InvViewProj = MathHelper::Identity4x4();
	XMFLOAT3 EyePosW = { 0.0f, 0.0f, 0.0f };
	float cbPerObjectPad1 = 0.0f;
	XMFLOAT2 RenderTargetSize = { 0.0f, 0.0f };
	XMFLOAT2 InvRenderTargetSize = { 0.0f, 0.0f };
	float NearZ = 0.0f;
	float FarZ = 0.0f;
	float TotalTime = 0.0f;
	float DeltaTime = 0.0f;
	XMFLOAT4 AmbientLight = { 0.2f, 0.2f, 0.2f, 1.0f };
	XMFLOAT4 FogColor = { 0.1f, 0.25f, 0.5f, 0.4f };
	float FogStart = 75.0f;
	float FogRange = 2000.0f;

	Light Lights[MaxLights];
};

struct Vertex
{
	DirectX::XMFLOAT3 Pos;
	DirectX::XMFLOAT3 Normal;
	DirectX::XMFLOAT2 TexCoord;
};

struct FrameResource
{
public:
	FrameResource(ID3D12Device* device, UINT passCount, UINT opaqueObjectCount, UINT transparentObjectCount, UINT skyObjectCount, UINT materialCount, UINT waveVertCount);
	FrameResource(const FrameResource& rhs) = delete;
	FrameResource& operator=(const FrameResource& rhs) = delete;
	~FrameResource();

	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CmdListAlloc;

	std::unique_ptr<UploadBuffer<PassConstants>> PassCB = nullptr;
	std::unique_ptr<UploadBuffer<ObjectConstants>> ObjectCB = nullptr;
	std::unique_ptr<UploadBuffer<MaterialConstants>> MaterialCB = nullptr;

	std::unique_ptr<UploadBuffer<Vertex>> WavesVB = nullptr;
	std::unique_ptr<UploadBuffer<WaterConstants>> WaterCB = nullptr;
	std::unique_ptr<UploadBuffer<TerrainConstants>> TerrainCB = nullptr;

	UINT Fence = 0;
};

struct RenderItem
{
	RenderItem() = default;

	XMFLOAT4X4 World = MathHelper::Identity4x4();

	XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

	int NumFramesDirty = gNumFrameResources;

	UINT ObjCBIndex = -1;

	Material* Mat = nullptr;
	MeshGeometry* Geo = nullptr;

	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	int BaseVertexLocation = 0;
};