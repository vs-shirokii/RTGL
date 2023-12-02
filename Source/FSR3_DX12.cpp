// Copyright (c) 2024 V.Shirokii
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "FSR3_DX12.h"

#include "DynamicSdk.h"
#include "LibraryConfig.h"
#include "RenderResolutionHelper.h"
#include "RgException.h"
#include "Utils.h"

#include "DX12_CopyFramebuf.h"
#include "DX12_Interop.h"

#include <FidelityFX/host/ffx_fsr3.h>
#include <FidelityFX/host/backends/dx12/ffx_dx12.h>

namespace
{


struct FsrSdk
{
    constexpr static const char* SdkName() { return "FSR3DX12"; }

    DYNAMICSDK_DECLARE( ffxAssertReport );
    DYNAMICSDK_DECLARE( ffxAssertSetPrintingCallback );

#if 0
    DYNAMICSDK_DECLARE( ffxFrameInterpolationContextCreate );
    DYNAMICSDK_DECLARE( ffxFrameInterpolationContextDestroy );
    DYNAMICSDK_DECLARE( ffxFrameInterpolationDispatch );

    DYNAMICSDK_DECLARE( ffxOpticalflowContextCreate );
    DYNAMICSDK_DECLARE( ffxOpticalflowContextDestroy );
    DYNAMICSDK_DECLARE( ffxOpticalflowContextDispatch );
    DYNAMICSDK_DECLARE( ffxOpticalflowGetSharedResourceDescriptions );
#endif

    DYNAMICSDK_DECLARE( ffxFsr3ConfigureFrameGeneration );
    DYNAMICSDK_DECLARE( ffxFsr3ContextCreate );
    DYNAMICSDK_DECLARE( ffxFsr3ContextDestroy );
    DYNAMICSDK_DECLARE( ffxFsr3ContextDispatchUpscale );
    DYNAMICSDK_DECLARE( ffxFsr3ContextGenerateReactiveMask );
    DYNAMICSDK_DECLARE( ffxFsr3DispatchFrameGeneration );
    DYNAMICSDK_DECLARE( ffxFsr3GetJitterOffset );
    DYNAMICSDK_DECLARE( ffxFsr3GetJitterPhaseCount );
    DYNAMICSDK_DECLARE( ffxFsr3GetRenderResolutionFromQualityMode );
    DYNAMICSDK_DECLARE( ffxFsr3GetUpscaleRatioFromQualityMode );
    DYNAMICSDK_DECLARE( ffxFsr3ResourceIsNull );
    DYNAMICSDK_DECLARE( ffxFsr3SkipPresent );

