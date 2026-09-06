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
    desc.Count = 2 * NumFrameResources;
    ThrowIfFailed(m_Device->CreateQueryHeap(&desc, IID_PPV_ARGS(&m_SmokeGpuQueries)));
    ThrowIfFailed(m_CommandQueue->GetTimestampFrequency(&m_SmokeGpuTimestampFrequency));
    const auto texture = m_GpuDensity[0].resource->GetDesc();
    UINT64 bytes = 0;
    // Two timestamps precede the aligned texture footprint.
    m_Device->GetCopyableFootprints(&texture, 0, 1, 512,
        &m_SmokeGpuReadbackFootprint, nullptr, nullptr, &bytes);
    const auto buffer = CD3DX12_RESOURCE_DESC::Buffer(bytes + 512);
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
        const auto* timestamps = static_cast<const UINT64*>(mapped);
        auto sample = slot.sample;
        sample.milliseconds = 1000.0 * static_cast<double>(timestamps[1] - timestamps[0]) /
            static_cast<double>(m_SmokeGpuTimestampFrequency);
        m_SmokeGpuLastMilliseconds = sample.milliseconds;
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
        SaveSmokeGpuBenchmark();
        m_SmokeGpuBenchmarkRunning = false;
        m_SmokeGpuPaused = true;
        m_SmokeGpuPendingSteps = 0;
        m_ShowSmokeVolume = m_SmokeGpuRestoreVolume;
    }
}

void Renderer::DrawSmokeGpuDebug()
{
    ImGui::Begin("Smoke 3D GPU controls");
    ImGui::Text("GPU solver: %.3f ms / step (delayed timestamp)", m_SmokeGpuLastMilliseconds);
    ImGui::Text("Simulation steps: %u | simulated time: %.2f s",
        m_SmokeGpuInjectionCount, m_SmokeGpuInjectionCount / 60.0);
    ImGui::BeginDisabled(m_SmokeGpuBenchmarkRunning);
    if (ImGui::Checkbox("Paused", &m_SmokeGpuPaused))
    {
        m_SmokeGpuPendingSteps = 0;
        m_SmokeGpuAccumulator = 0;
    }
    ImGui::Checkbox("Emitter enabled", &m_SmokeGpuEmitterEnabled);
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
    ImGui::EndDisabled();
    if (m_SmokeGpuBenchmarkRunning)
    {
        ImGui::ProgressBar(static_cast<float>(m_SmokeGpuBenchmarkSamples.size()) /
            static_cast<float>(m_SmokeGpuBenchmarkConfig.totalSteps));
        ImGui::Text("Submitted: %u | collected: %zu", m_SmokeGpuBenchmarkSubmitted, m_SmokeGpuBenchmarkSamples.size());
        if (ImGui::Button("Stop and save partial run")) m_SmokeGpuBenchmarkStopping = true;
    }
    ImGui::TextWrapped("%s", m_SmokeGpuBenchmarkStatus.c_str());
    ImGui::TextWrapped("Exports GPU solver time and density metrics. GPU divergence/velocity metrics are not captured. Compare equal step counts and settings with the CPU benchmark; PCG and Jacobi accuracy differs.");
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
        csv << "step_index,simulation_time_s,emitter_enabled,solver_gpu_ms,pressure_iterations,density_min,density_max,density_sum,density_integral,density_centre_x,density_centre_y,density_centre_z,nonfinite_density_cells\n";
        std::vector<double> timings;
        for (const auto& x : samples)
        {
            csv << x.step << ',' << x.step * config.timeStep << ',' << x.emit << ',' << x.milliseconds << ','
                << m_SmokeGpuBenchmarkIterations << ',' << x.densityMin << ',' << x.densityMax << ',' << x.densitySum << ','
                << x.densitySum * h.x * h.y * h.z << ',' << x.centre.x << ',' << x.centre.y << ',' << x.centre.z << ',' << x.nonfinite << '\n';
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
        auto summary = open("summary.csv");
        summary << "implementation,completed,steps_recorded,warmup_steps_excluded,solver_gpu_ms_mean,solver_gpu_ms_p50,solver_gpu_ms_p95,solver_gpu_ms_p99\n";
        summary << "gpu_jacobi," << completed << ',' << samples.size() << ',' << config.performanceWarmupSteps;
        if (!timings.empty())
            summary << ',' << std::accumulate(timings.begin(), timings.end(), 0.0) / timings.size()
                << ',' << percentile(0.50) << ',' << percentile(0.95) << ',' << percentile(0.99);
        else summary << ",,,,";
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
        manifest << "{\n  \"schema_version\": 1,\n  \"implementation\": \"gpu_jacobi\",\n  \"scenario\": " << std::quoted(config.scenario)
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
            << ",\n  \"jacobi_weight\": " << (2.0 / 3.0)
            << ",\n  \"fluid_density\": " << m_SmokeSolver.FluidDensity()
            << ",\n  \"ambient_temperature\": " << m_SmokeGpuBenchmarkPhysics.ambientTemperature
            << ",\n  \"temperature_buoyancy\": " << m_SmokeGpuBenchmarkPhysics.temperatureBuoyancy
            << ",\n  \"smoke_weight\": " << m_SmokeGpuBenchmarkPhysics.smokeWeight
            << ",\n  \"density_dissipation_per_s\": " << m_SmokeGpuBenchmarkPhysics.densityDissipation
            << ",\n  \"temperature_cooling_per_s\": " << m_SmokeGpuBenchmarkPhysics.temperatureCooling
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
            << ",\n  \"precision\": \"float32\",\n  \"boundary\": \"closed free-slip box\","
            << "\n  \"advection\": \"semi-Lagrangian midpoint, hardware trilinear\","
            << "\n  \"timing_scope\": \"GPU timestep only; excludes reset, readback, rendering, CPU diagnostics and file output\","
            << "\n  \"readback\": \"density every step after timestamp; may affect total frame cost\","
            << "\n  \"unavailable_metrics\": [\"divergence\",\"pressure_residual\",\"velocity\",\"temperature\"],"
            << "\n  \"comparison_note\": \"CPU PCG and fixed-iteration GPU Jacobi do not guarantee equal projection accuracy. Deterministic schedule, not cross-device bitwise determinism.\"\n}\n";
        csv.close(); summary.close(); manifest.close();
        m_SmokeGpuBenchmarkStatus = "Saved " + directory.string();
    }
    catch (const std::exception& e)
    {
        m_SmokeGpuBenchmarkStatus = std::string("Export failed: ") + e.what();
    }
}
