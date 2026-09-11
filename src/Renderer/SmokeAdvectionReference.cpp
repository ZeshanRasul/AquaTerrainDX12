#include "Renderer.h"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <cmath>
#include <cstdlib>

// Passive-advection reference harness.
//
// Increment 1 (this file): cases 1 (zero velocity, source only) and 2 (zero
// velocity, initialized field). Both use a ZERO prescribed velocity, so no
// periodicity is required and transport is the identity. They validate the
// harness plumbing (injection schedule, identity transport, error metrics)
// against an analytic reference before the moving-field cases are added.
//
// The run reuses the benchmark step loop and the per-step density readback; only
// the dispatch (inject + production scalar advection, nothing else) and the
// metric collection differ. Buoyancy, projection, confinement and damping are
// all disabled. The production AdvectScalarsCS / MacCormack combine kernels are
// used unchanged, so the harness tests the implementation we ship.

void Renderer::CreateReferenceTimingPSOs()
{
    if (m_SmokeRefOptAdvectScalarsPSO) return; // compile once
    const auto compile = [&](const char* entry, Microsoft::WRL::ComPtr<ID3D12PipelineState>& pso)
    {
        Microsoft::WRL::ComPtr<ID3DBlob> code, errors;
        // OPTIMIZATION_LEVEL3, no DEBUG/SKIP_OPTIMIZATION. These PSOs are used only
        // for timing runs; correctness runs keep the production SKIP_OPTIMIZATION
        // build, and numerical equality between the two is verified per run.
        const HRESULT result = D3DCompileFromFile(L"Shaders/3d_smoke_compute.hlsl", nullptr,
            D3D_COMPILE_STANDARD_FILE_INCLUDE, entry, "cs_5_1",
            D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
        if (errors) OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
        ThrowIfFailed(result);
        D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline{};
        pipeline.pRootSignature = m_SmokeBindingRootSignature.Get();
        pipeline.CS = { code->GetBufferPointer(), code->GetBufferSize() };
        ThrowIfFailed(m_Device->CreateComputePipelineState(&pipeline, IID_PPV_ARGS(&pso)));
    };
    compile("AdvectScalarsCS", m_SmokeRefOptAdvectScalarsPSO);
    compile("AdvectScalarsRawCS", m_SmokeRefOptAdvectScalarsRawPSO);
    compile("MacCormackScalarsCS", m_SmokeRefOptMacCormackScalarsPSO);
}

void Renderer::StartSmokeAdvectionReference(SmokeReferenceCase referenceCase, bool automatic)
{
    if (m_SmokeGpuBenchmarkRunning) return;
    // Timing runs use optimized advection PSOs; correctness runs (default) use the
    // production build so their numerics match every other run and the mass audit.
    wchar_t optText[8]{};
    m_SmokeReferenceTiming = GetEnvironmentVariableW(L"AQUA_SMOKE_REFERENCE_OPTIMIZE", optText, 8) && optText[0] == L'1';
    if (m_SmokeReferenceTiming) CreateReferenceTimingPSOs();
    m_SmokeReferenceCase = referenceCase;
    m_SmokeReferenceAutomatic = automatic;
    m_SmokeReferenceRows.clear();
    m_FluidDemoMode = FluidDemoMode::Smoke3DGPU;
    m_SmokeGpuOpenTopEnabled = false;   // sealed, inert at zero velocity
    m_SmokeGpuSphereEnabled = false;
    m_SphereTranslationEnabled = false;

    const auto n = m_SmokeSolver.Density().Resolution();
    const float nx = static_cast<float>(n.x);
    const double dt = 1.0 / 60.0;

    // Per-case setup. Blob centre/sigma in cells; velocity mode and speed drive
    // the prescribed field and the CPU analytic reference identically.
    const char* caseTag = "source_only";
    std::size_t totalSteps = 300, emitterSteps = 0;
    m_SmokeReferencePeriodic = 0;
    m_SmokeReferenceVelocityMode = 0;
    m_SmokeReferenceSpeed = 0.0f;
    m_SmokeReferenceBlobSigma = 0.12f * nx;
    m_SmokeReferenceBlobCentre[0] = n.x * 0.5f;
    m_SmokeReferenceBlobCentre[1] = n.y * 0.5f;
    m_SmokeReferenceBlobCentre[2] = n.z * 0.5f;
    switch (referenceCase)
    {
    case SmokeReferenceCase::SourceOnly:
        caseTag = "source_only"; emitterSteps = 240; break;
    case SmokeReferenceCase::StaticField:
        caseTag = "static"; break;
    case SmokeReferenceCase::Translation:
    {
        // Constant +x translation with periodic wrapping. Speed in cells/step:
        // 1.0 is a whole-cell shift (exact, no interpolation); a fractional value
        // exercises interpolation and diffusion. AQUA_SMOKE_REFERENCE_SPEED overrides.
        caseTag = "translation";
        m_SmokeReferencePeriodic = 1;
        m_SmokeReferenceVelocityMode = 1;
        m_SmokeReferenceSpeed = 0.5f;
        wchar_t speedText[32]{};
        if (GetEnvironmentVariableW(L"AQUA_SMOKE_REFERENCE_SPEED", speedText, 32))
            m_SmokeReferenceSpeed = static_cast<float>(_wtof(speedText));
        break;
    }
    case SmokeReferenceCase::Rotation:
        // Solid-body rotation about z of an off-centre blob; ~one revolution over
        // 300 steps so the blob returns to its start. Kept interior (no wrap).
        caseTag = "rotation";
        m_SmokeReferenceVelocityMode = 2;
        m_SmokeReferenceSpeed = static_cast<float>(6.28318530718 / 300.0);
        m_SmokeReferenceBlobSigma = 0.07f * nx;
        m_SmokeReferenceBlobCentre[1] = n.y * 0.66f;
        break;
    default: break;
    }

    SmokeBenchmarkConfig config;
    config.implementation = "gpu_reference";
    config.scenario = std::string("reference_") + caseTag + "_v1";
    config.runLabel = std::string("ref_") + caseTag + "_" +
        (m_SmokeGpuAdvectionMode == SmokeAdvectionMode::MacCormack ? "maccormack" : "sl");
    if (referenceCase == SmokeReferenceCase::Translation)
    {
        std::ostringstream speedLabel;
        speedLabel << "_s" << m_SmokeReferenceSpeed;
        config.runLabel += speedLabel.str();
    }
    config.totalSteps = totalSteps;
    config.emitterSteps = emitterSteps;
    config.performanceWarmupSteps = m_SmokeReferenceTiming ? 30 : 0;
    config.timeStep = dt;
    config.renderingEnabledDuringRun = false;
    config.advectionMode = m_SmokeGpuAdvectionMode;
    config.limiterMode = m_SmokeGpuLimiterMode;
    config.emitterCell = { n.x / 2, n.y / 4, n.z / 2 };

    // Transport only: buoyancy is a stage we skip, damping is zeroed here so the
    // advection kernels' exp(-dissipation*dt) factor is exactly 1.
    SmokePhysicsParameters physics{};
    physics.ambientTemperature = 0.0;
    physics.temperatureBuoyancy = 0.0;
    physics.smokeWeight = 0.0;
    physics.densityDissipation = 0.0;
    physics.temperatureCooling = 0.0;

    m_SmokeGpuBenchmarkConfig = config;
    m_SmokeGpuBenchmarkPhysics = physics;
    m_SmokeGpuBenchmarkVorticityEpsilon = 0.0f;
    m_SmokeGpuBenchmarkLimiterMode = static_cast<int>(m_SmokeGpuLimiterMode);
    m_SmokeGpuBenchmarkIterations = 0;
    ConfigureProjectionExperiment();
    m_SmokeGpuBenchmarkSamples.clear();
    m_SmokeGpuBenchmarkSamples.reserve(config.totalSteps);
    m_SmokeGpuBenchmarkSubmitted = 0;
    m_SmokeGpuBenchmarkStopping = false;
    m_SmokeGpuBenchmarkRunning = true;
    m_SmokeGpuResetRequested = true;
    m_SmokeGpuStepRequested = false;
    m_SmokeGpuPendingSteps = 0;
    m_SmokeGpuRestoreVolume = m_ShowSmokeVolume;
    m_ShowSmokeVolume = false;

    wchar_t output[32768]{};
    if (automatic && GetEnvironmentVariableW(L"AQUA_SMOKE_REFERENCE_OUTPUT", output, 32768))
        m_SmokeReferenceOutput = output;
    else
        m_SmokeReferenceOutput = std::filesystem::path("diagnostics/runs") /
            (std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) +
             "_" + config.runLabel);

    m_SmokeGpuBenchmarkStatus = "Recording passive-advection reference (" + config.runLabel + ").";
}