    DYNAMICSDK_DECLARE( GetFfxResourceDescriptionDX12 );
    DYNAMICSDK_DECLARE( ffxCreateFrameinterpolationSwapchainDX12 );
    DYNAMICSDK_DECLARE( ffxCreateFrameinterpolationSwapchainForHwndDX12 );
    DYNAMICSDK_DECLARE( ffxGetCommandListDX12 );
    DYNAMICSDK_DECLARE( ffxGetCommandQueueDX12 );
    DYNAMICSDK_DECLARE( ffxGetDX12SwapchainPtr );
    DYNAMICSDK_DECLARE( ffxGetDeviceDX12 );
    DYNAMICSDK_DECLARE( ffxGetFrameinterpolationCommandlistDX12 );
    DYNAMICSDK_DECLARE( ffxGetFrameinterpolationTextureDX12 );
    DYNAMICSDK_DECLARE( ffxGetInterfaceDX12 );
    DYNAMICSDK_DECLARE( ffxGetResourceDX12 );
    DYNAMICSDK_DECLARE( ffxGetScratchMemorySizeDX12 );
    DYNAMICSDK_DECLARE( ffxGetSurfaceFormatDX12 );
    DYNAMICSDK_DECLARE( ffxGetSwapchainDX12 );
    DYNAMICSDK_DECLARE( ffxRegisterFrameinterpolationUiResourceDX12 );
    DYNAMICSDK_DECLARE( ffxReplaceSwapchainForFrameinterpolationDX12 );
    DYNAMICSDK_DECLARE( ffxSetFrameGenerationConfigToSwapchainDX12 );
    DYNAMICSDK_DECLARE( ffxWaitForPresents );
};

FfxSurfaceFormat ToFfxFormat( DXGI_FORMAT f )
{
    switch( f )
    {
        case DXGI_FORMAT_R32G32B32A32_TYPELESS: return FFX_SURFACE_FORMAT_R32G32B32A32_TYPELESS;
        case DXGI_FORMAT_R32G32B32A32_UINT: return FFX_SURFACE_FORMAT_R32G32B32A32_UINT;
        case DXGI_FORMAT_R32G32B32A32_FLOAT: return FFX_SURFACE_FORMAT_R32G32B32A32_FLOAT;
        case DXGI_FORMAT_R16G16B16A16_FLOAT: return FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT;
        case DXGI_FORMAT_R32G32_FLOAT: return FFX_SURFACE_FORMAT_R32G32_FLOAT;
        case DXGI_FORMAT_R8_UINT: return FFX_SURFACE_FORMAT_R8_UINT;
        case DXGI_FORMAT_R32_UINT: return FFX_SURFACE_FORMAT_R32_UINT;
        case DXGI_FORMAT_R10G10B10A2_UNORM: return FFX_SURFACE_FORMAT_R10G10B10A2_UNORM;
        case DXGI_FORMAT_R8G8B8A8_TYPELESS: return FFX_SURFACE_FORMAT_R8G8B8A8_TYPELESS;
        case DXGI_FORMAT_R8G8B8A8_UNORM: return FFX_SURFACE_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_R8G8B8A8_SNORM: return FFX_SURFACE_FORMAT_R8G8B8A8_SNORM;
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return FFX_SURFACE_FORMAT_R8G8B8A8_SRGB;
        case DXGI_FORMAT_R11G11B10_FLOAT: return FFX_SURFACE_FORMAT_R11G11B10_FLOAT;
        case DXGI_FORMAT_R16G16_FLOAT: return FFX_SURFACE_FORMAT_R16G16_FLOAT;
        case DXGI_FORMAT_R16G16_UINT: return FFX_SURFACE_FORMAT_R16G16_UINT;
        case DXGI_FORMAT_R16G16_SINT: return FFX_SURFACE_FORMAT_R16G16_SINT;
        case DXGI_FORMAT_R16_FLOAT: return FFX_SURFACE_FORMAT_R16_FLOAT;
        case DXGI_FORMAT_R16_UINT: return FFX_SURFACE_FORMAT_R16_UINT;
        case DXGI_FORMAT_R16_UNORM: return FFX_SURFACE_FORMAT_R16_UNORM;
        case DXGI_FORMAT_R16_SNORM: return FFX_SURFACE_FORMAT_R16_SNORM;
        case DXGI_FORMAT_R8_UNORM: return FFX_SURFACE_FORMAT_R8_UNORM;
        case DXGI_FORMAT_R8G8_UNORM: return FFX_SURFACE_FORMAT_R8G8_UNORM;
        case DXGI_FORMAT_R8G8_UINT: return FFX_SURFACE_FORMAT_R8G8_UINT;
        case DXGI_FORMAT_R32_FLOAT: return FFX_SURFACE_FORMAT_R32_FLOAT;
        default: assert( 0 ); return FFX_SURFACE_FORMAT_UNKNOWN;
    }
}

void PrintFfxMessage( FfxMsgType type, const wchar_t* message )
{
    using namespace RTGL1;

    if( !message )
    {
        return;
    }

    char   str[ 256 ];
    size_t len{ 0 };
    if( wcstombs_s( &len, str, message, std::size( str ) ) != 0 )
    {
        debug::Error( "PrintFfxMessage: wcstombs_s failed" );
    }
    str[ std::size( str ) - 1 ] = '\0';

    switch( type )
    {
        case FFX_MESSAGE_TYPE_ERROR: debug::Error( str ); break;
        case FFX_MESSAGE_TYPE_WARNING: debug::Warning( str ); break;
        case FFX_MESSAGE_TYPE_COUNT:
        default: assert( 0 );
    }
}

struct DllFindDirectory
{
    explicit DllFindDirectory( const std::filesystem::path& dir )
    {
        assert( is_directory( dir ) );
        auto b = SetDllDirectoryW( dir.c_str() );
        if( !b )
        {
            auto err = GetLastError();
            RTGL1::debug::Warning(
                "SetDllDirectory({}) failed with GetLastError={}", dir.string(), err );
        }
    }
    ~DllFindDirectory() { SetDllDirectoryW( nullptr ); }

