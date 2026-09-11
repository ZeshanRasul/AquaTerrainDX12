#include "Renderer.h"
#include "imgui/imgui.h"
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <chrono>

void Renderer::CreateSmokeGpuDiagnostics()
{
    D3D12_QUERY_HEAP_DESC desc = {};
    desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    desc.Count = static_cast<UINT>(SmokeTimestampCount) *
        SmokeTimestampSlotsPerFrame * NumFrameResources;
    ThrowIfFailed(m_Device->CreateQueryHeap(&desc, IID_PPV_ARGS(&m_SmokeGpuQueries)));
    ThrowIfFailed(m_CommandQueue->GetTimestampFrequency(&m_SmokeGpuTimestampFrequency));
    const auto texture = m_GpuDensity[0].resource->GetDesc();
    UINT64 bytes = 0;
    // Timestamp results occupy the beginning of the buffer. The copied density
    // begins at the required 512-byte texture-footprint alignment.
    m_Device->GetCopyableFootprints(&texture, 0, 1, 512,
        &m_SmokeGpuReadbackFootprint, nullptr, nullptr, &bytes);
    // `bytes` is the density subresource size and does NOT include the 512-byte
    // base offset, so the density copy actually ends at footprint.Offset + bytes.
    // Place the diagnostics region after that end (not after `bytes`), otherwise
    // the buffer is too small for the copy and the diagnostics overlap density.
    const UINT64 densityEnd = m_SmokeGpuReadbackFootprint.Offset + bytes;
    m_SmokeGpuDiagnosticsReadbackOffset = (densityEnd + 255u) & ~UINT64{ 255u };
    constexpr UINT64 diagnosticBytes = 4 * sizeof(XMFLOAT4);
    const auto buffer = CD3DX12_RESOURCE_DESC::Buffer(
        m_SmokeGpuDiagnosticsReadbackOffset + diagnosticBytes);
    const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
    for (auto& slot : m_SmokeGpuReadbacks)
        ThrowIfFailed(m_Device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
            &buffer, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&slot.buffer)));
}

