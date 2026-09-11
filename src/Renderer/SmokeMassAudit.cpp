#include "Renderer.h"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <stdexcept>

void Renderer::StartSmokeMassAudit(bool automatic)
{
    if (m_SmokeGpuBenchmarkRunning) return;
    CreateSmokeMassAudit();
    m_SmokeMassAudit = true;
    m_SmokeMassAuditAutomatic = automatic;
    wchar_t probes[8]{};
    m_SmokeAuditProbes = !(automatic && GetEnvironmentVariableW(L"AQUA_SMOKE_AUDIT_PROBES", probes, 8) && probes[0] == L'0');
    m_SmokeAuditFailures = 0;
    m_SmokeAuditRows.clear();
    m_FluidDemoMode = FluidDemoMode::Smoke3DGPU;
    m_SmokeGpuOpenTopEnabled = true;
    m_SmokeGpuSphereEnabled = false;
    m_SphereTranslationEnabled = false;
    m_SmokeGpuBenchmarkVorticityEpsilon = 0;
    m_SmokeGpuBenchmarkLimiterMode = static_cast<int>(m_SmokeGpuLimiterMode);
    SmokeBenchmarkConfig config;
    config.totalSteps = 480;
    config.emitterSteps = 240;
    config.timeStep = 1.0 / 60;
    config.performanceWarmupSteps = 0;
    config.renderingEnabledDuringRun = false;
    config.scenario = "mass_budget_open_top_v1";
    config.advectionMode = m_SmokeGpuAdvectionMode;
    config.limiterMode = m_SmokeGpuLimiterMode;
    const auto n = m_SmokeSolver.Density().Resolution();
    config.emitterCell = {n.x / 2, n.y / 4, n.z / 2};
    m_SmokeGpuBenchmarkConfig = config;
    SmokePhysicsParameters physics;
    physics.ambientTemperature = 0;
    physics.temperatureBuoyancy = 0.6;
    physics.smokeWeight = 0.05;
    physics.densityDissipation = 0.1;
    physics.temperatureCooling = 0.5;
    m_SmokeGpuBenchmarkPhysics = physics;
    m_SmokeGpuBenchmarkIterations = 40;
    m_SmokeGpuBenchmarkSamples.clear();
    m_SmokeGpuBenchmarkSubmitted = 0;
    m_SmokeGpuBenchmarkStopping = false;
    m_SmokeGpuBenchmarkRunning = true;
    m_SmokeGpuResetRequested = true;
    m_SmokeGpuStepRequested = false;
    m_SmokeGpuPendingSteps = 0;
    m_SmokeGpuRestoreVolume = m_ShowSmokeVolume;
    m_ShowSmokeVolume = false;
    wchar_t output[32768]{};
    if (automatic && GetEnvironmentVariableW(L"AQUA_SMOKE_AUDIT_OUTPUT", output, 32768))
        m_SmokeAuditOutput = output;
    else
        m_SmokeAuditOutput = std::filesystem::path("diagnostics/runs") /
            (std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + "_mass_audit");
    m_SmokeGpuBenchmarkStatus = "Recording direct mass budget; timings are instrumented and excluded from performance reports.";
}

void Renderer::CreateSmokeMassAudit()
{
    if (m_SmokeAuditCells) return;
    const auto desc = m_GpuDensity[0].resource->GetDesc();
    UINT64 bytes = 0;
    m_Device->GetCopyableFootprints(&desc, 0, 1, 0, &m_SmokeAuditFootprint, nullptr, nullptr, &bytes);
    // Include the same nonzero base offset and row/slice pitch as normal readback.
    m_SmokeAuditFootprint = m_SmokeGpuReadbackFootprint;
    m_SmokeAuditTextureStride = (m_SmokeAuditFootprint.Offset + bytes + 511) & ~UINT64(511);
    m_SmokeAuditCellsOffset = SmokeAuditStages * m_SmokeAuditTextureStride;
    const UINT64 cellBytes = desc.Width * desc.Height * desc.DepthOrArraySize * sizeof(XMFLOAT4);
    const auto gpuHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    const auto gpuDesc = CD3DX12_RESOURCE_DESC::Buffer(cellBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ThrowIfFailed(m_Device->CreateCommittedResource(&gpuHeap, D3D12_HEAP_FLAG_NONE,
        &gpuDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_SmokeAuditCells)));
    const auto cpuHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
    const auto cpuDesc = CD3DX12_RESOURCE_DESC::Buffer(m_SmokeAuditCellsOffset + cellBytes);
    for (auto& buffer : m_SmokeAuditReadbacks)
        ThrowIfFailed(m_Device->CreateCommittedResource(&cpuHeap, D3D12_HEAP_FLAG_NONE,
            &cpuDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&buffer)));
    const D3D_SHADER_MACRO defines[] = {{"SMOKE_MASS_AUDIT", "1"}, {nullptr, nullptr}};
    const auto compile = [&](const char* entry, Microsoft::WRL::ComPtr<ID3D12PipelineState>& pso)
    {
        Microsoft::WRL::ComPtr<ID3DBlob> code, errors;
        const auto result = D3DCompileFromFile(L"Shaders/3d_smoke_compute.hlsl", defines,
            D3D_COMPILE_STANDARD_FILE_INCLUDE, entry, "cs_5_1",
            D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,
            0, &code, &errors);
        if (errors) OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
        ThrowIfFailed(result);
        D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline{};
        pipeline.pRootSignature = m_SmokeBindingRootSignature.Get();
        pipeline.CS = {code->GetBufferPointer(), code->GetBufferSize()};
        ThrowIfFailed(m_Device->CreateComputePipelineState(&pipeline, IID_PPV_ARGS(&pso)));
    };
    compile("AdvectScalarsCS", m_SmokeAuditSLPSO);
    compile("MacCormackScalarsCS", m_SmokeAuditMCPSO);
    compile("AuditPatternCS", m_SmokeAuditPatternPSO);
}

