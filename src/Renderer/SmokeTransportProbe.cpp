#include "Renderer.h"
#include <fstream>
#include <stdexcept>

void Renderer::CreateTransportProbe()
{
    if(m_SmokeGpuAdvectionMode!=SmokeAdvectionMode::SemiLagrangian)
        throw std::runtime_error("Transport trace probe currently validates SL only");
    m_TransportProbeHashes={};
    const auto n=m_SmokeSolver.Density().Resolution();
    m_TransportProbeBytes=UINT64(n.x)*n.y*n.z*4*sizeof(XMFLOAT4);
    auto gpuHeap=CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    auto gpuDesc=CD3DX12_RESOURCE_DESC::Buffer(m_TransportProbeBytes,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ThrowIfFailed(m_Device->CreateCommittedResource(&gpuHeap,D3D12_HEAP_FLAG_NONE,&gpuDesc,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&m_TransportProbeBuffer)));
    auto cpuHeap=CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
    auto cpuDesc=CD3DX12_RESOURCE_DESC::Buffer(m_TransportProbeBytes*m_ProjectionAdvectionSteps);
    for(auto& buffer:m_TransportProbeReadbacks)
        ThrowIfFailed(m_Device->CreateCommittedResource(&cpuHeap,D3D12_HEAP_FLAG_NONE,&cpuDesc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&buffer)));
    const char* entries[]={"TraceTransportCS","RecordTransportOutputCS","AdvectFloatDensityCS"};
    for(unsigned i=0;i<3;++i)
    {
        Microsoft::WRL::ComPtr<ID3DBlob> code,errors;
        auto result=D3DCompileFromFile(L"Shaders/transport_probe.hlsl",nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,entries[i],"cs_5_1",
            D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
        if(errors)OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
        ThrowIfFailed(result);
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};desc.pRootSignature=m_SmokeBindingRootSignature.Get();
        desc.CS={code->GetBufferPointer(),code->GetBufferSize()};
        ThrowIfFailed(m_Device->CreateComputePipelineState(&desc,IID_PPV_ARGS(&m_TransportProbePSOs[i])));
    }
}

void Renderer::DispatchTransportProbe(ID3D12GraphicsCommandList* list,unsigned step,bool after)
{
    auto srv=[&](SmokeGpuTexture& t){if(t.state!=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE){
        auto b=CD3DX12_RESOURCE_BARRIER::Transition(t.resource.Get(),t.state,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        list->ResourceBarrier(1,&b);t.state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;}};
    srv(m_GpuDensity[m_GpuScalarReadIndex]);srv(m_GpuTemperature[m_GpuScalarReadIndex]);
    list->SetComputeRootDescriptorTable(SmokeBindingInputRoot,m_GpuDensity[m_GpuScalarReadIndex].srv);
    list->SetComputeRootUnorderedAccessView(SmokeBindingAuditRoot,m_TransportProbeBuffer->GetGPUVirtualAddress());
    auto order=CD3DX12_RESOURCE_BARRIER::UAV(m_TransportProbeBuffer.Get());list->ResourceBarrier(1,&order);
    list->SetPipelineState(m_TransportProbePSOs[after?1:0].Get());
    const auto n=m_SmokeSolver.Density().Resolution();list->Dispatch((UINT(n.x)+7)/8,(UINT(n.y)+7)/8,(UINT(n.z)+3)/4);
    if(after)
    {
        auto copy=CD3DX12_RESOURCE_BARRIER::Transition(m_TransportProbeBuffer.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->ResourceBarrier(1,&copy);
        list->CopyBufferRegion(m_TransportProbeReadbacks[m_CurrentFrameResourceIndex].Get(),step*m_TransportProbeBytes,m_TransportProbeBuffer.Get(),0,m_TransportProbeBytes);
        auto restore=CD3DX12_RESOURCE_BARRIER::Transition(m_TransportProbeBuffer.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        list->ResourceBarrier(1,&restore);
    }
}

void Renderer::RecordTransportProbe(unsigned trial)
{
    std::filesystem::create_directories(m_SmokeReferenceOutput);
    auto& buffer=m_TransportProbeReadbacks[m_CurrentFrameResourceIndex];void* mapped=nullptr;
    D3D12_RANGE range{0,static_cast<SIZE_T>(buffer->GetDesc().Width)};ThrowIfFailed(buffer->Map(0,&range,&mapped));
    for(unsigned step=0;step<m_ProjectionAdvectionSteps;++step)
    {
        auto* bytes=static_cast<const unsigned char*>(mapped)+step*m_TransportProbeBytes;
        std::uint64_t hash=14695981039346656037ull;
        for(UINT64 i=0;i<m_TransportProbeBytes;++i)hash=(hash^bytes[i])*1099511628211ull;
        auto* values=reinterpret_cast<const float*>(bytes);
        for(UINT64 i=0;i<m_TransportProbeBytes/sizeof(float);++i)if(!std::isfinite(values[i]))++m_ProjectionFailures;
        if(trial==1)
        {
            m_TransportProbeHashes[step]=hash;
            std::ofstream output(m_SmokeReferenceOutput/("trace-step"+std::to_string(step+1)+".f32"),std::ios::binary);
            output.exceptions(std::ios::failbit|std::ios::badbit);output.write(reinterpret_cast<const char*>(bytes),m_TransportProbeBytes);
        }
        else if(hash!=m_TransportProbeHashes[step])++m_ProjectionFailures;
    }
    D3D12_RANGE writes{0,0};buffer->Unmap(0,&writes);
}