void Renderer::CollectSmokeGpuDiagnostics()
{
    // Update has already waited for this frame resource's fence.
    auto& slot = m_SmokeGpuReadbacks[m_CurrentFrameResourceIndex];
    if (slot.pending)
    {
        void* mapped = nullptr;
        const D3D12_RANGE reads = { 0, static_cast<SIZE_T>(slot.buffer->GetDesc().Width) };
        ThrowIfFailed(slot.buffer->Map(0, &reads, &mapped));
        auto sample = slot.sample;
        // Reference dispatches copy density only: timestamps and numerical
        // diagnostic records were not written and must not be read.
        if (m_SmokeReferenceCase == SmokeReferenceCase::None)
        {
        const auto* timestamps = reinterpret_cast<const UINT64*>(
            static_cast<const std::byte*>(mapped) + slot.timestampOffset);

        const double tickToMilliseconds =
            1000.0 /
            static_cast<double>(m_SmokeGpuTimestampFrequency);

        const auto elapsedMilliseconds = [&](SmokeTimestamp begin, SmokeTimestamp end)
        {
            return static_cast<double>(timestamps[end] - timestamps[begin]) *
                tickToMilliseconds;
        };

        sample.sourceMilliseconds = elapsedMilliseconds(
            SmokeTimestampStepBegin, SmokeTimestampSourceEnd);
        sample.velocityAdvectionMilliseconds = elapsedMilliseconds(
            SmokeTimestampSourceEnd, SmokeTimestampVelocityAdvectionEnd);
        sample.buoyancyMilliseconds = elapsedMilliseconds(
            SmokeTimestampVelocityAdvectionEnd, SmokeTimestampBuoyancyEnd);
        sample.divergenceMilliseconds = elapsedMilliseconds(
            SmokeTimestampBuoyancyEnd, SmokeTimestampDivergenceEnd);
        sample.pressureClearMilliseconds = elapsedMilliseconds(
            SmokeTimestampDivergenceEnd, SmokeTimestampPressureClearEnd);
        sample.pressureMilliseconds = elapsedMilliseconds(
            SmokeTimestampPressureClearEnd, SmokeTimestampPressureSolveEnd);
        sample.pressureGradientMilliseconds = elapsedMilliseconds(
            SmokeTimestampPressureSolveEnd, SmokeTimestampPressureGradientEnd);
        sample.scalarAdvectionMilliseconds = elapsedMilliseconds(
            SmokeTimestampPressureGradientEnd, SmokeTimestampStepEnd);
        sample.milliseconds = elapsedMilliseconds(
            SmokeTimestampStepBegin, SmokeTimestampStepEnd);
        sample.diagnosticsMilliseconds = elapsedMilliseconds(
            SmokeTimestampStepEnd, SmokeTimestampDiagnosticsEnd);
        m_SmokeGpuLastMilliseconds = sample.milliseconds;
        m_SmokeGpuLastPressureMilliseconds = sample.pressureMilliseconds;
        m_SmokeGpuLastPressureIterations = sample.pressureIterations;
        m_SmokeGpuLastStageMilliseconds = {
            sample.sourceMilliseconds,
            sample.velocityAdvectionMilliseconds,
            sample.buoyancyMilliseconds,
            sample.divergenceMilliseconds,
            sample.pressureClearMilliseconds,
            sample.pressureMilliseconds,
            sample.pressureGradientMilliseconds,
            sample.scalarAdvectionMilliseconds,
            sample.diagnosticsMilliseconds
        };

        m_SmokeGpuAverageIterationMicroSeconds =
            sample.pressureMilliseconds * 1000.0 /
            static_cast<double>(std::max(sample.pressureIterations, 1u));

        m_SmokeGpuPressureFraction =
            sample.milliseconds > 0.0
            ? sample.pressureMilliseconds / sample.milliseconds
            : 0.0;

        const auto* diagnostics = reinterpret_cast<const XMFLOAT4*>(
            static_cast<const std::byte*>(mapped) +
            m_SmokeGpuDiagnosticsReadbackOffset);
        const auto divergenceMetric = [](const XMFLOAT4& record)
        {
            const double count = std::max(static_cast<double>(record.z), 1.0);
            return std::sqrt(std::max(static_cast<double>(record.x), 0.0) / count);
        };
        sample.divergenceBeforeRms = divergenceMetric(diagnostics[0]);
        sample.divergenceBeforeMax = diagnostics[0].y;
        sample.divergenceAfterRms = divergenceMetric(diagnostics[1]);
        sample.divergenceAfterMax = diagnostics[1].y;
        const double velocityCount =
            std::max(static_cast<double>(diagnostics[2].z), 1.0);
        const double speedSquaredSum =
            std::max(static_cast<double>(diagnostics[2].x), 0.0);
        const auto gridSpacing = m_SmokeSolver.Density().GridSpacing();
        const double cellVolume =
            gridSpacing.x * gridSpacing.y * gridSpacing.z;
        sample.kineticEnergy = 0.5 * m_SmokeSolver.FluidDensity() *
            speedSquaredSum * cellVolume;
        sample.rmsSpeed = std::sqrt(speedSquaredSum / velocityCount);
        sample.maxSpeed = diagnostics[2].y;
        // Record 3: enstrophy = integral of |omega|^2 over fluid cells.
        sample.enstrophy =
            std::max(static_cast<double>(diagnostics[3].x), 0.0) * cellVolume;
        sample.maxVorticity = diagnostics[3].y;
        sample.divergenceBeforeNonfinite =
            static_cast<unsigned>(std::max(diagnostics[0].w, 0.0f));
        sample.divergenceAfterNonfinite =
            static_cast<unsigned>(std::max(diagnostics[1].w, 0.0f));
        sample.velocityNonfinite =
            static_cast<unsigned>(std::max(diagnostics[2].w, 0.0f));
        sample.diagnosticNonfinite =
            sample.divergenceBeforeNonfinite +
            sample.divergenceAfterNonfinite +
            sample.velocityNonfinite;

        m_SmokeGpuLastDivergenceBeforeRms = sample.divergenceBeforeRms;
        m_SmokeGpuLastDivergenceBeforeMax = sample.divergenceBeforeMax;
        m_SmokeGpuLastDivergenceAfterRms = sample.divergenceAfterRms;
        m_SmokeGpuLastDivergenceAfterMax = sample.divergenceAfterMax;
        m_SmokeGpuLastKineticEnergy = sample.kineticEnergy;
        m_SmokeGpuLastRmsSpeed = sample.rmsSpeed;
        m_SmokeGpuLastMaxSpeed = sample.maxSpeed;
        m_SmokeGpuLastDiagnosticNonfinite = sample.diagnosticNonfinite;
        }

        if (slot.benchmark)
        {
            const auto n = m_SmokeSolver.Density().Resolution();
            const auto h = m_SmokeSolver.Density().GridSpacing();
            const auto origin = m_SmokeSolver.Density().Origin();
            sample.densityMin = std::numeric_limits<double>::infinity();
            sample.densityMax = -std::numeric_limits<double>::infinity();
            const auto& fp = m_SmokeGpuReadbackFootprint;
            for (std::size_t k = 0; k < n.z; ++k)
                for (std::size_t j = 0; j < n.y; ++j)
                {
                    const auto* row = reinterpret_cast<const float*>(
                        static_cast<const std::byte*>(mapped) + fp.Offset +
                        (k * fp.Footprint.Height + j) * fp.Footprint.RowPitch);
                    for (std::size_t i = 0; i < n.x; ++i)
                    {
                        const double d = row[i];
                        if (m_SmokeMassAudit)
                        {
                            const auto* bytes = reinterpret_cast<const unsigned char*>(row + i);
                            for (unsigned b = 0; b < sizeof(float); ++b)
                                sample.densityHash = (sample.densityHash ^ bytes[b]) * 1099511628211ull;
                        }
                        if (!std::isfinite(d)) { ++sample.nonfinite; continue; }
                        sample.densityMin = std::min(sample.densityMin, d);
                        sample.densityMax = std::max(sample.densityMax, d);
                        sample.densitySum += d;
                        sample.centre.x += d * (origin.x + (i + 0.5) * h.x);
                        sample.centre.y += d * (origin.y + (j + 0.5) * h.y);
                        sample.centre.z += d * (origin.z + (k + 0.5) * h.z);
                    }
                }
            if (sample.densitySum > 0)
            {
                sample.centre.x /= sample.densitySum;
                sample.centre.y /= sample.densitySum;
                sample.centre.z /= sample.densitySum;
            }
            m_SmokeGpuBenchmarkSamples.push_back(sample);
            if (m_SmokeMassAudit && m_SmokeAuditProbes)
                CollectSmokeMassAudit(m_CurrentFrameResourceIndex, sample.step, sample.emit, sample.densitySum, sample.densityHash);
            if (m_SmokeReferenceCase != SmokeReferenceCase::None)
                RecordReferenceStep(mapped, sample.step);
        }
        const D3D12_RANGE writes = { 0, 0 };
        slot.buffer->Unmap(0, &writes);
        slot.pending = false;
    }
    if (m_SmokeGpuBenchmarkRunning &&
        (m_SmokeGpuBenchmarkStopping ||
         m_SmokeGpuBenchmarkSubmitted >= m_SmokeGpuBenchmarkConfig.totalSteps) &&
        m_SmokeGpuBenchmarkSamples.size() == m_SmokeGpuBenchmarkSubmitted)
    {
        const bool reference = m_SmokeReferenceCase != SmokeReferenceCase::None;
        if (reference) SaveSmokeAdvectionReference();
        else if (m_SmokeMassAudit) SaveSmokeMassAudit();
        else SaveSmokeGpuBenchmark();
        m_SmokeGpuBenchmarkRunning = false;
        m_SmokeGpuPaused = true;
        m_SmokeGpuPendingSteps = 0;
        m_ShowSmokeVolume = m_SmokeGpuRestoreVolume;
        if (m_SmokeMassAuditAutomatic) PostQuitMessage(m_SmokeAuditFailures ? 2 : 0);
        if (reference && m_SmokeReferenceAutomatic) PostQuitMessage(0);
        m_SmokeMassAudit = false;
        m_SmokeReferenceCase = SmokeReferenceCase::None;
    }
}