void Renderer::CaptureSmokeMassAudit(ID3D12GraphicsCommandList* commands, SmokeGpuTexture& texture, unsigned stage)
{
    // Restore state so the probe does not change subsequent binding decisions.
    const auto previous = texture.state;
    D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(texture.resource.Get(), previous, D3D12_RESOURCE_STATE_COPY_SOURCE);
    if (previous != D3D12_RESOURCE_STATE_COPY_SOURCE) commands->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION source{}, destination{};
    source.pResource = texture.resource.Get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destination.pResource = m_SmokeAuditReadbacks[m_CurrentFrameResourceIndex].Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = m_SmokeAuditFootprint;
    destination.PlacedFootprint.Offset += stage * m_SmokeAuditTextureStride;
    commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    if (previous != D3D12_RESOURCE_STATE_COPY_SOURCE)
    {
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        commands->ResourceBarrier(1, &barrier);
    }
}

void Renderer::CollectSmokeMassAudit(unsigned frame, unsigned step, bool emit, double densitySum, std::uint64_t densityHash)
{
    // Called only after the owning frame fence completed, alongside normal readback.
    const auto n = m_SmokeSolver.Density().Resolution();
    void* memory = nullptr;
    const auto& buffer = m_SmokeAuditReadbacks[frame];
    D3D12_RANGE range{0, static_cast<SIZE_T>(buffer->GetDesc().Width)};
    ThrowIfFailed(buffer->Map(0, &range, &memory));
    const auto* base = static_cast<const std::byte*>(memory);
    const auto texel = [&](unsigned stage, unsigned x, unsigned y, unsigned z)
    {
        const auto offset = stage * m_SmokeAuditTextureStride + m_SmokeAuditFootprint.Offset +
            (z * m_SmokeAuditFootprint.Footprint.Height + y) * m_SmokeAuditFootprint.Footprint.RowPitch;
        return reinterpret_cast<const float*>(base + offset)[x];
    };
    double sums[5]{};
    double corrected = 0, limited = 0, lowerDelta = 0, upperDelta = 0, expectedFinal = 0;
    unsigned lowerCount = 0, upperCount = 0, patternErrors = 0, injectionErrors = 0, untouchedErrors = 0, nonfinite = 0;
    std::uint64_t finalHash = 14695981039346656037ull;
    double finalCopySum = 0;
    const bool mc = m_SmokeGpuAdvectionMode == SmokeAdvectionMode::MacCormack;
    const auto* cells = reinterpret_cast<const XMFLOAT4*>(base + m_SmokeAuditCellsOffset);
    const float added = emit ? 30.0f * static_cast<float>(m_SmokeGpuBenchmarkConfig.timeStep) : 0;
    const double decay = std::exp(-static_cast<double>(static_cast<float>(m_SmokeGpuBenchmarkPhysics.densityDissipation)) *
        static_cast<float>(m_SmokeGpuBenchmarkConfig.timeStep));
    for (unsigned z = 0; z < n.z; ++z)
        for (unsigned y = 0; y < n.y; ++y)
            for (unsigned x = 0; x < n.x; ++x)
            {
                for (unsigned s = 0; s < (mc ? 5u : 3u); ++s)
                {
                    const float v = texel(s, x, y, z);
                    if (std::isfinite(v)) sums[s] += v; else ++nonfinite;
                }
                const bool source = emit && x == n.x / 2 && y == n.y / 4 && z == n.z / 2;
                // GPU may fuse rate*dt + old while CPU rounds multiply first.
                // Permit float rounding at the one injected cell only.
                const double expected = texel(0, x, y, z) + (source ?
                    30.0 * static_cast<float>(m_SmokeGpuBenchmarkConfig.timeStep) : 0.0);
                const double tolerance = source ? 2e-7 * std::max(1.0, std::abs(expected)) : 0;
                if (std::abs(texel(1, x, y, z) - expected) > tolerance) ++injectionErrors;
                if (texel(1, x, y, z) != texel(2, x, y, z)) ++untouchedErrors;
                if (texel(5, x, y, z) != 0.25f + (step % 7) * 0.125f) ++patternErrors;
                const float impulse = x == (step * 3) % n.x && y == (step * 5) % n.y && z == (step * 7) % n.z ? 7.25f : 0;
                if (texel(6, x, y, z) != impulse) ++patternErrors;
                const auto& c = cells[(z * n.y + y) * n.x + x];
                const float finalValue = texel(7, x, y, z);
                finalCopySum += finalValue;
                const auto* valueBytes = reinterpret_cast<const unsigned char*>(&finalValue);
                for (unsigned b = 0; b < sizeof(float); ++b)
                    finalHash = (finalHash ^ valueBytes[b]) * 1099511628211ull;
                if (!std::isfinite(c.x) || !std::isfinite(c.y) || !std::isfinite(c.z) || !std::isfinite(c.w)) ++nonfinite;
                corrected += c.x; limited += c.y; lowerDelta += c.z; upperDelta += c.w;
                lowerCount += c.z != 0; upperCount += c.w != 0;
                expectedFinal += std::max(c.y * decay, 0.0);
            }
    D3D12_RANGE writes{0, 0};
    buffer->Unmap(0, &writes);
    const double dampingResidual = densitySum - expectedFinal;
    const double limiterResidual = limited - corrected - lowerDelta - upperDelta;
    const double scale = std::max({1.0, std::abs(densitySum), std::abs(limited), std::abs(corrected)});
    // Float shader exp/multiply versus double reference; not a conservation tolerance.
    const bool readbackMatch = finalHash == densityHash && finalCopySum == densitySum;
    const bool failed = !readbackMatch || patternErrors || injectionErrors || untouchedErrors || nonfinite ||
        std::abs(dampingResidual) > 2e-6 * scale || std::abs(limiterResidual) > 2e-6 * scale;
    m_SmokeAuditFailures += failed;
    std::ostringstream row;
    row << std::setprecision(17) << step << ',' << emit << ',' << sums[0] << ',' << sums[1] << ',' << sums[2] << ',';
    if (mc) row << sums[3] << ',' << sums[4]; else row << corrected << ',';
    row << ',' << corrected << ',' << limited << ',' << densitySum << ',' << added << ',' << sums[1] - sums[0]
        << ',' << (mc ? sums[3] : corrected) - sums[2] << ',';
    if (mc) row << corrected - sums[3];
    row << ',' << lowerCount << ',' << lowerDelta << ',' << upperCount << ',' << upperDelta
        << ',' << densitySum - limited << ',' << dampingResidual << ',' << limiterResidual
        << ',' << patternErrors << ',' << injectionErrors << ',' << untouchedErrors << ',' << nonfinite << ',' << readbackMatch << ',' << failed;
    m_SmokeAuditRows.push_back(row.str());
}

