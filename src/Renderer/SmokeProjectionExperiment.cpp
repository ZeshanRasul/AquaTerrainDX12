#include "Renderer.h"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

void Renderer::ConfigureProjectionExperiment()
{
    wchar_t text[64]{};
    m_ProjectionExperiment = GetEnvironmentVariableW(L"AQUA_SMOKE_PROJECTION", text, 64) != 0;
    if (!m_ProjectionExperiment) return;
    const std::wstring shape(text);
    if (shape != L"smooth" && shape != L"sharp") throw std::runtime_error("Invalid projection shape");
    m_ProjectionSharp = shape == L"sharp";
    m_ProjectionIterations = 32;
    if (GetEnvironmentVariableW(L"AQUA_SMOKE_PROJECTION_ITERATIONS", text, 64)) m_ProjectionIterations = std::stoi(text);
    if (m_ProjectionIterations < -1 || m_ProjectionIterations > 65536) throw std::runtime_error("Invalid projection iterations");
    unsigned trials = 24;
    if (GetEnvironmentVariableW(L"AQUA_SMOKE_PROJECTION_TRIALS", text, 64)) trials = std::stoul(text);
    if (trials < 2 || trials > 1000) throw std::runtime_error("Invalid projection trials");
    m_ProjectionAdvectionSteps = 1;
    m_ProjectionShiftCells = 0.0f;
    m_ProjectionDtScale = 1.0;
    if (GetEnvironmentVariableW(L"AQUA_SMOKE_PROJECTION_STEPS", text, 64)) m_ProjectionAdvectionSteps = std::stoul(text);
    if (GetEnvironmentVariableW(L"AQUA_SMOKE_PROJECTION_SHIFT", text, 64)) m_ProjectionShiftCells = std::stof(text);
    if (GetEnvironmentVariableW(L"AQUA_SMOKE_PROJECTION_DT_SCALE", text, 64)) m_ProjectionDtScale = std::stod(text);
    if (m_ProjectionAdvectionSteps < 1 || m_ProjectionAdvectionSteps > 120 ||
        !std::isfinite(m_ProjectionShiftCells) || std::abs(m_ProjectionShiftCells) > 2 ||
        !std::isfinite(m_ProjectionDtScale) || m_ProjectionDtScale < 0.25 || m_ProjectionDtScale > 4)
        throw std::runtime_error("Invalid plateau perturbation");
    m_SmokeGpuBenchmarkConfig.timeStep = m_ProjectionDtScale / 60.0;
    for (auto& centre : m_SmokeReferenceBlobCentre) centre = m_ProjectionShiftCells;
    m_SmokeReferenceCase = SmokeReferenceCase::StaticField;
    m_SmokeReferenceVelocityMode = m_ProjectionIterations < 0 ? 4 : 3;
    m_SmokeReferencePeriodic = 0;
    m_SmokeReferenceBlobSigma = m_ProjectionSharp ? -1.0f : 1.0f;
    m_CoarseObstacleCase = 0;
    if (GetEnvironmentVariableW(L"AQUA_SMOKE_COARSE_OBSTACLE", text, 64))
    {
        m_CoarseObstacleCase = std::stoul(text);
        if (m_CoarseObstacleCase < 10 || m_CoarseObstacleCase > 16 ||
            (m_CoarseObstacleCase<=13 && m_ProjectionIterations!=-1) ||
            (m_CoarseObstacleCase>=14 && m_ProjectionIterations<0))
            throw std::runtime_error("Invalid obstacle mode / pressure configuration");
        m_SmokeReferenceVelocityMode = m_CoarseObstacleCase;
        m_SmokeReferenceSpeed = 0.5f;
        if (GetEnvironmentVariableW(L"AQUA_SMOKE_OBSTACLE_CFL", text, 64)) m_SmokeReferenceSpeed = std::stof(text);
        if (!std::isfinite(m_SmokeReferenceSpeed) || m_SmokeReferenceSpeed < 0 || m_SmokeReferenceSpeed > 8)
            throw std::runtime_error("Invalid obstacle Courant number");
    }
    m_SmokeGpuBenchmarkLimiterMode = 0;
    m_SmokeGpuBenchmarkConfig.totalSteps = trials;
    m_SmokeGpuBenchmarkConfig.emitterSteps = 0;
    m_SmokeGpuBenchmarkConfig.performanceWarmupSteps = trials > 6 ? 6 : 0;
    m_ProjectionRows.clear(); m_ProjectionFailures = 0; m_ProjectionFirstHashes = {};
    const char* entries[] = { "SetProjectionFieldsCS", "ApplyDivergenceCS", "ClearPressureCS", "ApplyPressureCS", "SubtractPressureGradientCS" };
    for (unsigned i=0; i<5; ++i)
    {
        Microsoft::WRL::ComPtr<ID3DBlob> code, errors;
        const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | (m_SmokeReferenceTiming ? D3DCOMPILE_OPTIMIZATION_LEVEL3 : D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION);
        // Initial conditions always use the same optimized initializer.
        const auto result = D3DCompileFromFile(i == 0 ? (m_CoarseObstacleCase ? L"Shaders/coarse_obstacle_test.hlsl" : L"Shaders/projection_experiment.hlsl") : L"Shaders/3d_smoke_compute.hlsl",
            nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, i==0 && m_CoarseObstacleCase ? "SetCoarseObstacleFieldsCS" : entries[i], "cs_5_1", i == 0 ? D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3 : flags,
            0, &code, &errors);
        if(errors) OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
        ThrowIfFailed(result);
        D3D12_COMPUTE_PIPELINE_STATE_DESC p{}; p.pRootSignature=m_SmokeBindingRootSignature.Get();
        p.CS={code->GetBufferPointer(),code->GetBufferSize()};
        ThrowIfFailed(m_Device->CreateComputePipelineState(&p,IID_PPV_ARGS(&m_ProjectionPSOs[i])));
    }
    SmokeGpuTexture* textures[]={&m_GpuDensity[0],&m_GpuDivergence,&m_GpuPressure[0],&m_GpuDivergence,&m_GpuU[0],&m_GpuV[0],&m_GpuW[0]};
    UINT64 offset=0;
    for(unsigned i=0;i<7;++i)
    {
        auto d=textures[i]->resource->GetDesc();
        m_Device->GetCopyableFootprints(&d,0,1,offset,&m_ProjectionFootprints[i],nullptr,nullptr,nullptr);
        const auto& f=m_ProjectionFootprints[i];
        offset=(f.Offset+UINT64(f.Footprint.RowPitch)*f.Footprint.Height*f.Footprint.Depth+511)&~UINT64(511);
    }
    auto heap=CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
    auto desc=CD3DX12_RESOURCE_DESC::Buffer(offset);
    for(auto& b:m_ProjectionReadbacks) ThrowIfFailed(m_Device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&b)));
    m_TransportProbeEnabled = GetEnvironmentVariableW(L"AQUA_SMOKE_TRANSPORT_PROBE", text, 64) && text[0] == L'1';
    m_DensitySamplingFloat = GetEnvironmentVariableW(L"AQUA_SMOKE_DENSITY_FLOAT", text, 64) && text[0] == L'1';
    if (m_DensitySamplingFloat && !m_TransportProbeEnabled)
        throw std::runtime_error("Density sampling ablation requires transport probes");
    if (m_TransportProbeEnabled) CreateTransportProbe();
}