void Renderer::DrawSmokeGpuDebug()
{
    ImGui::Begin("Smoke 3D GPU controls");
    if (m_SmokeMassAudit)
        ImGui::TextWrapped("Correctness audit active: displayed timings include probes and are not performance results.");
    ImGui::Text("GPU solver: %.3f ms / step (delayed timestamp)", m_SmokeGpuLastMilliseconds);
    ImGui::Text("Simulation steps: %u | simulated time: %.2f s",
        m_SmokeGpuInjectionCount, m_SmokeGpuInjectionCount / 60.0);

    ImGui::Text(
        "GPU pressure solve: %.3f ms (%u Jacobi iterations)",
        m_SmokeGpuLastPressureMilliseconds,
        m_SmokeGpuLastPressureIterations);

    ImGui::Text(
		"GPU average iteration: %.3f µs",
        m_SmokeGpuAverageIterationMicroSeconds);
    ImGui::Text("Pressure share: %.1f%%", 100.0 * m_SmokeGpuPressureFraction);
    ImGui::SeparatorText("Latest GPU stage timings");
    ImGui::Text("Source injection       %8.3f ms", m_SmokeGpuLastStageMilliseconds[0]);
    ImGui::Text("Velocity advection     %8.3f ms", m_SmokeGpuLastStageMilliseconds[1]);
    ImGui::Text("Buoyancy               %8.3f ms", m_SmokeGpuLastStageMilliseconds[2]);
    ImGui::Text("Divergence             %8.3f ms", m_SmokeGpuLastStageMilliseconds[3]);
    ImGui::Text("Pressure clear/setup   %8.3f ms", m_SmokeGpuLastStageMilliseconds[4]);
    ImGui::Text("Jacobi pressure solve  %8.3f ms", m_SmokeGpuLastStageMilliseconds[5]);
    ImGui::Text("Gradient subtraction   %8.3f ms", m_SmokeGpuLastStageMilliseconds[6]);
    ImGui::Text("Scalar advection       %8.3f ms", m_SmokeGpuLastStageMilliseconds[7]);

    ImGui::SeparatorText("Numerical diagnostics");
    ImGui::Text("RMS divergence: %.3e -> %.3e",
        m_SmokeGpuLastDivergenceBeforeRms,
        m_SmokeGpuLastDivergenceAfterRms);
    ImGui::Text("Max |divergence|: %.3e -> %.3e",
        m_SmokeGpuLastDivergenceBeforeMax,
        m_SmokeGpuLastDivergenceAfterMax);
    const double remainingDivergence =
        m_SmokeGpuLastDivergenceBeforeRms > 1.0e-20
        ? 100.0 * m_SmokeGpuLastDivergenceAfterRms /
            m_SmokeGpuLastDivergenceBeforeRms
        : 0.0;
    ImGui::Text("Remaining RMS divergence: %.2f%%", remainingDivergence);
    ImGui::Text("Kinetic energy: %.6e", m_SmokeGpuLastKineticEnergy);
    ImGui::Text("Speed RMS / max: %.4f / %.4f",
        m_SmokeGpuLastRmsSpeed,
        m_SmokeGpuLastMaxSpeed);
    ImGui::Text("Nonfinite diagnostic samples: %u",
        m_SmokeGpuLastDiagnosticNonfinite);
    ImGui::Text("GPU reduction cost: %.3f ms",
        m_SmokeGpuLastStageMilliseconds[8]);
    ImGui::TextDisabled("Numerical reductions and readback are outside solver timestamps.");

    ImGui::BeginDisabled(m_SmokeGpuBenchmarkRunning);
    if (ImGui::Button("Start mass-budget audit (480 steps)")) StartSmokeMassAudit();
    if (ImGui::Button("Reference: source-only (case 1)")) StartSmokeAdvectionReference(SmokeReferenceCase::SourceOnly);
    ImGui::SameLine();
    if (ImGui::Button("Reference: static field (case 2)")) StartSmokeAdvectionReference(SmokeReferenceCase::StaticField);
    if (ImGui::Button("Reference: translation (case 3)")) StartSmokeAdvectionReference(SmokeReferenceCase::Translation);
    ImGui::SameLine();
    if (ImGui::Button("Reference: rotation (case 4)")) StartSmokeAdvectionReference(SmokeReferenceCase::Rotation);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Passive-advection reference (zero velocity). Uses the current\n"
            "Advection Mode / Limiter. Source-only checks injection; static checks\n"
            "identity transport. Both should give ~0 error for a correct scheme.");
    ImGui::TextDisabled("Audit captures intermediate fields; its timings are not performance measurements.");
    if (ImGui::Checkbox("Paused", &m_SmokeGpuPaused))
    {
        m_SmokeGpuPendingSteps = 0;
        m_SmokeGpuAccumulator = 0;
    }
	ImGui::Combo("Advection Mode", reinterpret_cast<int*>(&m_SmokeGpuAdvectionMode), "Semi-Lagrangian\0MacCormack\0\0");
	ImGui::Combo("MacCormack Limiter", reinterpret_cast<int*>(&m_SmokeGpuLimiterMode), "Clamp\0Revert\0Adaptive\0\0");
    ImGui::Checkbox("Emitter enabled", &m_SmokeGpuEmitterEnabled);
    ImGui::Checkbox("Sphere obstacle enabled", &m_SmokeGpuSphereEnabled);
    ImGui::BeginDisabled(!m_SmokeGpuSphereEnabled);
	ImGui::Checkbox("Sphere obstacle moving", &m_SphereTranslationEnabled);
    ImGui::SliderFloat("Sphere radius", &m_SmokeGpuSphereRadius,
        0.02f, 0.30f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Radius in simulation units. Changes apply on the next simulation step.\n"
            "Use Reset for a clean comparison; resizing does not model a moving solid.");
    ImGui::EndDisabled();
    const auto worldRadii = SmokeObstacleWorldRadii();
    const auto cellSpacing = m_SmokeSolver.Density().GridSpacing();
    ImGui::Text("Radius in cells: %.2f, %.2f, %.2f",
        m_SmokeGpuSphereRadius / cellSpacing.x,
        m_SmokeGpuSphereRadius / cellSpacing.y,
        m_SmokeGpuSphereRadius / cellSpacing.z);
    ImGui::Text("World diameters: %.2f, %.2f, %.2f",
        2.0f * worldRadii.x, 2.0f * worldRadii.y, 2.0f * worldRadii.z);

    ImGui::SliderFloat("Vorticity Epsilon", &m_SmokeGpuVorticityEpsilon,
        0.0f, 20.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);

	ImGui::Checkbox("Open Top Enabled", &m_SmokeGpuOpenTopEnabled);
    if (ImGui::Button("Single step"))
    {
        m_SmokeGpuPaused = true;
        m_SmokeGpuPendingSteps = 0;
        m_SmokeGpuStepRequested = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset"))
    {
        m_SmokeGpuResetRequested = true;
        m_SmokeGpuPendingSteps = 0;
        m_SmokeGpuStepRequested = false;
        m_SmokeGpuAccumulator = 0;
    }
    ImGui::SliderInt("Jacobi iterations", &m_SmokeGpuPressureIterations, 1, 400);
    ImGui::TextWrapped("Fixed 60 Hz with up to two catch-up steps per frame. Excess time is dropped under load.");
    ImGui::SeparatorText("Deterministic comparison");
    ImGui::InputText("Run label", m_SmokeBenchmarkRunLabel, IM_ARRAYSIZE(m_SmokeBenchmarkRunLabel));
    ImGui::InputInt("Total fixed steps", &m_SmokeBenchmarkTotalSteps);
    ImGui::InputInt("Emitter steps", &m_SmokeBenchmarkEmitterSteps);
    ImGui::InputInt("Timing warmup steps", &m_SmokeBenchmarkWarmupSteps);
    ImGui::Checkbox("Simulation-only run", &m_SmokeBenchmarkSimulationOnly);
    if (ImGui::Button("Start GPU benchmark"))
    {
        SmokeBenchmarkConfig config;
        config.implementation = "gpu_jacobi";
        config.scenario = m_SmokeGpuOpenTopEnabled
            ? "buoyant_plume_open_top_v1"
            : "buoyant_plume_closed_box_v1";
        if (m_SmokeGpuSphereEnabled)
            config.scenario = m_SmokeGpuOpenTopEnabled
                ? "buoyant_plume_open_top_sphere_v1"
                : "buoyant_plume_closed_box_sphere_v1";
        config.runLabel = m_SmokeBenchmarkRunLabel;
        config.totalSteps = std::max(1, m_SmokeBenchmarkTotalSteps);
        config.emitterSteps = std::clamp(m_SmokeBenchmarkEmitterSteps, 0, static_cast<int>(config.totalSteps));
        config.performanceWarmupSteps = std::clamp(m_SmokeBenchmarkWarmupSteps, 0, static_cast<int>(config.totalSteps) - 1);
        const auto n = m_SmokeSolver.Density().Resolution();
        config.emitterCell = { n.x / 2, n.y / 4, n.z / 2 };
        config.renderingEnabledDuringRun = !m_SmokeBenchmarkSimulationOnly;
        m_SmokeGpuBenchmarkConfig = config;
        m_SmokeGpuBenchmarkPhysics = m_SmokeSolver.PhysicsParameters();
        m_SmokeGpuBenchmarkIterations = m_SmokeGpuPressureIterations;
        m_SmokeGpuBenchmarkSamples.clear();
        m_SmokeGpuBenchmarkSamples.reserve(config.totalSteps);
        m_SmokeGpuBenchmarkSubmitted = 0;
        m_SmokeGpuBenchmarkStopping = false;
        m_SmokeGpuBenchmarkRunning = true;
        m_SmokeGpuResetRequested = true;
        m_SmokeGpuStepRequested = false;
        m_SmokeGpuPendingSteps = 0;
        m_SmokeGpuRestoreVolume = m_ShowSmokeVolume;
        m_ShowSmokeVolume = config.renderingEnabledDuringRun;
        m_SmokeGpuBenchmarkStatus = "Recording GPU timestamps and density; waiting for completed frames.";
    }

    // Matched advection A/B: pins every parameter to a canonical value so the
    // Semi-Lagrangian and MacCormack runs differ ONLY in advection mode. Set the
    // Advection Mode combo, click once, wait for the saved path; repeat for the
    // other mode. The two runs are then directly comparable with
    // diagnostics/Compare-SmokeRuns.ps1.
    if (ImGui::Button("Start matched A/B benchmark"))
    {
        // Canonical scenario: open-topped, vented buoyant plume with mild dissipation,
        // no obstacle. This matches a realistic rising plume and, crucially, is well
        // posed: mass can leave through the top, so no scheme accumulates pathologically.
        // The headline metric is peak-density preservation (effective numerical
        // diffusion): MacCormack holds sharp peaks that semi-Lagrangian smears away.
        // (A sealed, zero-dissipation box was tried first and is ill posed - with no
        // outlet the source accumulates without bound, and the non-conservative clamp
        // limiter then fills the domain; that measures non-conservation, not quality.)
        m_SmokeGpuOpenTopEnabled = true;
        m_SmokeGpuSphereEnabled = false;
        m_SphereTranslationEnabled = false;

        SmokeBenchmarkConfig config;
        config.implementation = "gpu_jacobi";
        config.scenario = "abtest_buoyant_plume_open_top_v1";
        // The run label encodes BOTH varied dimensions so any A/B pair is
        // self-identifying: hold one fixed and vary the other between runs
        // (advection mode via the combo, or vorticity strength via the slider).
        {
            const std::string modeTag =
                (m_SmokeGpuAdvectionMode == SmokeAdvectionMode::MacCormack)
                    ? "maccormack" : "semilagrangian";
            const int epsilonTag =
                static_cast<int>(m_SmokeGpuVorticityEpsilon + 0.5f);
            config.runLabel =
                "ab_" + modeTag + "_vort" + std::to_string(epsilonTag);
        }
        config.vorticityEpsilon = m_SmokeGpuVorticityEpsilon;
        config.totalSteps = 480;
        config.emitterSteps = 240;
        config.performanceWarmupSteps = 30;
        config.timeStep = 1.0 / 60.0;
        const auto n = m_SmokeSolver.Density().Resolution();
        config.emitterCell = { n.x / 2, n.y / 4, n.z / 2 };
        config.advectionMode = m_SmokeGpuAdvectionMode;
        config.renderingEnabledDuringRun = false;

        // Moderate buoyancy drives a steady plume up through the open top; mild,
        // equal dissipation keeps both schemes well behaved. Because the dissipation
        // and cooling are identical across the pair, any remaining difference in peak
        // density and plume sharpness is due to the advection scheme's numerical
        // diffusion alone.
        SmokePhysicsParameters physics;
        physics.ambientTemperature = 0.0;
        physics.temperatureBuoyancy = 0.6;
        physics.smokeWeight = 0.05;
        physics.densityDissipation = 0.1;
        physics.temperatureCooling = 0.5;

        config.limiterMode = m_SmokeGpuLimiterMode;
        m_SmokeGpuBenchmarkConfig = config;
        m_SmokeGpuBenchmarkPhysics = physics;
        m_SmokeGpuBenchmarkVorticityEpsilon = config.vorticityEpsilon;
        m_SmokeGpuBenchmarkLimiterMode = static_cast<int>(m_SmokeGpuLimiterMode);
        m_SmokeGpuBenchmarkIterations = 40;
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
        m_SmokeGpuBenchmarkStatus =
            std::string("Matched A/B run (") + config.runLabel +
            "): recording. Repeat with the other setting (advection mode or vorticity epsilon).";
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Pins closed box, no sphere, 480 steps (240 emitting), 40 Jacobi\n"
            "iterations, zero density dissipation. Only the advection mode varies.\n"
            "Run once per mode, then compare the two output directories.");

    // Limiter gate: the adversarial sealed-box scenario (closed top, zero
    // dissipation) where clamp fills the domain and revert over-diffuses.
    // Forces MacCormack + no confinement so ONLY the limiter varies. Run once
    // per limiter mode (Clamp / Revert / Adaptive) and compare.
    if (ImGui::Button("Start limiter-stress benchmark"))
    {
        m_SmokeGpuOpenTopEnabled = false;
        m_SmokeGpuSphereEnabled = false;
        m_SphereTranslationEnabled = false;
        m_SmokeGpuAdvectionMode = SmokeAdvectionMode::MacCormack;
        m_SmokeGpuVorticityEpsilon = 0.0f;

        SmokeBenchmarkConfig config;
        config.implementation = "gpu_jacobi";
        config.scenario = "limiter_stress_closed_box_v1";
        const char* limiterTag =
            (m_SmokeGpuLimiterMode == SmokeLimiterMode::Adaptive) ? "adaptive"
            : (m_SmokeGpuLimiterMode == SmokeLimiterMode::Revert) ? "revert"
            : "clamp";
        config.runLabel = std::string("stress_") + limiterTag;
        config.totalSteps = 480;
        config.emitterSteps = 240;
        config.performanceWarmupSteps = 30;
        config.timeStep = 1.0 / 60.0;
        const auto n = m_SmokeSolver.Density().Resolution();
        config.emitterCell = { n.x / 2, n.y / 4, n.z / 2 };
        config.advectionMode = m_SmokeGpuAdvectionMode;
        config.limiterMode = m_SmokeGpuLimiterMode;
        config.vorticityEpsilon = 0.0;
        config.renderingEnabledDuringRun = false;

        // Gentle cooled buoyancy keeps the plume in-domain (no top-boundary sink),
        // and zero dissipation means any density-integral drift is the limiter's
        // (non-)conservation alone: clamp grows it, revert collapses it.
        SmokePhysicsParameters physics;
        physics.ambientTemperature = 0.0;
        physics.temperatureBuoyancy = 0.3;
        physics.smokeWeight = 0.05;
        physics.densityDissipation = 0.0;
        physics.temperatureCooling = 0.5;

        m_SmokeGpuBenchmarkConfig = config;
        m_SmokeGpuBenchmarkPhysics = physics;
        m_SmokeGpuBenchmarkVorticityEpsilon = 0.0f;
        m_SmokeGpuBenchmarkLimiterMode = static_cast<int>(m_SmokeGpuLimiterMode);
        m_SmokeGpuBenchmarkIterations = 40;
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
        m_SmokeGpuBenchmarkStatus =
            std::string("Limiter-stress run (") + config.runLabel +
            "): recording. Repeat for Clamp / Revert / Adaptive.";
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Adversarial sealed box (closed top, zero dissipation), MacCormack,\n"
            "no confinement. Only the MacCormack Limiter combo varies. Run once\n"
            "per limiter, then compare density integral (stability) vs peak (sharpness).");
    ImGui::EndDisabled();
    if (m_SmokeGpuBenchmarkRunning)
    {
        ImGui::ProgressBar(static_cast<float>(m_SmokeGpuBenchmarkSamples.size()) /
            static_cast<float>(m_SmokeGpuBenchmarkConfig.totalSteps));
        ImGui::Text("Submitted: %u | collected: %zu", m_SmokeGpuBenchmarkSubmitted, m_SmokeGpuBenchmarkSamples.size());
        if (ImGui::Button("Stop and save partial run")) m_SmokeGpuBenchmarkStopping = true;
    }
    ImGui::TextWrapped("%s", m_SmokeGpuBenchmarkStatus.c_str());
    ImGui::TextWrapped("Exports GPU stage time, density, divergence and velocity metrics. Compare equal step counts and settings with the CPU benchmark; PCG and Jacobi accuracy differs.");
    ImGui::End();
}