    HMODULE Load( const char* dllfilename )
    {
        // because of SetDllDirectory, load DLL without specifying a directory
        assert( !strchr( dllfilename, '/' ) && !strchr( dllfilename, '\\' ) );

        HMODULE dll = LoadLibraryA( dllfilename );
        if( !dll )
        {
            RTGL1::debug::Error( "FSR3: Failed to load DLL \'{}\'", dllfilename );
        }
        return dll;
    }
};

auto LoadDllFunctions( const std::filesystem::path& folder ) -> DynamicSdk< FsrSdk >
{
    // because FSR3's dll has other dlls as dependencies, find them in this folder
    auto dlldir = DllFindDirectory{ folder };

    auto sdk = DynamicSdk< FsrSdk >{};

#if 0
    if( auto fsr3fi = sdk.Add( dlldir.Load( "ffx_frameinterpolation_x64.dll" ) ))
    {
        loadedDlls.push_back( fsr3fi );
        DYNAMICSDK_FETCH( fsr3fi, ffxFrameInterpolationContextCreate );
        DYNAMICSDK_FETCH( fsr3fi, ffxFrameInterpolationContextDestroy );
        DYNAMICSDK_FETCH( fsr3fi, ffxFrameInterpolationDispatch );
    }

    if( auto fsr3of = sdk.Add( dlldir.Load( "ffx_opticalflow_x64.dll" ) ))
    {
        loadedDlls.push_back( fsr3of );
        DYNAMICSDK_FETCH( fsr3of, ffxOpticalflowContextCreate );
        DYNAMICSDK_FETCH( fsr3of, ffxOpticalflowContextDestroy );
        DYNAMICSDK_FETCH( fsr3of, ffxOpticalflowContextDispatch );
        DYNAMICSDK_FETCH( fsr3of, ffxOpticalflowGetSharedResourceDescriptions );
    }
#endif

    if( auto fsr3dll = sdk.Add( dlldir.Load( "ffx_fsr3_x64.dll" ) ) )
    {
        DYNAMICSDK_FETCH( fsr3dll, ffxFsr3ConfigureFrameGeneration );
        DYNAMICSDK_FETCH( fsr3dll, ffxFsr3ContextCreate );
        DYNAMICSDK_FETCH( fsr3dll, ffxFsr3ContextDestroy );
        DYNAMICSDK_FETCH( fsr3dll, ffxFsr3ContextDispatchUpscale );
        DYNAMICSDK_FETCH( fsr3dll, ffxFsr3ContextGenerateReactiveMask );
        DYNAMICSDK_FETCH( fsr3dll, ffxFsr3DispatchFrameGeneration );
        DYNAMICSDK_FETCH( fsr3dll, ffxFsr3GetJitterOffset );
        DYNAMICSDK_FETCH( fsr3dll, ffxFsr3GetJitterPhaseCount );
        DYNAMICSDK_FETCH( fsr3dll, ffxFsr3GetRenderResolutionFromQualityMode );
        DYNAMICSDK_FETCH( fsr3dll, ffxFsr3GetUpscaleRatioFromQualityMode );
        DYNAMICSDK_FETCH( fsr3dll, ffxFsr3ResourceIsNull );
        DYNAMICSDK_FETCH( fsr3dll, ffxFsr3SkipPresent );
    }

    if( auto dx12dll = sdk.Add( dlldir.Load( "ffx_backend_dx12_x64.dll" ) ) )
    {
        DYNAMICSDK_FETCH( dx12dll, GetFfxResourceDescriptionDX12 );
        DYNAMICSDK_FETCH( dx12dll, ffxCreateFrameinterpolationSwapchainDX12 );
        DYNAMICSDK_FETCH( dx12dll, ffxCreateFrameinterpolationSwapchainForHwndDX12 );
        DYNAMICSDK_FETCH( dx12dll, ffxGetCommandListDX12 );
        DYNAMICSDK_FETCH( dx12dll, ffxGetCommandQueueDX12 );
        DYNAMICSDK_FETCH( dx12dll, ffxGetDX12SwapchainPtr );
        DYNAMICSDK_FETCH( dx12dll, ffxGetDeviceDX12 );
        DYNAMICSDK_FETCH( dx12dll, ffxGetFrameinterpolationCommandlistDX12 );
        DYNAMICSDK_FETCH( dx12dll, ffxGetFrameinterpolationTextureDX12 );
        DYNAMICSDK_FETCH( dx12dll, ffxGetInterfaceDX12 );
        DYNAMICSDK_FETCH( dx12dll, ffxGetResourceDX12 );
        DYNAMICSDK_FETCH( dx12dll, ffxGetScratchMemorySizeDX12 );
        DYNAMICSDK_FETCH( dx12dll, ffxGetSurfaceFormatDX12 );
        DYNAMICSDK_FETCH( dx12dll, ffxGetSwapchainDX12 );
        DYNAMICSDK_FETCH( dx12dll, ffxRegisterFrameinterpolationUiResourceDX12 );
        DYNAMICSDK_FETCH( dx12dll, ffxReplaceSwapchainForFrameinterpolationDX12 );
        DYNAMICSDK_FETCH( dx12dll, ffxSetFrameGenerationConfigToSwapchainDX12 );
    }

    return OnlyFullyLoaded( std::move( sdk ) );
}


auto g_storage = FfxFsr3Context{};


auto pfn               = DynamicSdk< FsrSdk >{};
bool g_wasNewSwapchain = false;

}