void Renderer::DispatchProjectionExperiment(ID3D12GraphicsCommandList* list, const SmokeBindingConstants& c, UINT queryBase)
{
    auto state=[&](SmokeGpuTexture& t,D3D12_RESOURCE_STATES s){if(t.state!=s){auto b=CD3DX12_RESOURCE_BARRIER::Transition(t.resource.Get(),t.state,s);list->ResourceBarrier(1,&b);t.state=s;}};
    auto srv=[&](SmokeGpuTexture& t){state(t,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);};
    auto uav=[&](SmokeGpuTexture& t){state(t,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);};
    auto bind=[&](UINT r,D3D12_GPU_DESCRIPTOR_HANDLE h){list->SetComputeRootDescriptorTable(r,h);};
    UINT gx=(c.gridResolution[0]+7)/8,gy=(c.gridResolution[1]+7)/8,gz=(c.gridResolution[2]+3)/4;
    srv(m_GpuU[0]);srv(m_GpuV[0]);srv(m_GpuW[0]); bind(SmokeBindingVelocityInputRoot,m_GpuU[0].srv);
    uav(m_GpuDivergence);bind(SmokeBindingDivergenceRoot,m_GpuDivergence.uav);
    list->SetPipelineState(m_ProjectionPSOs[1].Get());list->Dispatch(gx,gy,gz);
    uav(m_GpuPressure[0]);uav(m_GpuPressure[1]);bind(SmokeBindingPressureReadInputRoot,m_GpuPressure[0].uav);
    list->SetPipelineState(m_ProjectionPSOs[2].Get());list->Dispatch(gx,gy,gz);
    auto barrier=CD3DX12_RESOURCE_BARRIER::UAV(nullptr);list->ResourceBarrier(1,&barrier);
    srv(m_GpuDivergence);bind(SmokeBindingDivergenceReadRoot,m_GpuDivergence.srv);
    UINT read=0,write=1;
    list->SetPipelineState(m_ProjectionPSOs[3].Get());
    for(int i=0;i<m_ProjectionIterations;++i)
    {
        srv(m_GpuPressure[read]);uav(m_GpuPressure[write]);
        bind(SmokeBindingPressureReadRoot,m_GpuPressure[read].srv);bind(SmokeBindingPressureWriteRoot,m_GpuPressure[write].uav);
        list->Dispatch(gx,gy,gz);std::swap(read,write);
    }
    // Even the zero-iteration run enforces wall conditions through the production gradient kernel.
    if(m_ProjectionIterations>=0)
    {
        srv(m_GpuPressure[read]);bind(SmokeBindingPressureReadRoot,m_GpuPressure[read].srv);
        uav(m_GpuU[0]);uav(m_GpuV[0]);uav(m_GpuW[0]);bind(SmokeBindingVelocityRoot,m_GpuU[0].uav);
        list->SetPipelineState(m_ProjectionPSOs[4].Get());
        list->Dispatch((c.gridResolution[0]+8)/8,(c.gridResolution[1]+8)/8,(c.gridResolution[2]+4)/4);
    }
    list->EndQuery(m_SmokeGpuQueries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,queryBase+SmokeTimestampPressureGradientEnd);
}