void Renderer::SaveSmokeGpuBenchmark()
{
    try
    {
        auto& samples = m_SmokeGpuBenchmarkSamples;
        std::sort(samples.begin(), samples.end(), [](const auto& a, const auto& b) { return a.step < b.step; });
        const auto& config = m_SmokeGpuBenchmarkConfig;
        const auto n = m_SmokeSolver.Density().Resolution();
        const auto h = m_SmokeSolver.Density().GridSpacing();
        const auto origin = m_SmokeSolver.Density().Origin();
        const auto stamp = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        const auto directory = std::filesystem::absolute(config.outputRoot / (std::to_string(stamp) + "_gpu_jacobi"));
        std::filesystem::create_directories(directory);
        auto open = [&](const char* name)
        {
            std::ofstream file(directory / name);
            file.exceptions(std::ios::failbit | std::ios::badbit);
            file << std::setprecision(17);
            return file;
        };
        auto csv = open("steps.csv");
        csv << "step_index,simulation_time_s,emitter_enabled,solver_gpu_ms,diagnostics_gpu_ms,source_gpu_ms,velocity_advection_gpu_ms,buoyancy_gpu_ms,divergence_gpu_ms,pressure_clear_gpu_ms,pressure_solve_gpu_ms,pressure_gradient_gpu_ms,scalar_advection_gpu_ms,pressure_fraction,pressure_iteration_us,pressure_iterations,rms_divergence_before,max_abs_divergence_before,rms_divergence_after,max_abs_divergence_after,divergence_remaining_fraction,kinetic_energy,velocity_rms,velocity_max,nonfinite_divergence_before,nonfinite_divergence_after,nonfinite_velocity,density_min,density_max,density_sum,density_integral,density_centre_x,density_centre_y,density_centre_z,nonfinite_density_cells,enstrophy,max_vorticity\n";
        std::vector<double> timings;
        for (const auto& x : samples)
        {
            const double pressureFraction = x.milliseconds > 0.0
                ? x.pressureMilliseconds / x.milliseconds : 0.0;
            const double iterationMicroseconds = x.pressureMilliseconds * 1000.0 /
                static_cast<double>(std::max(x.pressureIterations, 1u));
            const double remainingDivergence = x.divergenceBeforeRms > 1.0e-20
                ? x.divergenceAfterRms / x.divergenceBeforeRms : 0.0;
            csv << x.step << ',' << x.step * config.timeStep << ',' << x.emit << ','
                << x.milliseconds << ',' << x.diagnosticsMilliseconds << ','
                << x.sourceMilliseconds << ',' << x.velocityAdvectionMilliseconds << ','
                << x.buoyancyMilliseconds << ',' << x.divergenceMilliseconds << ','
                << x.pressureClearMilliseconds << ',' << x.pressureMilliseconds << ','
                << x.pressureGradientMilliseconds << ',' << x.scalarAdvectionMilliseconds << ','
                << pressureFraction << ',' << iterationMicroseconds << ','
                << x.pressureIterations << ','
                << x.divergenceBeforeRms << ',' << x.divergenceBeforeMax << ','
                << x.divergenceAfterRms << ',' << x.divergenceAfterMax << ','
                << remainingDivergence << ',' << x.kineticEnergy << ','
                << x.rmsSpeed << ',' << x.maxSpeed << ','
                << x.divergenceBeforeNonfinite << ','
                << x.divergenceAfterNonfinite << ',' << x.velocityNonfinite << ','
                << x.densityMin << ',' << x.densityMax << ',' << x.densitySum << ','
                << x.densitySum * h.x * h.y * h.z << ',' << x.centre.x << ',' << x.centre.y << ',' << x.centre.z << ',' << x.nonfinite
                << ',' << x.enstrophy << ',' << x.maxVorticity << '\n';
            if (x.step > config.performanceWarmupSteps) timings.push_back(x.milliseconds);
        }
        const bool completed = samples.size() == config.totalSteps;
        std::sort(timings.begin(), timings.end());
        auto percentile = [&](double p)
        {
            const double i = p * (timings.size() - 1);
            const auto a = static_cast<std::size_t>(i);
            const auto b = std::min(a + 1, timings.size() - 1);
            return timings[a] + (i - a) * (timings[b] - timings[a]);
        };
        auto stageMean = [&](double GpuSmokeSample::* member)
        {
            double total = 0.0;
            std::size_t count = 0;
            for (const auto& x : samples)
                if (x.step > config.performanceWarmupSteps)
                {
                    total += x.*member;
                    ++count;
                }
            return count > 0 ? total / static_cast<double>(count) : 0.0;
        };
        double remainingDivergenceTotal = 0.0;
        double divergenceAfterPeak = 0.0;
        double maxSpeedPeak = 0.0;
        std::uint64_t diagnosticNonfiniteTotal = 0;
        std::size_t numericalCount = 0;
        for (const auto& x : samples)
            if (x.step > config.performanceWarmupSteps)
            {
                if (x.divergenceBeforeRms > 1.0e-20)
                    remainingDivergenceTotal +=
                        x.divergenceAfterRms / x.divergenceBeforeRms;
                divergenceAfterPeak = std::max(
                    divergenceAfterPeak, x.divergenceAfterMax);
                maxSpeedPeak = std::max(maxSpeedPeak, x.maxSpeed);
                diagnosticNonfiniteTotal += x.diagnosticNonfinite;
                ++numericalCount;
            }
        auto summary = open("summary.csv");
        summary << "implementation,completed,steps_recorded,warmup_steps_excluded,solver_gpu_ms_mean,solver_gpu_ms_p50,solver_gpu_ms_p95,solver_gpu_ms_p99,diagnostics_gpu_ms_mean,source_gpu_ms_mean,velocity_advection_gpu_ms_mean,buoyancy_gpu_ms_mean,divergence_gpu_ms_mean,pressure_clear_gpu_ms_mean,pressure_solve_gpu_ms_mean,pressure_gradient_gpu_ms_mean,scalar_advection_gpu_ms_mean,pressure_fraction_mean,rms_divergence_before_mean,rms_divergence_after_mean,divergence_remaining_fraction_mean,max_abs_divergence_after_peak,kinetic_energy_mean,velocity_rms_mean,velocity_max_peak,nonfinite_diagnostic_samples_total,enstrophy_mean\n";
        summary << "gpu_jacobi," << completed << ',' << samples.size() << ',' << config.performanceWarmupSteps;
        if (!timings.empty())
        {
            const double stepMean =
                std::accumulate(timings.begin(), timings.end(), 0.0) / timings.size();
            summary << ',' << stepMean
                << ',' << percentile(0.50) << ',' << percentile(0.95) << ',' << percentile(0.99)
                << ',' << stageMean(&GpuSmokeSample::diagnosticsMilliseconds)
                << ',' << stageMean(&GpuSmokeSample::sourceMilliseconds)
                << ',' << stageMean(&GpuSmokeSample::velocityAdvectionMilliseconds)
                << ',' << stageMean(&GpuSmokeSample::buoyancyMilliseconds)
                << ',' << stageMean(&GpuSmokeSample::divergenceMilliseconds)
                << ',' << stageMean(&GpuSmokeSample::pressureClearMilliseconds)
                << ',' << stageMean(&GpuSmokeSample::pressureMilliseconds)
                << ',' << stageMean(&GpuSmokeSample::pressureGradientMilliseconds)
                << ',' << stageMean(&GpuSmokeSample::scalarAdvectionMilliseconds)
                << ',' << stageMean(&GpuSmokeSample::pressureMilliseconds) / stepMean
                << ',' << stageMean(&GpuSmokeSample::divergenceBeforeRms)
                << ',' << stageMean(&GpuSmokeSample::divergenceAfterRms)
                << ',' << (numericalCount > 0
                    ? remainingDivergenceTotal / static_cast<double>(numericalCount)
                    : 0.0)
                << ',' << divergenceAfterPeak
                << ',' << stageMean(&GpuSmokeSample::kineticEnergy)
                << ',' << stageMean(&GpuSmokeSample::rmsSpeed)
                << ',' << maxSpeedPeak
                << ',' << diagnosticNonfiniteTotal
                << ',' << stageMean(&GpuSmokeSample::enstrophy);
        }
        else
            for (int column = 0; column < 23; ++column)
                summary << ',';
        summary << '\n';
        auto manifest = open("manifest.json");
        Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        DXGI_ADAPTER_DESC1 adapterDesc = {};
        std::string adapterName = "unavailable";
        if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) &&
            SUCCEEDED(factory->EnumAdapterByLuid(m_Device->GetAdapterLuid(), IID_PPV_ARGS(&adapter))) &&
            SUCCEEDED(adapter->GetDesc1(&adapterDesc)))
        {
            char name[512] = {};
            WideCharToMultiByte(CP_UTF8, 0, adapterDesc.Description, -1, name, sizeof(name), nullptr, nullptr);
            adapterName = name;
        }
        manifest << "{\n  \"schema_version\": 3,\n  \"implementation\": \"gpu_jacobi\",\n  \"scenario\": " << std::quoted(config.scenario)
            << ",\n  \"run_label\": " << std::quoted(config.runLabel)
            << ",\n  \"completed\": " << (completed ? "true" : "false")
            << ",\n  \"resolution\": [" << n.x << ',' << n.y << ',' << n.z << ']'
            << ",\n  \"grid_spacing\": [" << h.x << ',' << h.y << ',' << h.z << ']'
            << ",\n  \"origin\": [" << origin.x << ',' << origin.y << ',' << origin.z << ']'
            << ",\n  \"time_step_s\": " << config.timeStep
            << ",\n  \"total_steps\": " << config.totalSteps << ",\n  \"emitter_steps\": " << config.emitterSteps
            << ",\n  \"warmup_steps\": " << config.performanceWarmupSteps
            << ",\n  \"source_cell\": [" << config.emitterCell.x << ',' << config.emitterCell.y << ',' << config.emitterCell.z << ']'
            << ",\n  \"density_rate\": 30,\n  \"temperature_rate\": 10,\n  \"source_acceleration\": [0,0,0],"
            << "\n  \"pressure_iterations\": " << m_SmokeGpuBenchmarkIterations
			<< ",\n  \"advection_mode\": " << (m_SmokeGpuAdvectionMode == SmokeAdvectionMode::SemiLagrangian ? "\"semi-Lagrangian\"" : "\"MacCormack\"")
            << ",\n  \"limiter_mode\": " << (m_SmokeGpuBenchmarkLimiterMode == 2 ? "\"adaptive\"" : m_SmokeGpuBenchmarkLimiterMode == 1 ? "\"revert\"" : "\"clamp\"")
            << ",\n  \"vorticity_epsilon\": " << m_SmokeGpuBenchmarkVorticityEpsilon
            << ",\n  \"sphere_enabled\": " << (m_SmokeGpuSphereEnabled ? "true" : "false")
            << ",\n  \"sphere_centre\": [" << m_SmokeGpuSphereCentre.x << ','
            << m_SmokeGpuSphereCentre.y << ',' << m_SmokeGpuSphereCentre.z << ']'
            << ",\n  \"sphere_radius\": " << m_SmokeGpuSphereRadius
            << ",\n  \"jacobi_weight\": " << (2.0 / 3.0)
            << ",\n  \"fluid_density\": " << m_SmokeSolver.FluidDensity()
            << ",\n  \"ambient_temperature\": " << m_SmokeGpuBenchmarkPhysics.ambientTemperature
            << ",\n  \"temperature_buoyancy\": " << m_SmokeGpuBenchmarkPhysics.temperatureBuoyancy
            << ",\n  \"smoke_weight\": " << m_SmokeGpuBenchmarkPhysics.smokeWeight
            << ",\n  \"density_dissipation_per_s\": " << m_SmokeGpuBenchmarkPhysics.densityDissipation
            << ",\n  \"temperature_cooling_per_s\": " << m_SmokeGpuBenchmarkPhysics.temperatureCooling
			<< ",\n  \"open_top_enabled\": " << (m_SmokeGpuOpenTopEnabled ? "true" : "false")
			<< ",\n  \"solver_gpu_ms\": " << m_SmokeGpuLastMilliseconds
            << ",\n  \"solver_gpu_pressure_ms\": " << m_SmokeGpuLastPressureMilliseconds
			<< ",\n  \"solver_gpu_pressure_fraction\": " << m_SmokeGpuPressureFraction
            << ",\n  \"solver_gpu_average_pressure_iteration_us\": " << m_SmokeGpuAverageIterationMicroSeconds
            << ",\n  \"rendering_enabled\": " << (config.renderingEnabledDuringRun ? "true" : "false")
            << ",\n  \"timestamp_frequency_hz\": " << m_SmokeGpuTimestampFrequency
            << ",\n  \"gpu_adapter\": " << std::quoted(adapterName)
            << ",\n  \"gpu_vendor_id\": " << adapterDesc.VendorId
            << ",\n  \"gpu_device_id\": " << adapterDesc.DeviceId
            << ",\n  \"compiler_msvc\": " << _MSC_VER