void Renderer::DispatchSmokeReferenceStep(ID3D12GraphicsCommandList* commandList, UINT timestampSlot)
{
    if (!m_SmokeGpuResetRequested && !m_SmokeGpuStepRequested) return;
    if (m_ProjectionExperiment && m_SmokeGpuStepRequested) m_SmokeGpuResetRequested = true;

    auto transition = [&](SmokeGpuTexture& texture, D3D12_RESOURCE_STATES state)
    {
        if (texture.state == state) return;
        const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(texture.resource.Get(), texture.state, state);
        commandList->ResourceBarrier(1, &barrier);
        texture.state = state;
    };
    auto srv = [&](SmokeGpuTexture& t) { transition(t, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE); };
    auto uav = [&](SmokeGpuTexture& t) { transition(t, D3D12_RESOURCE_STATE_UNORDERED_ACCESS); };
    auto orderWrites = [&]() { const auto b = CD3DX12_RESOURCE_BARRIER::UAV(nullptr); commandList->ResourceBarrier(1, &b); };
    auto bind = [&](UINT root, D3D12_GPU_DESCRIPTOR_HANDLE handle) { commandList->SetComputeRootDescriptorTable(root, handle); };
    auto velocityInputs = [&](UINT i) { srv(m_GpuU[i]); srv(m_GpuV[i]); srv(m_GpuW[i]); bind(SmokeBindingVelocityInputRoot, m_GpuU[i].srv); };
    auto velocityOutputs = [&](UINT i) { uav(m_GpuU[i]); uav(m_GpuV[i]); uav(m_GpuW[i]); bind(SmokeBindingVelocityRoot, m_GpuU[i].uav); };
    auto scalarInputs = [&](UINT i) { srv(m_GpuDensity[i]); srv(m_GpuTemperature[i]); bind(SmokeBindingInputRoot, m_GpuDensity[i].srv); };
    auto scalarOutputs = [&](UINT i) { uav(m_GpuDensity[i]); uav(m_GpuTemperature[i]); bind(SmokeBindingOutputRoot, m_GpuDensity[i].uav); };

    const auto desc = m_GpuDensity[0].resource->GetDesc();
    SmokeBindingConstants constants{};
    constants.gridResolution[0] = static_cast<std::uint32_t>(desc.Width);
    constants.gridResolution[1] = desc.Height;
    constants.gridResolution[2] = desc.DepthOrArraySize;
    constants.dt = static_cast<float>(m_SmokeGpuBenchmarkConfig.timeStep);
    constants.sourceCell[0] = constants.gridResolution[0] / 2;
    constants.sourceCell[1] = constants.gridResolution[1] / 4;
    constants.sourceCell[2] = constants.gridResolution[2] / 2;
    constants.densityRate = 30.0f;
    constants.temperatureRate = 10.0f;
    const auto spacing = m_SmokeSolver.Density().GridSpacing();
    constants.hx = constants.GridSpacing[0] = static_cast<float>(spacing.x);
    constants.hy = constants.GridSpacing[1] = static_cast<float>(spacing.y);
    constants.hz = constants.GridSpacing[2] = static_cast<float>(spacing.z);
    constants.FluidDensity = static_cast<float>(m_SmokeSolver.FluidDensity());
    constants.JacobiWeight = 2.0f / 3.0f;
    constants.Padding[0] = 0.0f; // density dissipation OFF
    constants.Padding[1] = 0.0f; // temperature cooling OFF
    constants.origin[0] = static_cast<float>(m_SmokeSolver.Density().Origin().x);
    constants.origin[1] = static_cast<float>(m_SmokeSolver.Density().Origin().y);
    constants.origin[2] = static_cast<float>(m_SmokeSolver.Density().Origin().z);
    constants.openTopEnabled = 0;
    constants.limiterMode = m_SmokeGpuBenchmarkLimiterMode;
    constants.vorticityEpsilon = 0.0f;
    constants.periodicDomain = m_SmokeReferencePeriodic;
    constants.referenceVelocityMode = m_SmokeReferenceVelocityMode;
    constants.referenceSpeed = m_SmokeReferenceSpeed;
    constants.referenceBlobSigma = m_SmokeReferenceBlobSigma;
    constants.referenceBlobCentre[0] = m_SmokeReferenceBlobCentre[0];
    constants.referenceBlobCentre[1] = m_SmokeReferenceBlobCentre[1];
    constants.referenceBlobCentre[2] = m_SmokeReferenceBlobCentre[2];

    ID3D12DescriptorHeap* heaps[] = { m_SmokeGpuDescriptorHeap.Get() };
    commandList->SetDescriptorHeaps(1, heaps);
    commandList->SetComputeRootSignature(m_SmokeBindingRootSignature.Get());
    commandList->SetComputeRoot32BitConstants(SmokeBindingConstantsRoot, SmokeConstantCount, &constants, 0);
    const UINT gx = (constants.gridResolution[0] + 7) / 8;
    const UINT gy = (constants.gridResolution[1] + 7) / 8;
    const UINT gz = (constants.gridResolution[2] + 3) / 4;
    const UINT fx = (constants.gridResolution[0] + 8) / 8;
    const UINT fy = (constants.gridResolution[1] + 8) / 8;
    const UINT fz = (constants.gridResolution[2] + 4) / 4;

    if (m_SmokeGpuResetRequested)
    {
        m_GpuScalarReadIndex = m_GpuVelocityReadIndex = 0;
        m_GpuScalarWriteIndex = m_GpuVelocityWriteIndex = 1;
        // ClearSourceFieldsCS also writes the pressure UAVs (u7/u8); bind them.
        uav(m_GpuPressure[0]); uav(m_GpuPressure[1]);
        bind(SmokeBindingPressureReadInputRoot, m_GpuPressure[0].uav);
        commandList->SetPipelineState(m_SmokeClearPSO.Get());
        for (UINT i = 0; i < 2; ++i)
        {
            scalarOutputs(i);
            velocityOutputs(i);
            commandList->Dispatch(fx, fy, fz);
            orderWrites();
        }
        // Static/transport cases initialise a blob; source-only starts from zero.
        if (m_SmokeReferenceCase != SmokeReferenceCase::SourceOnly)
        {
            scalarOutputs(m_GpuScalarReadIndex);
            if (m_ProjectionExperiment) velocityOutputs(m_GpuVelocityReadIndex);
            commandList->SetPipelineState(m_ProjectionExperiment ? m_ProjectionPSOs[0].Get() : m_SmokeReferenceBlobPSO.Get());
            commandList->Dispatch(m_ProjectionExperiment ? fx : gx, m_ProjectionExperiment ? fy : gy, m_ProjectionExperiment ? fz : gz);
            orderWrites();
        }
        // Transport cases set a prescribed, time-invariant velocity once.
        if (m_SmokeReferenceVelocityMode != 0 && !m_ProjectionExperiment)
        {
            velocityOutputs(m_GpuVelocityReadIndex);
            commandList->SetPipelineState(m_SmokeReferenceVelocityPSO.Get());
            commandList->Dispatch(fx, fy, fz);
            orderWrites();
        }
        m_SmokeGpuResetRequested = false;
        m_SmokeGpuInjectionCount = 0;
    }
    if (!m_SmokeGpuStepRequested) return;

    const bool emit = m_SmokeGpuBenchmarkSubmitted < m_SmokeGpuBenchmarkConfig.emitterSteps;
    if (emit)
    {
        scalarOutputs(m_GpuScalarReadIndex);
        commandList->SetPipelineState(m_SmokeInjectPSO.Get());
        commandList->Dispatch(gx, gy, gz);
        orderWrites();
    }

    // Isolated advection timing (no pressure/buoyancy in this path, so this is the
    // pure scheme cost). Resolved into the readback buffer's timestamp region below.
    const UINT queryBase = m_CurrentFrameResourceIndex * SmokeTimestampSlotsPerFrame *
        static_cast<UINT>(SmokeTimestampCount);
    if (m_ProjectionExperiment) CaptureProjectionInitialDensity(commandList);
    commandList->EndQuery(m_SmokeGpuQueries.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
        queryBase + SmokeTimestampStepBegin);
    if (m_ProjectionExperiment) DispatchProjectionExperiment(commandList, constants, queryBase);

    // Prescribed velocity is the cleared zero field; bind it as the advection input.
    velocityInputs(m_GpuVelocityReadIndex);

    // Timing runs use the optimized advection PSOs; correctness runs use production.
    ID3D12PipelineState* slPSO = m_SmokeReferenceTiming ? m_SmokeRefOptAdvectScalarsPSO.Get() : m_SmokeAdvectScalarsPSO.Get();
    if (m_ProjectionExperiment && m_DensitySamplingFloat) slPSO = m_TransportProbePSOs[2].Get();
    ID3D12PipelineState* rawPSO = m_SmokeReferenceTiming ? m_SmokeRefOptAdvectScalarsRawPSO.Get() : m_SmokeAdvectScalarsRawPSO.Get();
    ID3D12PipelineState* mcPSO = m_SmokeReferenceTiming ? m_SmokeRefOptMacCormackScalarsPSO.Get() : m_SmokeMacCormackScalarsPSO.Get();

    const unsigned advectionSteps = m_ProjectionExperiment ? m_ProjectionAdvectionSteps : 1;
    for (unsigned advectionStep = 0; advectionStep < advectionSteps; ++advectionStep)
    {
    if (m_ProjectionExperiment && m_TransportProbeEnabled) DispatchTransportProbe(commandList, advectionStep, false);
    if (m_SmokeGpuAdvectionMode == SmokeAdvectionMode::SemiLagrangian)
    {
        scalarInputs(m_GpuScalarReadIndex);
        scalarOutputs(m_GpuScalarWriteIndex);
        commandList->SetPipelineState(slPSO);
        commandList->Dispatch(gx, gy, gz);
    }
    else
    {
        const UINT s = m_GpuScalarReadIndex;
        // Forward: phi_hat into the Hat pair.
        scalarInputs(m_GpuScalarReadIndex);
        uav(m_GpuDensityHat[s]); uav(m_GpuTemperatureHat[s]);
        bind(SmokeBindingOutputRoot, m_GpuDensityHat[s].uav);
        commandList->SetPipelineState(rawPSO);
        commandList->Dispatch(gx, gy, gz);
        orderWrites();
        // Reverse: phi_bar into the Bar pair, dt negated.
        srv(m_GpuDensityHat[s]); srv(m_GpuTemperatureHat[s]);
        bind(SmokeBindingInputRoot, m_GpuDensityHat[s].srv);
        uav(m_GpuDensityBar[s]); uav(m_GpuTemperatureBar[s]);
        bind(SmokeBindingOutputRoot, m_GpuDensityBar[s].uav);
        constants.dt = -constants.dt;
        commandList->SetComputeRoot32BitConstants(SmokeBindingConstantsRoot, SmokeConstantCount, &constants, 0);
        commandList->SetPipelineState(rawPSO);
        commandList->Dispatch(gx, gy, gz);
        orderWrites();
        constants.dt = -constants.dt;
        commandList->SetComputeRoot32BitConstants(SmokeBindingConstantsRoot, SmokeConstantCount, &constants, 0);
        // Combine + limiter.
        scalarInputs(m_GpuScalarReadIndex);
        srv(m_GpuDensityBar[s]); srv(m_GpuTemperatureBar[s]);
        bind(SmokeBindingHatBarRoot, m_GpuDensityHat[s].srv);
        scalarOutputs(m_GpuScalarWriteIndex);
        commandList->SetPipelineState(mcPSO);
        commandList->Dispatch(gx, gy, gz);
    }
    std::swap(m_GpuScalarReadIndex, m_GpuScalarWriteIndex);
    if (m_ProjectionExperiment && m_TransportProbeEnabled) DispatchTransportProbe(commandList, advectionStep, true);
    if (advectionStep + 1 < advectionSteps) orderWrites();
    }
    m_SmokeGpuStepRequested = false;
    ++m_SmokeGpuInjectionCount;
    commandList->EndQuery(m_SmokeGpuQueries.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
        queryBase + SmokeTimestampStepEnd);
    if (m_ProjectionExperiment) CaptureProjectionExperiment(commandList, constants);

    // Density readback, exactly as the benchmark path, so CollectSmokeGpuDiagnostics
    // maps the field and RecordReferenceStep can score it. Timestamps resolve into
    // the [0, 512) region ahead of the density copy at footprint.Offset (512).
    auto& readback = m_SmokeGpuReadbacks[m_CurrentFrameResourceIndex];
    readback.pending = true;
    readback.benchmark = true;
    readback.timestampOffset = 0;
    // Resolve only queries written by this path (resolving unwritten queries is invalid).
    for (UINT q : { UINT(SmokeTimestampStepBegin), UINT(SmokeTimestampStepEnd) })
        commandList->ResolveQueryData(m_SmokeGpuQueries.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
            queryBase + q, 1, readback.buffer.Get(), q * sizeof(UINT64));
    if (m_ProjectionExperiment)
        commandList->ResolveQueryData(m_SmokeGpuQueries.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
            queryBase + SmokeTimestampPressureGradientEnd, 1, readback.buffer.Get(), SmokeTimestampPressureGradientEnd * sizeof(UINT64));
    readback.sample = {};
    readback.sample.emit = emit;
    readback.sample.step = ++m_SmokeGpuBenchmarkSubmitted;
    auto& density = m_GpuDensity[m_GpuScalarReadIndex];
    transition(density, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION source{}, destination{};
    source.pResource = density.resource.Get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destination.pResource = readback.buffer.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = m_SmokeGpuReadbackFootprint;
    commandList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
}

void Renderer::RecordReferenceStep(const void* mapped, unsigned step)
{
    if (m_ProjectionExperiment) { RecordProjectionExperiment(mapped, step); return; }
    const auto n = m_SmokeSolver.Density().Resolution();
    const auto& fp = m_SmokeGpuReadbackFootprint;
    const bool sourceOnly = m_SmokeReferenceCase == SmokeReferenceCase::SourceOnly;
    // Source-only reference: accumulated injection at the source cell, zero elsewhere.
    const unsigned emitted = std::min<unsigned>(step, static_cast<unsigned>(m_SmokeGpuBenchmarkConfig.emitterSteps));
    const double sourceValue = 30.0 * m_SmokeGpuBenchmarkConfig.timeStep * emitted;
    const std::size_t scx = n.x / 2, scy = n.y / 4, scz = n.z / 2;

    // Analytic blob centre for this step (cells). Translation shifts +x (periodic);
    // rotation orbits the off-centre blob about the domain z axis. A radially
    // symmetric Gaussian stays Gaussian under both, so the reference is exact.
    const double sigma = m_SmokeReferenceBlobSigma;
    const bool periodic = m_SmokeReferencePeriodic != 0;
    double cx = m_SmokeReferenceBlobCentre[0];
    double cy = m_SmokeReferenceBlobCentre[1];
    const double cz = m_SmokeReferenceBlobCentre[2];
    if (m_SmokeReferenceCase == SmokeReferenceCase::Translation)
        cx += static_cast<double>(step) * m_SmokeReferenceSpeed;
    else if (m_SmokeReferenceCase == SmokeReferenceCase::Rotation)
    {
        const double angle = static_cast<double>(step) * m_SmokeReferenceSpeed;
        const double dcx = n.x * 0.5, dcy = n.y * 0.5;
        const double ox = m_SmokeReferenceBlobCentre[0] - dcx;
        const double oy = m_SmokeReferenceBlobCentre[1] - dcy;
        cx = dcx + std::cos(angle) * ox - std::sin(angle) * oy;
        cy = dcy + std::sin(angle) * ox + std::cos(angle) * oy;
    }

    double l1 = 0.0, l2sq = 0.0, refL1 = 0.0, refL2sq = 0.0;
    double sum = 0.0, refSum = 0.0, dmin = 1e300, dmax = -1e300;
    unsigned nonfinite = 0;
    for (std::size_t k = 0; k < n.z; ++k)
        for (std::size_t j = 0; j < n.y; ++j)
        {
            const auto* row = reinterpret_cast<const float*>(
                static_cast<const std::byte*>(mapped) + fp.Offset +
                (k * fp.Footprint.Height + j) * fp.Footprint.RowPitch);
            for (std::size_t i = 0; i < n.x; ++i)
            {
                const double d = row[i];
                double ref;
                if (sourceOnly)
                    ref = (i == scx && j == scy && k == scz) ? sourceValue : 0.0;
                else
                {
                    double dx = (i + 0.5) - cx;
                    if (periodic) dx -= n.x * std::round(dx / n.x); // nearest periodic image
                    const double dy = (j + 0.5) - cy;
                    const double dz = (k + 0.5) - cz;
                    ref = std::exp(-(dx * dx + dy * dy + dz * dz) / (2.0 * sigma * sigma));
                }
                // Reference integrals must not depend on which simulated cells
                // happen to be finite; nonfinite output always fails validation.
                refL1 += std::abs(ref);
                refL2sq += ref * ref;
                refSum += ref;
                if (!std::isfinite(d)) { ++nonfinite; continue; }
                const double e = d - ref;
                l1 += std::abs(e);
                l2sq += e * e;
                sum += d;
                dmin = std::min(dmin, d);
                dmax = std::max(dmax, d);
            }
        }
    const double l1n = refL1 > 0 ? l1 / refL1 : l1;
    const double l2n = refL2sq > 0 ? std::sqrt(l2sq / refL2sq) : std::sqrt(l2sq);
    const double massErr = refSum != 0 ? (sum - refSum) / refSum : sum - refSum;

    // Isolated advection time for this step (timestamps resolved at buffer offset 0,
    // ahead of the density copy at footprint.Offset). Advection-only path, so this
    // is the pure scheme cost with no pressure/buoyancy confound.
    const auto* timestamps = reinterpret_cast<const std::uint64_t*>(mapped);
    const double advectionMs = m_SmokeGpuTimestampFrequency > 0
        ? static_cast<double>(timestamps[SmokeTimestampStepEnd] - timestamps[SmokeTimestampStepBegin])
            * 1000.0 / static_cast<double>(m_SmokeGpuTimestampFrequency)
        : 0.0;

    std::ostringstream r;
    r << std::setprecision(17)
      << step << ',' << sum << ',' << refSum << ',' << (sum - refSum) << ',' << massErr << ','
      << l1 << ',' << l1n << ',' << std::sqrt(l2sq) << ',' << l2n << ','
      << dmin << ',' << dmax << ',' << nonfinite << ',' << advectionMs;
    m_SmokeReferenceRows.push_back(r.str());
}

void Renderer::SaveSmokeAdvectionReference()
{
    if (m_ProjectionExperiment) { SaveProjectionExperiment(); return; }
    try
    {
        std::filesystem::create_directories(m_SmokeReferenceOutput);
        std::ofstream csv(m_SmokeReferenceOutput / "steps.csv");
        csv.exceptions(std::ios::failbit | std::ios::badbit);
        csv << "step,density_sum,reference_sum,mass_error_abs,mass_error_rel,"
               "l1,l1_normalized,l2,l2_normalized,density_min,density_max,nonfinite,advection_ms\n";
        for (const auto& row : m_SmokeReferenceRows) csv << row << '\n';
        csv.close();

        const auto n = m_SmokeSolver.Density().Resolution();
        std::ofstream manifest(m_SmokeReferenceOutput / "manifest.json");
        manifest.exceptions(std::ios::failbit | std::ios::badbit);
        manifest << std::setprecision(17)
            << "{\n  \"schema_version\": 1,\n  \"purpose\": \"passive_advection_reference_correctness\","
            << "\n  \"scenario\": " << std::quoted(m_SmokeGpuBenchmarkConfig.scenario)
            << ",\n  \"run_label\": " << std::quoted(m_SmokeGpuBenchmarkConfig.runLabel)
            << ",\n  \"case\": " << static_cast<int>(m_SmokeReferenceCase)
            << ",\n  \"advection\": " << std::quoted(m_SmokeGpuAdvectionMode == SmokeAdvectionMode::MacCormack ? "maccormack" : "sl")
            << ",\n  \"limiter_mode\": " << m_SmokeGpuBenchmarkLimiterMode
            << ",\n  \"optimized_shaders\": " << (m_SmokeReferenceTiming ? "true" : "false")
            << ",\n  \"shader_build\": " << std::quoted(m_SmokeReferenceTiming ? "OPTIMIZATION_LEVEL3 (timing only)" : "STRICTNESS|DEBUG|SKIP_OPTIMIZATION (production, correctness)")
            << ",\n  \"warmup_steps\": " << m_SmokeGpuBenchmarkConfig.performanceWarmupSteps
            << ",\n  \"reference_definition\": \"cell_centred_samples\""
            << ",\n  \"velocity_mode\": " << m_SmokeReferenceVelocityMode
            << " ,\"velocity_mode_name\": " << std::quoted(m_SmokeReferenceVelocityMode == 1 ? "translation_x" : m_SmokeReferenceVelocityMode == 2 ? "rotation_z" : "zero")
            << ",\n  \"reference_speed\": " << m_SmokeReferenceSpeed
            << ",\n  \"periodic\": " << (m_SmokeReferencePeriodic ? "true" : "false")
            << ",\n  \"buoyancy\": false,\n  \"projection\": false,\n  \"confinement\": false,\n  \"damping\": false"
            << ",\n  \"resolution\": [" << n.x << ',' << n.y << ',' << n.z << ']'
            << ",\n  \"dt\": " << m_SmokeGpuBenchmarkConfig.timeStep
            << ",\n  \"total_steps\": " << m_SmokeGpuBenchmarkConfig.totalSteps
            << ",\n  \"emitter_steps\": " << m_SmokeGpuBenchmarkConfig.emitterSteps
            << ",\n  \"recorded_steps\": " << m_SmokeReferenceRows.size()
            << ",\n  \"blob_sigma_cells\": " << m_SmokeReferenceBlobSigma
            << ",\n  \"blob_centre_cells\": [" << m_SmokeReferenceBlobCentre[0] << ',' << m_SmokeReferenceBlobCentre[1] << ',' << m_SmokeReferenceBlobCentre[2] << ']'
#ifdef _DEBUG
            << ",\n  \"build\": \"Debug\""
#else
            << ",\n  \"build\": \"Release\""
#endif
            << "\n}\n";
        manifest.close();
        m_SmokeGpuBenchmarkStatus = "Saved reference run: " + std::filesystem::absolute(m_SmokeReferenceOutput).string();
    }
    catch (const std::exception& error)
    {
        m_SmokeGpuBenchmarkStatus = std::string("Reference export failed: ") + error.what();
    }
}