void RTGL1::FSR3_DX12::LoadSDK()
{
    pfn = LoadDllFunctions( Utils::FindBinFolder() );
}

void RTGL1::FSR3_DX12::UnloadSDK()
{
    pfn.Free();
}


auto RTGL1::FSR3_DX12::MakeInstance( uint64_t gpuLuid ) -> std::expected< FSR3_DX12*, const char* >
{
    if( !pfn.Valid() )
    {
        return std::unexpected{ "Couldn't load AMD FSR3 libraries. Ensure that DLL files are available in the \'bin/\' folder" };
    }

    if( !dxgi::InitAsFSR3( //
            gpuLuid,
            []( IDXGIFactory4*               factory,
                ID3D12CommandQueue*          queue,
                void*                        hwnd,
                const DXGI_SWAP_CHAIN_DESC1* desc1 ) -> IDXGISwapChain4* //
            {
                g_wasNewSwapchain = true;

                assert( pfn.ffxCreateFrameinterpolationSwapchainForHwndDX12 &&
                        pfn.ffxGetDX12SwapchainPtr );

                FfxSwapchain ffxSw{};
                FfxErrorCode r = pfn.ffxCreateFrameinterpolationSwapchainForHwndDX12(
                    static_cast< HWND >( hwnd ), //
                    desc1,
                    nullptr,
                    queue,
                    factory,
                    ffxSw );
                if( r != FFX_OK )
                {
                    assert( 0 );
                    return nullptr;
                }

                // From SDK sample:
                // In case the app is handling Alt-Enter manually we need to update the window
                // association after creating a different swapchain
                HRESULT hr = factory->MakeWindowAssociation( static_cast< HWND >( hwnd ),
                                                             DXGI_MWA_NO_WINDOW_CHANGES );
                if( FAILED( hr ) )
                {
                    debug::Warning( "IDXGIFactory4::MakeWindowAssociation failed: {:08x}",
                                    uint32_t( hr ) );
                }

                return pfn.ffxGetDX12SwapchainPtr( ffxSw );
            } ) )
    {
        debug::Error( "[NVIDIA Streamline] Failed to init DX12 for FSR3" );
        return std::unexpected{ "DirectX 12 initialization failed for AMD FSR3" };
    }

    auto inst = new FSR3_DX12{};
    {
        inst->m_framegenConfig = new FfxFrameGenerationConfig{};
    }
    return inst;
}

RTGL1::FSR3_DX12::~FSR3_DX12()
{
    if( m_context )
    {
        pfn.ffxFsr3ContextDestroy( m_context );
    }
    delete m_framegenConfig;
}