void Renderer::CaptureProjectionInitialDensity(ID3D12GraphicsCommandList* list)
{
    // Preserve the true initial field before either scalar ping-pong buffer is
    // overwritten by the second step. This copy is outside the timed interval.
    auto& density = m_GpuDensity[m_GpuScalarReadIndex];
    if (density.state != D3D12_RESOURCE_STATE_COPY_SOURCE)
    {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(density.resource.Get(), density.state, D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->ResourceBarrier(1, &barrier);
        density.state = D3D12_RESOURCE_STATE_COPY_SOURCE;
    }
    D3D12_TEXTURE_COPY_LOCATION source{}, destination{};
    source.pResource = density.resource.Get(); source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destination.pResource = m_ProjectionReadbacks[m_CurrentFrameResourceIndex].Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = m_ProjectionFootprints[0];
    list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
}

void Renderer::CaptureProjectionExperiment(ID3D12GraphicsCommandList* list,const SmokeBindingConstants& c)
{
    auto state=[&](SmokeGpuTexture& t,D3D12_RESOURCE_STATES s){if(t.state!=s){auto b=CD3DX12_RESOURCE_BARRIER::Transition(t.resource.Get(),t.state,s);list->ResourceBarrier(1,&b);t.state=s;}};
    auto copy=[&](SmokeGpuTexture& t,unsigned i){state(t,D3D12_RESOURCE_STATE_COPY_SOURCE);D3D12_TEXTURE_COPY_LOCATION a{},b{};
        a.pResource=t.resource.Get();a.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        b.pResource=m_ProjectionReadbacks[m_CurrentFrameResourceIndex].Get();b.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;b.PlacedFootprint=m_ProjectionFootprints[i];
        list->CopyTextureRegion(&b,0,0,0,&a,nullptr);};
    // Everything here is after the final timestamp; no readbacks pollute cost.
    copy(m_GpuDivergence,1);
    copy(m_GpuPressure[m_ProjectionIterations>0 ? m_ProjectionIterations%2 : 0],2);
    for(auto* t:{&m_GpuU[0],&m_GpuV[0],&m_GpuW[0]})state(*t,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    list->SetComputeRootDescriptorTable(SmokeBindingVelocityInputRoot,m_GpuU[0].srv);
    state(m_GpuDivergence,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    list->SetComputeRootDescriptorTable(SmokeBindingDivergenceRoot,m_GpuDivergence.uav);
    list->SetPipelineState(m_ProjectionPSOs[1].Get());list->Dispatch((c.gridResolution[0]+7)/8,(c.gridResolution[1]+7)/8,(c.gridResolution[2]+3)/4);
    copy(m_GpuDivergence,3);copy(m_GpuU[0],4);copy(m_GpuV[0],5);copy(m_GpuW[0],6);
}

void Renderer::RecordProjectionExperiment(const void* mapped,unsigned step)
{
    if (m_TransportProbeEnabled) RecordTransportProbe(step);
    void* extra=nullptr;auto& buffer=m_ProjectionReadbacks[m_CurrentFrameResourceIndex];
    D3D12_RANGE range{0,static_cast<SIZE_T>(buffer->GetDesc().Width)};ThrowIfFailed(buffer->Map(0,&range,&extra));
    const char* names[]={"initial_density","div_before","pressure","div_after","u","v","w","density"};
    std::filesystem::create_directories(m_SmokeReferenceOutput);
    unsigned nonfinite=0;bool identical=true;
    for(unsigned f=0;f<8;++f)
    {
        const auto& fp=f==7?m_SmokeGpuReadbackFootprint:m_ProjectionFootprints[f];
        const auto* ptr=static_cast<const std::byte*>(f==7?mapped:extra);
        std::uint64_t hash=14695981039346656037ull;
        std::ofstream file;
        if(step==1){file.open(m_SmokeReferenceOutput/(std::string(names[f])+".f32"),std::ios::binary);file.exceptions(std::ios::failbit|std::ios::badbit);}
        for(UINT z=0;z<fp.Footprint.Depth;++z)for(UINT y=0;y<fp.Footprint.Height;++y)
        {
            auto* row=reinterpret_cast<const float*>(ptr+fp.Offset+(UINT64(z)*fp.Footprint.Height+y)*fp.Footprint.RowPitch);
            for(UINT x=0;x<fp.Footprint.Width;++x)if(!std::isfinite(row[x]))++nonfinite;
            auto* bytes=reinterpret_cast<const unsigned char*>(row);
            for(UINT b=0;b<fp.Footprint.Width*sizeof(float);++b)hash=(hash^bytes[b])*1099511628211ull;
            if(step==1)file.write(reinterpret_cast<const char*>(row),fp.Footprint.Width*sizeof(float));
        }
        if(step==1)m_ProjectionFirstHashes[f]=hash;else identical=identical&&(hash==m_ProjectionFirstHashes[f]);
    }
    D3D12_RANGE writes{0,0};buffer->Unmap(0,&writes);
    const auto* t=static_cast<const UINT64*>(mapped);double scale=1000.0/m_SmokeGpuTimestampFrequency;
    const double p=(t[SmokeTimestampPressureGradientEnd]-t[SmokeTimestampStepBegin])*scale;
    const double a=(t[SmokeTimestampStepEnd]-t[SmokeTimestampPressureGradientEnd])*scale;
    if(nonfinite || !identical || !(p>0) || !(a>0))++m_ProjectionFailures;
    std::ostringstream row;row<<std::setprecision(17)<<step<<','<<p<<','<<a<<','<<p+a<<','<<nonfinite<<','<<(identical?1:0);
    m_ProjectionRows.push_back(row.str());
}

void Renderer::SaveProjectionExperiment()
{
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    ThrowIfFailed(m_DxgiFactory->EnumAdapterByLuid(m_Device->GetAdapterLuid(), IID_PPV_ARGS(&adapter)));
    DXGI_ADAPTER_DESC adapterDesc{}; ThrowIfFailed(adapter->GetDesc(&adapterDesc));
    const std::wstring wideName(adapterDesc.Description);
    const int nameBytes=WideCharToMultiByte(CP_UTF8,0,wideName.data(),static_cast<int>(wideName.size()),nullptr,0,nullptr,nullptr);
    std::string gpuName(nameBytes,'\0');
    WideCharToMultiByte(CP_UTF8,0,wideName.data(),static_cast<int>(wideName.size()),gpuName.data(),nameBytes,nullptr,nullptr);
    LARGE_INTEGER driverVersion{};
    adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driverVersion);
    std::ofstream csv(m_SmokeReferenceOutput/"trials.csv");csv.exceptions(std::ios::failbit|std::ios::badbit);
    csv<<"trial,projection_ms,advection_ms,total_ms,nonfinite,repeat_identical\n";
    for(const auto& row:m_ProjectionRows)csv<<row<<'\n';csv.close();
    const auto n=m_SmokeSolver.Density().Resolution();const auto h=m_SmokeSolver.Density().GridSpacing();
    std::ofstream m(m_SmokeReferenceOutput/"manifest.json");m.exceptions(std::ios::failbit|std::ios::badbit);
    m<<std::setprecision(17)<<"{\n\"schema_version\":2,\n\"experiment\":\"projection_sensitivity_v2\",\n\"shape\":"<<std::quoted(m_ProjectionSharp?"sharp":"smooth")
        <<",\n\"advection_steps\":"<<m_ProjectionAdvectionSteps<<",\n\"shift_cells_diagonal\":"<<m_ProjectionShiftCells
        <<",\n\"coarse_obstacle_case\":"<<m_CoarseObstacleCase<<",\n\"obstacle_cfl\":"<<m_SmokeReferenceSpeed
        <<",\n\"dt_scale\":"<<m_ProjectionDtScale<<",\n\"velocity_evolution\":\"project_once_then_hold_fixed\""
        <<",\n\"transport_probe\":"<<(m_TransportProbeEnabled?"true":"false")
        <<",\n\"density_sampling_float\":"<<(m_DensitySamplingFloat?"true":"false")
        <<",\n\"timings_include_probes\":"<<(m_TransportProbeEnabled?"true":"false")
        <<",\n\"advection\":"<<std::quoted(m_SmokeGpuAdvectionMode==SmokeAdvectionMode::MacCormack?"maccormack":"sl")
        <<",\n\"iterations\":"<<m_ProjectionIterations<<",\n\"resolution\":"<<n.x
        <<",\n\"spacing\":["<<h.x<<','<<h.y<<','<<h.z<<"],\n\"dt\":"<<m_SmokeGpuBenchmarkConfig.timeStep
        <<",\n\"fluid_density\":"<<m_SmokeSolver.FluidDensity()<<",\n\"optimized\":"<<(m_SmokeReferenceTiming?"true":"false")
        <<",\n\"gpu\":"<<std::quoted(gpuName)<<",\n\"driver_version_raw\":"<<driverVersion.QuadPart
        <<",\n\"timestamp_frequency\":"<<m_SmokeGpuTimestampFrequency
        <<",\n\"closed_domain\":true,\n\"limiter\":\"clamp\",\n\"total_trials\":"<<m_SmokeGpuBenchmarkConfig.totalSteps
        <<",\n\"recorded_trials\":"<<m_ProjectionRows.size()<<",\n\"warmup_trials\":"<<m_SmokeGpuBenchmarkConfig.performanceWarmupSteps
        <<",\n\"validation_failures\":"<<m_ProjectionFailures<<",\n\"field_hashes\":[";
    for(unsigned i=0;i<8;++i)m<<(i?",":"")<<std::quoted(std::to_string(m_ProjectionFirstHashes[i]));
    m<<"]\n}\n";
}
