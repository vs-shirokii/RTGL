/*

Copyright (c) 2024 V.Shirokii

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

#include "DX12_Interop.h"

#ifdef RG_USE_DX12

#include "DebugPrint.h"
#include "LibraryConfig.h"

#include <d3d12.h>
#include <d3dx12.h>
#include <dxgi1_5.h>
#include <d3dcompiler.h>

#include <cassert>
#include <optional>
#include <span>
#include <variant>

namespace RTGL1::dxgi
{

// https://en.cppreference.com/w/cpp/utility/variant/visit
// helper type for the visitor #4
template< class... Ts >
struct overloaded : Ts...
{
    using Ts::operator()...;
};

template< typename T, typename Target >
concept is_of_type = std::same_as< std::remove_cvref_t< T >, Target >;


struct DxgiBackBuffer
{
    ID3D12Resource* d3d12resource{};
    HANDLE          sharedHandle{};
};
struct DxgiSwapchainInstance
{
    IDXGISwapChain4*              dxgiSwapchain{};
    IDXGISwapChain4*              dxgiSwapchain_proxy{};
    std::vector< DxgiBackBuffer > backbuffers{};
    DXGI_FORMAT                   format{ DXGI_FORMAT_UNKNOWN };
    bool                          vsync{ false };
    ID3D12Resource*               copySrc{};
    D3D12_MIP_REGION              copySrcSize{};
    bool                          toSrgb{ false };
};

struct ShaderInstance
{
    ID3DBlob*             code{};
    ID3D12PipelineState*  pipeline{};
    ID3DBlob*             signature{};
    ID3D12RootSignature*  rootSignature{};
    ID3D12DescriptorHeap* descriptorHeap{};
};

struct DX12Instance
{
    IDXGIFactory4*                    dxgiFactory{};
    IDXGIAdapter1*                    adapter{};
    uint64_t                          adapterLUID{ 0 };
    ID3D12Device*                     dx12device{};
    ID3D12CommandQueue*               graphicsQueue{};
    ID3D12CommandAllocator*           cmdAllocators[ MAX_FRAMES_IN_FLIGHT_DX12 ]{};
    std::vector< ID3D12CommandList* > cmdToFree[ MAX_FRAMES_IN_FLIGHT_DX12 ]{};
    ShaderInstance                    blitComputeShader{};
    DxgiSwapchainInstance             swapchain{};
};

struct DLFG_DX12 : DX12Instance
{
    IDXGIFactory4*         dlfg_dxgiFactory_proxy{};
    ID3D12Device*          dlfg_dx12device_proxy{};
    PFN_GetNativeInterface dlfg_pfnGetNativeInterface{ nullptr };
};

struct FSR3_DX12 : DX12Instance
{
    PFN_CreateSwapchain fsr3_pfnCreateFrameGenSwapchain{ nullptr };
};

struct Raw_DX12 : DX12Instance
{
};


auto g_dx12 = std::variant< std::monostate, DLFG_DX12, FSR3_DX12, Raw_DX12 >{};
auto g_hwnd = HWND{};


bool Semaphores_Create( ID3D12Device* dx12device );
void Semaphores_Destroy( ID3D12Device* dx12device );

namespace
{
    void DestroyHandle( auto** handle )
    {
        if( *handle )
        {
            auto left = ( *handle )->Release();
            if( left > 0 )
            {
                // DebugBreak();
            }

            ( *handle ) = nullptr;
        }
    }

    template< typename... Ts >
        requires( std::is_base_of_v< DX12Instance, Ts > && ... )
    auto Base( std::variant< std::monostate, Ts... >& dx12 ) -> DX12Instance*
    {
        return std::visit( //
            overloaded{
                []( std::monostate& ) -> DX12Instance* { return nullptr; },
                []( auto& ts ) -> DX12Instance* { return &ts; },
            },
            dx12 );
    }
}

void Destroy()
{
    std::visit( //
        overloaded{
            []( std::monostate& ) {},
            []< typename T >( T& dlfgOrFsr3 ) {
                dxgi::WaitIdle();

                Framebuf_Destroy();
                DestroySwapchain( false );
                Semaphores_Destroy( dlfgOrFsr3.dx12device );

                DestroyHandle( &dlfgOrFsr3.blitComputeShader.code );
                DestroyHandle( &dlfgOrFsr3.blitComputeShader.pipeline );
                DestroyHandle( &dlfgOrFsr3.blitComputeShader.signature );
                DestroyHandle( &dlfgOrFsr3.blitComputeShader.rootSignature );
                for( auto& vec : dlfgOrFsr3.cmdToFree )
                {
                    for( auto& cmd : vec )
                    {
                        DestroyHandle( &cmd );
                    }
                }
                for( auto& c : dlfgOrFsr3.cmdAllocators )
                {
                    DestroyHandle( &c );
                }
                DestroyHandle( &dlfgOrFsr3.adapter );
                dlfgOrFsr3.adapterLUID = 0;
                DestroyHandle( &dlfgOrFsr3.dxgiFactory );
                DestroyHandle( &dlfgOrFsr3.graphicsQueue );
                DestroyHandle( &dlfgOrFsr3.dx12device );

                if constexpr( is_of_type< T, DLFG_DX12 > )
                {
                    // TODO: is it safe?
                    DestroyHandle( &dlfgOrFsr3.dlfg_dxgiFactory_proxy );
                    DestroyHandle( &dlfgOrFsr3.dlfg_dx12device_proxy );
                }
            },
        },
        g_dx12 );

    g_dx12 = std::monostate{};
}

auto GetD3D12Device() -> ID3D12Device*
{
    return std::visit( //
        overloaded{
            []( std::monostate& ) {
                assert( 0 );
                return static_cast< ID3D12Device* >( nullptr );
            },
            []( DLFG_DX12& dlfg ) {
                assert( dlfg.dlfg_dx12device_proxy );
                return dlfg.dlfg_dx12device_proxy;
            },
            []( auto& other ) {
                assert( other.dx12device );
                return other.dx12device;
            },
        },
        g_dx12 );
}

auto GetD3D12CommandQueue() -> ID3D12CommandQueue*
{
    return std::visit( //
        overloaded{
            []( std::monostate& ) {
                assert( 0 );
                return static_cast< ID3D12CommandQueue* >( nullptr );
            },
            []( auto& dlfgOrFsr3 ) {
                assert( dlfgOrFsr3.graphicsQueue );
                return dlfgOrFsr3.graphicsQueue;
            },
        },
        g_dx12 );
}

auto GetAdapterLUID() -> uint64_t
{
    return std::visit( //
        overloaded{
            []( std::monostate& ) {
                assert( 0 );
                return uint64_t{ 0 };
            },
            []( auto& dlfgOrFsr3 ) {
                assert( dlfgOrFsr3.adapter );
                return dlfgOrFsr3.adapterLUID;
            },
        },
        g_dx12 );
}

auto CreateD3D12CommandList( uint32_t frameIndex ) -> ID3D12GraphicsCommandList*
{
    assert( frameIndex < MAX_FRAMES_IN_FLIGHT_DX12 );
    auto hr = HRESULT{};

    DX12Instance* dx12 = Base( g_dx12 );
    if( !dx12 )
    {
        return nullptr;
    }

    ID3D12Device* dx12device = GetD3D12Device();
    if( !dx12device )
    {
        debug::Error( "CreateD3D12CommandList failed: no D3D12 device" );
        assert( 0 );
        return nullptr;
    }

    ID3D12GraphicsCommandList* cmd{};
    hr = dx12device->CreateCommandList( 0,
                                        D3D12_COMMAND_LIST_TYPE_DIRECT,
                                        dx12->cmdAllocators[ frameIndex ],
                                        nullptr,
                                        IID_PPV_ARGS( &cmd ) );
    if( FAILED( hr ) )
    {
        debug::Error( "ID3D12Device::CreateCommandList failed: {:08x}", uint32_t( hr ) );
        Destroy();
        return nullptr;
    }

    dx12->cmdToFree[ frameIndex ].push_back( cmd );
    return cmd;
}


template< uint32_t GroupSize >
    requires( GroupSize > 0 )
constexpr uint32_t ThreadGroupCount( uint32_t size )
{
    return ( size + ( GroupSize - 1 ) ) / GroupSize;
}


void FillBlitPipelineDescriptors( DX12Instance* dx12, ID3D12Resource* src, ID3D12Resource* dst )
{
    {
        const auto dstDesc = dst->GetDesc();

        auto uavDesc = D3D12_UNORDERED_ACCESS_VIEW_DESC{
            .Format        = dstDesc.Format,
            .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D,
            .Texture2D     = { .MipSlice = 0, .PlaneSlice = 0 },
        };

        dx12->dx12device->CreateUnorderedAccessView(
            dst,
            nullptr,
            &uavDesc,
            CD3DX12_CPU_DESCRIPTOR_HANDLE{
                dx12->blitComputeShader.descriptorHeap->GetCPUDescriptorHandleForHeapStart(),
                0,
            } );
    }
    {
        const auto srcDesc = src->GetDesc();

        auto srvDesc = D3D12_SHADER_RESOURCE_VIEW_DESC{
            .Format                  = srcDesc.Format,
            .ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D,
            .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
            .Texture2D               = { .MostDetailedMip     = 0,
                                         .MipLevels           = 1,
                                         .PlaneSlice          = 0,
                                         .ResourceMinLODClamp = 0.0f },
        };

        dx12->dx12device->CreateShaderResourceView(
            src,
            &srvDesc,
            CD3DX12_CPU_DESCRIPTOR_HANDLE{
                dx12->blitComputeShader.descriptorHeap->GetCPUDescriptorHandleForHeapStart(),
                1,
                dx12->dx12device->GetDescriptorHandleIncrementSize(
                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ),
            } );
    }
}

void DispatchBlit( ID3D12GraphicsCommandList* dx12cmd,
                   ID3D12Resource*            src,
                   ID3D12Resource*            dst,
                   uint32_t                   dst_width,
                   uint32_t                   dst_height,
                   bool                       dst_tosrgb )
{
    auto dx12 = Base( g_dx12 );
    if( !dx12 )
    {
        assert( 0 );
        return;
    }

    const ShaderInstance& inst = dx12->blitComputeShader;

    constexpr bool recreateDescriptors = true;

    if( recreateDescriptors )
    {
        FillBlitPipelineDescriptors( dx12, src, dst );
    }

    dx12cmd->SetPipelineState( inst.pipeline );
    dx12cmd->SetComputeRootSignature( inst.rootSignature );
    dx12cmd->SetDescriptorHeaps( 1, &inst.descriptorHeap );

    {
        dx12cmd->SetComputeRootDescriptorTable(
            0,
            CD3DX12_GPU_DESCRIPTOR_HANDLE{
                inst.descriptorHeap->GetGPUDescriptorHandleForHeapStart(),
                0,
            } );
        dx12cmd->SetComputeRootDescriptorTable(
            1,
            CD3DX12_GPU_DESCRIPTOR_HANDLE{
                inst.descriptorHeap->GetGPUDescriptorHandleForHeapStart(),
                1,
                dx12->dx12device->GetDescriptorHandleIncrementSize(
                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ),
            } );

        const uint32_t data[] = { dst_width, dst_height, dst_tosrgb };
        dx12cmd->SetComputeRoot32BitConstants( 2, std::size( data ), data, 0 );
    }


    dx12cmd->Dispatch( ThreadGroupCount< 16 >( dst_width ), //
                       ThreadGroupCount< 16 >( dst_height ),
                       1 );
}


namespace
{
    auto CreateComputeShader( ID3D12Device*                       device,
                              const char*                         shaderText,
                              std::span< CD3DX12_ROOT_PARAMETER > rootParams,
                              const D3D12_DESCRIPTOR_HEAP_DESC&   heapDesc )
        -> std::optional< ShaderInstance >
    {
        ID3DBlob*             shaderSource{};
        ID3D12PipelineState*  pipelineState{};
        ID3DBlob*             signature{};
        ID3D12RootSignature*  rootSignature{};
        ID3D12DescriptorHeap* descriptorHeap{};
        HRESULT               hr{};

        ID3DBlob* errorMsg = nullptr;

        hr = D3DCompile( shaderText,
                         strlen( shaderText ),
                         nullptr,
                         nullptr,
                         nullptr,
                         "main",
                         "cs_5_0",
                         D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
                         0,
                         &shaderSource,
                         &errorMsg );
        if( FAILED( hr ) || !shaderSource )
        {
            const char* errorMsgCStr = errorMsg //
                                           ? static_cast< char* >( errorMsg->GetBufferPointer() )
                                           : nullptr;
            debug::Error( "D3DCompile failed: {:08x}: {}",
                          uint32_t( hr ),
                          errorMsgCStr ? errorMsgCStr : "<no error msg>" );
            return std::nullopt;
        }

        // Create the root signature description
        auto rootSignatureDesc = D3D12_ROOT_SIGNATURE_DESC{
            .NumParameters     = static_cast< uint32_t >( std::size( rootParams ) ),
            .pParameters       = std::data( rootParams ),
            .NumStaticSamplers = 0,
            .pStaticSamplers   = nullptr,
            .Flags             = D3D12_ROOT_SIGNATURE_FLAG_NONE,
        };

        // Serialize the root signature
        hr = D3D12SerializeRootSignature( &rootSignatureDesc, //
                                          D3D_ROOT_SIGNATURE_VERSION_1,
                                          &signature,
                                          &errorMsg );
        if( FAILED( hr ) )
        {
            const char* errorMsgCStr = errorMsg //
                                           ? static_cast< char* >( errorMsg->GetBufferPointer() )
                                           : nullptr;
            debug::Error( "D3D12SerializeRootSignature failed: {:08x}: {}",
                          uint32_t( hr ),
                          errorMsgCStr ? errorMsgCStr : "<no error msg>" );
            DestroyHandle( &shaderSource );
            return std::nullopt;
        }

        // Create the root signature
        hr = device->CreateRootSignature( 0,
                                          signature->GetBufferPointer(),
                                          signature->GetBufferSize(),
                                          IID_PPV_ARGS( &rootSignature ) );
        if( FAILED( hr ) )
        {
            debug::Error( "ID3D12Device::CreateRootSignature failed: {:08x}: {}", uint32_t( hr ) );
            DestroyHandle( &signature );
            DestroyHandle( &shaderSource );
            return std::nullopt;
        }

        auto psoDesc = D3D12_COMPUTE_PIPELINE_STATE_DESC{
            .pRootSignature = rootSignature,
            .CS =
                D3D12_SHADER_BYTECODE{
                    .pShaderBytecode = shaderSource->GetBufferPointer(),
                    .BytecodeLength  = shaderSource->GetBufferSize(),
                },
            .NodeMask = 0,
            .Flags    = D3D12_PIPELINE_STATE_FLAG_NONE,
        };

        hr = device->CreateComputePipelineState( &psoDesc, IID_PPV_ARGS( &pipelineState ) );
        if( FAILED( hr ) || !pipelineState )
        {
            debug::Error( "ID3D12Device::CreateComputePipelineState failed: {:08x}",
                          uint32_t( hr ) );
            DestroyHandle( &signature );
            DestroyHandle( &shaderSource );
            DestroyHandle( &shaderSource );
            return std::nullopt;
        }

        hr = device->CreateDescriptorHeap( &heapDesc, IID_PPV_ARGS( &descriptorHeap ) );
        if( FAILED( hr ) || !descriptorHeap )
        {
            debug::Error( "ID3D12Device::CreateDescriptorHeap failed: {:08x}", uint32_t( hr ) );
            DestroyHandle( &pipelineState );
            DestroyHandle( &signature );
            DestroyHandle( &shaderSource );
            DestroyHandle( &shaderSource );
            return std::nullopt;
        }

        return ShaderInstance{
            .code           = shaderSource,
            .pipeline       = pipelineState,
            .signature      = signature,
            .rootSignature  = rootSignature,
            .descriptorHeap = descriptorHeap,
        };
    }

    auto CreateBlitShader( ID3D12Device* device )
    {
        const char* blitShader = R"(
            RWTexture2D<float4> dst : register( u0 );
            Texture2D<float4> src : register( t0 );
            cbuffer DstSize : register( b0 ) {
               uint dst_width;
               uint dst_height;
               uint dst_tosrgb;
            };

            float LinearToSrgb( float x )
            {
                return x <= 0.0031308f
                    ? x * 12.92f
                    : ( 1.055f * pow( x, 1.0f / 2.4f ) ) - 0.055f;
            }

            [numthreads(16, 16, 1)]
            void main( uint3 DispatchThreadID : SV_DispatchThreadID )
            {
                if ( DispatchThreadID.x >= dst_width || DispatchThreadID.y >= dst_height )
                {
                    return;
                }

                float4 c = src.Load( int3( DispatchThreadID.xy, 0 ) );

                if ( dst_tosrgb != 0 )
                {
                    c.rgb = float3(
                        LinearToSrgb( c.r ),
                        LinearToSrgb( c.g ),
                        LinearToSrgb( c.b )
                    );
                }

                dst[ DispatchThreadID.xy ] = c;
            }
        )";

        CD3DX12_DESCRIPTOR_RANGE ranges[ 2 ];
        ranges[ 0 ].Init( D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0 ); // 1 UAV at register u0.
        ranges[ 1 ].Init( D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0 ); // 1 SRV at register t0.

        CD3DX12_ROOT_PARAMETER rootParams[ 3 ];
        rootParams[ 0 ].InitAsDescriptorTable( 1, &ranges[ 0 ] ); // UAV descriptor table
        rootParams[ 1 ].InitAsDescriptorTable( 1, &ranges[ 1 ] ); // SRV descriptor table
        rootParams[ 2 ].InitAsConstants( 3, 0 );                  // b0

        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {
            .Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
            .NumDescriptors = 2,
            .Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
        };

        return CreateComputeShader( device, blitShader, rootParams, heapDesc );
    }

    template< typename T >
        requires( std::is_base_of_v< DX12Instance, T > )
    auto Create( uint64_t               gpuLuid,
                 PFN_CreateSwapchain    pfn_CreateFrameGenSwapchain,
                 PFN_SetD3D12           pfn_SetD3D12,
                 PFN_UpgradeInterface   pfn_UpgradeInterface,
                 PFN_GetNativeInterface pfn_GetNativeInterface ) -> std::optional< T >
    {
        if constexpr( std::is_same_v< T, DLFG_DX12 > )
        {
            assert( pfn_SetD3D12 && pfn_UpgradeInterface && pfn_GetNativeInterface );
        }
        else if constexpr( std::is_same_v< T, FSR3_DX12 > )
        {
            assert( pfn_CreateFrameGenSwapchain );
        }

        auto dx12 = T{};
        auto hr   = HRESULT{};

        hr = CreateDXGIFactory1( IID_PPV_ARGS( &dx12.dxgiFactory ) );
        if( FAILED( hr ) )
        {
            debug::Error( "CreateDXGIFactory1 failed: {:08x}", uint32_t( hr ) );
            return std::nullopt;
        }

        // adapter

        dx12.adapter     = nullptr;
        dx12.adapterLUID = 0;
        {
            IDXGIAdapter1* dxgiAdapter = nullptr;
            for( UINT i = 0;
                 dx12.dxgiFactory->EnumAdapters1( i, &dxgiAdapter ) != DXGI_ERROR_NOT_FOUND;
                 ++i )
            {
                if( dxgiAdapter )
                {
                    auto desc = DXGI_ADAPTER_DESC1{};
                    dxgiAdapter->GetDesc1( &desc );

                    uint64_t l = 0;

                    static_assert( sizeof( gpuLuid ) == sizeof( l ) );
                    static_assert( sizeof( l ) == sizeof( desc.AdapterLuid ) );
                    memcpy( &l, &desc.AdapterLuid, sizeof( l ) );

                    if( gpuLuid != l )
                    {
                        dxgiAdapter->Release();
                        dxgiAdapter = nullptr;
                        continue;
                    }

                    // found
                    dx12.adapter     = dxgiAdapter;
                    dx12.adapterLUID = gpuLuid;
                    break;
                }
            }
        }

        if( dx12.adapter == nullptr )
        {
            debug::Error( "Failed to find GPU with LUID={}. DX12 features are disabled.", gpuLuid );
            DestroyHandle( &dx12.dxgiFactory );
            return std::nullopt;
        }

        if( LibConfig().dx12Validation )
        {
            ID3D12Debug* debugController;
            if( SUCCEEDED( D3D12GetDebugInterface( IID_PPV_ARGS( &debugController ) ) ) )
            {
                debugController->EnableDebugLayer();
            }
        }

        // d3d device

        hr = D3D12CreateDevice( dx12.adapter, //
                                D3D_FEATURE_LEVEL_12_2,
                                IID_PPV_ARGS( &dx12.dx12device ) );
        if( FAILED( hr ) )
        {
            debug::Error( "D3D12CreateDevice failed: {:08x}", uint32_t( hr ) );
            DestroyHandle( &dx12.adapter );
            DestroyHandle( &dx12.dxgiFactory );
            return std::nullopt;
        }

        if constexpr( std::is_same_v< T, DLFG_DX12 > )
        {
            dx12.dlfg_dx12device_proxy = dx12.dx12device;
            pfn_UpgradeInterface( ( void** )&dx12.dlfg_dx12device_proxy );

            pfn_SetD3D12( dx12.dlfg_dx12device_proxy );
        }

        // fences

        if( !Semaphores_Create( dx12.dx12device ) )
        {
            DestroyHandle( &dx12.dx12device );
            DestroyHandle( &dx12.adapter );
            DestroyHandle( &dx12.dxgiFactory );
            return std::nullopt;
        }

        // shaders

        if( auto s = CreateBlitShader( dx12.dx12device ) )
        {
            dx12.blitComputeShader = *s;
        }
        else
        {
            Semaphores_Destroy( dx12.dx12device );
            DestroyHandle( &dx12.dx12device );
            DestroyHandle( &dx12.adapter );
            DestroyHandle( &dx12.dxgiFactory );
            return std::nullopt;
        }

        // queue

        auto queueDesc = D3D12_COMMAND_QUEUE_DESC{
            .Type     = D3D12_COMMAND_LIST_TYPE_DIRECT,
            .Priority = 0,
            .Flags    = D3D12_COMMAND_QUEUE_FLAG_NONE,
            .NodeMask = 0,
        };

        if constexpr( std::is_same_v< T, DLFG_DX12 > )
        {
            hr = dx12.dlfg_dx12device_proxy->CreateCommandQueue(
                &queueDesc, IID_PPV_ARGS( &dx12.graphicsQueue ) );
        }
        else
        {
            hr = dx12.dx12device->CreateCommandQueue( &queueDesc,
                                                      IID_PPV_ARGS( &dx12.graphicsQueue ) );
        }

        if( FAILED( hr ) )
        {
            debug::Error( "ID3D12Device::CreateCommandQueue failed: {:08x}", uint32_t( hr ) );
            Semaphores_Destroy( dx12.dx12device );
            DestroyHandle( &dx12.dx12device );
            DestroyHandle( &dx12.adapter );
            DestroyHandle( &dx12.dxgiFactory );
            return std::nullopt;
        }

        // TODO: can move to device create?
        if constexpr( std::is_same_v< T, DLFG_DX12 > )
        {
            dx12.dlfg_dxgiFactory_proxy = dx12.dxgiFactory;
            pfn_UpgradeInterface( reinterpret_cast< void** >( &dx12.dlfg_dxgiFactory_proxy ) );
        }

        // cmds

        bool fail = false;
        for( auto& cmdAllocator : dx12.cmdAllocators )
        {
            hr = dx12.dx12device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                          IID_PPV_ARGS( &cmdAllocator ) );
            if( FAILED( hr ) )
            {
                debug::Error( "ID3D12Device::CreateCommandAllocator failed: {:08x}",
                              uint32_t( hr ) );
                fail = true;
                break;
            }

            hr = cmdAllocator->Reset();
            if( FAILED( hr ) )
            {
                debug::Error( "ID3D12CommandAllocator::Reset failed: {:08x}", uint32_t( hr ) );
                fail = true;
                break;
            }
        }
        if( fail )
        {
            for( auto& c : dx12.cmdAllocators )
            {
                DestroyHandle( &c );
            }
            DestroyHandle( &dx12.graphicsQueue );
            Semaphores_Destroy( dx12.dx12device );
            DestroyHandle( &dx12.dx12device );
            DestroyHandle( &dx12.adapter );
            DestroyHandle( &dx12.dxgiFactory );
            return std::nullopt;
        }


        if constexpr( std::is_same_v< T, DLFG_DX12 > )
        {
            dx12.dlfg_pfnGetNativeInterface = pfn_GetNativeInterface;
        }
        else if constexpr( std::is_same_v< T, FSR3_DX12 > )
        {
            dx12.fsr3_pfnCreateFrameGenSwapchain = pfn_CreateFrameGenSwapchain;
        }

        return dx12;
    }
}

bool InitAsFSR3( uint64_t gpuLuid, PFN_CreateSwapchain pfn_CreateFrameGenSwapchain )
{
    assert( std::holds_alternative< std::monostate >( g_dx12 ) );

    if( auto d = Create< FSR3_DX12 >( gpuLuid, //
                                      pfn_CreateFrameGenSwapchain,
                                      nullptr,
                                      nullptr,
                                      nullptr ) )
    {
        g_dx12 = std::move( d.value() );
        return true;
    }
    return false;
}

bool InitAsDLFG( uint64_t               gpuLuid,
                 PFN_SetD3D12           pfn_SetD3D12,
                 PFN_UpgradeInterface   pfn_UpgradeInterface,
                 PFN_GetNativeInterface pfn_GetNativeInterface )
{
    assert( std::holds_alternative< std::monostate >( g_dx12 ) );

    if( auto d = Create< DLFG_DX12 >( gpuLuid, //
                                      nullptr,
                                      pfn_SetD3D12,
                                      pfn_UpgradeInterface,
                                      pfn_GetNativeInterface ) )
    {
        g_dx12 = std::move( d.value() );
        return true;
    }
    return false;
}

auto InitAsRawDXGI( uint64_t gpuLuid ) -> std::expected< bool, const char* >
{
    assert( std::holds_alternative< std::monostate >( g_dx12 ) );

    if( auto d = Create< Raw_DX12 >( gpuLuid, //
                                     nullptr,
                                     nullptr,
                                     nullptr,
                                     nullptr ) )
    {
        g_dx12 = std::move( d.value() );
        return true;
    }
    return std::unexpected{ "Failed to initialize DirectX 12" };
}

void SetHwnd( void* hwnd )
{
    g_hwnd = static_cast< HWND >( hwnd );
}

bool HasDX12Instance()
{
    return g_hwnd && !std::holds_alternative< std::monostate >( g_dx12 );
}

bool HasDX12SwapchainInstance()
{
    if( HasDX12Instance() )
    {
        if( DX12Instance* dx12 = Base( g_dx12 ) )
        {
            return dx12->swapchain.dxgiSwapchain && !dx12->swapchain.backbuffers.empty();
        }
    }
    return false;
}

bool HasRawDXGI()
{
    return HasDX12Instance() && std::holds_alternative< Raw_DX12 >( g_dx12 );
}

namespace
{
    auto RetrieveBackbuffers( IDXGISwapChain4* dxgiSwapchain, IDXGISwapChain4* dxgiSwapchain_proxy )
        -> std::vector< DxgiBackBuffer >
    {
        ID3D12Device* dx12device = GetD3D12Device();
        if( !dx12device )
        {
            debug::Error( "RetrieveBackbuffers failed: no D3D12 device" );
            assert( 0 );
            return {};
        }

        auto hr   = HRESULT{};
        auto desc = DXGI_SWAP_CHAIN_DESC{};
        hr        = ( dxgiSwapchain_proxy ? dxgiSwapchain_proxy : dxgiSwapchain )->GetDesc( &desc );
        if( FAILED( hr ) )
        {
            debug::Error( "IDXGISwapChain::GetDesc failed: {}", hr );
            assert( 0 );
            return {};
        }

        auto backbuffers = std::vector< DxgiBackBuffer >{ desc.BufferCount };

        bool failed = false;
        for( uint32_t i = 0; i < desc.BufferCount; i++ )
        {
            DxgiBackBuffer& dst = backbuffers[ i ];

            hr = ( dxgiSwapchain_proxy ? dxgiSwapchain_proxy : dxgiSwapchain )
                     ->GetBuffer( i, IID_PPV_ARGS( &dst.d3d12resource ) );
            if( FAILED( hr ) || !dst.d3d12resource )
            {
                debug::Error( "IDXGISwapChain::GetBuffer failed: {:08x}", uint32_t( hr ) );
                failed = true;
                break;
            }
        }

        if( failed )
        {
            for( DxgiBackBuffer& d : backbuffers )
            {
                if( d.d3d12resource )
                {
                    d.d3d12resource->Release();
                }
                if( d.sharedHandle )
                {
                    CloseHandle( d.sharedHandle );
                }
            }
            return {};
        }
        return backbuffers;
    }
}


extern int internal_VkFormatToDxgiFormat( int vkformat );
extern int internal_VkColorSpaceToDxgiColorSpace( int vkcolorspace );

DXGI_FORMAT StripSrgb( DXGI_FORMAT f )
{
    // passing SRGB to DXGI_SWAP_CHAIN_DESC1 fails
    switch( f )
    {
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM;
        default: return f;
    }
}


auto CreateSwapchain( uint32_t width, //
                      uint32_t height,
                      uint32_t imageCount,
                      int      vkformat,
                      int      vkcolorspace,
                      bool     vsync ) -> uint32_t
{
    if( !HasDX12Instance() )
    {
        assert( 0 );
        return 0;
    }

    DxgiSwapchainInstance& sw = Base( g_dx12 )->swapchain;
    assert( !sw.dxgiSwapchain && !sw.dxgiSwapchain_proxy && sw.backbuffers.empty() );
    sw = {};

    auto desc1 = DXGI_SWAP_CHAIN_DESC1{
        .Width       = width,
        .Height      = height,
        .Format      = DXGI_FORMAT( internal_VkFormatToDxgiFormat( vkformat ) ),
        .SampleDesc  = { .Count = 1 },
        .BufferUsage = 0,
        .BufferCount = imageCount,
        .SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD,
        .Flags       = ( vsync ? 0u : DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING ) |
                 DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH,
    };

    if( desc1.Format != StripSrgb( desc1.Format ) )
    {
        desc1.Format = StripSrgb( desc1.Format );
        sw.toSrgb    = true;
    }

    if( auto dlfg = std::get_if< DLFG_DX12 >( &g_dx12 ) )
    {
        assert( dlfg->dlfg_dxgiFactory_proxy );
        HRESULT hr = dlfg->dlfg_dxgiFactory_proxy->CreateSwapChainForHwnd(
            dlfg->graphicsQueue,
            g_hwnd,
            &desc1,
            nullptr,
            nullptr,
            reinterpret_cast< IDXGISwapChain1** >( &sw.dxgiSwapchain_proxy ) );

        if( FAILED( hr ) )
        {
            debug::Error( "IDXGIFactory2::CreateSwapChainForHwnd for DLFG failed: {:08x}",
                          uint32_t( hr ) );
            Destroy();
            return 0;
        }
        dlfg->dlfg_pfnGetNativeInterface( sw.dxgiSwapchain_proxy, ( void** )&sw.dxgiSwapchain );
    }
    else if( auto fsr3 = std::get_if< FSR3_DX12 >( &g_dx12 ) )
    {
        assert( fsr3->fsr3_pfnCreateFrameGenSwapchain );
        sw.dxgiSwapchain = fsr3->fsr3_pfnCreateFrameGenSwapchain( fsr3->dxgiFactory, //
                                                                  fsr3->graphicsQueue,
                                                                  g_hwnd,
                                                                  &desc1 );
        if( !sw.dxgiSwapchain )
        {
            debug::Error( "fsr3_pfnCreateFrameGenSwapchain failed" );
            Destroy();
            return 0;
        }
        sw.dxgiSwapchain_proxy = nullptr;
    }
    else if( auto raw = std::get_if< Raw_DX12 >( &g_dx12 ) )
    {
        assert( raw->dxgiFactory );
        HRESULT hr = raw->dxgiFactory->CreateSwapChainForHwnd(
            raw->graphicsQueue,
            g_hwnd,
            &desc1,
            nullptr,
            nullptr,
            reinterpret_cast< IDXGISwapChain1** >( &sw.dxgiSwapchain ) );

        if( FAILED( hr ) )
        {
            debug::Error( "IDXGIFactory2::CreateSwapChainForHwnd for DXGI failed: {:08x}",
                          uint32_t( hr ) );
            Destroy();
            return 0;
        }
        sw.dxgiSwapchain_proxy = nullptr;

        // From D3D12Fullscreen sample
        // When tearing support is enabled we will handle ALT+Enter key presses in the
        // window message loop rather than let DXGI handle it by calling SetFullscreenState.
        raw->dxgiFactory->MakeWindowAssociation( g_hwnd, DXGI_MWA_NO_ALT_ENTER );
    }
    else
    {
        assert( 0 );
        Destroy();
        return 0;
    }

    // HDR
    {
        const auto dxgiColorSpace =
            DXGI_COLOR_SPACE_TYPE( internal_VkColorSpaceToDxgiColorSpace( vkcolorspace ) );

        HRESULT hr = sw.dxgiSwapchain->SetColorSpace1( dxgiColorSpace );
        if( FAILED( hr ) )
        {
            debug::Error(
                "IDXGISwapChain3::SetColorSpace1 failed trying to set DXGI_COLOR_SPACE_TYPE={}",
                int( dxgiColorSpace ) );
        }
    }

    sw.backbuffers = RetrieveBackbuffers( sw.dxgiSwapchain, //
                                          sw.dxgiSwapchain_proxy );
    if( sw.backbuffers.empty() )
    {
        Destroy();
        return 0;
    }

    {
        D3D12_RESOURCE_DESC descCopySrc = sw.backbuffers[ 0 ].d3d12resource->GetDesc();

        descCopySrc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        auto heapProps = D3D12_HEAP_PROPERTIES{
            .Type = D3D12_HEAP_TYPE_DEFAULT,
        };
        HRESULT hr =
            Base( g_dx12 )->dx12device->CreateCommittedResource( &heapProps,
                                                                 D3D12_HEAP_FLAG_NONE,
                                                                 &descCopySrc,
                                                                 D3D12_RESOURCE_STATE_COMMON,
                                                                 nullptr,
                                                                 IID_PPV_ARGS( &sw.copySrc ) );
        if( FAILED( hr ) || !sw.copySrc )
        {
            debug::Error( "CreateSwapchain: ID3D12Device::CreateCommittedResource failed ({})",
                          hr );
            Destroy();
            return 0;
        }

        sw.copySrcSize = D3D12_MIP_REGION{
            static_cast< UINT >( descCopySrc.Width ),
            descCopySrc.Height,
            1,
        };
    }

    sw.vsync  = vsync;
    sw.format = desc1.Format;

    return static_cast< uint32_t >( sw.backbuffers.size() );
}

auto GetSwapchainDxgiFormat() -> int
{
    if( DX12Instance* dx12 = Base( g_dx12 ) )
    {
        assert( dx12->swapchain.format != DXGI_FORMAT_UNKNOWN );
        return dx12->swapchain.format;
    }
    assert( 0 );
    return DXGI_FORMAT_UNKNOWN;
}

// Only for FSR3
auto GetSwapchainDxgiSwapchain() -> IDXGISwapChain4*
{
    assert( std::holds_alternative< FSR3_DX12 >( g_dx12 ) );

    if( DX12Instance* dx12 = Base( g_dx12 ) )
    {
        assert( dx12->swapchain.dxgiSwapchain );
        return dx12->swapchain.dxgiSwapchain;
    }
    assert( 0 );
    return nullptr;
}

namespace
{
    void WaitForGPUToComplete( ID3D12Fence* fence, HANDLE event, uint64_t fenceValueToWait )
    {
        if( !fence )
        {
            assert( 0 );
            return;
        }

        UINT64 completed = fence->GetCompletedValue();
        if( completed >= fenceValueToWait )
        {
            return;
        }

        HRESULT hr = fence->SetEventOnCompletion( fenceValueToWait, event );
        if( FAILED( hr ) )
        {
            assert( 0 );
            return;
        }

        DWORD dw = WaitForSingleObject( event, 1000 );
        assert( dw == WAIT_OBJECT_0 );
    }

    void InsertFenceAndWait( DX12Instance& dx12 )
    {
        if( !dx12.dx12device || !dx12.graphicsQueue )
        {
            assert( 0 );
            return;
        }

        ID3D12Fence* tempFence{ nullptr };
        HRESULT      hr{};

        constexpr uint64_t InitValue = 0;
        constexpr uint64_t WaitValue = 1;

        hr = dx12.dx12device->CreateFence( InitValue, //
                                           D3D12_FENCE_FLAG_NONE,
                                           IID_PPV_ARGS( &tempFence ) );
        if( FAILED( hr ) )
        {
            debug::Error( "ID3D12Device::CreateFence failed: {:08x}", uint32_t( hr ) );
            return;
        }

        HANDLE tempEvent = CreateEventEx( nullptr, //
                                          nullptr,
                                          0,
                                          EVENT_ALL_ACCESS );
        if( !tempEvent )
        {
            debug::Error( "InsertFenceAndWait: CreateEventEx failed" );
            return;
        }

        hr = dx12.graphicsQueue->Signal( tempFence, WaitValue );
        assert( SUCCEEDED( hr ) );
        if( FAILED( hr ) )
        {
            debug::Error( "ID3D12CommandQueue::Signal failed: {:08x}", uint32_t( hr ) );
            DestroyHandle( &tempFence );
            return;
        }

        WaitForGPUToComplete( tempFence, tempEvent, WaitValue );
        DestroyHandle( &tempFence );
        BOOL c = CloseHandle( tempEvent );
        assert( c );
    }
}

void WaitIdle()
{
    if( DX12Instance* dx12 = Base( g_dx12 ) )
    {
        InsertFenceAndWait( *dx12 );
    }

    // HACKHACK: this will enforce FSR3 to enter a critical section,
    //           so it would be safer to recreate resources
    {
        if( std::holds_alternative< FSR3_DX12 >( g_dx12 ) && HasDX12SwapchainInstance() )
        {
            dxgi::GetCurrentBackBufferIndex();
        }
    }
}

void DestroySwapchain( bool waitForIdle )
{
    if( DX12Instance* dx12 = Base( g_dx12 ) )
    {
        if( waitForIdle )
        {
            WaitIdle();
        }

        DestroyHandle( &dx12->swapchain.copySrc );
        for( DxgiBackBuffer& buf : dx12->swapchain.backbuffers )
        {
            if( buf.sharedHandle )
            {
                auto b = CloseHandle( buf.sharedHandle );
                assert( b );
            }
            if( buf.d3d12resource )

            {
                buf.d3d12resource->Release();
            }
        }
        DestroyHandle( &dx12->swapchain.dxgiSwapchain_proxy );
        DestroyHandle( &dx12->swapchain.dxgiSwapchain );

        dx12->swapchain = {};
    }
}

auto GetSwapchainBack( uint32_t i ) -> ID3D12Resource*
{
    if( DX12Instance* dx12 = Base( g_dx12 ) )
    {
        return dx12->swapchain.backbuffers[ i ].d3d12resource;
    }
    assert( 0 );
    return nullptr;
}

auto GetSwapchainCopySrc( uint32_t* width, uint32_t* height, bool *convertToSrgb ) -> ID3D12Resource*
{
    if( DX12Instance* dx12 = Base( g_dx12 ) )
    {
        if( width )
        {
            *width = dx12->swapchain.copySrcSize.Width;
        }
        if( height )
        {
            *height = dx12->swapchain.copySrcSize.Height;
        }
        if( convertToSrgb )
        {
            *convertToSrgb = dx12->swapchain.toSrgb;
        }
        return dx12->swapchain.copySrc;
    }
    assert( 0 );
    return nullptr;
}

bool DX12Supported()
{
    // TODO
    return true;
}

void WaitAndPrepareForFrame( ID3D12Fence* fence, HANDLE fenceEvent, uint64_t currentTimelineFrame )
{
    if( !fence )
    {
        debug::Warning( "Skipping DX12 WaitAndPrepareForFrame, as {}",
                        dxgi::HasDX12Instance() ? "DX12 was destroyed"
                                                : "Semaphores_GetVkDx12Shared failed" );
        return;
    }

    if( currentTimelineFrame < MAX_FRAMES_IN_FLIGHT_DX12 )
    {
        return;
    }

    // wait frame N-1 to finish, so we can reuse its resources
    WaitForGPUToComplete( fence, fenceEvent, currentTimelineFrame - 1 );

    DX12Instance* dx12 = Base( g_dx12 );
    if( !dx12 )
    {
        assert( 0 );
        return;
    }

    const uint64_t frameIndex = currentTimelineFrame % MAX_FRAMES_IN_FLIGHT_DX12;

    // clear and reset
    {
        for( auto cmd : dx12->cmdToFree[ frameIndex ] )
        {
            DestroyHandle( &cmd );
        }
        dx12->cmdToFree[ frameIndex ].clear();

        auto hr = dx12->cmdAllocators[ frameIndex ]->Reset();
        if( FAILED( hr ) )
        {
            debug::Error( "ID3D12CommandAllocator::Reset failed: {:08x}", uint32_t( hr ) );
            Destroy();
            return;
        }
    }
}

void Present( ID3D12Fence* fence, uint64_t waitValue )
{
    if( !HasDX12SwapchainInstance() )
    {
        return;
    }
    DX12Instance* dx12 = Base( g_dx12 );
    assert( dx12 );

    if( !fence )
    {
        assert( 0 );
        return;
    }

    HRESULT hr = S_OK;

    hr = dx12->graphicsQueue->Wait( fence, waitValue );
    if( FAILED( hr ) )
    {
        debug::Error( "ID3D12CommandQueue::Wait failed: {:08x}", uint32_t( hr ) );
        Destroy();
        return;
    }

    IDXGISwapChain4* sw = dx12->swapchain.dxgiSwapchain_proxy ? dx12->swapchain.dxgiSwapchain_proxy
                                                              : dx12->swapchain.dxgiSwapchain;
    if( !sw )
    {
        debug::Error( "No DX12 swapchain" );
        Destroy();
        return;
    }

    hr = sw->Present( 0, //
                      dx12->swapchain.vsync ? 0 : DXGI_PRESENT_ALLOW_TEARING );
    if( FAILED( hr ) )
    {
        debug::Error( "IDXGIFactory2::Present failed: {:08x}", uint32_t( hr ) );
        Destroy();
        return;
    }
}

auto GetCurrentBackBufferIndex() -> uint32_t
{
    if( !HasDX12SwapchainInstance() )
    {
        assert( 0 );
        return 0;
    }
    auto& sw = Base( g_dx12 )->swapchain;

    return ( sw.dxgiSwapchain_proxy ? sw.dxgiSwapchain_proxy : sw.dxgiSwapchain )
        ->GetCurrentBackBufferIndex();
}
}

#else

#include <cassert>

namespace RTGL1::dxgi
{

bool Supports()
{
    return false;
}
void Init() {}
void InitHwnd( void* hwnd ) {}
void InitSemaphores( void* handle ) {}
void Destroy() {}
void CreateSwapchain( uint32_t width, uint32_t height, uint32_t imageCount ) {}
void DestroySwapchain() {}
void Present( uint64_t fenceValue ) {}
auto GetCurrentBackBufferIndex() -> uint32_t
{
    assert( 0 );
    return 0;
}

}

#endif