namespace
{

constexpr RTGL1::FramebufferImageIndex INPUT_IMAGE_INDICES[] = {
    RTGL1::FB_IMAGE_INDEX_FINAL,
    RTGL1::FB_IMAGE_INDEX_DEPTH_NDC,
    RTGL1::FB_IMAGE_INDEX_MOTION_DLSS,
    RTGL1::FB_IMAGE_INDEX_REACTIVITY,
};

constexpr RTGL1::FramebufferImageIndex OUTPUT_IMAGE_INDEX = RTGL1::FB_IMAGE_INDEX_UPSCALED_PONG;


FfxResource ToFSRResource( RTGL1::FramebufferImageIndex fbImage, bool forceReadOnly = false )
{
    assert( fbImage == OUTPUT_IMAGE_INDEX ||
            std::ranges::contains( INPUT_IMAGE_INDICES, fbImage ) ||
            fbImage == RTGL1::FB_IMAGE_INDEX_HUD_ONLY );

    auto sharedImage = RTGL1::dxgi::Framebuf_GetVkDx12Shared( fbImage );

    auto desc = FfxResourceDescription{
        .type     = FFX_RESOURCE_TYPE_TEXTURE2D,
        .format   = ToFfxFormat( DXGI_FORMAT( sharedImage.dxgiformat ) ),
        .width    = sharedImage.width,
        .height   = sharedImage.height,
        .depth    = 1,
        .mipCount = 1,
        .flags    = FFX_RESOURCE_FLAGS_NONE,
        .usage    = ( fbImage == OUTPUT_IMAGE_INDEX && !forceReadOnly ) ? FFX_RESOURCE_USAGE_UAV
                                                                        : FFX_RESOURCE_USAGE_READ_ONLY,
    };

    FfxResourceStates state = ( fbImage == OUTPUT_IMAGE_INDEX && !forceReadOnly )
                                  ? FFX_RESOURCE_STATE_UNORDERED_ACCESS
                                  : FFX_RESOURCE_STATE_COMPUTE_READ;

    wchar_t name[ 64 ];
    wcscpy_s( name, RTGL1::ShFramebuffers_DebugNamesW[ fbImage ] );
    name[ std::size( name ) - 1 ] = '\0';

    return pfn.ffxGetResourceDX12( sharedImage.d3d12resource, desc, name, state );
}

}

