// Standalone offscreen capture: no simulator, audit, window or swap chain.
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include "d3dx12.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
using namespace DirectX;
static void Check(HRESULT h) { if (FAILED(h)) throw std::runtime_error("HRESULT " + std::to_string(unsigned(h))); }
static std::string Read(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary); if (!f) throw std::runtime_error("Cannot read source");
    return {std::istreambuf_iterator<char>(f), {}};
}
int wmain(int argc, wchar_t** argv) try {
    if (argc != 7) throw std::runtime_error("Usage: SmokeAppearanceCapture N empty|constant|gaussian step shader-dir output-dir wrapper");
    const UINT n = std::stoul(argv[1]), width = 512;
    const std::wstring field(argv[2]); const float step = std::stof(argv[3]);
    if ((n!=32 && n!=64 && n!=128) || (step!=.02f && step!=.01f && step!=.005f)) throw std::runtime_error("Invalid configuration");
    const bool fileField=field!=L"empty" && field!=L"constant" && field!=L"gaussian";
    std::vector<float> inputDensity;
    if(fileField) {
        std::ifstream input(std::filesystem::path(field),std::ios::binary|std::ios::ate);
        if(!input || input.tellg()!=std::streamoff(size_t(n)*n*n*4)) throw std::runtime_error("Invalid density file length");
        inputDensity.resize(size_t(n)*n*n); input.seekg(0);
        input.read(reinterpret_cast<char*>(inputDensity.data()),std::streamsize(inputDensity.size()*4));
        if(!input) throw std::runtime_error("Density read failed");
        for(float value:inputDensity) if(!std::isfinite(value)||value<0) throw std::runtime_error("Invalid density value");
    }
    const std::filesystem::path out(argv[5]);
    if (std::filesystem::exists(out)) throw std::runtime_error("Output already exists");
    std::filesystem::create_directories(out);
    auto production = Read(std::filesystem::path(argv[4])/"pixel_smoke.hlsl");
    const std::string marker = "stepIndex < 96";
    const auto at = production.find(marker);
    if (at==std::string::npos || production.find(marker, at+1)!=std::string::npos) throw std::runtime_error("Unexpected production loop");
    const UINT bound = step==.02f ? 96 : step==.01f ? 192 : 384;
    production.replace(at, marker.size(), "stepIndex < " + std::to_string(bound));
    const auto source = std::string("#define PS ProductionPS\n") + production + "\n#undef PS\n" + Read(argv[6]);
    std::ofstream(out/"compiled-input.hlsl", std::ios::binary) << source;
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) debug->EnableDebugLayer();
    ComPtr<ID3D12Device> dev; Check(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&dev)));
    ComPtr<ID3D12InfoQueue> info; if (debug) Check(dev.As(&info));
    ComPtr<IDXGIFactory4> factory; Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    ComPtr<IDXGIAdapter1> adapter; Check(factory->EnumAdapterByLuid(dev->GetAdapterLuid(),IID_PPV_ARGS(&adapter)));
    DXGI_ADAPTER_DESC1 device{}; Check(adapter->GetDesc1(&device)); LARGE_INTEGER driver{};
    Check(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driver));
    std::wofstream(out/"device.txt") << device.Description << L"\nDriver=" << driver.QuadPart << L"\n";
    D3D12_COMMAND_QUEUE_DESC qd{}; ComPtr<ID3D12CommandQueue> queue; Check(dev->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> alloc; Check(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&alloc)));
    ComPtr<ID3D12GraphicsCommandList> list; Check(dev->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc.Get(),nullptr,IID_PPV_ARGS(&list)));
    ComPtr<ID3D12Fence> fence; Check(dev->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    HANDLE event=CreateEvent(nullptr,FALSE,FALSE,nullptr); if (!event) throw std::runtime_error("CreateEvent failed"); UINT64 serial=0;
    auto submit=[&] { Check(list->Close()); ID3D12CommandList* ls[]={list.Get()}; queue->ExecuteCommandLists(1,ls); Check(queue->Signal(fence.Get(),++serial)); Check(fence->SetEventOnCompletion(serial,event)); if(WaitForSingleObject(event,60000)!=WAIT_OBJECT_0) throw std::runtime_error("GPU timeout"); Check(dev->GetDeviceRemovedReason()); };
    auto resource=[&](const D3D12_RESOURCE_DESC& d, D3D12_HEAP_TYPE h, D3D12_RESOURCE_STATES s) {
        ComPtr<ID3D12Resource> r; auto hp=CD3DX12_HEAP_PROPERTIES(h); Check(dev->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&d,s,nullptr,IID_PPV_ARGS(&r))); return r;
    };
    auto buffer=[&](UINT64 bytes,D3D12_HEAP_TYPE h,D3D12_RESOURCE_STATES s) { return resource(CD3DX12_RESOURCE_DESC::Buffer(bytes),h,s); };
    D3D12_DESCRIPTOR_HEAP_DESC hd{}; hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.NumDescriptors=2; hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ComPtr<ID3D12DescriptorHeap> heap; Check(dev->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));
    std::vector<ComPtr<ID3D12Resource>> textures, uploads;
    for (UINT t=0;t<2;++t) {
        auto rd=t==0 ? CD3DX12_RESOURCE_DESC::Tex3D(DXGI_FORMAT_R32_FLOAT,n,n,UINT16(n),1) : CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32_FLOAT,width,width,1,1);
        auto tex=resource(rd,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{}; UINT rows; UINT64 total;
        dev->GetCopyableFootprints(&rd,0,1,0,&fp,&rows,nullptr,&total);
        auto upload=buffer(total,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ); char* p;
        Check(upload->Map(0,nullptr,reinterpret_cast<void**>(&p))); memset(p,0,size_t(total));
        std::vector<float> density;
        for (UINT z=0;z<fp.Footprint.Depth;++z) for (UINT y=0;y<rows;++y) {
            auto row=reinterpret_cast<float*>(p+fp.Offset+(size_t(z)*rows+y)*fp.Footprint.RowPitch);
            for (UINT x=0;x<fp.Footprint.Width;++x) {
                double r2=0; for (UINT c : {x,y,z}) { double d=(c+.5)/n-.5; r2+=d*d; }
                row[x]=t==1 ? 10.f : fileField ? inputDensity[(size_t(z)*n+y)*n+x] : field==L"empty" ? 0.f : field==L"constant" ? .25f : float(std::exp(-r2/(2*.15*.15)));
                if (t==0) density.push_back(row[x]);
            }
        }
        upload->Unmap(0,nullptr);
        if (t==0) { std::ofstream f(out/"density.f32",std::ios::binary); f.write(reinterpret_cast<const char*>(density.data()),density.size()*4); }
        auto dst=CD3DX12_TEXTURE_COPY_LOCATION(tex.Get(),0), src=CD3DX12_TEXTURE_COPY_LOCATION(upload.Get(),fp); list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        auto b=CD3DX12_RESOURCE_BARRIER::Transition(tex.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE); list->ResourceBarrier(1,&b);
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{}; srv.Format=DXGI_FORMAT_R32_FLOAT; srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.ViewDimension=t==0 ? D3D12_SRV_DIMENSION_TEXTURE3D : D3D12_SRV_DIMENSION_TEXTURE2D;
        if(t==0) srv.Texture3D.MipLevels=1; else srv.Texture2D.MipLevels=1;
        auto handle=heap->GetCPUDescriptorHandleForHeapStart(); handle.ptr+=size_t(t)*dev->GetDescriptorHandleIncrementSize(hd.Type); dev->CreateShaderResourceView(tex.Get(),&srv,handle);
        textures.push_back(tex); uploads.push_back(upload);
    }
    auto rd=CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32G32B32A32_FLOAT,width,width,1,1,1,0,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    auto target=resource(rd,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_RENDER_TARGET);
    D3D12_DESCRIPTOR_HEAP_DESC rh{}; rh.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV; rh.NumDescriptors=1;
    ComPtr<ID3D12DescriptorHeap> rtv; Check(dev->CreateDescriptorHeap(&rh,IID_PPV_ARGS(&rtv))); auto rt=rtv->GetCPUDescriptorHandleForHeapStart(); dev->CreateRenderTargetView(target.Get(),nullptr,rt);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{}; UINT rows; UINT64 total; dev->GetCopyableFootprints(&rd,0,1,0,&fp,&rows,nullptr,&total);
    auto readback=buffer(total,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    CD3DX12_DESCRIPTOR_RANGE range; range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,2,0);
    CD3DX12_ROOT_PARAMETER params[3]; params[0].InitAsDescriptorTable(1,&range); params[1].InitAsConstantBufferView(0); params[2].InitAsConstantBufferView(1);
    CD3DX12_STATIC_SAMPLER_DESC sampler(0,D3D12_FILTER_MIN_MAG_MIP_LINEAR,D3D12_TEXTURE_ADDRESS_MODE_CLAMP,D3D12_TEXTURE_ADDRESS_MODE_CLAMP,D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
    CD3DX12_ROOT_SIGNATURE_DESC rs(3,params,1,&sampler); ComPtr<ID3DBlob> blob,err; Check(D3D12SerializeRootSignature(&rs,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&err));
    ComPtr<ID3D12RootSignature> root; Check(dev->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));
    auto compile=[&](const char* entry,const char* profile) { ComPtr<ID3DBlob> s; HRESULT h=D3DCompile(source.data(),source.size(),"appearance",nullptr,nullptr,entry,profile,D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&s,&err); if(FAILED(h)&&err) std::cerr<<static_cast<char*>(err->GetBufferPointer()); Check(h); return s; };
    auto vs=compile("CaptureVS","vs_5_1"), ps=compile("CapturePS","ps_5_1");
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{}; pd.pRootSignature=root.Get(); pd.VS={vs->GetBufferPointer(),vs->GetBufferSize()}; pd.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
    pd.BlendState=CD3DX12_BLEND_DESC(D3D12_DEFAULT); pd.RasterizerState=CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT); pd.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
    pd.DepthStencilState=CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT); pd.DepthStencilState.DepthEnable=FALSE; pd.DepthStencilState.StencilEnable=FALSE;
    pd.SampleMask=UINT_MAX; pd.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; pd.NumRenderTargets=1; pd.RTVFormats[0]=rd.Format; pd.SampleDesc.Count=1;
    ComPtr<ID3D12PipelineState> pso; Check(dev->CreateGraphicsPipelineState(&pd,IID_PPV_ARGS(&pso)));
    auto constants=buffer(512,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
    struct Smoke { XMFLOAT4X4 world,view; XMFLOAT3 eye; float scale; XMFLOAT3 colour; float absorption; XMFLOAT3 light; float stepScale; };
    struct Camera { XMFLOAT3 eye; float fov; XMFLOAT3 forward; float width; XMFLOAT3 right; float height; XMFLOAT3 up; float pad; };
    const std::array<XMFLOAT3,3> eyes={XMFLOAT3(2.5f,1.5f,2.5f),XMFLOAT3(-1.5f,1.5f,2.5f),XMFLOAT3(.5f,1.5f,-2.f)};
    // Validate the uploaded texture itself, not just the CPU staging copy.
    auto densityDesc=textures[0]->GetDesc(); D3D12_PLACED_SUBRESOURCE_FOOTPRINT densityFp{};
    UINT densityRows; UINT64 densityBytes;
    dev->GetCopyableFootprints(&densityDesc,0,1,0,&densityFp,&densityRows,nullptr,&densityBytes);
    auto densityReadback=buffer(densityBytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    auto densityBarrier=CD3DX12_RESOURCE_BARRIER::Transition(textures[0].Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
    list->ResourceBarrier(1,&densityBarrier);
    auto densityDst=CD3DX12_TEXTURE_COPY_LOCATION(densityReadback.Get(),densityFp), densitySrc=CD3DX12_TEXTURE_COPY_LOCATION(textures[0].Get(),0);
    list->CopyTextureRegion(&densityDst,0,0,0,&densitySrc,nullptr);
    densityBarrier=CD3DX12_RESOURCE_BARRIER::Transition(textures[0].Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    list->ResourceBarrier(1,&densityBarrier);
    submit();
    {
        void* p; Check(densityReadback->Map(0,nullptr,&p));
        std::ofstream uploaded(out/"density-upload-readback.f32",std::ios::binary);
        for(UINT z=0;z<n;++z) for(UINT y=0;y<n;++y)
            uploaded.write(static_cast<char*>(p)+densityFp.Offset+(size_t(z)*densityRows+y)*densityFp.Footprint.RowPitch,n*4);
        densityReadback->Unmap(0,nullptr); uploaded.close();
        if(!uploaded || Read(out/"density-upload-readback.f32")!=Read(out/"density.f32")) throw std::runtime_error("GPU density upload mismatch");
    }
    for(UINT view=0;view<3;++view) for(UINT trial=0;trial<3;++trial) {
        Check(alloc->Reset()); Check(list->Reset(alloc.Get(),pso.Get()));
        Smoke s{}; XMStoreFloat4x4(&s.world,XMMatrixIdentity()); XMStoreFloat4x4(&s.view,XMMatrixIdentity());
        auto eye=eyes[view]; s.eye={-.75f+eye.x*1.5f,-.25f+eye.y*.5f,-.75f+eye.z*1.5f}; s.scale=1; s.colour={.7f,.7f,.7f}; s.absorption=4; s.light={1,-1,1}; s.stepScale=step*32;
        Camera c{}; c.eye=eye; c.fov=std::tan(XMConvertToRadians(35.f)/2); c.width=c.height=float(width);
        auto f=XMVector3Normalize(XMVectorSubtract(XMVectorSet(.5f,.5f,.5f,0),XMLoadFloat3(&eye)));
        auto r=XMVector3Normalize(XMVector3Cross(f,XMVectorSet(0,1,0,0))); auto u=XMVector3Cross(r,f);
        XMStoreFloat3(&c.forward,f); XMStoreFloat3(&c.right,r); XMStoreFloat3(&c.up,u);
        void* p; Check(constants->Map(0,nullptr,&p)); memset(p,0,512); memcpy(p,&s,sizeof(s)); memcpy(static_cast<char*>(p)+256,&c,sizeof(c)); constants->Unmap(0,nullptr);
        ID3D12DescriptorHeap* heaps[]={heap.Get()}; list->SetDescriptorHeaps(1,heaps); list->SetGraphicsRootSignature(root.Get()); list->SetGraphicsRootDescriptorTable(0,heap->GetGPUDescriptorHandleForHeapStart()); list->SetGraphicsRootConstantBufferView(1,constants->GetGPUVirtualAddress()); list->SetGraphicsRootConstantBufferView(2,constants->GetGPUVirtualAddress()+256);
        D3D12_VIEWPORT viewport{0,0,float(width),float(width),0,1}; D3D12_RECT scissor{0,0,LONG(width),LONG(width)}; list->RSSetViewports(1,&viewport); list->RSSetScissorRects(1,&scissor); list->OMSetRenderTargets(1,&rt,FALSE,nullptr); float clear[4]{}; list->ClearRenderTargetView(rt,clear,0,nullptr); list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); list->DrawInstanced(3,1,0,0);
        auto b=CD3DX12_RESOURCE_BARRIER::Transition(target.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE); list->ResourceBarrier(1,&b);
        auto dst=CD3DX12_TEXTURE_COPY_LOCATION(readback.Get(),fp), src=CD3DX12_TEXTURE_COPY_LOCATION(target.Get(),0); list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        b=CD3DX12_RESOURCE_BARRIER::Transition(target.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET); list->ResourceBarrier(1,&b); submit();
        Check(readback->Map(0,nullptr,&p)); std::ofstream image(out/("view"+std::to_string(view)+"-trial"+std::to_string(trial)+".rgba32f"),std::ios::binary);
        for(UINT y=0;y<width;++y) image.write(static_cast<char*>(p)+fp.Offset+size_t(y)*fp.Footprint.RowPitch,width*16); readback->Unmap(0,nullptr); if(!image) throw std::runtime_error("Image write failed");
    }
    CloseHandle(event);
    if (info) {
        std::ofstream messages(out/"debug-messages.txt");
        bool invalid=false;
        for (UINT64 i=0;i<info->GetNumStoredMessages();++i) {
            SIZE_T bytes=0; Check(info->GetMessage(i,nullptr,&bytes));
            std::vector<char> storage(bytes); auto message=reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            Check(info->GetMessage(i,message,&bytes)); messages << message->pDescription << "\n";
            invalid |= message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR;
        }
        if (invalid) throw std::runtime_error("D3D12 validation error; see debug-messages.txt");
    }
    std::ofstream(out/"debug-status.txt") << (info ? "enabled; zero errors" : "unavailable") << "\n";
    std::ofstream(out/"complete.txt") << "9 captures; 512x512 RGBA32F little endian; rows top to bottom\n";
    std::cout << "Captured N="<<n<<" step="<<step<<"\n"; return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<"\n"; return 1; }