#ifdef _DEBUG
            << ",\n  \"build\": \"Debug\""
#else
            << ",\n  \"build\": \"Release\""
#endif
            << ",\n  \"precision\": \"float32\",\n  \"boundary\": " << std::quoted(
                m_SmokeGpuOpenTopEnabled
                    ? "free-slip box with open top"
                    : "closed free-slip box") << ','
            << "\n  \"advection\": \"semi-Lagrangian midpoint, hardware trilinear\","
            << "\n  \"timing_scope\": \"GPU timestep split into source, velocity advection, buoyancy, divergence, pressure clear/setup, Jacobi solve, pressure-gradient subtraction and scalar advection; excludes reset, readback, rendering, CPU diagnostics and file output\","
            << "\n  \"numerical_diagnostics\": \"single-group float32 GPU reductions over fluid cells; divergence before/after projection and cell-centred velocity energy; executed after solver timestamp\","
            << "\n  \"readback\": \"density during benchmark and compact numerical diagnostics every step after timestamp; may affect total frame cost\","
            << "\n  \"unavailable_metrics\": [\"pressure_residual\",\"temperature\"],"
            << "\n  \"comparison_note\": \"CPU PCG and fixed-iteration GPU Jacobi do not guarantee equal projection accuracy. Deterministic schedule, not cross-device bitwise determinism.\"\n}\n";
        csv.close(); summary.close(); manifest.close();
        m_SmokeGpuBenchmarkStatus = "Saved " + directory.string();
    }
    catch (const std::exception& e)
    {
        m_SmokeGpuBenchmarkStatus = std::string("Export failed: ") + e.what();
    }
}