void RTGL1::FSR3_DX12::OnFramebuffersSizeChange( const ResolutionState& resolutionState )
{
    if( !pfn.Valid() )
    {
        assert( 0 );
        return;
    }

    if( m_context )
    {
        pfn.ffxFsr3ContextDestroy( m_context );
        m_context = nullptr;
    }

    // maxContexts are hardcoded from the SDK sample
    auto l_fetchInterface = []( FfxErrorCode&           err,
                                FfxDevice               device,
                                uint32_t                maxContexts,
                                std::vector< uint8_t >& scratch ) -> FfxInterface {
        if( err == FFX_OK )
        {
            scratch.resize( pfn.ffxGetScratchMemorySizeDX12( maxContexts ) );
            std::ranges::fill( scratch, 0 );

            FfxInterface interf{};
            err = pfn.ffxGetInterfaceDX12( &interf, //
                                           device,
                                           scratch.data(),
                                           scratch.size(),
                                           maxContexts );
            return interf;
        }
        return {};
    };

    ID3D12Device* dx12device = dxgi::GetD3D12Device();
    if( !dx12device )
    {
        assert( 0 );
        return;
    }

    auto r = FfxErrorCode{ FFX_OK };
    auto d = FfxDevice{ pfn.ffxGetDeviceDX12( dx12device ) };

    // clang-format off
    auto contextDesc = FfxFsr3ContextDescription{
        .flags             = FFX_FSR3_ENABLE_AUTO_EXPOSURE |
                             FFX_FSR3_ENABLE_HIGH_DYNAMIC_RANGE |
                             ( LibConfig().fsr3async ? FFX_FSR3_ENABLE_ASYNC_WORKLOAD_SUPPORT : 0u ) | 
                             ( LibConfig().fsrValidation ? FFX_FSR3_ENABLE_DEBUG_CHECKING : 0u ),
        .maxRenderSize     = { resolutionState.renderWidth, resolutionState.renderHeight },
        .upscaleOutputSize = { resolutionState.upscaledWidth, resolutionState.upscaledHeight },
        .displaySize       = { resolutionState.upscaledWidth, resolutionState.upscaledHeight },
        .backendInterfaceSharedResources    = l_fetchInterface( r, d, 1, m_scratchBufferSharedResources),
        .backendInterfaceUpscaling          = l_fetchInterface( r, d, 1, m_scratchBufferUpscaling),
        .backendInterfaceFrameInterpolation = l_fetchInterface( r, d, 2, m_scratchBufferFrameInterpolation),
        .fpMessage                          = PrintFfxMessage,
        .backBufferFormat                   = ToFfxFormat( DXGI_FORMAT( dxgi::GetSwapchainDxgiFormat() ) ),
    };
    if ( r!= FFX_OK )
    {
        assert( 0 );
        return;
    }
    // clang-format on

#ifndef NDEBUG
    contextDesc.flags |= FFX_FSR3_ENABLE_DEBUG_CHECKING;
#endif

    {
        memset( &g_storage, 0, sizeof( g_storage ) );
        m_context = &g_storage;
    }

    r = pfn.ffxFsr3ContextCreate( m_context, &contextDesc );
    if( r != FFX_OK )
    {
        m_context = nullptr;
        assert( 0 );
        return;
    }


    bool prevenabled  = m_framegenConfig->frameGenerationEnabled;
    *m_framegenConfig = FfxFrameGenerationConfig{
        .swapChain               = pfn.ffxGetSwapchainDX12( dxgi::GetSwapchainDxgiSwapchain() ),
        .presentCallback         = nullptr,
        .frameGenerationCallback = pfn.ffxFsr3DispatchFrameGeneration,
        .frameGenerationEnabled  = prevenabled, // set in Apply()
        .allowAsyncWorkloads     = LibConfig().fsr3async,
        .HUDLessColor            = FfxResource{}, // TODO: hudless, copy the image in DX12
        .flags                   = false,
        .onlyPresentInterpolated = false,
    };

    r = pfn.ffxFsr3ConfigureFrameGeneration( m_context, m_framegenConfig );
    if( r != FFX_OK )
    {
        debug::Error( "ffxFsr3ConfigureFrameGeneration fail: {}", int( r ) );
        pfn.ffxFsr3ContextDestroy( m_context );
        m_context = nullptr;
        assert( 0 );
        return;
    }

    r = pfn.ffxRegisterFrameinterpolationUiResourceDX12( m_framegenConfig->swapChain,
                                                         ToFSRResource( FB_IMAGE_INDEX_HUD_ONLY ) );
    if( r != FFX_OK )
    {
        debug::Error( "ffxRegisterFrameinterpolationUiResourceDX12 fail: {}", int( r ) );
        pfn.ffxFsr3ContextDestroy( m_context );
        m_context = nullptr;
        assert( 0 );
        return;
    }
}

void RTGL1::FSR3_DX12::CopyVkInputsToDX12( VkCommandBuffer        cmd,
                                           uint32_t               frameIndex,
                                           const Framebuffers&    framebuffers,
                                           const ResolutionState& resolution )
{
    // if fails, need support for .._Prev framebufs
    {
        assert( framebuffers.GetImage( OUTPUT_IMAGE_INDEX, frameIndex ) ==
                framebuffers.GetImage( OUTPUT_IMAGE_INDEX,
                                       ( frameIndex + 1 ) % RTGL1::MAX_FRAMES_IN_FLIGHT ) );
        for( auto f : INPUT_IMAGE_INDICES )
        {
            assert( framebuffers.GetImage( f, frameIndex ) ==
                    framebuffers.GetImage( f, ( frameIndex + 1 ) % RTGL1::MAX_FRAMES_IN_FLIGHT ) );
        }
    }

    Framebuf_CopyVkToDX12( cmd,
                           frameIndex,
                           framebuffers,
                           resolution.renderWidth,
                           resolution.renderHeight,
                           INPUT_IMAGE_INDICES );
}

void RTGL1::FSR3_DX12::CopyDX12OutputToVk( VkCommandBuffer        cmd,
                                           uint32_t               frameIndex,
                                           const Framebuffers&    framebuffers,
                                           const ResolutionState& resolution )
{
    FramebufferImageIndex outputs[] = { OUTPUT_IMAGE_INDEX };
    Framebuf_CopyDX12ToVk( cmd,
                           frameIndex,
                           framebuffers,
                           resolution.upscaledWidth,
                           resolution.upscaledHeight,
                           outputs );
}

