#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include "d3dx12.h"
#include <array>
#include <vector>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cstring>
using Microsoft::WRL::ComPtr;
static void Check(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("HRESULT " + std::to_string(static_cast<unsigned>(hr))); }
template<class T> static void Write(const std::filesystem::path& p, const std::vector<T>& v) {
    std::ofstream f(p, std::ios::binary); f.write(reinterpret_cast<const char*>(v.data()), v.size()*sizeof(T)); if (!f) throw std::runtime_error("Write failed");
}
int wmain(int argc, wchar_t** argv) try {
    if (argc != 5) throw std::runtime_error("Usage: SamplerMicrobenchmark N positions.f32 shader.hlsl output-directory");
    UINT n = std::stoul(argv[1]); if (n < 4 || n > 128) throw std::runtime_error("Invalid N");
    std::filesystem::path out(argv[4]); std::filesystem::create_directories(out);
    std::ifstream qfile(argv[2], std::ios::binary | std::ios::ate);
    auto qbytes = qfile.tellg(); if (qbytes <= 0 || qbytes % 16 != 0) throw std::runtime_error("Invalid queries");
    std::vector<float> queries(static_cast<size_t>(qbytes)/4); qfile.seekg(0); qfile.read(reinterpret_cast<char*>(queries.data()), qbytes);
    UINT count = static_cast<UINT>(queries.size()/4), bx=n/3, by=n/2-1, bz=3*n/8;
    ComPtr<ID3D12Device> dev; Check(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&dev)));
    ComPtr<IDXGIFactory4> factory; Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    ComPtr<IDXGIAdapter1> adapter; Check(factory->EnumAdapterByLuid(dev->GetAdapterLuid(),IID_PPV_ARGS(&adapter)));
    DXGI_ADAPTER_DESC1 desc{}; Check(adapter->GetDesc1(&desc)); LARGE_INTEGER driver{}; adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice),&driver);
    std::wofstream meta(out/L"device.txt"); meta << desc.Description << L"\nDriver=" << driver.QuadPart << L"\nN=" << n << L"\nQueries=" << count << L"\nFormat=R32_FLOAT\nFilter=MIN_MAG_MIP_LINEAR\nAddress=CLAMP\nCompiler=cs_5_1 O3 strict\n"; meta.close();
    D3D12_COMMAND_QUEUE_DESC qd{}; ComPtr<ID3D12CommandQueue> queue; Check(dev->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> alloc; Check(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&alloc)));
    ComPtr<ID3D12GraphicsCommandList> list; Check(dev->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc.Get(),nullptr,IID_PPV_ARGS(&list)));
    ComPtr<ID3D12Fence> fence; Check(dev->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))); HANDLE event=CreateEvent(nullptr,FALSE,FALSE,nullptr);
    UINT64 serial=0;
    auto submit=[&] { Check(list->Close()); ID3D12CommandList* lists[]={list.Get()}; queue->ExecuteCommandLists(1,lists); Check(queue->Signal(fence.Get(),++serial)); Check(fence->SetEventOnCompletion(serial,event)); if(WaitForSingleObject(event,120000)!=WAIT_OBJECT_0) throw std::runtime_error("GPU timeout"); Check(dev->GetDeviceRemovedReason()); };
    auto buffer=[&](UINT64 bytes,D3D12_HEAP_TYPE heap,D3D12_RESOURCE_STATES state,D3D12_RESOURCE_FLAGS flags=D3D12_RESOURCE_FLAG_NONE) {
        ComPtr<ID3D12Resource> r; auto hp=CD3DX12_HEAP_PROPERTIES(heap); auto rd=CD3DX12_RESOURCE_DESC::Buffer(bytes,flags); Check(dev->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,state,nullptr,IID_PPV_ARGS(&r))); return r;
    };
    D3D12_DESCRIPTOR_HEAP_DESC hd{}; hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.NumDescriptors=17; hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ComPtr<ID3D12DescriptorHeap> heap; Check(dev->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap))); UINT stride=dev->GetDescriptorHandleIncrementSize(hd.Type);
    std::array<std::array<float,8>,16> corners{};
    for(UINT i=0;i<8;i++) corners[i][i]=1;
    corners[8].fill(1); corners[9]={0,1,2,3,4,5,6,7};
    corners[10]={0,.125f,.5f,1,.25f,.75f,.375f,.875f};
    corners[11]={.137f,.411f,.731f,.923f,.259f,.583f,.047f,.817f};
    corners[12]={-1,2,-.3f,.7f,1.1f,-.8f,.2f,3};
    for(UINT i=0;i<8;i++) { corners[13][i]=corners[11][i]*256; corners[14][i]=corners[11][i]+8; }
    corners[15]={.91f,.02f,.66f,.31f,.48f,.77f,.19f,.54f};
    std::vector<float> flat; for(auto& c:corners) flat.insert(flat.end(),c.begin(),c.end()); Write(out/L"corners.f32",flat);
    std::vector<ComPtr<ID3D12Resource>> textures,uploads;
    for(UINT t=0;t<16;t++) {
        auto rd=CD3DX12_RESOURCE_DESC::Tex3D(DXGI_FORMAT_R32_FLOAT,n,n,static_cast<UINT16>(n),1); auto hp=CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        ComPtr<ID3D12Resource> tex; Check(dev->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&tex)));
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{}; UINT rows; UINT64 rowbytes,total; dev->GetCopyableFootprints(&rd,0,1,0,&fp,&rows,&rowbytes,&total);
        auto upload=buffer(total,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ); char* ptr; Check(upload->Map(0,nullptr,reinterpret_cast<void**>(&ptr))); memset(ptr,0,static_cast<size_t>(total));
        for(UINT z=0;z<n;z++) for(UINT y=0;y<n;y++) { auto row=reinterpret_cast<float*>(ptr+fp.Offset+(static_cast<size_t>(z)*rows+y)*fp.Footprint.RowPitch); for(UINT x=0;x<n;x++) {
            if(t==8) row[x]=1; else if(x>=bx&&x<=bx+1&&y>=by&&y<=by+1&&z>=bz&&z<=bz+1) row[x]=corners[t][(x-bx)+2*(y-by)+4*(z-bz)];
        }} upload->Unmap(0,nullptr);
        auto dst=CD3DX12_TEXTURE_COPY_LOCATION(tex.Get(),0); auto src=CD3DX12_TEXTURE_COPY_LOCATION(upload.Get(),fp); list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        auto barrier=CD3DX12_RESOURCE_BARRIER::Transition(tex.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE); list->ResourceBarrier(1,&barrier);
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{}; srv.Format=DXGI_FORMAT_R32_FLOAT; srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE3D; srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srv.Texture3D.MipLevels=1;
        auto handle=heap->GetCPUDescriptorHandleForHeapStart(); handle.ptr+=static_cast<SIZE_T>(stride)*t; dev->CreateShaderResourceView(tex.Get(),&srv,handle); textures.push_back(tex); uploads.push_back(upload);
    }
    auto query=buffer(queries.size()*4,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ); void* ptr; Check(query->Map(0,nullptr,&ptr)); memcpy(ptr,queries.data(),queries.size()*4); query->Unmap(0,nullptr);
    D3D12_SHADER_RESOURCE_VIEW_DESC qs{}; qs.ViewDimension=D3D12_SRV_DIMENSION_BUFFER; qs.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; qs.Buffer.NumElements=count; qs.Buffer.StructureByteStride=16;
    auto handle=heap->GetCPUDescriptorHandleForHeapStart(); handle.ptr+=static_cast<SIZE_T>(stride)*16; dev->CreateShaderResourceView(query.Get(),&qs,handle);
    UINT64 resultBytes=static_cast<UINT64>(count)*20*4; auto result=buffer(resultBytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS); auto readback=buffer(resultBytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    CD3DX12_DESCRIPTOR_RANGE range; range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,17,0); CD3DX12_ROOT_PARAMETER params[3]; params[0].InitAsDescriptorTable(1,&range); params[1].InitAsUnorderedAccessView(0); params[2].InitAsConstants(5,0);
    CD3DX12_STATIC_SAMPLER_DESC sampler(0,D3D12_FILTER_MIN_MAG_MIP_LINEAR,D3D12_TEXTURE_ADDRESS_MODE_CLAMP,D3D12_TEXTURE_ADDRESS_MODE_CLAMP,D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
    CD3DX12_ROOT_SIGNATURE_DESC rootdesc(3,params,1,&sampler); ComPtr<ID3DBlob> blob,errors; Check(D3D12SerializeRootSignature(&rootdesc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&errors)); ComPtr<ID3D12RootSignature> root; Check(dev->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));
    ComPtr<ID3DBlob> shader; HRESULT hr=D3DCompileFromFile(argv[3],nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,"Main","cs_5_1",D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&shader,&errors); if(FAILED(hr)&&errors) std::cerr<<static_cast<char*>(errors->GetBufferPointer()); Check(hr);
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{}; pd.pRootSignature=root.Get(); pd.CS={shader->GetBufferPointer(),shader->GetBufferSize()}; ComPtr<ID3D12PipelineState> pso; Check(dev->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso)));
    std::vector<float> first;
    for(UINT trial=0;trial<3;trial++) {
        if(trial) { Check(alloc->Reset()); Check(list->Reset(alloc.Get(),nullptr)); }
        ID3D12DescriptorHeap* heaps[]={heap.Get()}; list->SetDescriptorHeaps(1,heaps); list->SetComputeRootSignature(root.Get()); list->SetPipelineState(pso.Get()); list->SetComputeRootDescriptorTable(0,heap->GetGPUDescriptorHandleForHeapStart()); list->SetComputeRootUnorderedAccessView(1,result->GetGPUVirtualAddress()); UINT constants[]={count,n,bx,by,bz}; list->SetComputeRoot32BitConstants(2,5,constants,0); list->Dispatch((count+63)/64,1,1);
        auto barrier=CD3DX12_RESOURCE_BARRIER::Transition(result.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE); list->ResourceBarrier(1,&barrier); list->CopyResource(readback.Get(),result.Get()); barrier=CD3DX12_RESOURCE_BARRIER::Transition(result.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS); list->ResourceBarrier(1,&barrier); submit();
        Check(readback->Map(0,nullptr,&ptr)); std::vector<float> values(static_cast<size_t>(count)*20); memcpy(values.data(),ptr,static_cast<size_t>(resultBytes)); readback->Unmap(0,nullptr); Write(out/("samples-"+std::to_string(trial)+".f32"),values); if(trial && memcmp(first.data(),values.data(),static_cast<size_t>(resultBytes))) throw std::runtime_error("Repeat mismatch"); if(!trial) first=values;
    }
    CloseHandle(event); std::cout<<"N="<<n<<" queries="<<count<<" three trials bitwise identical\n"; return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<"\n"; return 1; }