DirectX::XMFLOAT3 Renderer::SmokeObstacleWorldRadii() const
{
    const auto n = m_SmokeSolver.Density().Resolution();
    const auto h = m_SmokeSolver.Density().GridSpacing();
    return {
        static_cast<float>(m_SmokeGpuSphereRadius * m_SmokeSize[0] / (n.x * h.x)),
        static_cast<float>(m_SmokeGpuSphereRadius * m_SmokeSize[1] / (n.y * h.y)),
        static_cast<float>(m_SmokeGpuSphereRadius * m_SmokeSize[2] / (n.z * h.z))
    };
}

void Renderer::CreateSmokeObstaclePipeline()
{
    CD3DX12_ROOT_PARAMETER root;
    root.InitAsConstants(28, 0);
    CD3DX12_ROOT_SIGNATURE_DESC description(1, &root, 0, nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    Microsoft::WRL::ComPtr<ID3DBlob> serialized, errors;
    const HRESULT result = D3D12SerializeRootSignature(&description,
        D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors);
    if (errors) OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
    ThrowIfFailed(result);
    ThrowIfFailed(m_Device->CreateRootSignature(0, serialized->GetBufferPointer(),
        serialized->GetBufferSize(), IID_PPV_ARGS(&m_SmokeObstacleRootSignature)));
    const auto vs = d3dUtil::CompileShader(L"Shaders/smoke_obstacle.hlsl", nullptr, "VS", "vs_5_0");
    const auto ps = d3dUtil::CompileShader(L"Shaders/smoke_obstacle.hlsl", nullptr, "PS", "ps_5_0");
    const D3D12_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
    pso.pRootSignature = m_SmokeObstacleRootSignature.Get();
    pso.InputLayout = { layout, _countof(layout) };
    pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    pso.SampleMask = UINT_MAX;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = m_BackBufferFormat;
    pso.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    pso.SampleDesc.Count = 1;
    ThrowIfFailed(m_Device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_SmokeObstaclePSO)));
    ThrowIfFailed(m_SmokeObstaclePSO->SetName(L"Smoke.Obstacle.Opaque"));
}

