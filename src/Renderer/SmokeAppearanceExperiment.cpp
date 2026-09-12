#include "Renderer.h"

#include <d3d12sdklayers.h>
#include <bit>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace
{
    void CALLBACK AppearanceMessage(D3D12_MESSAGE_CATEGORY category,
        D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID id,
        LPCSTR description, void* context)
    {
        // Never call D3D from a debug callback. Flush each message so fatal
        // process termination cannot erase the last API diagnostic.
        static std::mutex mutex;
        std::lock_guard<std::mutex> lock(mutex);
        const auto& root = *static_cast<const std::filesystem::path*>(context);
        std::ofstream log(root / "debug-live.txt", std::ios::app);
        log << "severity=" << severity << " category=" << category
            << " id=" << id << '\n' << description << std::endl;
    }
    constexpr std::uint64_t FnvOffset = 14695981039346656037ull;
    constexpr std::uint64_t FnvPrime = 1099511628211ull;

    std::wstring RequiredEnvironment(const wchar_t* name)
    {
        const DWORD count = GetEnvironmentVariableW(name, nullptr, 0);
        if (count == 0)
            throw std::runtime_error("Missing required appearance environment variable");
        std::vector<wchar_t> value(count);
        if (GetEnvironmentVariableW(name, value.data(), count) + 1 != count)
            throw std::runtime_error("Failed to read appearance environment variable");
        return std::wstring(value.data());
    }

    unsigned ParseUnsignedEnvironment(const wchar_t* name, unsigned minimum, unsigned maximum)
    {
        const std::wstring text = RequiredEnvironment(name);
        std::size_t consumed = 0;
        const unsigned long parsed = std::stoul(text, &consumed);
        if (consumed != text.size() || parsed < minimum || parsed > maximum)
            throw std::runtime_error("Invalid unsigned appearance environment value");
        return static_cast<unsigned>(parsed);
    }

    std::string Utf8(const std::wstring& value)
    {
        if (value.empty()) return {};
        const int count = WideCharToMultiByte(
            CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
            nullptr, 0, nullptr, nullptr);
        if (count <= 0) throw std::runtime_error("UTF-8 conversion failed");
        std::string output(static_cast<std::size_t>(count), '\0');
        if (WideCharToMultiByte(
            CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
            output.data(), count, nullptr, nullptr) != count)
            throw std::runtime_error("UTF-8 conversion failed");
        return output;
    }

    std::string JsonEscape(const std::string& value)
    {
        std::ostringstream output;
        for (const unsigned char character : value)
        {
            switch (character)
            {
            case '\\': output << "\\\\"; break;
            case '"': output << "\\\""; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (character < 0x20)
                    output << "\\u" << std::hex << std::setw(4)
                        << std::setfill('0') << static_cast<unsigned>(character)
                        << std::dec << std::setfill(' ');
                else output << static_cast<char>(character);
                break;
            }
        }
        return output.str();
    }

    std::uint64_t HashBytes(const void* data, std::size_t size)
    {
        std::uint64_t hash = FnvOffset;
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (std::size_t index = 0; index < size; ++index)
            hash = (hash ^ bytes[index]) * FnvPrime;
        return hash;
    }

    struct PackedField
    {
        std::vector<float> values;
        double sum = 0.0;
        double minimum = std::numeric_limits<double>::infinity();
        double maximum = -std::numeric_limits<double>::infinity();
        unsigned nonfinite = 0;
        unsigned negative = 0;
        std::uint64_t hash = FnvOffset;
    };

    PackedField PackField(
        const void* mapped,
        const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint)
    {
        const std::size_t cellCount =
            static_cast<std::size_t>(footprint.Footprint.Width) *
            footprint.Footprint.Height * footprint.Footprint.Depth;
        PackedField result;
        result.values.reserve(cellCount);
        const auto* base = static_cast<const std::byte*>(mapped);
        for (UINT z = 0; z < footprint.Footprint.Depth; ++z)
            for (UINT y = 0; y < footprint.Footprint.Height; ++y)
            {
                const auto* row = reinterpret_cast<const float*>(
                    base + footprint.Offset +
                    (static_cast<UINT64>(z) * footprint.Footprint.Height + y) *
                    footprint.Footprint.RowPitch);
                result.values.insert(
                    result.values.end(), row, row + footprint.Footprint.Width);
                for (UINT x = 0; x < footprint.Footprint.Width; ++x)
                {
                    const double value = row[x];
                    if (!std::isfinite(value))
                    {
                        ++result.nonfinite;
                        continue;
                    }
                    result.sum += value;
                    result.minimum = std::min(result.minimum, value);
                    result.maximum = std::max(result.maximum, value);
                    if (value < 0.0) ++result.negative;
                }
            }
        result.hash = HashBytes(
            result.values.data(), result.values.size() * sizeof(float));
        if (result.values.empty() || result.nonfinite == result.values.size())
        {
            result.minimum = 0.0;
            result.maximum = 0.0;
        }
        return result;
    }

    void WritePackedField(const std::filesystem::path& path, const PackedField& field)
    {
        std::ofstream output(path, std::ios::binary);
        output.exceptions(std::ios::failbit | std::ios::badbit);
        output.write(
            reinterpret_cast<const char*>(field.values.data()),
            static_cast<std::streamsize>(field.values.size() * sizeof(float)));
    }

    D3D12_CPU_DESCRIPTOR_HANDLE CpuDescriptor(
        ID3D12DescriptorHeap* heap, UINT index, UINT increment)
    {
        return CD3DX12_CPU_DESCRIPTOR_HANDLE(
            heap->GetCPUDescriptorHandleForHeapStart(), index, increment);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GpuDescriptor(
        ID3D12DescriptorHeap* heap, UINT index, UINT increment)
    {
        return CD3DX12_GPU_DESCRIPTOR_HANDLE(
            heap->GetGPUDescriptorHandleForHeapStart(), index, increment);
    }
}

void Renderer::StartSmokeAppearanceExperiment(bool automatic)
{
    if (m_SmokeGpuBenchmarkRunning)
        throw std::runtime_error("Cannot start appearance experiment during another GPU run");

    const std::wstring sceneText =
        RequiredEnvironment(L"AQUA_SMOKE_APPEARANCE_SCENE");
    if (sceneText != L"A" && sceneText != L"B")
        throw std::runtime_error("AQUA_SMOKE_APPEARANCE_SCENE must be A or B");

    SmokeAppearanceConfig appearance;
    appearance.scene = Utf8(sceneText);
    appearance.sourceCount = sceneText == L"A" ? 1u : 2u;
    appearance.targetIntegratedRate = 0.002 * appearance.sourceCount;
    appearance.sourceRatePath = std::filesystem::absolute(
        RequiredEnvironment(L"AQUA_SMOKE_APPEARANCE_SOURCE"));
    appearance.outputDirectory = std::filesystem::absolute(
        RequiredEnvironment(L"AQUA_SMOKE_APPEARANCE_OUTPUT"));
    appearance.pressureIterations = static_cast<int>(ParseUnsignedEnvironment(
        L"AQUA_SMOKE_APPEARANCE_ITERATIONS", 0, 65536));
    wchar_t offlineText[32]{};
    if (GetEnvironmentVariableW(L"AQUA_SMOKE_OFFLINE_PRESSURE", offlineText, 32))
        m_OfflinePressureMode = ParseUnsignedEnvironment(L"AQUA_SMOKE_OFFLINE_PRESSURE", 0, 2);
    if ((m_OfflinePressureMode != 0) != (appearance.pressureIterations == 0))
        throw std::runtime_error("Offline reference requires zero Jacobi iterations; production requires positive iterations");
    appearance.totalSteps = ParseUnsignedEnvironment(
        L"AQUA_SMOKE_APPEARANCE_STEPS", 1, 360);
    appearance.emitterSteps = ParseUnsignedEnvironment(
        L"AQUA_SMOKE_APPEARANCE_EMITTER_STEPS", 0, appearance.totalSteps);
    wchar_t triageText[32]{};
    if (GetEnvironmentVariableW(L"AQUA_SMOKE_PRESSURE_TRIAGE_STEP", triageText, 32))
        m_SmokePressureTriageStep = ParseUnsignedEnvironment(
            L"AQUA_SMOKE_PRESSURE_TRIAGE_STEP", 0, appearance.totalSteps);
    if (m_SmokePressureTriageStep && appearance.pressureIterations != 65536)
        throw std::runtime_error("Pressure triage requires the unchanged cap trajectory");

    const auto resolution = m_SmokeSolver.Density().Resolution();
    if (resolution.x != resolution.y || resolution.x != resolution.z ||
        (resolution.x != 32 && resolution.x != 64 && resolution.x != 128))
        throw std::runtime_error("Appearance experiment requires 32, 64 or 128 cubed");
    const std::size_t cellCount = resolution.x * resolution.y * resolution.z;
    const std::uintmax_t expectedBytes = cellCount * sizeof(float);
    if (!std::filesystem::is_regular_file(appearance.sourceRatePath) ||
        std::filesystem::file_size(appearance.sourceRatePath) != expectedBytes)
        throw std::runtime_error("Appearance source table is missing or has the wrong size");
    if (std::filesystem::exists(appearance.outputDirectory))
        throw std::runtime_error("Appearance output directory already exists");

    m_SmokeAppearanceSourceRates.resize(cellCount);
    {
        std::ifstream input(appearance.sourceRatePath, std::ios::binary);
        input.exceptions(std::ios::failbit | std::ios::badbit);
        input.read(
            reinterpret_cast<char*>(m_SmokeAppearanceSourceRates.data()),
            static_cast<std::streamsize>(expectedBytes));
    }
    double storedRateSum = 0.0;
    for (const float rate : m_SmokeAppearanceSourceRates)
    {
        if (!std::isfinite(rate) || rate < 0.0f)
            throw std::runtime_error("Appearance source table must be finite and nonnegative");
        storedRateSum += rate;
    }
    const double cellVolume = 1.0 /
        static_cast<double>(resolution.x * resolution.y * resolution.z);
    m_SmokeAppearanceStoredRateIntegral = storedRateSum * cellVolume;
    const double sourceRelativeError = std::abs(
        m_SmokeAppearanceStoredRateIntegral - appearance.targetIntegratedRate) /
        appearance.targetIntegratedRate;
    if (sourceRelativeError > 1.0e-7)
        throw std::runtime_error("Appearance source table fails its registered integral tolerance");
    m_SmokeAppearanceSourceRateBytes = static_cast<UINT64>(expectedBytes);
    m_SmokeAppearanceSourceRateHash = HashBytes(
        m_SmokeAppearanceSourceRates.data(), static_cast<std::size_t>(expectedBytes));

    const auto gridSpacing = m_SmokeSolver.Density().GridSpacing();
    const auto gridOrigin = m_SmokeSolver.Density().Origin();
    std::ostringstream identity;
    identity << "schema=2"
        << ";scene=" << appearance.scene
        << ";resolution=" << resolution.x
        << ";spacing_bits=" << std::bit_cast<std::uint64_t>(gridSpacing.x)
        << ',' << std::bit_cast<std::uint64_t>(gridSpacing.y)
        << ',' << std::bit_cast<std::uint64_t>(gridSpacing.z)
        << ";origin_bits=" << std::bit_cast<std::uint64_t>(gridOrigin.x)
        << ',' << std::bit_cast<std::uint64_t>(gridOrigin.y)
        << ',' << std::bit_cast<std::uint64_t>(gridOrigin.z)
        << ";dt_bits=" << std::bit_cast<std::uint32_t>(appearance.timeStep)
        << ";steps=" << appearance.totalSteps
        << ";emitter_steps=" << appearance.emitterSteps
        << ";pressure_iterations=" << appearance.pressureIterations
        << ";source_count=" << appearance.sourceCount
        << ";source_rate_fnv1a64=" << m_SmokeAppearanceSourceRateHash
        << ";advection=maccormack;limiter=clamp;closed=1;obstacle=0"
        << ";vorticity_bits=" << std::bit_cast<std::uint32_t>(0.0f)
        << ";density_dissipation_bits=" << std::bit_cast<std::uint32_t>(0.0f)
        << ";temperature_cooling_bits=" << std::bit_cast<std::uint32_t>(0.5f)
        << ";ambient_bits=" << std::bit_cast<std::uint32_t>(0.0f)
        << ";buoyancy_bits=" << std::bit_cast<std::uint32_t>(0.6f)
        << ";smoke_weight_bits=" << std::bit_cast<std::uint32_t>(0.05f)
        << ";fluid_density_bits=" << std::bit_cast<std::uint32_t>(1.0f)
        << ";jacobi_weight_bits="
        << std::bit_cast<std::uint32_t>(2.0f / 3.0f);
    m_SmokeAppearanceConfigurationIdentity = identity.str();
    if (m_OfflinePressureMode)
        m_SmokeAppearanceConfigurationIdentity += ";offline_pressure=" + std::to_string(m_OfflinePressureMode);
    m_SmokeAppearanceConfigurationHash = HashBytes(
        m_SmokeAppearanceConfigurationIdentity.data(),
        m_SmokeAppearanceConfigurationIdentity.size());

    if (!m_DebugController)
        throw std::runtime_error("Appearance experiment requires the D3D12 debug layer");
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue;
    ThrowIfFailed(m_Device.As(&infoQueue));
    ThrowIfFailed(infoQueue->SetMessageCountLimit(1ull << 20));
    infoQueue->ClearStoredMessages();
    m_SmokeAppearanceDiscardedMessageBaseline =
        infoQueue->GetNumMessagesDiscardedByMessageCountLimit();

    CD3DX12_DESCRIPTOR_RANGE sourceRange;
    sourceRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    CD3DX12_DESCRIPTOR_RANGE outputRange;
    outputRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0);
    CD3DX12_ROOT_PARAMETER parameters[3];
    parameters[0].InitAsConstants(4, 0);
    parameters[1].InitAsDescriptorTable(1, &sourceRange);
    parameters[2].InitAsDescriptorTable(1, &outputRange);
    CD3DX12_ROOT_SIGNATURE_DESC rootDescription;
    rootDescription.Init(3, parameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);
    Microsoft::WRL::ComPtr<ID3DBlob> serializedRoot;
    Microsoft::WRL::ComPtr<ID3DBlob> rootErrors;
    const HRESULT rootResult = D3D12SerializeRootSignature(
        &rootDescription, D3D_ROOT_SIGNATURE_VERSION_1,
        &serializedRoot, &rootErrors);
    if (rootErrors)
        OutputDebugStringA(static_cast<const char*>(rootErrors->GetBufferPointer()));
    ThrowIfFailed(rootResult);
    ThrowIfFailed(m_Device->CreateRootSignature(
        0, serializedRoot->GetBufferPointer(), serializedRoot->GetBufferSize(),
        IID_PPV_ARGS(&m_SmokeAppearanceSourceRootSignature)));
    ThrowIfFailed(m_SmokeAppearanceSourceRootSignature->SetName(
        L"Smoke.Appearance.Source.RootSignature"));

    Microsoft::WRL::ComPtr<ID3DBlob> shader;
    Microsoft::WRL::ComPtr<ID3DBlob> shaderErrors;
    const UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS |
        D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
    const HRESULT shaderResult = D3DCompileFromFile(
        L"Shaders/appearance_source_injection.hlsl", nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE, "InjectNormalizedSourceCS", "cs_5_1",
        compileFlags, 0, &shader, &shaderErrors);
    if (shaderErrors)
        OutputDebugStringA(static_cast<const char*>(shaderErrors->GetBufferPointer()));
    ThrowIfFailed(shaderResult);
    D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline = {};
    pipeline.pRootSignature = m_SmokeAppearanceSourceRootSignature.Get();
    pipeline.CS = { shader->GetBufferPointer(), shader->GetBufferSize() };
    ThrowIfFailed(m_Device->CreateComputePipelineState(
        &pipeline, IID_PPV_ARGS(&m_SmokeAppearanceSourcePSO)));
    ThrowIfFailed(m_SmokeAppearanceSourcePSO->SetName(L"Smoke.Appearance.Source"));

    const auto rateDescription = CD3DX12_RESOURCE_DESC::Buffer(
        m_SmokeAppearanceSourceRateBytes);
    const auto defaultHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    const auto uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    ThrowIfFailed(m_Device->CreateCommittedResource(
        &defaultHeap, D3D12_HEAP_FLAG_NONE, &rateDescription,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&m_SmokeAppearanceSourceRateBuffer)));
    ThrowIfFailed(m_Device->CreateCommittedResource(
        &uploadHeap, D3D12_HEAP_FLAG_NONE, &rateDescription,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&m_SmokeAppearanceSourceUploadBuffer)));
    void* upload = nullptr;
    const D3D12_RANGE noCpuRead = { 0, 0 };
    ThrowIfFailed(m_SmokeAppearanceSourceUploadBuffer->Map(0, &noCpuRead, &upload));
    std::memcpy(upload, m_SmokeAppearanceSourceRates.data(),
        static_cast<std::size_t>(m_SmokeAppearanceSourceRateBytes));
    const D3D12_RANGE written = {
        0, static_cast<SIZE_T>(m_SmokeAppearanceSourceRateBytes) };
    m_SmokeAppearanceSourceUploadBuffer->Unmap(0, &written);
    m_SmokeAppearanceSourceRateState = D3D12_RESOURCE_STATE_COPY_DEST;
    m_SmokeAppearanceSourceUploaded = false;

    D3D12_DESCRIPTOR_HEAP_DESC heapDescription = {};
    heapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDescription.NumDescriptors = SmokeAppearanceSourceDescriptorCount;
    heapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(m_Device->CreateDescriptorHeap(
        &heapDescription, IID_PPV_ARGS(&m_SmokeAppearanceSourceHeap)));
    D3D12_SHADER_RESOURCE_VIEW_DESC rateView = {};
    rateView.Format = DXGI_FORMAT_R32_TYPELESS;
    rateView.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    rateView.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    rateView.Buffer.FirstElement = 0;
    rateView.Buffer.NumElements = static_cast<UINT>(cellCount);
    rateView.Buffer.StructureByteStride = 0;
    rateView.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
    m_Device->CreateShaderResourceView(
        m_SmokeAppearanceSourceRateBuffer.Get(), &rateView,
        CpuDescriptor(m_SmokeAppearanceSourceHeap.Get(),
            SmokeAppearanceSourceSrvDescriptor, m_CbvSrvUavDescriptorSize));
    for (UINT scalarIndex = 0; scalarIndex < 2; ++scalarIndex)
    {
        const UINT first = scalarIndex == 0 ?
            SmokeAppearanceScalar0UavDescriptor :
            SmokeAppearanceScalar1UavDescriptor;
        SmokeGpuTexture* fields[2] = {
            &m_GpuDensity[scalarIndex], &m_GpuTemperature[scalarIndex] };
        for (UINT field = 0; field < 2; ++field)
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC view = {};
            view.Format = DXGI_FORMAT_R32_FLOAT;
            view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
            view.Texture3D.MipSlice = 0;
            view.Texture3D.FirstWSlice = 0;
            view.Texture3D.WSize = fields[field]->resource->GetDesc().DepthOrArraySize;
            m_Device->CreateUnorderedAccessView(
                fields[field]->resource.Get(), nullptr, &view,
                CpuDescriptor(m_SmokeAppearanceSourceHeap.Get(),
                    first + field, m_CbvSrvUavDescriptorSize));
        }
    }

    UINT64 auditOffset = 0;
    const auto densityDescription = m_GpuDensity[0].resource->GetDesc();
    for (auto& footprint : m_SmokeAppearanceSourceAuditFootprints)
    {
        m_Device->GetCopyableFootprints(
            &densityDescription, 0, 1, auditOffset,
            &footprint, nullptr, nullptr, nullptr);
        auditOffset = (footprint.Offset +
            static_cast<UINT64>(footprint.Footprint.RowPitch) *
            footprint.Footprint.Height * footprint.Footprint.Depth + 511u) &
            ~UINT64{ 511u };
    }
    m_SmokeAppearanceSourceAuditBytes = auditOffset;
    const auto auditDescription = CD3DX12_RESOURCE_DESC::Buffer(auditOffset);
    const auto readbackHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
    for (auto& slot : m_SmokeAppearanceSourceAuditReadbacks)
    {
        ThrowIfFailed(m_Device->CreateCommittedResource(
            &readbackHeap, D3D12_HEAP_FLAG_NONE, &auditDescription,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&slot.buffer)));
        slot.pending = false;
        slot.step = 0;
        slot.emit = false;
    }

    std::filesystem::create_directories(appearance.outputDirectory / "snapshots");
    std::filesystem::create_directories(appearance.outputDirectory / "source-audit");
    std::filesystem::copy_file(
        appearance.sourceRatePath, appearance.outputDirectory / "source-rate.f32",
        std::filesystem::copy_options::none);

    m_SmokeAppearanceSavedPressureIterations = m_SmokeGpuPressureIterations;
    m_SmokeAppearanceSavedBenchmarkIterations = m_SmokeGpuBenchmarkIterations;
    m_SmokeAppearanceConfig = appearance;
    if (m_SmokeAppearanceDeviceTrace)
    {
    Microsoft::WRL::ComPtr<ID3D12InfoQueue1> liveQueue;
    ThrowIfFailed(m_Device.As(&liveQueue));
    ThrowIfFailed(liveQueue->RegisterMessageCallback(AppearanceMessage,
        D3D12_MESSAGE_CALLBACK_FLAG_NONE,
        &m_SmokeAppearanceConfig.outputDirectory, &m_SmokeAppearanceMessageCookie));
    m_SmokeAppearanceMessageCallback = true;
    }
    m_SmokeAppearanceAutomatic = automatic;
    m_SmokeAppearanceFailures = 0;
    m_SmokeAppearanceSourceAuditIssued = false;
    m_SmokeAppearanceSourceAuditRecords.clear();
    m_SmokeAppearanceSnapshots.clear();

    m_FluidDemoMode = FluidDemoMode::Smoke3DGPU;
    m_SmokeGpuOpenTopEnabled = false;
    m_SmokeGpuSphereEnabled = false;
    m_SphereTranslationEnabled = false;
    m_SmokeGpuAdvectionMode = SmokeAdvectionMode::MacCormack;
    m_SmokeGpuLimiterMode = SmokeLimiterMode::Clamp;
    m_SmokeGpuVorticityEpsilon = 0.0f;
    m_SmokeGpuBenchmarkVorticityEpsilon = 0.0f;
    m_SmokeGpuBenchmarkLimiterMode = static_cast<int>(SmokeLimiterMode::Clamp);
    m_SmokeGpuBenchmarkIterations = appearance.pressureIterations;

    SmokeBenchmarkConfig benchmark;
    benchmark.implementation = "gpu_weighted_jacobi";
    benchmark.scenario = "appearance_coupled_scene_" + appearance.scene;
    benchmark.runLabel = "appearance_" + appearance.scene;
    benchmark.totalSteps = appearance.totalSteps;
    benchmark.emitterSteps = appearance.emitterSteps;
    benchmark.performanceWarmupSteps = 0;
    benchmark.timeStep = static_cast<double>(appearance.timeStep);
    benchmark.emitterCell = {
        resolution.x / 2, resolution.y / 4, resolution.z / 2 };
    benchmark.advectionMode = SmokeAdvectionMode::MacCormack;
    benchmark.limiterMode = SmokeLimiterMode::Clamp;
    benchmark.vorticityEpsilon = 0.0;
    benchmark.renderingEnabledDuringRun = false;
    benchmark.outputRoot = appearance.outputDirectory;
    m_SmokeGpuBenchmarkConfig = benchmark;

    SmokePhysicsParameters physics;
    physics.ambientTemperature = 0.0;
    physics.temperatureBuoyancy = 0.6;
    physics.smokeWeight = 0.05;
    physics.densityDissipation = 0.0;
    physics.temperatureCooling = 0.5;
    m_SmokeGpuBenchmarkPhysics = physics;

    m_SmokeGpuBenchmarkSamples.clear();
    m_SmokeGpuBenchmarkSamples.reserve(appearance.totalSteps);
    m_SmokeGpuBenchmarkSubmitted = 0;
    m_SmokeGpuBenchmarkStopping = false;
    m_SmokeGpuBenchmarkRunning = true;
    m_SmokeGpuResetRequested = true;
    m_SmokeGpuStepRequested = false;
    m_SmokeGpuPendingSteps = 0;
    m_SmokeGpuRestoreVolume = m_ShowSmokeVolume;
    m_ShowSmokeVolume = false;
    m_SmokeGpuPaused = false;
    m_SmokeAppearanceExperiment = true;
    m_SmokeGpuBenchmarkStatus =
        "Coupled appearance control: recording instrumented correctness data.";
    {
        std::ofstream phase(appearance.outputDirectory / "phase.txt");
        phase << "configured\n";
    }
}