void Renderer::SaveSmokeMassAudit()
{
    try
    {
        std::filesystem::create_directories(m_SmokeAuditOutput);
        std::ofstream csv(m_SmokeAuditOutput / "budget.csv");
        csv.exceptions(std::ios::failbit | std::ios::badbit);
        csv << "step,emitter,before_source,after_source,before_advection,forward,reverse,corrected,limited,final,expected_injection,measured_injection,forward_delta,correction_delta,lower_changed_cells,lower_delta,upper_changed_cells,upper_delta,damping_and_positivity_delta,damping_residual,limiter_residual,pattern_errors,injection_errors,untouched_errors,nonfinite,readback_match,audit_failed\n";
        for (const auto& row : m_SmokeAuditRows) csv << row << '\n';
        csv.close();
        const auto n = m_SmokeSolver.Density().Resolution();
        DXGI_ADAPTER_DESC adapter{};
        Microsoft::WRL::ComPtr<IDXGIAdapter> gpu;
        ThrowIfFailed(m_DxgiFactory->EnumAdapterByLuid(m_Device->GetAdapterLuid(), IID_PPV_ARGS(&gpu)));
        ThrowIfFailed(gpu->GetDesc(&adapter));
        char adapterName[512]{};
        WideCharToMultiByte(CP_UTF8, 0, adapter.Description, -1, adapterName, sizeof(adapterName), nullptr, nullptr);
        std::ofstream fields(m_SmokeAuditOutput / "end-fields.csv");
        fields.exceptions(std::ios::failbit | std::ios::badbit);
        fields << "step,density_sum,density_min,density_max,density_hash_fnv1a64,kinetic_energy,rms_divergence_after\n" << std::setprecision(17);
        for (const auto& s : m_SmokeGpuBenchmarkSamples)
            fields << s.step << ',' << s.densitySum << ',' << s.densityMin << ',' << s.densityMax << ','
                << s.densityHash << ',' << s.kineticEnergy << ',' << s.divergenceAfterRms << '\n';
        fields.close();
        std::ofstream manifest(m_SmokeAuditOutput / "manifest.json");
        manifest.exceptions(std::ios::failbit | std::ios::badbit);
        manifest << std::setprecision(17)
            << "{\n  \"schema_version\": 1,\n  \"purpose\": \"correctness_only_no_performance_claims\","
            << "\n  \"scenario\": \"mass_budget_open_top_v1\",\n  \"advection\": "
            << std::quoted(m_SmokeGpuAdvectionMode == SmokeAdvectionMode::MacCormack ? "maccormack" : "sl")
            << ",\n  \"limiter_mode\": " << m_SmokeGpuBenchmarkLimiterMode
            << ",\n  \"probes_enabled\": " << (m_SmokeAuditProbes ? "true" : "false")
            << ",\n  \"descriptor_layout\": \"density_hat,temperature_hat,density_bar,temperature_bar\""
            << ",\n  \"resolution\": [" << n.x << ',' << n.y << ',' << n.z << ']'
            << ",\n  \"grid_spacing\": [" << m_SmokeSolver.Density().GridSpacing().x << ','
                << m_SmokeSolver.Density().GridSpacing().y << ',' << m_SmokeSolver.Density().GridSpacing().z << ']'
            << ",\n  \"origin\": [" << m_SmokeSolver.Density().Origin().x << ','
                << m_SmokeSolver.Density().Origin().y << ',' << m_SmokeSolver.Density().Origin().z << ']'
            << ",\n  \"dt\": " << m_SmokeGpuBenchmarkConfig.timeStep
            << ",\n  \"source_cell\": [" << n.x / 2 << ',' << n.y / 4 << ',' << n.z / 2 << ']'
            << ",\n  \"density_rate\": 30,\n  \"temperature_rate\": 10,\n  \"emitter_steps\": 240,"
            << "\n  \"temperature_buoyancy\": 0.6,\n  \"smoke_weight\": 0.05,\n  \"density_dissipation\": 0.1,"
            << "\n  \"temperature_cooling\": 0.5,\n  \"pressure_iterations\": 40,\n  \"vorticity_epsilon\": 0,"
            << "\n  \"obstacle\": false,\n  \"open_top\": true,\n  \"expected_steps\": 480,\n  \"recorded_steps\": " << m_SmokeGpuBenchmarkSamples.size()
            << ",\n  \"completed\": " << (m_SmokeGpuBenchmarkSamples.size() == 480 ? "true" : "false")
            << ",\n  \"audit_failed_steps\": " << m_SmokeAuditFailures
            << ",\n  \"gpu\": " << std::quoted(adapterName)
            << ",\n  \"gpu_vendor_id\": " << adapter.VendorId << ",\n  \"gpu_device_id\": " << adapter.DeviceId
            << ",\n  \"compiler_msvc\": " << _MSC_VER
#ifdef _DEBUG
            << ",\n  \"build\": \"Debug\""
#else
            << ",\n  \"build\": \"Release\""
#endif
            << ",\n  \"shader_flags\": \"STRICTNESS|DEBUG|SKIP_OPTIMIZATION (same as production path)\","
            << "\n  \"units\": \"sum of density texels; multiply by cell volume for integral\","
            << "\n  \"scope\": \"direct snapshots and in-kernel limiter values; forward/correction deltas include boundary handling, not measured physical flux; reverse is an intermediate\","
            << "\n  \"checks\": \"every-texel moving impulse and varying constant; injection tolerance 2e-7 times max(1,expected source value) at source only, exact elsewhere; unchanged pre-advection exact; final copy hash and sum match normal readback; damping/limiter residual tolerance 2e-6 times field-sum scale\"\n}\n";
        manifest.close();
        m_SmokeGpuBenchmarkStatus = "Saved mass audit: " + std::filesystem::absolute(m_SmokeAuditOutput).string();
    }
    catch (const std::exception& error)
    {
        ++m_SmokeAuditFailures;
        m_SmokeGpuBenchmarkStatus = std::string("Mass audit export failed: ") + error.what();
    }
}
