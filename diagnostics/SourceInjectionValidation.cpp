#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include "d3dx12.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <array>
#include <bit>
#include <cstring>
#include <cmath>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
static void Check(HRESULT h){if(FAILED(h))throw std::runtime_error("HRESULT "+std::to_string(unsigned(h)));}
static std::vector<char> Read(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("Cannot read "+p.string());return {std::istreambuf_iterator<char>(f),{}};}
int wmain(int argc,wchar_t** argv) try {
    if(argc!=10)throw std::runtime_error("Usage: SourceInjectionValidation N rate.f32 initial-density.f32 initial-temperature.f32 dt steps shader.hlsl output-directory optimize[0/1]");
    const UINT n=std::stoul(argv[1]),steps=std::stoul(argv[6]);
    const float dt=std::stof(argv[5]); const bool optimized=std::stoi(argv[9])!=0;
    const std::filesystem::path ratePath(argv[2]),densityPath(argv[3]),temperaturePath(argv[4]),shaderPath(argv[7]),out(argv[8]);
    const UINT64 cellCount=UINT64(n)*n*n,fieldBytes=cellCount*sizeof(float);
    if((n!=32&&n!=64&&n!=128)||!std::isfinite(dt)||dt<=0||steps<1||steps>120||std::filesystem::exists(out))
        throw std::runtime_error("Invalid configuration or existing output");
    const auto rate=Read(ratePath),initialDensity=Read(densityPath),initialTemperature=Read(temperaturePath);
    if(rate.size()!=fieldBytes||initialDensity.size()!=fieldBytes||initialTemperature.size()!=fieldBytes)
        throw std::runtime_error("Input size mismatch");
    for(const auto* data:{&rate,&initialDensity,&initialTemperature}) for(UINT64 i=0;i<cellCount;++i){
        float v; memcpy(&v,data->data()+i*4,4); if(!std::isfinite(v))throw std::runtime_error("Nonfinite input");
    }
    std::filesystem::create_directories(out);
    ComPtr<ID3D12Debug> debug; Check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))); debug->EnableDebugLayer();
    ComPtr<ID3D12Device> dev; Check(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&dev)));
    ComPtr<ID3D12InfoQueue> info; Check(dev.As(&info));
    ComPtr<IDXGIFactory4> factory; Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    ComPtr<IDXGIAdapter1> adapter; Check(factory->EnumAdapterByLuid(dev->GetAdapterLuid(),IID_PPV_ARGS(&adapter)));
    DXGI_ADAPTER_DESC1 ad{}; Check(adapter->GetDesc1(&ad)); LARGE_INTEGER driver{};
    Check(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice),&driver));
    std::wofstream(out/"device.txt")<<ad.Description<<L"\nDriver="<<driver.QuadPart<<L"\n";
    D3D12_COMMAND_QUEUE_DESC qd{}; ComPtr<ID3D12CommandQueue> queue;
    Check(dev->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> alloc; Check(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&alloc)));
    ComPtr<ID3D12GraphicsCommandList> list; Check(dev->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc.Get(),nullptr,IID_PPV_ARGS(&list)));
    ComPtr<ID3D12Fence> fence; Check(dev->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    HANDLE event=CreateEvent(nullptr,FALSE,FALSE,nullptr); if(!event)throw std::runtime_error("CreateEvent failed"); UINT64 serial=0;
    auto submit=[&]{Check(list->Close());ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);Check(queue->Signal(fence.Get(),++serial));Check(fence->SetEventOnCompletion(serial,event));if(WaitForSingleObject(event,60000)!=WAIT_OBJECT_0)throw std::runtime_error("GPU timeout");Check(dev->GetDeviceRemovedReason());};
    auto resource=[&](const D3D12_RESOURCE_DESC& d,D3D12_HEAP_TYPE h,D3D12_RESOURCE_STATES s){ComPtr<ID3D12Resource> r;auto heap=CD3DX12_HEAP_PROPERTIES(h);Check(dev->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,s,nullptr,IID_PPV_ARGS(&r)));return r;};
    auto buffer=[&](UINT64 bytes,D3D12_HEAP_TYPE h,D3D12_RESOURCE_STATES s){return resource(CD3DX12_RESOURCE_DESC::Buffer(bytes),h,s);};
    auto rateBuffer=buffer(fieldBytes,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
    void* mapped; Check(rateBuffer->Map(0,nullptr,&mapped)); memcpy(mapped,rate.data(),rate.size()); rateBuffer->Unmap(0,nullptr);
    auto td=CD3DX12_RESOURCE_DESC::Tex3D(DXGI_FORMAT_R32_FLOAT,n,n,UINT16(n),1,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    std::array<ComPtr<ID3D12Resource>,2> fields={resource(td,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST),resource(td,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST)};
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT uploadFootprint{}; UINT rows; UINT64 rowBytes,uploadBytes;
    dev->GetCopyableFootprints(&td,0,1,0,&uploadFootprint,&rows,&rowBytes,&uploadBytes);
    std::array<ComPtr<ID3D12Resource>,2> uploads={buffer(uploadBytes,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ),buffer(uploadBytes,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ)};
    for(UINT field=0;field<2;++field){
        const auto& source=field?initialTemperature:initialDensity;
        Check(uploads[field]->Map(0,nullptr,&mapped)); memset(mapped,0,size_t(uploadBytes));
        for(UINT z=0;z<n;++z)for(UINT y=0;y<n;++y)
            memcpy(static_cast<char*>(mapped)+uploadFootprint.Offset+(UINT64(z)*rows+y)*uploadFootprint.Footprint.RowPitch,source.data()+(UINT64(z)*n+y)*n*4,n*4);
        uploads[field]->Unmap(0,nullptr);
    }
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT readFootprints[2]{}; UINT readRows[2]{}; UINT64 offset=0;
    for(UINT field=0;field<2;++field){UINT64 bytes;dev->GetCopyableFootprints(&td,0,1,offset,&readFootprints[field],&readRows[field],nullptr,&bytes);offset=(readFootprints[field].Offset+UINT64(readFootprints[field].Footprint.RowPitch)*readFootprints[field].Footprint.Height*readFootprints[field].Footprint.Depth+511)&~UINT64(511);}
    auto readback=buffer(offset,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_DESCRIPTOR_HEAP_DESC hd{}; hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.NumDescriptors=2; hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ComPtr<ID3D12DescriptorHeap> descriptors; Check(dev->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&descriptors))); UINT stride=dev->GetDescriptorHandleIncrementSize(hd.Type);
    for(UINT field=0;field<2;++field){D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=DXGI_FORMAT_R32_FLOAT;uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE3D;uav.Texture3D.WSize=n;auto h=descriptors->GetCPUDescriptorHandleForHeapStart();h.ptr+=SIZE_T(field)*stride;dev->CreateUnorderedAccessView(fields[field].Get(),nullptr,&uav,h);}
    CD3DX12_DESCRIPTOR_RANGE range; range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV,2,0);
    CD3DX12_ROOT_PARAMETER params[3]; params[0].InitAsConstants(4,0); params[1].InitAsShaderResourceView(0); params[2].InitAsDescriptorTable(1,&range);
    CD3DX12_ROOT_SIGNATURE_DESC rd(3,params); ComPtr<ID3DBlob> blob,errors;
    Check(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&errors));
    ComPtr<ID3D12RootSignature> root; Check(dev->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));
    ComPtr<ID3DBlob> shader; UINT flags=D3DCOMPILE_ENABLE_STRICTNESS|(optimized?D3DCOMPILE_OPTIMIZATION_LEVEL3:D3DCOMPILE_DEBUG|D3DCOMPILE_SKIP_OPTIMIZATION);
    HRESULT compile=D3DCompileFromFile(shaderPath.c_str(),nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,"InjectNormalizedSourceCS","cs_5_1",flags,0,&shader,&errors);
    if(FAILED(compile)&&errors)std::cerr<<static_cast<char*>(errors->GetBufferPointer()); Check(compile);
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{}; pd.pRootSignature=root.Get(); pd.CS={shader->GetBufferPointer(),shader->GetBufferSize()};
    ComPtr<ID3D12PipelineState> pso; Check(dev->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso)));
    submit(); std::array<D3D12_RESOURCE_STATES,2> states={D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_DEST};
    for(UINT trial=0;trial<3;++trial){
        Check(alloc->Reset()); Check(list->Reset(alloc.Get(),pso.Get()));
        for(UINT field=0;field<2;++field){
            if(states[field]!=D3D12_RESOURCE_STATE_COPY_DEST){auto b=CD3DX12_RESOURCE_BARRIER::Transition(fields[field].Get(),states[field],D3D12_RESOURCE_STATE_COPY_DEST);list->ResourceBarrier(1,&b);states[field]=D3D12_RESOURCE_STATE_COPY_DEST;}
            auto dst=CD3DX12_TEXTURE_COPY_LOCATION(fields[field].Get(),0),src=CD3DX12_TEXTURE_COPY_LOCATION(uploads[field].Get(),uploadFootprint); list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
            auto b=CD3DX12_RESOURCE_BARRIER::Transition(fields[field].Get(),states[field],D3D12_RESOURCE_STATE_UNORDERED_ACCESS); list->ResourceBarrier(1,&b); states[field]=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }
        ID3D12DescriptorHeap* heaps[]={descriptors.Get()}; list->SetDescriptorHeaps(1,heaps); list->SetComputeRootSignature(root.Get());
        const std::array<UINT,4> constants={n,n,n,std::bit_cast<UINT>(dt)}; list->SetComputeRoot32BitConstants(0,4,constants.data(),0);
        list->SetComputeRootShaderResourceView(1,rateBuffer->GetGPUVirtualAddress()); list->SetComputeRootDescriptorTable(2,descriptors->GetGPUDescriptorHandleForHeapStart());
        for(UINT step=0;step<steps;++step){list->Dispatch((n+7)/8,(n+7)/8,(n+3)/4);if(step+1<steps){auto barrier=CD3DX12_RESOURCE_BARRIER::UAV(nullptr);list->ResourceBarrier(1,&barrier);}}
        for(UINT field=0;field<2;++field){auto b=CD3DX12_RESOURCE_BARRIER::Transition(fields[field].Get(),states[field],D3D12_RESOURCE_STATE_COPY_SOURCE);list->ResourceBarrier(1,&b);states[field]=D3D12_RESOURCE_STATE_COPY_SOURCE;auto dst=CD3DX12_TEXTURE_COPY_LOCATION(readback.Get(),readFootprints[field]),src=CD3DX12_TEXTURE_COPY_LOCATION(fields[field].Get(),0);list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);}
        submit(); Check(readback->Map(0,nullptr,&mapped));
        for(UINT field=0;field<2;++field){std::ofstream output(out/((field?"temperature-":"density-")+std::to_string(trial)+".f32"),std::ios::binary);for(UINT z=0;z<n;++z)for(UINT y=0;y<n;++y)output.write(static_cast<char*>(mapped)+readFootprints[field].Offset+(UINT64(z)*readRows[field]+y)*readFootprints[field].Footprint.RowPitch,n*4);if(!output)throw std::runtime_error("Output write failed");}
        readback->Unmap(0,nullptr);
    }
    CloseHandle(event); bool invalid=false; std::ofstream messages(out/"debug.txt");
    for(UINT64 i=0;i<info->GetNumStoredMessages();++i){SIZE_T bytes=0;Check(info->GetMessage(i,nullptr,&bytes));std::vector<char> storage(bytes);auto message=reinterpret_cast<D3D12_MESSAGE*>(storage.data());Check(info->GetMessage(i,message,&bytes));messages<<message->pDescription<<"\n";invalid|=message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR;}
    if(invalid)throw std::runtime_error("D3D12 validation error");
    std::ofstream(out/"complete.txt")<<"3 repeats; density and temperature; debug enabled; zero errors\n"; return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