void Renderer::DrawSmokeObstacle(ID3D12GraphicsCommandList* commandList)
{
    const auto n = m_SmokeSolver.Density().Resolution();
    const auto h = m_SmokeSolver.Density().GridSpacing();
    const auto origin = m_SmokeSolver.Density().Origin();
    // Same mapping as the density volume: simulation domain -> unit texture box -> world.
    const XMFLOAT3 centreWorld = {
        static_cast<float>(m_SmokePosition[0] +
            ((m_SmokeGpuSphereCentre.x - origin.x) / (n.x * h.x) - 0.5) * m_SmokeSize[0]),
        static_cast<float>(m_SmokePosition[1] +
            ((m_SmokeGpuSphereCentre.y - origin.y) / (n.y * h.y) - 0.5) * m_SmokeSize[1]),
        static_cast<float>(m_SmokePosition[2] +
            ((m_SmokeGpuSphereCentre.z - origin.z) / (n.z * h.z) - 0.5) * m_SmokeSize[2])
    };
    const auto radii = SmokeObstacleWorldRadii();
    // Luna's existing sphere mesh has radius 0.5.
    const XMFLOAT3 scale = { 2 * radii.x, 2 * radii.y, 2 * radii.z };
    const XMMATRIX world = XMMatrixScaling(scale.x, scale.y, scale.z) *
        XMMatrixTranslation(centreWorld.x, centreWorld.y, centreWorld.z);
    struct Constants
    {
        XMFLOAT4X4 worldViewProjection;
        XMFLOAT3 inverseScale; float pad0;
        XMFLOAT3 lightDirection; float pad1;
        XMFLOAT4 colour;
    } constants{};
    static_assert(sizeof(Constants) == 28 * sizeof(float));
    XMStoreFloat4x4(&constants.worldViewProjection, XMMatrixTranspose(
        world * XMLoadFloat4x4(&m_View) * XMLoadFloat4x4(&m_Proj)));
    constants.inverseScale = { 1.0f / scale.x, 1.0f / scale.y, 1.0f / scale.z };
    constants.lightDirection = m_MainPassCB.Lights[0].Direction;
    constants.colour = { 0.65f, 0.70f, 0.76f, 1.0f };
    commandList->SetPipelineState(m_SmokeObstaclePSO.Get());
    commandList->SetGraphicsRootSignature(m_SmokeObstacleRootSignature.Get());
    commandList->SetGraphicsRoot32BitConstants(0, 28, &constants, 0);
    const auto* geometry = m_Geometries.at("shapeGeo").get();
    const auto& sphere = geometry->DrawArgs.at("sphere");
    const auto vertices = geometry->VertexBufferView();
    const auto indices = geometry->IndexBufferView();
    commandList->IASetVertexBuffers(0, 1, &vertices);
    commandList->IASetIndexBuffer(&indices);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->DrawIndexedInstanced(sphere.IndexCount, 1,
        sphere.StartIndexLocation, sphere.BaseVertexLocation, 0);
}