auto RTGL1::FSR3_DX12::Apply( ID3D12CommandList*            dx12cmd,
                              uint32_t                      frameIndex,
                              const Framebuffers&           framebuffers,
                              const RenderResolutionHelper& renderResolution,
                              RgFloat2D                     jitterOffset,
                              double                        timeDelta,
                              float                         nearPlane,
                              float                         farPlane,
                              float                         fovVerticalRad,
                              bool                          resetAccumulation,
                              float                         oneGameUnitInMeters,
                              bool skipGeneratedFrame ) -> std::optional< FramebufferImageIndex >
{
    if( !dx12cmd )
    {
        debug::Warning( "FSR3_DX12::Apply() was ignored, as ID3D12CommandList failed" );
        return {};
    }

    if( !pfn.Valid() || !m_context )
    {
        assert( 0 );
        return {};
    }

    assert( nearPlane > 0.0f && nearPlane < farPlane );
    const bool frameGeneration = !skipGeneratedFrame;

    if( m_framegenConfig->frameGenerationEnabled != frameGeneration || g_wasNewSwapchain )
    {
        m_framegenConfig->swapChain = pfn.ffxGetSwapchainDX12( dxgi::GetSwapchainDxgiSwapchain() );
        m_framegenConfig->frameGenerationEnabled = frameGeneration;
        m_framegenConfig->flags = 0; // FFX_FSR3_FRAME_GENERATION_FLAG_DRAW_DEBUG_TEAR_LINES;

        FfxErrorCode r = pfn.ffxFsr3ConfigureFrameGeneration( m_context, m_framegenConfig );
        if( r != FFX_OK )
        {
            debug::Error( "ffxFsr3ConfigureFrameGeneration fail" );
            return {};
        }
    }

    // clang-format off
    auto info = FfxFsr3DispatchUpscaleDescription{
        .commandList                   = pfn.ffxGetCommandListDX12( dx12cmd ),
        .color                         = ToFSRResource( FB_IMAGE_INDEX_FINAL ),
        .depth                         = ToFSRResource( FB_IMAGE_INDEX_DEPTH_NDC ),
        .motionVectors                 = ToFSRResource( FB_IMAGE_INDEX_MOTION_DLSS ),
        .exposure                      = FfxResource{},
        .reactive                      = ToFSRResource( FB_IMAGE_INDEX_REACTIVITY ),
        .transparencyAndComposition    = FfxResource{},
        .upscaleOutput                 = ToFSRResource( OUTPUT_IMAGE_INDEX ),
        .jitterOffset                  = { -jitterOffset.data[ 0 ], -jitterOffset.data[ 1 ] },
        .motionVectorScale             = { float( renderResolution.GetResolutionState().renderWidth ), float( renderResolution.GetResolutionState().renderHeight ) },
        .renderSize                    = { renderResolution.GetResolutionState().renderWidth, renderResolution.GetResolutionState().renderHeight },
        .enableSharpening              = renderResolution.IsCASInsideFSR2(),
        .sharpness                     = renderResolution.GetSharpeningIntensity(),
        .frameTimeDelta                = float( timeDelta * 1000.0 ),
        .preExposure                   = 1.0f,
        .reset                         = resetAccumulation,
        .cameraNear                    = nearPlane,
        .cameraFar                     = farPlane,
        .cameraFovAngleVertical        = fovVerticalRad,
        .viewSpaceToMetersFactor       = oneGameUnitInMeters,
    };
    // clang-format on

    FfxErrorCode r = pfn.ffxFsr3ContextDispatchUpscale( m_context, &info );
    if( r != FFX_OK )
    {
        debug::Error( "ffxFsr3ContextDispatchUpscale fail" );
        return {};
    }

    return OUTPUT_IMAGE_INDEX;
}

RgFloat2D RTGL1::FSR3_DX12::GetJitter( const ResolutionState& resolutionState,
                                       uint32_t               frameId ) const
{
    if( !pfn.Valid() )
    {
        assert( 0 );
        return {};
    }

    int32_t id    = int32_t( frameId % uint32_t{ INT32_MAX } );
    int32_t phase = pfn.ffxFsr3GetJitterPhaseCount( int32_t( resolutionState.renderWidth ),
                                                    int32_t( resolutionState.upscaledWidth ) );

    float x = 0, y = 0;

    FfxErrorCode r = pfn.ffxFsr3GetJitterOffset( &x, &y, id, phase );
    assert( r == FFX_OK );

    return { x, y };
}