void Renderer::DispatchSmokeAppearanceSource(
    ID3D12GraphicsCommandList* commandList,
    const SmokeBindingConstants& constants,
    UINT scalarIndex,
    bool emit)
{
    if (!m_SmokeAppearanceExperiment || scalarIndex > 1)
        throw std::runtime_error("Invalid appearance source dispatch state");

    auto transition = [&](SmokeGpuTexture& texture, D3D12_RESOURCE_STATES target)
    {
        if (texture.state == target) return;
        const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            texture.resource.Get(), texture.state, target);
        commandList->ResourceBarrier(1, &barrier);
        texture.state = target;
    };
    auto copyTexture = [&](SmokeGpuTexture& texture,
        ID3D12Resource* destinationBuffer,
        const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& destinationFootprint)
    {
        transition(texture, D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION source = {};
        source.pResource = texture.resource.Get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION destination = {};
        destination.pResource = destinationBuffer;
        destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        destination.PlacedFootprint = destinationFootprint;
        commandList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        transition(texture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    };

    if (!m_SmokeAppearanceSourceUploaded)
    {
        commandList->CopyBufferRegion(
            m_SmokeAppearanceSourceRateBuffer.Get(), 0,
            m_SmokeAppearanceSourceUploadBuffer.Get(), 0,
            m_SmokeAppearanceSourceRateBytes);
        const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            m_SmokeAppearanceSourceRateBuffer.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        commandList->ResourceBarrier(1, &barrier);
        m_SmokeAppearanceSourceRateState =
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        m_SmokeAppearanceSourceUploaded = true;
    }

    const unsigned step = m_SmokeGpuBenchmarkSubmitted + 1;
    if (step == 1)
    {
        std::ofstream phase(
            m_SmokeAppearanceConfig.outputDirectory / "phase.txt", std::ios::app);
        phase << "step-1-source-command-recorded\n";
    }
    const bool auditStep = step == 1 || (m_OfflinePressureMode ?
        (step == m_SmokeAppearanceConfig.emitterSteps || step == m_SmokeAppearanceConfig.emitterSteps + 1) :
        (step == 120 || step == 121));
    // Temperature normally remains a UAV between steps. Order the prior scalar
    // advection write before this in-place source update; audit-copy transitions
    // must not be the only reason the registered audit steps are ordered.
    const auto priorScalarWriteBarrier = CD3DX12_RESOURCE_BARRIER::UAV(nullptr);
    commandList->ResourceBarrier(1, &priorScalarWriteBarrier);
    auto& auditSlot = m_SmokeAppearanceSourceAuditReadbacks[
        m_CurrentFrameResourceIndex];
    if (auditStep)
    {
        if (auditSlot.pending)
            throw std::runtime_error("Appearance source audit readback was overwritten");
        copyTexture(m_GpuDensity[scalarIndex], auditSlot.buffer.Get(),
            m_SmokeAppearanceSourceAuditFootprints[0]);
        copyTexture(m_GpuTemperature[scalarIndex], auditSlot.buffer.Get(),
            m_SmokeAppearanceSourceAuditFootprints[1]);
    }

    // The benchmark readback leaves the current density in COPY_SOURCE at the
    // end of every preceding step. Both outputs must be UAVs even on non-audit
    // steps and even when emission is disabled.
    transition(m_GpuDensity[scalarIndex],
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    transition(m_GpuTemperature[scalarIndex],
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    ID3D12DescriptorHeap* sourceHeaps[] = {
        m_SmokeAppearanceSourceHeap.Get() };
    commandList->SetDescriptorHeaps(1, sourceHeaps);
    commandList->SetComputeRootSignature(
        m_SmokeAppearanceSourceRootSignature.Get());
    const std::uint32_t sourceConstants[4] = {
        constants.gridResolution[0], constants.gridResolution[1],
        constants.gridResolution[2], std::bit_cast<std::uint32_t>(constants.dt) };
    commandList->SetComputeRoot32BitConstants(0, 4, sourceConstants, 0);
    commandList->SetComputeRootDescriptorTable(
        1, GpuDescriptor(m_SmokeAppearanceSourceHeap.Get(),
            SmokeAppearanceSourceSrvDescriptor, m_CbvSrvUavDescriptorSize));
    const UINT outputDescriptor = scalarIndex == 0 ?
        SmokeAppearanceScalar0UavDescriptor :
        SmokeAppearanceScalar1UavDescriptor;
    commandList->SetComputeRootDescriptorTable(
        2, GpuDescriptor(m_SmokeAppearanceSourceHeap.Get(),
            outputDescriptor, m_CbvSrvUavDescriptorSize));
    commandList->SetPipelineState(m_SmokeAppearanceSourcePSO.Get());
    if (emit)
    {
        commandList->Dispatch(
            (constants.gridResolution[0] + 7) / 8,
            (constants.gridResolution[1] + 7) / 8,
            (constants.gridResolution[2] + 3) / 4);
    }
    const auto writeBarrier = CD3DX12_RESOURCE_BARRIER::UAV(nullptr);
    commandList->ResourceBarrier(1, &writeBarrier);

    if (auditStep)
    {
        copyTexture(m_GpuDensity[scalarIndex], auditSlot.buffer.Get(),
            m_SmokeAppearanceSourceAuditFootprints[2]);
        copyTexture(m_GpuTemperature[scalarIndex], auditSlot.buffer.Get(),
            m_SmokeAppearanceSourceAuditFootprints[3]);
        auditSlot.pending = true;
        auditSlot.step = step;
        auditSlot.emit = emit;
        m_SmokeAppearanceSourceAuditIssued = true;
    }

    // Changing descriptor heaps invalidates descriptor-table bindings. Restore
    // the production root state needed by the stages following source injection.
    ID3D12DescriptorHeap* mainHeaps[] = { m_SmokeGpuDescriptorHeap.Get() };
    commandList->SetDescriptorHeaps(1, mainHeaps);
    commandList->SetComputeRootSignature(m_SmokeBindingRootSignature.Get());
    commandList->SetComputeRoot32BitConstants(
        SmokeBindingConstantsRoot, SmokeConstantCount, &constants, 0);
    commandList->SetComputeRootDescriptorTable(
        SmokeBindingPressureReadInputRoot, m_GpuPressure[0].uav);
}

void Renderer::CaptureSmokePressureTriage(ID3D12GraphicsCommandList* list,
    SmokeGpuTexture& texture, unsigned field)
{
    if (field >= 16 || m_SmokePressureTriageCaptured[field])
        throw std::runtime_error("Pressure triage field overwritten");
    if (!m_SmokePressureTriageReadback)
    {
        UINT64 offset = 0;
        for (unsigned index = 0; index < 16; ++index)
        {
            SmokeGpuTexture* t = &m_GpuPressure[0];
            if (index == 1 || index == 13) t = &m_GpuU[0];
            if (index == 2 || index == 14) t = &m_GpuV[0];
            if (index == 3 || index == 15) t = &m_GpuW[0];
            const auto desc = t->resource->GetDesc();
            auto& fp = m_SmokePressureTriageFootprints[index];
            m_Device->GetCopyableFootprints(&desc, 0, 1, offset, &fp, nullptr, nullptr, nullptr);
            offset = (fp.Offset + UINT64(fp.Footprint.RowPitch) * fp.Footprint.Height *
                fp.Footprint.Depth + 511) & ~UINT64(511);
        }
        const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
        const auto desc = CD3DX12_RESOURCE_DESC::Buffer(offset);
        ThrowIfFailed(m_Device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
            &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&m_SmokePressureTriageReadback)));
    }
    const auto previous = texture.state;
    if (previous != D3D12_RESOURCE_STATE_COPY_SOURCE)
    {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(texture.resource.Get(),
            previous, D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->ResourceBarrier(1, &barrier);
    }
    D3D12_TEXTURE_COPY_LOCATION source{}, destination{};
    source.pResource = texture.resource.Get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destination.pResource = m_SmokePressureTriageReadback.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = m_SmokePressureTriageFootprints[field];
    list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    if (previous != D3D12_RESOURCE_STATE_COPY_SOURCE)
    {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(texture.resource.Get(),
            D3D12_RESOURCE_STATE_COPY_SOURCE, previous);
        list->ResourceBarrier(1, &barrier);
    }
    m_SmokePressureTriageCaptured[field] = true;
}

void Renderer::SaveSmokePressureTriage(unsigned step)
{
    if (!m_SmokePressureTriageStep || step != m_SmokePressureTriageStep) return;
    // Called by the existing readback collector only after this step's fence.
    for (bool captured : m_SmokePressureTriageCaptured)
        if (!captured) throw std::runtime_error("Pressure triage field missing");
    const auto root = m_SmokeAppearanceConfig.outputDirectory / "pressure-triage";
    std::filesystem::create_directories(root);
    const char* names[] = {"div_before", "u_before", "v_before", "w_before",
        "p00000", "p00256", "p01024", "p04096", "p08192", "p16384", "p32768",
        "p65536", "div_after", "u_after", "v_after", "w_after"};
    void* mapped = nullptr;
    const D3D12_RANGE range{0, static_cast<SIZE_T>(m_SmokePressureTriageReadback->GetDesc().Width)};
    ThrowIfFailed(m_SmokePressureTriageReadback->Map(0, &range, &mapped));
    for (unsigned field = 0; field < 16; ++field)
    {
        const auto& fp = m_SmokePressureTriageFootprints[field];
        std::ofstream file(root / (std::string(names[field]) + ".f32"), std::ios::binary);
        file.exceptions(std::ios::failbit | std::ios::badbit);
        for (UINT z = 0; z < fp.Footprint.Depth; ++z)
            for (UINT y = 0; y < fp.Footprint.Height; ++y)
                file.write(static_cast<const char*>(mapped) + fp.Offset +
                    (UINT64(z) * fp.Footprint.Height + y) * fp.Footprint.RowPitch,
                    fp.Footprint.Width * sizeof(float));
    }
    const D3D12_RANGE writes{0, 0};
    m_SmokePressureTriageReadback->Unmap(0, &writes);
    std::ofstream metadata(root / "capture.json");
    metadata.exceptions(std::ios::failbit | std::ios::badbit);
    metadata << "{\"schema\":1,\"step\":" << step
        << ",\"iterations\":65536,\"field_count\":16,\"restores_resource_states\":true}\n";
}

void Renderer::RecordSmokeAppearanceStep(const void* mapped, unsigned step)
{
    if (!m_SmokeAppearanceExperiment) return;
    SaveSmokePressureTriage(step);
    if (step == 1)
    {
        std::ofstream phase(
            m_SmokeAppearanceConfig.outputDirectory / "phase.txt", std::ios::app);
        phase << "step-1-readback-complete\n";
    }
    const auto resolution = m_SmokeSolver.Density().Resolution();
    const double cellVolume = 1.0 /
        static_cast<double>(resolution.x * resolution.y * resolution.z);
    auto saveSnapshot = [&](unsigned snapshotStep, const PackedField& field)
    {
        for (const auto& existing : m_SmokeAppearanceSnapshots)
            if (existing.step == snapshotStep) return;
        std::ostringstream name;
        name << "density-step-" << std::setw(3) << std::setfill('0')
            << snapshotStep << ".f32";
        const auto relative = std::filesystem::path("snapshots") / name.str();
        WritePackedField(m_SmokeAppearanceConfig.outputDirectory / relative, field);
        SmokeAppearanceSnapshotRecord record;
        record.step = snapshotStep;
        record.simulationTime =
            snapshotStep * static_cast<double>(m_SmokeAppearanceConfig.timeStep);
        record.densitySum = field.sum;
        record.densityIntegral = field.sum * cellVolume;
        record.densityMinimum = field.minimum;
        record.densityMaximum = field.maximum;
        record.nonfinite = field.nonfinite;
        record.negative = field.negative;
        record.hash = field.hash;
        record.file = relative;
        m_SmokeAppearanceSnapshots.push_back(record);
        if (field.nonfinite != 0 || field.negative != 0)
            ++m_SmokeAppearanceFailures;
    };

    auto& auditSlot = m_SmokeAppearanceSourceAuditReadbacks[
        m_CurrentFrameResourceIndex];
    if (auditSlot.pending)
    {
        if (auditSlot.step != step)
        {
            ++m_SmokeAppearanceFailures;
        }
        void* auditMapped = nullptr;
        const D3D12_RANGE reads = {
            0, static_cast<SIZE_T>(m_SmokeAppearanceSourceAuditBytes) };
        ThrowIfFailed(auditSlot.buffer->Map(0, &reads, &auditMapped));
        const PackedField densityBefore = PackField(
            auditMapped, m_SmokeAppearanceSourceAuditFootprints[0]);
        const PackedField temperatureBefore = PackField(
            auditMapped, m_SmokeAppearanceSourceAuditFootprints[1]);
        const PackedField densityAfter = PackField(
            auditMapped, m_SmokeAppearanceSourceAuditFootprints[2]);
        const PackedField temperatureAfter = PackField(
            auditMapped, m_SmokeAppearanceSourceAuditFootprints[3]);

        const std::string stepTag = [&]
        {
            std::ostringstream value;
            value << "step-" << std::setw(3) << std::setfill('0') << step;
            return value.str();
        }();
        const auto auditRoot = m_SmokeAppearanceConfig.outputDirectory /
            "source-audit";
        WritePackedField(auditRoot / (stepTag + "-density-before.f32"),
            densityBefore);
        WritePackedField(auditRoot / (stepTag + "-temperature-before.f32"),
            temperatureBefore);
        WritePackedField(auditRoot / (stepTag + "-density-after.f32"),
            densityAfter);
        WritePackedField(auditRoot / (stepTag + "-temperature-after.f32"),
            temperatureAfter);

        SmokeAppearanceSourceAuditRecord record;
        record.step = step;
        record.emit = auditSlot.emit;
        record.densityBefore = densityBefore.sum * cellVolume;
        record.densityAfter = densityAfter.sum * cellVolume;
        record.temperatureBefore = temperatureBefore.sum * cellVolume;
        record.temperatureAfter = temperatureAfter.sum * cellVolume;
        record.densityIncrement =
            (densityAfter.sum - densityBefore.sum) * cellVolume;
        record.temperatureIncrement =
            (temperatureAfter.sum - temperatureBefore.sum) * cellVolume;
        record.densityNonfiniteCells =
            densityBefore.nonfinite + densityAfter.nonfinite;
        record.temperatureNonfiniteCells =
            temperatureBefore.nonfinite + temperatureAfter.nonfinite;
        record.densityNegativeCells =
            densityBefore.negative + densityAfter.negative;
        record.temperatureNegativeCells =
            temperatureBefore.negative + temperatureAfter.negative;
        record.densityPreHash = densityBefore.hash;
        record.densityPostHash = densityAfter.hash;
        record.temperaturePreHash = temperatureBefore.hash;
        record.temperaturePostHash = temperatureAfter.hash;
        record.scheduleMatches =
            auditSlot.emit == (step <= m_SmokeAppearanceConfig.emitterSteps);

        double expectedDensityIncrementSum = 0.0;
        double expectedTemperatureIncrementSum = 0.0;
        std::vector<float> expectedDensityValues;
        std::vector<float> expectedTemperatureValues;
        expectedDensityValues.reserve(m_SmokeAppearanceSourceRates.size());
        expectedTemperatureValues.reserve(m_SmokeAppearanceSourceRates.size());
        if (densityBefore.values.size() != m_SmokeAppearanceSourceRates.size() ||
            temperatureBefore.values.size() != m_SmokeAppearanceSourceRates.size() ||
            densityAfter.values.size() != m_SmokeAppearanceSourceRates.size() ||
            temperatureAfter.values.size() != m_SmokeAppearanceSourceRates.size())
        {
            throw std::runtime_error("Appearance source audit field size mismatch");
        }
        for (std::size_t index = 0;
            index < m_SmokeAppearanceSourceRates.size(); ++index)
        {
            volatile float increment = auditSlot.emit ?
                m_SmokeAppearanceSourceRates[index] *
                    m_SmokeAppearanceConfig.timeStep : 0.0f;
            volatile float expectedDensity =
                densityBefore.values[index] + increment;
            volatile float expectedTemperature =
                temperatureBefore.values[index] + increment;
            const float expectedDensityValue = static_cast<float>(expectedDensity);
            const float expectedTemperatureValue = static_cast<float>(expectedTemperature);
            expectedDensityValues.push_back(expectedDensityValue);
            expectedTemperatureValues.push_back(expectedTemperatureValue);
            if (densityBefore.values[index] == 0.0f)
                ++record.densityPreZeroCells;
            if (temperatureBefore.values[index] == 0.0f)
                ++record.temperaturePreZeroCells;
            if (std::bit_cast<std::uint32_t>(densityAfter.values[index]) !=
                std::bit_cast<std::uint32_t>(expectedDensityValue))
                ++record.densityMismatchCells;
            if (std::bit_cast<std::uint32_t>(temperatureAfter.values[index]) !=
                std::bit_cast<std::uint32_t>(expectedTemperatureValue))
                ++record.temperatureMismatchCells;
            expectedDensityIncrementSum += static_cast<double>(
                expectedDensityValue - densityBefore.values[index]);
            expectedTemperatureIncrementSum += static_cast<double>(
                expectedTemperatureValue - temperatureBefore.values[index]);
        }
        record.densityExpectedHash = HashBytes(
            expectedDensityValues.data(), expectedDensityValues.size() * sizeof(float));
        record.temperatureExpectedHash = HashBytes(
            expectedTemperatureValues.data(), expectedTemperatureValues.size() * sizeof(float));
        record.expectedDensityIncrement =
            expectedDensityIncrementSum * cellVolume;
        record.expectedTemperatureIncrement =
            expectedTemperatureIncrementSum * cellVolume;
        if (record.densityMismatchCells || record.temperatureMismatchCells ||
            record.densityNonfiniteCells || record.temperatureNonfiniteCells ||
            record.densityNegativeCells || record.temperatureNegativeCells ||
            !record.scheduleMatches)
            ++m_SmokeAppearanceFailures;
        m_SmokeAppearanceSourceAuditRecords.push_back(record);
        if (step == 1) saveSnapshot(0, densityBefore);

        const D3D12_RANGE noWrites = { 0, 0 };
        auditSlot.buffer->Unmap(0, &noWrites);
        auditSlot.pending = false;
    }

    if (step % m_SmokeAppearanceConfig.snapshotStride == 0 ||
        step == m_SmokeAppearanceConfig.totalSteps)
        saveSnapshot(step, PackField(mapped, m_SmokeGpuReadbackFootprint));
}

void Renderer::SaveSmokeAppearanceExperiment()
{
    TraceSmokeAppearanceDevice("save-after-final-readback");
    if (m_SmokeAppearanceMessageCallback)
    {
        Microsoft::WRL::ComPtr<ID3D12InfoQueue1> liveQueue;
        ThrowIfFailed(m_Device.As(&liveQueue));
        ThrowIfFailed(liveQueue->UnregisterMessageCallback(m_SmokeAppearanceMessageCookie));
        m_SmokeAppearanceMessageCallback = false;
    }
    auto& samples = m_SmokeGpuBenchmarkSamples;
    std::sort(samples.begin(), samples.end(),
        [](const auto& left, const auto& right) { return left.step < right.step; });
    std::sort(m_SmokeAppearanceSourceAuditRecords.begin(),
        m_SmokeAppearanceSourceAuditRecords.end(),
        [](const auto& left, const auto& right) { return left.step < right.step; });
    std::sort(m_SmokeAppearanceSnapshots.begin(), m_SmokeAppearanceSnapshots.end(),
        [](const auto& left, const auto& right) { return left.step < right.step; });

    const auto resolution = m_SmokeSolver.Density().Resolution();
    const auto spacing = m_SmokeSolver.Density().GridSpacing();
    const auto origin = m_SmokeSolver.Density().Origin();
    const double cellVolume = spacing.x * spacing.y * spacing.z;
    const auto& output = m_SmokeAppearanceConfig.outputDirectory;

    Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue;
    ThrowIfFailed(m_Device.As(&infoQueue));
    const UINT64 debugMessageCount = infoQueue->GetNumStoredMessages();
    const UINT64 discardedTotal =
        infoQueue->GetNumMessagesDiscardedByMessageCountLimit();
    const bool discardedCounterValid =
        discardedTotal >= m_SmokeAppearanceDiscardedMessageBaseline;
    const UINT64 debugDiscardedCount = discardedCounterValid ?
        discardedTotal - m_SmokeAppearanceDiscardedMessageBaseline : 1;
    unsigned debugErrorCount = 0;
    {
        std::ofstream messages(output / "debug-messages.txt");
        messages.exceptions(std::ios::failbit | std::ios::badbit);
        for (UINT64 index = 0; index < debugMessageCount; ++index)
        {
            SIZE_T messageBytes = 0;
            ThrowIfFailed(infoQueue->GetMessage(index, nullptr, &messageBytes));
            std::vector<std::byte> storage(messageBytes);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            ThrowIfFailed(infoQueue->GetMessage(index, message, &messageBytes));
            messages << "severity=" << static_cast<unsigned>(message->Severity)
                << " id=" << static_cast<unsigned>(message->ID) << '\n'
                << (message->pDescription ? message->pDescription : "") << "\n\n";
            if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR)
                ++debugErrorCount;
        }
    }
    {
        std::ofstream status(output / "debug-status.txt");
        status.exceptions(std::ios::failbit | std::ios::badbit);
        status << "enabled; messages=" << debugMessageCount
            << "; errors=" << debugErrorCount
            << "; discarded=" << debugDiscardedCount << '\n';
    }
    if (debugErrorCount != 0 || debugDiscardedCount != 0 ||
        !discardedCounterValid)
        ++m_SmokeAppearanceFailures;

    {
        std::ofstream csv(output / "steps.csv");
        csv.exceptions(std::ios::failbit | std::ios::badbit);
        csv << std::setprecision(17);
        csv << "step,simulation_time_s,emit,pressure_iterations,solver_gpu_ms,"
            "source_gpu_ms,velocity_advection_gpu_ms,buoyancy_gpu_ms,"
            "divergence_gpu_ms,pressure_clear_gpu_ms,pressure_solve_gpu_ms,"
            "pressure_gradient_gpu_ms,scalar_advection_gpu_ms,diagnostics_gpu_ms,"
            "rms_divergence_before,max_abs_divergence_before,"
            "rms_divergence_after,max_abs_divergence_after,relative_residual,"
            "scaled_max_divergence,kinetic_energy,velocity_rms,velocity_max,"
            "nonfinite_divergence_before,nonfinite_divergence_after,"
            "nonfinite_velocity,density_min,density_max,density_sum,"
            "density_integral,density_centre_x,density_centre_y,density_centre_z,"
            "nonfinite_density_cells,density_fnv1a64,configuration_fnv1a64\n";
        for (const auto& sample : samples)
        {
            const double relativeResidual = sample.divergenceBeforeRms > 0.0 ?
                sample.divergenceAfterRms / sample.divergenceBeforeRms :
                std::numeric_limits<double>::quiet_NaN();
            csv << sample.step << ','
                << sample.step * static_cast<double>(m_SmokeAppearanceConfig.timeStep)
                << ',' << (sample.emit ? 1 : 0) << ',' << sample.pressureIterations
                << ',' << sample.milliseconds << ',' << sample.sourceMilliseconds
                << ',' << sample.velocityAdvectionMilliseconds
                << ',' << sample.buoyancyMilliseconds
                << ',' << sample.divergenceMilliseconds
                << ',' << sample.pressureClearMilliseconds
                << ',' << sample.pressureMilliseconds
                << ',' << sample.pressureGradientMilliseconds
                << ',' << sample.scalarAdvectionMilliseconds
                << ',' << sample.diagnosticsMilliseconds
                << ',' << sample.divergenceBeforeRms
                << ',' << sample.divergenceBeforeMax
                << ',' << sample.divergenceAfterRms
                << ',' << sample.divergenceAfterMax
                << ',' << relativeResidual
                << ',' << m_SmokeAppearanceConfig.timeStep * sample.divergenceAfterMax
                << ',' << sample.kineticEnergy << ',' << sample.rmsSpeed
                << ',' << sample.maxSpeed
                << ',' << sample.divergenceBeforeNonfinite
                << ',' << sample.divergenceAfterNonfinite
                << ',' << sample.velocityNonfinite
                << ',' << sample.densityMin << ',' << sample.densityMax
                << ',' << sample.densitySum << ',' << sample.densitySum * cellVolume
                << ',' << sample.centre.x << ',' << sample.centre.y
                << ',' << sample.centre.z << ',' << sample.nonfinite
                << ',' << std::quoted(std::to_string(sample.densityHash))
                << ',' << std::quoted(std::to_string(
                    m_SmokeAppearanceConfigurationHash)) << '\n';
        }
    }

    {
        std::ofstream csv(output / "source-audit.csv");
        csv.exceptions(std::ios::failbit | std::ios::badbit);
        csv << std::setprecision(17);
        csv << "step,emit,density_before_integral,density_after_integral,"
            "temperature_before_integral,temperature_after_integral,"
            "expected_density_increment,expected_temperature_increment,"
            "density_increment,temperature_increment,"
            "density_mismatch_cells,temperature_mismatch_cells,"
            "density_nonfinite_cells,temperature_nonfinite_cells,"
            "density_negative_cells,temperature_negative_cells,"
            "density_pre_zero_cells,temperature_pre_zero_cells,"
            "density_pre_fnv1a64,density_post_fnv1a64,density_expected_fnv1a64,"
            "temperature_pre_fnv1a64,temperature_post_fnv1a64,"
            "temperature_expected_fnv1a64,schedule_matches,nonfinite,passed\n";
        for (const auto& record : m_SmokeAppearanceSourceAuditRecords)
        {
            const bool passed = record.densityMismatchCells == 0 &&
                record.temperatureMismatchCells == 0 &&
                record.densityNonfiniteCells == 0 &&
                record.temperatureNonfiniteCells == 0 &&
                record.densityNegativeCells == 0 &&
                record.temperatureNegativeCells == 0 && record.scheduleMatches;
            csv << record.step << ',' << (record.emit ? 1 : 0)
                << ',' << record.densityBefore << ',' << record.densityAfter
                << ',' << record.temperatureBefore << ',' << record.temperatureAfter
                << ',' << record.expectedDensityIncrement
                << ',' << record.expectedTemperatureIncrement
                << ',' << record.densityIncrement
                << ',' << record.temperatureIncrement
                << ',' << record.densityMismatchCells
                << ',' << record.temperatureMismatchCells
                << ',' << record.densityNonfiniteCells
                << ',' << record.temperatureNonfiniteCells
                << ',' << record.densityNegativeCells
                << ',' << record.temperatureNegativeCells
                << ',' << record.densityPreZeroCells
                << ',' << record.temperaturePreZeroCells
                << ',' << std::quoted(std::to_string(record.densityPreHash))
                << ',' << std::quoted(std::to_string(record.densityPostHash))
                << ',' << std::quoted(std::to_string(record.densityExpectedHash))
                << ',' << std::quoted(std::to_string(record.temperaturePreHash))
                << ',' << std::quoted(std::to_string(record.temperaturePostHash))
                << ',' << std::quoted(std::to_string(record.temperatureExpectedHash))
                << ',' << (record.scheduleMatches ? 1 : 0)
                << ',' << (record.densityNonfiniteCells + record.temperatureNonfiniteCells)
                << ',' << (passed ? 1 : 0) << '\n';
        }
    }

    {
        std::ofstream csv(output / "snapshots.csv");
        csv.exceptions(std::ios::failbit | std::ios::badbit);
        csv << std::setprecision(17);
        csv << "step,simulation_time_s,file,fnv1a64,density_sum,"
            "density_integral,density_min,density_max,nonfinite,negative\n";
        for (const auto& snapshot : m_SmokeAppearanceSnapshots)
            csv << snapshot.step << ',' << snapshot.simulationTime << ','
                << std::quoted(snapshot.file.generic_string()) << ','
                << std::quoted(std::to_string(snapshot.hash)) << ','
                << snapshot.densitySum << ',' << snapshot.densityIntegral << ','
                << snapshot.densityMinimum << ',' << snapshot.densityMaximum << ','
                << snapshot.nonfinite << ',' << snapshot.negative << '\n';
    }

    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    ThrowIfFailed(m_DxgiFactory->EnumAdapterByLuid(
        m_Device->GetAdapterLuid(), IID_PPV_ARGS(&adapter)));
    DXGI_ADAPTER_DESC adapterDescription = {};
    ThrowIfFailed(adapter->GetDesc(&adapterDescription));
    LARGE_INTEGER driverVersion = {};
    adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driverVersion);
    const std::string adapterName = Utf8(adapterDescription.Description);

    unsigned expectedAuditCount =
        1u + (m_SmokeAppearanceConfig.totalSteps >= 120 ? 1u : 0u) +
        (m_SmokeAppearanceConfig.totalSteps >= 121 ? 1u : 0u);
    if (m_OfflinePressureMode)
    {
        expectedAuditCount = 0;
        for (unsigned step = 1; step <= m_SmokeAppearanceConfig.totalSteps; ++step)
            if (step == 1 || step == m_SmokeAppearanceConfig.emitterSteps ||
                step == m_SmokeAppearanceConfig.emitterSteps + 1) ++expectedAuditCount;
    }
    const unsigned expectedSnapshotCount = 1u +
        m_SmokeAppearanceConfig.totalSteps /
            m_SmokeAppearanceConfig.snapshotStride +
        (m_SmokeAppearanceConfig.totalSteps %
            m_SmokeAppearanceConfig.snapshotStride ? 1u : 0u);
    if (samples.size() != m_SmokeAppearanceConfig.totalSteps ||
        m_SmokeAppearanceSourceAuditRecords.size() != expectedAuditCount ||
        m_SmokeAppearanceSnapshots.size() != expectedSnapshotCount)
        ++m_SmokeAppearanceFailures;

    {
        std::ofstream json(output / "config.json");
        json.exceptions(std::ios::failbit | std::ios::badbit);
        json << std::setprecision(17)
            << "{\n"
            << "  \"schema_version\": 2,\n"
            << "  \"experiment\": \"coupled_appearance_bridge_v1\",\n"
            << "  \"scene\": \"" << JsonEscape(m_SmokeAppearanceConfig.scene) << "\",\n"
            << "  \"resolution\": " << resolution.x << ",\n"
            << "  \"grid_spacing\": [" << spacing.x << ',' << spacing.y << ',' << spacing.z << "],\n"
            << "  \"origin\": [" << origin.x << ',' << origin.y << ',' << origin.z << "],\n"
            << "  \"dt\": " << m_SmokeAppearanceConfig.timeStep << ",\n"
            << "  \"total_steps\": " << m_SmokeAppearanceConfig.totalSteps << ",\n"
            << "  \"emitter_steps\": " << m_SmokeAppearanceConfig.emitterSteps << ",\n"
            << "  \"snapshot_stride\": " << m_SmokeAppearanceConfig.snapshotStride << ",\n"
            << "  \"pressure_iterations\": " << m_SmokeAppearanceConfig.pressureIterations << ",\n"
            << "  \"offline_pressure_mode\": " << m_OfflinePressureMode << ",\n"
            << "  \"advection\": \"maccormack\",\n"
            << "  \"limiter\": \"clamp\",\n"
            << "  \"closed_domain\": true,\n"
            << "  \"obstacle\": false,\n"
            << "  \"vorticity_confinement\": 0,\n"
            << "  \"density_dissipation_per_s\": 0,\n"
            << "  \"temperature_cooling_per_s\": 0.5,\n"
            << "  \"ambient_temperature\": 0,\n"
            << "  \"temperature_buoyancy\": 0.6,\n"
            << "  \"smoke_weight\": 0.05,\n"
            << "  \"source_count\": " << m_SmokeAppearanceConfig.sourceCount << ",\n"
            << "  \"target_integrated_source_rate\": " << m_SmokeAppearanceConfig.targetIntegratedRate << ",\n"
            << "  \"stored_integrated_source_rate\": " << m_SmokeAppearanceStoredRateIntegral << ",\n"
            << "  \"source_rate_fnv1a64\": \"" << m_SmokeAppearanceSourceRateHash << "\",\n"
            << "  \"configuration_identity\": \""
            << JsonEscape(m_SmokeAppearanceConfigurationIdentity) << "\",\n"
            << "  \"configuration_fnv1a64\": \""
            << m_SmokeAppearanceConfigurationHash << "\",\n"
            << "  \"source_shader_flags\": \"strict|debug|skip_optimization\",\n"
            << "  \"production_solver_shader_flags\": \"strict|debug|skip_optimization\",\n"
            << "  \"instrumented_timing\": true,\n"
            << "  \"device_trace_enabled\": "
            << (m_SmokeAppearanceDeviceTrace ? "true" : "false") << ",\n"
            << "  \"d3d12_debug_layer_enabled\": true,\n"
            << "  \"d3d12_message_count\": " << debugMessageCount << ",\n"
            << "  \"d3d12_error_count\": " << debugErrorCount << ",\n"
            << "  \"d3d12_discarded_message_count\": " << debugDiscardedCount << ",\n"
            << "  \"gpu\": \"" << JsonEscape(adapterName) << "\",\n"
            << "  \"driver_version_raw\": " << driverVersion.QuadPart << ",\n"
            << "  \"recorded_steps\": " << samples.size() << ",\n"
            << "  \"source_audit_records\": " << m_SmokeAppearanceSourceAuditRecords.size() << ",\n"
            << "  \"snapshots\": " << m_SmokeAppearanceSnapshots.size() << ",\n"
            << "  \"validation_failures\": " << m_SmokeAppearanceFailures << ",\n"
            << "  \"scope\": \"coupled numerical bridge; no G1 calculation or performance claim\"\n"
            << "}\n";
    }

    {
        std::ofstream complete(output / "complete.txt");
        complete.exceptions(std::ios::failbit | std::ios::badbit);
        complete << (m_SmokeAppearanceFailures == 0 ? "PASS" : "FAIL")
            << "\nvalidation_failures=" << m_SmokeAppearanceFailures
            << "\nd3d12_errors=" << debugErrorCount
            << "\nd3d12_discarded_messages=" << debugDiscardedCount << '\n';
    }
    m_SmokeGpuPressureIterations = m_SmokeAppearanceSavedPressureIterations;
    m_SmokeGpuBenchmarkIterations = m_SmokeAppearanceSavedBenchmarkIterations;
    m_SmokeGpuBenchmarkStatus = m_SmokeAppearanceFailures == 0 ?
        "Coupled appearance control saved." :
        "Coupled appearance control saved with validation failures.";
}

