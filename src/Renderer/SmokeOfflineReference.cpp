#include "Renderer.h"
#include "OfflineResponseMarker.h"
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <thread>

void Renderer::ExchangeOfflinePressure(ID3D12GraphicsCommandList* list, bool after)
{
    if (!m_SmokeAppearanceExperiment || !m_OfflinePressureMode)
        throw std::runtime_error("Offline exchange outside appearance run");
    SmokeGpuTexture* textures[] = {&m_GpuDivergence,
        &m_GpuU[m_GpuVelocityReadIndex], &m_GpuV[m_GpuVelocityReadIndex], &m_GpuW[m_GpuVelocityReadIndex],
        &m_GpuPressure[0], &m_GpuDivergence,
        &m_GpuU[m_GpuVelocityReadIndex], &m_GpuV[m_GpuVelocityReadIndex], &m_GpuW[m_GpuVelocityReadIndex]};
    const char* names[] = {"div_before", "u_before", "v_before", "w_before",
        "pressure_uploaded", "div_after", "u_after", "v_after", "w_after"};
    if (!m_OfflinePressureReadback)
    {
        UINT64 offset = 0;
        for (unsigned f = 0; f < 9; ++f)
        {
            const auto desc = textures[f]->resource->GetDesc();
            auto& fp = m_OfflinePressureFootprints[f];
            m_Device->GetCopyableFootprints(&desc, 0, 1, offset, &fp, nullptr, nullptr, nullptr);
            offset = (fp.Offset + UINT64(fp.Footprint.RowPitch) * fp.Footprint.Height * fp.Footprint.Depth + 511) & ~UINT64(511);
        }
        const auto desc = CD3DX12_RESOURCE_DESC::Buffer(offset);
        const auto readHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
        const auto uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        ThrowIfFailed(m_Device->CreateCommittedResource(&readHeap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_OfflinePressureReadback)));
        ThrowIfFailed(m_Device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_OfflinePressureUpload)));
    }
    auto copy = [&](unsigned f, bool upload)
    {
        auto& t = *textures[f];
        const auto previous = t.state;
        const auto needed = upload ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_COPY_SOURCE;
        if (previous != needed)
        {
            auto b = CD3DX12_RESOURCE_BARRIER::Transition(t.resource.Get(), previous, needed);
            list->ResourceBarrier(1, &b);
        }
        D3D12_TEXTURE_COPY_LOCATION texture{}, buffer{};
        texture.pResource = t.resource.Get(); texture.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        buffer.pResource = upload ? m_OfflinePressureUpload.Get() : m_OfflinePressureReadback.Get();
        buffer.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        buffer.PlacedFootprint = m_OfflinePressureFootprints[f];
        list->CopyTextureRegion(upload ? &texture : &buffer, 0, 0, 0, upload ? &buffer : &texture, nullptr);
        if (previous != needed)
        {
            auto b = CD3DX12_RESOURCE_BARRIER::Transition(t.resource.Get(), needed, previous);
            list->ResourceBarrier(1, &b);
        }
    };
    for (unsigned f = after ? 4 : 0; f < (after ? 9 : 4); ++f) copy(f, false);
    // Offline-only split: fence all copies before CPU access or reuse. Do not
    // reset the allocator; other lists from this frame use its allocations.
    ThrowIfFailed(list->Close());
    ID3D12CommandList* lists[] = {list};
    m_CommandQueue->ExecuteCommandLists(1, lists);
    FlushCommandQueue();
    TraceSmokeAppearanceDevice(after ? "offline-post-fence" : "offline-pre-fence");
    ThrowIfFailed(list->Reset(m_CurrentFrameResource->CmdListAlloc.Get(), nullptr));
    const unsigned step = m_SmokeGpuBenchmarkSubmitted + 1;
    std::ostringstream label; label << "step-" << std::setw(4) << std::setfill('0') << step;
    const auto root = m_SmokeAppearanceConfig.outputDirectory / "offline" / label.str();
    std::filesystem::create_directories(root);
    void* mapped = nullptr;
    const D3D12_RANGE reads{0, static_cast<SIZE_T>(m_OfflinePressureReadback->GetDesc().Width)};
    ThrowIfFailed(m_OfflinePressureReadback->Map(0, &reads, &mapped));
    for (unsigned f = after ? 4 : 0; f < (after ? 9 : 4); ++f)
    {
        const auto& fp = m_OfflinePressureFootprints[f];
        std::ofstream file(root / (std::string(names[f])+".f32"), std::ios::binary);
        file.exceptions(std::ios::failbit | std::ios::badbit);
        for (UINT z = 0; z < fp.Footprint.Depth; ++z) for (UINT y = 0; y < fp.Footprint.Height; ++y)
            file.write(static_cast<const char*>(mapped)+fp.Offset+(UINT64(z)*fp.Footprint.Height+y)*fp.Footprint.RowPitch,
                fp.Footprint.Width*sizeof(float));
    }
    const D3D12_RANGE noWrites{0,0}; m_OfflinePressureReadback->Unmap(0, &noWrites);
    if (after) return;
    {
        std::ofstream request(root / "request.tmp");
        request.exceptions(std::ios::failbit | std::ios::badbit);
        request << "{\"step\":" << step << ",\"resolution\":" << m_SmokeSolver.Density().Resolution().x
            << ",\"mode\":" << m_OfflinePressureMode << ",\"dt\":" << std::setprecision(17)
            << m_SmokeAppearanceConfig.timeStep << "}\n";
    }
    std::filesystem::rename(root/"request.tmp", root/"request.json");
    WaitForOfflineResponse(root, step);
    const D3D12_RANGE noReads{0,0};
    ThrowIfFailed(m_OfflinePressureUpload->Map(0, &noReads, &mapped));
    for (unsigned f : {4u,6u,7u,8u})
    {
        if (f != 4 && m_OfflinePressureMode != 2) continue;
        const auto& fp = m_OfflinePressureFootprints[f];
        const char* response = f == 4 ? "pressure.f32" : f == 6 ? "u_response.f32" : f == 7 ? "v_response.f32" : "w_response.f32";
        const auto path = root/response;
        if (std::filesystem::file_size(path) != UINT64(fp.Footprint.Width)*fp.Footprint.Height*fp.Footprint.Depth*sizeof(float))
            throw std::runtime_error("Offline response field size mismatch");
        std::ifstream file(path, std::ios::binary); file.exceptions(std::ios::failbit | std::ios::badbit);
        // Upload heaps may be write-combined: validate in ordinary CPU memory
        // and only write mapped upload memory. Reading it stalls this path.
        std::vector<float> cpuRow(fp.Footprint.Width);
        for (UINT z=0; z<fp.Footprint.Depth; ++z) for (UINT y=0; y<fp.Footprint.Height; ++y)
        {
            auto* row = reinterpret_cast<float*>(static_cast<char*>(mapped)+fp.Offset+(UINT64(z)*fp.Footprint.Height+y)*fp.Footprint.RowPitch);
            file.read(reinterpret_cast<char*>(cpuRow.data()), fp.Footprint.Width*sizeof(float));
            for (float value : cpuRow) if (!std::isfinite(value)) throw std::runtime_error("Nonfinite offline response");
            std::memcpy(row, cpuRow.data(), fp.Footprint.Width*sizeof(float));
        }
    }
    m_OfflinePressureUpload->Unmap(0, nullptr);
    copy(4,true);
    if (m_OfflinePressureMode == 2) for (unsigned f : {6u,7u,8u}) copy(f,true);
}
