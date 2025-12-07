#include "FrameResource.h"

FrameResource::FrameResource(ID3D12Device* device, UINT passCount, UINT opaqueObjectCount, UINT transparentObjectCount, UINT skyObjectCount, UINT materialCount, UINT waveVertCount)
{
	ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(CmdListAlloc.GetAddressOf())));

	UINT totalObjectCount = opaqueObjectCount + transparentObjectCount + skyObjectCount;

	PassCB = std::make_unique<UploadBuffer<PassConstants>>(device, passCount, true);
	TerrainCB = std::make_unique<UploadBuffer<TerrainConstants>>(device, passCount, true);
	ObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(device, totalObjectCount, true);
	MaterialCB = std::make_unique<UploadBuffer<MaterialConstants>>(device, totalObjectCount, true);

	WavesVB = std::make_unique<UploadBuffer<Vertex>>(device, waveVertCount, false);
	WaterCB = std::make_unique<UploadBuffer<WaterConstants>>(device, transparentObjectCount, true);
}

FrameResource::~FrameResource()
{

}