void Renderer::TraceSmokeAppearanceDevice(const char* phase)
{
    if (!m_SmokeAppearanceExperiment || !m_SmokeAppearanceDeviceTrace) return;
    const HRESULT reason = m_Device->GetDeviceRemovedReason();
    std::ofstream log(m_SmokeAppearanceConfig.outputDirectory / "device-lifecycle.txt",
        std::ios::app);
    log << phase << " submitted=" << m_SmokeGpuBenchmarkSubmitted
        << " device_reason=0x" << std::hex << static_cast<unsigned>(reason)
        << std::dec << " completed_fence=" << m_Fence->GetCompletedValue() << std::endl;
    if (SUCCEEDED(reason)) return;
    Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedData1> dred;
    const HRESULT query = m_Device.As(&dred);
    log << "dred_query=0x" << std::hex << static_cast<unsigned>(query) << std::endl;
    if (SUCCEEDED(query))
    {
        D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs{};
        const HRESULT bh = dred->GetAutoBreadcrumbsOutput1(&breadcrumbs);
        log << "breadcrumbs_hr=0x" << static_cast<unsigned>(bh) << std::endl;
        if (SUCCEEDED(bh))
            for (auto* node = breadcrumbs.pHeadAutoBreadcrumbNode; node; node = node->pNext)
                log << "command_list=" << node->pCommandList << " count=" << std::dec
                    << node->BreadcrumbCount << " completed="
                    << (node->pLastBreadcrumbValue ? *node->pLastBreadcrumbValue : 0)
                    << std::endl;
        D3D12_DRED_PAGE_FAULT_OUTPUT1 fault{};
        const HRESULT fh = dred->GetPageFaultAllocationOutput1(&fault);
        log << "page_fault_hr=0x" << std::hex << static_cast<unsigned>(fh)
            << " gpu_va=0x" << fault.PageFaultVA << std::endl;
    }
    ThrowIfFailed(reason);
}
