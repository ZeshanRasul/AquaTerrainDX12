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
    ImGui::Checkbox("Sphere obstacle enabled", &m_SmokeGpuSphereEnabled);
    ImGui::BeginDisabled(!m_SmokeGpuSphereEnabled);
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
        if (m_SmokeGpuSphereEnabled)
            config.scenario = "buoyant_plume_closed_box_sphere_v1";
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
