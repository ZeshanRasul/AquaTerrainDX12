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
#include <cstring>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
static void Check(HRESULT h){if(FAILED(h))throw std::runtime_error("HRESULT "+std::to_string(unsigned(h)));}
static std::vector<char> Read(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("Missing input");return {std::istreambuf_iterator<char>(f),{}};}
int wmain(int argc,wchar_t** argv) try {
    if(argc!=6)throw std::runtime_error("Usage: AdvectionAccounting inputs method[0 SL/1 raw/2 MC] observer.hlsl output optimize[0/1]");
    const std::filesystem::path input(argv[1]),shaderPath(argv[3]),out(argv[4]);
    const unsigned method=std::stoul(argv[2]); const bool optimized=std::stoi(argv[5])!=0;
    if(method>2||std::filesystem::exists(out))throw std::runtime_error("Invalid method/existing output");
    auto constants=Read(input/"constants.bin");if(constants.size()!=176)throw std::runtime_error("Expected 176-byte production constants");
    UINT n;memcpy(&n,constants.data(),4);if(n!=32&&n!=64&&n!=128)throw std::runtime_error("Invalid N");
    std::filesystem::create_directories(out);
    ComPtr<ID3D12Debug> debug;Check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));debug->EnableDebugLayer();
    ComPtr<ID3D12Device> dev;Check(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&dev)));
    ComPtr<ID3D12InfoQueue> info;Check(dev.As(&info));
    ComPtr<IDXGIFactory4> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));ComPtr<IDXGIAdapter1> adapter;Check(factory->EnumAdapterByLuid(dev->GetAdapterLuid(),IID_PPV_ARGS(&adapter)));
    DXGI_ADAPTER_DESC1 ad{};Check(adapter->GetDesc1(&ad));LARGE_INTEGER driver{};Check(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice),&driver));std::wofstream(out/"device.txt")<<ad.Description<<L"\nDriver="<<driver.QuadPart<<L"\n";
    D3D12_COMMAND_QUEUE_DESC qd{};ComPtr<ID3D12CommandQueue> queue;Check(dev->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> alloc;Check(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&alloc)));
    ComPtr<ID3D12GraphicsCommandList> list;Check(dev->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc.Get(),nullptr,IID_PPV_ARGS(&list)));
    ComPtr<ID3D12Fence> fence;Check(dev->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));HANDLE event=CreateEvent(nullptr,FALSE,FALSE,nullptr);if(!event)throw std::runtime_error("CreateEvent failed");UINT64 serial=0;
    auto submit=[&]{Check(list->Close());ID3D12CommandList* ls[]={list.Get()};queue->ExecuteCommandLists(1,ls);Check(queue->Signal(fence.Get(),++serial));Check(fence->SetEventOnCompletion(serial,event));if(WaitForSingleObject(event,60000)!=WAIT_OBJECT_0)throw std::runtime_error("GPU timeout");Check(dev->GetDeviceRemovedReason());};
    auto resource=[&](D3D12_RESOURCE_DESC d,D3D12_HEAP_TYPE h,D3D12_RESOURCE_STATES s){ComPtr<ID3D12Resource> r;auto hp=CD3DX12_HEAP_PROPERTIES(h);Check(dev->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,s,nullptr,IID_PPV_ARGS(&r)));return r;};
    auto buffer=[&](UINT64 bytes,D3D12_HEAP_TYPE h,D3D12_RESOURCE_STATES s,D3D12_RESOURCE_FLAGS f=D3D12_RESOURCE_FLAG_NONE){return resource(CD3DX12_RESOURCE_DESC::Buffer(bytes,f),h,s);};
    D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=13;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ComPtr<ID3D12DescriptorHeap> heap;Check(dev->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));UINT stride=dev->GetDescriptorHandleIncrementSize(hd.Type);
    auto cpu=[&](UINT i){auto h=heap->GetCPUDescriptorHandleForHeapStart();h.ptr+=SIZE_T(i)*stride;return h;};
    auto gpu=[&](UINT i){auto h=heap->GetGPUDescriptorHandleForHeapStart();h.ptr+=UINT64(i)*stride;return h;};
    std::vector<ComPtr<ID3D12Resource>> textures,uploads;
    const char* names[]={"density","temperature","u","v","w","zero","zero","hat","temperature","bar","temperature"};
    for(UINT t=0;t<13;++t){
        UINT nx=n+(t==2),ny=n+(t==3),nz=n+(t==4);
        auto desc=CD3DX12_RESOURCE_DESC::Tex3D(DXGI_FORMAT_R32_FLOAT,nx,ny,UINT16(nz),1,t>=11?D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS:D3D12_RESOURCE_FLAG_NONE);
        auto tex=resource(desc,D3D12_HEAP_TYPE_DEFAULT,t>=11?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_COPY_DEST);
        if(t<11){
            auto bytes=Read(input/(std::string(names[t])+".f32"));if(bytes.size()!=size_t(nx)*ny*nz*4)throw std::runtime_error("Texture size mismatch");
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT rows;UINT64 total;dev->GetCopyableFootprints(&desc,0,1,0,&fp,&rows,nullptr,&total);
            auto upload=buffer(total,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);void* p;Check(upload->Map(0,nullptr,&p));memset(p,0,size_t(total));
            for(UINT z=0;z<nz;++z)for(UINT y=0;y<ny;++y)memcpy(static_cast<char*>(p)+fp.Offset+(size_t(z)*rows+y)*fp.Footprint.RowPitch,bytes.data()+(size_t(z)*ny+y)*nx*4,nx*4);upload->Unmap(0,nullptr);
            auto dst=CD3DX12_TEXTURE_COPY_LOCATION(tex.Get(),0),src=CD3DX12_TEXTURE_COPY_LOCATION(upload.Get(),fp);list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
            auto b=CD3DX12_RESOURCE_BARRIER::Transition(tex.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);list->ResourceBarrier(1,&b);uploads.push_back(upload);
            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=DXGI_FORMAT_R32_FLOAT;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE3D;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture3D.MipLevels=1;dev->CreateShaderResourceView(tex.Get(),&srv,cpu(t));
        }else{D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=DXGI_FORMAT_R32_FLOAT;uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE3D;uav.Texture3D.WSize=n;dev->CreateUnorderedAccessView(tex.Get(),nullptr,&uav,cpu(t));}
        textures.push_back(tex);
    }
    const UINT64 traceBytes=UINT64(n)*n*n*80;
    auto trace=buffer(traceBytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    auto traceReadback=buffer(traceBytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    auto desc=textures[11]->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT rows;UINT64 total;dev->GetCopyableFootprints(&desc,0,1,0,&fp,&rows,nullptr,&total);
    auto outputReadback=buffer(total,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    CD3DX12_DESCRIPTOR_RANGE ranges[2];ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,11,0);ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV,2,0);
    CD3DX12_ROOT_PARAMETER params[4];params[0].InitAsConstants(44,0);params[1].InitAsDescriptorTable(1,&ranges[0]);params[2].InitAsDescriptorTable(1,&ranges[1]);params[3].InitAsUnorderedAccessView(15);
    CD3DX12_STATIC_SAMPLER_DESC samplers[2]={CD3DX12_STATIC_SAMPLER_DESC(0,D3D12_FILTER_MIN_MAG_MIP_LINEAR,D3D12_TEXTURE_ADDRESS_MODE_CLAMP,D3D12_TEXTURE_ADDRESS_MODE_CLAMP,D3D12_TEXTURE_ADDRESS_MODE_CLAMP),CD3DX12_STATIC_SAMPLER_DESC(1,D3D12_FILTER_MIN_MAG_MIP_LINEAR,D3D12_TEXTURE_ADDRESS_MODE_WRAP,D3D12_TEXTURE_ADDRESS_MODE_WRAP,D3D12_TEXTURE_ADDRESS_MODE_WRAP)};
    CD3DX12_ROOT_SIGNATURE_DESC rs(4,params,2,samplers);ComPtr<ID3DBlob> blob,errors;Check(D3D12SerializeRootSignature(&rs,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&errors));ComPtr<ID3D12RootSignature> root;Check(dev->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));
    const auto methodText=std::to_string(method);const D3D_SHADER_MACRO defines[]={{"METHOD",methodText.c_str()},{nullptr,nullptr}};
    auto compile=[&](const char* entry,bool observer){ComPtr<ID3DBlob> code;auto path=observer?shaderPath:shaderPath.parent_path()/"../src/Shaders/3d_smoke_compute.hlsl";
        HRESULT h=D3DCompileFromFile(path.c_str(),observer?defines:nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,entry,"cs_5_1",D3DCOMPILE_ENABLE_STRICTNESS|(optimized?D3DCOMPILE_OPTIMIZATION_LEVEL3:D3DCOMPILE_DEBUG|D3DCOMPILE_SKIP_OPTIMIZATION),0,&code,&errors);if(FAILED(h)&&errors)std::cerr<<static_cast<char*>(errors->GetBufferPointer());Check(h);
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root.Get();pd.CS={code->GetBufferPointer(),code->GetBufferSize()};ComPtr<ID3D12PipelineState> pso;Check(dev->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso)));return pso;};
    const char* entries[]={"AdvectScalarsCS","AdvectScalarsRawCS","MacCormackScalarsCS"};auto production=compile(entries[method],false),observer=compile("TraceAccountingCS",true);
    submit();
    for(UINT trial=0;trial<3;++trial){
        Check(alloc->Reset());Check(list->Reset(alloc.Get(),nullptr));ID3D12DescriptorHeap* heaps[]={heap.Get()};list->SetDescriptorHeaps(1,heaps);list->SetComputeRootSignature(root.Get());list->SetComputeRoot32BitConstants(0,44,constants.data(),0);list->SetComputeRootDescriptorTable(1,gpu(0));list->SetComputeRootDescriptorTable(2,gpu(11));list->SetComputeRootUnorderedAccessView(3,trace->GetGPUVirtualAddress());
        list->SetPipelineState(production.Get());list->Dispatch((n+7)/8,(n+7)/8,(n+3)/4);
        // The observer reads inputs only. It cannot affect the already-written result.
        list->SetPipelineState(observer.Get());list->Dispatch((n+7)/8,(n+7)/8,(n+3)/4);
        auto b=CD3DX12_RESOURCE_BARRIER::Transition(textures[11].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);list->ResourceBarrier(1,&b);
        auto dst=CD3DX12_TEXTURE_COPY_LOCATION(outputReadback.Get(),fp),src=CD3DX12_TEXTURE_COPY_LOCATION(textures[11].Get(),0);list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        b=CD3DX12_RESOURCE_BARRIER::Transition(textures[11].Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);list->ResourceBarrier(1,&b);
        b=CD3DX12_RESOURCE_BARRIER::Transition(trace.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);list->ResourceBarrier(1,&b);list->CopyResource(traceReadback.Get(),trace.Get());b=CD3DX12_RESOURCE_BARRIER::Transition(trace.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);list->ResourceBarrier(1,&b);submit();
        void* p;Check(outputReadback->Map(0,nullptr,&p));std::ofstream f(out/("density-"+std::to_string(trial)+".f32"),std::ios::binary);for(UINT z=0;z<n;++z)for(UINT y=0;y<n;++y)f.write(static_cast<char*>(p)+fp.Offset+(size_t(z)*rows+y)*fp.Footprint.RowPitch,n*4);outputReadback->Unmap(0,nullptr);if(!f)throw std::runtime_error("Write failed");
        Check(traceReadback->Map(0,nullptr,&p));std::ofstream t(out/("trace-"+std::to_string(trial)+".f32"),std::ios::binary);t.write(static_cast<char*>(p),traceBytes);traceReadback->Unmap(0,nullptr);if(!t)throw std::runtime_error("Write failed");
    }
    CloseHandle(event);bool invalid=false;std::ofstream log(out/"debug.txt");
    for(UINT64 i=0;i<info->GetNumStoredMessages();++i){SIZE_T bytes=0;Check(info->GetMessage(i,nullptr,&bytes));std::vector<char> data(bytes);auto m=reinterpret_cast<D3D12_MESSAGE*>(data.data());Check(info->GetMessage(i,m,&bytes));log<<m->pDescription<<"\n";invalid|=m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR;}
    if(invalid)throw std::runtime_error("D3D12 validation error");std::ofstream(out/"complete.txt")<<"3 repeats; debug enabled; zero errors\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
