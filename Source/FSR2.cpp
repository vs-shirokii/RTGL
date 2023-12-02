// Copyright (c) 2022 Sultim Tsyrendashiev
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

#include "FSR2.h"

#include "LibraryConfig.h"
#include "RenderResolutionHelper.h"
#include "RgException.h"
#include "Utils.h"

#include <FidelityFX/host/ffx_fsr2.h>
#include <FidelityFX/host/backends/vk/ffx_vk.h>

namespace
{
#define DECLARE_DLL_FUNC( f ) decltype( &( f ) ) f = nullptr

struct FsrSdk
{
    DECLARE_DLL_FUNC( ffxAssertReport );
    DECLARE_DLL_FUNC( ffxAssertSetPrintingCallback );
    DECLARE_DLL_FUNC( ffxFsr2ContextCreate );
    DECLARE_DLL_FUNC( ffxFsr2ContextDestroy );
    DECLARE_DLL_FUNC( ffxFsr2ContextDispatch );
    DECLARE_DLL_FUNC( ffxFsr2ContextGenerateReactiveMask );
    DECLARE_DLL_FUNC( ffxFsr2GetJitterOffset );
    DECLARE_DLL_FUNC( ffxFsr2GetJitterPhaseCount );
    DECLARE_DLL_FUNC( ffxFsr2GetRenderResolutionFromQualityMode );
    DECLARE_DLL_FUNC( ffxFsr2GetUpscaleRatioFromQualityMode );
    DECLARE_DLL_FUNC( ffxFsr2ResourceIsNull );

    DECLARE_DLL_FUNC( ffxGetCommandListVK );
    DECLARE_DLL_FUNC( ffxGetDeviceVK );
    DECLARE_DLL_FUNC( ffxGetInterfaceVK );
    DECLARE_DLL_FUNC( ffxGetResourceVK );
    DECLARE_DLL_FUNC( ffxGetScratchMemorySizeVK );
};

void CheckError( FfxErrorCode r )
{
    if( r != FFX_OK )
    {
        RTGL1::debug::Error( "FSR2: Fail, FfxErrorCode={}", r );
        throw RTGL1::RgException( RG_RESULT_GRAPHICS_API_ERROR, "Can't initialize FSR2" );
    }
}

FfxSurfaceFormat ToFfxFormat( VkFormat f )
{
    switch( f )
    {
        // case VK_FORMAT_R32G32B32A32_TYPELESS: return FFX_SURFACE_FORMAT_R32G32B32A32_TYPELESS;
        case VK_FORMAT_R32G32B32A32_UINT: return FFX_SURFACE_FORMAT_R32G32B32A32_UINT;
        case VK_FORMAT_R32G32B32A32_SFLOAT: return FFX_SURFACE_FORMAT_R32G32B32A32_FLOAT;
        case VK_FORMAT_R16G16B16A16_SFLOAT: return FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT;
        case VK_FORMAT_R32G32_SFLOAT: return FFX_SURFACE_FORMAT_R32G32_FLOAT;
        case VK_FORMAT_R8_UINT: return FFX_SURFACE_FORMAT_R8_UINT;
        case VK_FORMAT_R32_UINT: return FFX_SURFACE_FORMAT_R32_UINT;
        // case VK_FORMAT_R10G10B10A2_UNORM: return FFX_SURFACE_FORMAT_R10G10B10A2_UNORM;
        // case VK_FORMAT_R8G8B8A8_TYPELESS: return FFX_SURFACE_FORMAT_R8G8B8A8_TYPELESS;
        case VK_FORMAT_R8G8B8A8_UNORM: return FFX_SURFACE_FORMAT_R8G8B8A8_UNORM;
        case VK_FORMAT_R8G8B8A8_SNORM: return FFX_SURFACE_FORMAT_R8G8B8A8_SNORM;
        case VK_FORMAT_R8G8B8A8_SRGB: return FFX_SURFACE_FORMAT_R8G8B8A8_SRGB;
        // case VK_FORMAT_R11G11B10_SFLOAT: return FFX_SURFACE_FORMAT_R11G11B10_FLOAT;
        case VK_FORMAT_R16G16_SFLOAT: return FFX_SURFACE_FORMAT_R16G16_FLOAT;
        case VK_FORMAT_R16G16_UINT: return FFX_SURFACE_FORMAT_R16G16_UINT;
        case VK_FORMAT_R16G16_SINT: return FFX_SURFACE_FORMAT_R16G16_SINT;
        case VK_FORMAT_R16_SFLOAT: return FFX_SURFACE_FORMAT_R16_FLOAT;
        case VK_FORMAT_R16_UINT: return FFX_SURFACE_FORMAT_R16_UINT;
        case VK_FORMAT_R16_UNORM: return FFX_SURFACE_FORMAT_R16_UNORM;
        case VK_FORMAT_R16_SNORM: return FFX_SURFACE_FORMAT_R16_SNORM;
        case VK_FORMAT_R8_UNORM: return FFX_SURFACE_FORMAT_R8_UNORM;
        case VK_FORMAT_R8G8_UNORM: return FFX_SURFACE_FORMAT_R8G8_UNORM;
        case VK_FORMAT_R8G8_UINT: return FFX_SURFACE_FORMAT_R8G8_UINT;
        case VK_FORMAT_R32_SFLOAT: return FFX_SURFACE_FORMAT_R32_FLOAT;
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

// Wrapper to keep path lifetime
HMODULE LoadLibrary_Path( const std::filesystem::path& p )
{
    HMODULE dll = LoadLibraryW( p.c_str() );
    if( !dll )
    {
        RTGL1::debug::Error( "FSR2: Failed to load DLL \'{}\'", p.string() );
    }
    return dll;
}

FsrSdk pfn{};

void FreeDlls( const std::vector< void* >& dlls )
{
    for( void* ptr : dlls )
    {
        FreeLibrary( static_cast< HMODULE >( ptr ) );
    }
}

#define RETURN_FAIL         \
    pfn = {};               \
    FreeDlls( loadedDlls ); \
    return {}

#define GET_FUNC( dll, f )                                                          \
    do                                                                              \
    {                                                                               \
        pfn.f = reinterpret_cast< decltype( pfn.f ) >( GetProcAddress( dll, #f ) ); \
        if( !pfn.f )                                                                \
        {                                                                           \
            RTGL1::debug::Error( "FSR2: Failed to load DLL function: \'" #f "\'" ); \
            RETURN_FAIL;                                                            \
        }                                                                           \
    } while( 0 )

auto LoadDllFunctions( const std::filesystem::path& folder ) -> std::vector< void* >
{
    auto loadedDlls = std::vector< void* >{};

    if( auto fsr2dll = LoadLibrary_Path( folder / "ffx_fsr2_x64.dll" ) )
    {
        loadedDlls.push_back( fsr2dll );
        GET_FUNC( fsr2dll, ffxAssertReport );
        GET_FUNC( fsr2dll, ffxAssertSetPrintingCallback );
        GET_FUNC( fsr2dll, ffxFsr2ContextCreate );
        GET_FUNC( fsr2dll, ffxFsr2ContextDestroy );
        GET_FUNC( fsr2dll, ffxFsr2ContextDispatch );
        GET_FUNC( fsr2dll, ffxFsr2ContextGenerateReactiveMask );
        GET_FUNC( fsr2dll, ffxFsr2GetJitterOffset );
        GET_FUNC( fsr2dll, ffxFsr2GetJitterPhaseCount );
        GET_FUNC( fsr2dll, ffxFsr2GetRenderResolutionFromQualityMode );
        GET_FUNC( fsr2dll, ffxFsr2GetUpscaleRatioFromQualityMode );
        GET_FUNC( fsr2dll, ffxFsr2ResourceIsNull );
    }
    else
    {
        RETURN_FAIL;
    }

    if( auto vkdll = LoadLibrary_Path( folder / "ffx_backend_vk_x64.dll" ) )
    {
        loadedDlls.push_back( vkdll );
        GET_FUNC( vkdll, ffxGetCommandListVK );
        GET_FUNC( vkdll, ffxGetDeviceVK );
        GET_FUNC( vkdll, ffxGetInterfaceVK );
        GET_FUNC( vkdll, ffxGetResourceVK );
        GET_FUNC( vkdll, ffxGetScratchMemorySizeVK );
    }
    else
    {
        RETURN_FAIL;
    }

    return loadedDlls;
}

}

RTGL1::FSR2::FSR2( VkDevice _device, VkPhysicalDevice _physDevice )
    : device{ _device } //
    , physDevice{ _physDevice }
{
    m_loadedDlls = LoadDllFunctions( Utils::FindBinFolder() );
    if( m_loadedDlls.empty() )
    {
        debug::Error( "FSR2: Failed to initialize DLL-s. FSR2 will not be available." );
    }
}

RTGL1::FSR2::~FSR2()
{
    if( m_context )
    {
        pfn.ffxFsr2ContextDestroy( m_context );
        delete m_context;
    }

    FreeDlls( m_loadedDlls );
}

bool RTGL1::FSR2::Valid() const
{
    return !m_loadedDlls.empty();
}

auto RTGL1::FSR2::MakeInstance( VkDevice device, VkPhysicalDevice physDevice )
    -> std::shared_ptr< FSR2 >
{
    auto inst = std::make_shared< FSR2 >( device, physDevice );
    if( !inst || !inst->Valid() )
    {
        return {};
    }
    return inst;
}

void RTGL1::FSR2::OnFramebuffersSizeChange( const ResolutionState& resolutionState )
{
    if( m_context )
    {
        pfn.ffxFsr2ContextDestroy( m_context );
        *m_context = {};
    }
    else
    {
        m_context = new FfxFsr2Context{};
    }

    FfxErrorCode r{};

    auto contextDesc = FfxFsr2ContextDescription{
        .flags = FFX_FSR2_ENABLE_HIGH_DYNAMIC_RANGE |
                 ( LibConfig().fsrValidation ? FFX_FSR2_ENABLE_DEBUG_CHECKING : 0u ),
        .maxRenderSize    = { resolutionState.renderWidth, resolutionState.renderHeight },
        .displaySize      = { resolutionState.upscaledWidth, resolutionState.upscaledHeight },
        .backendInterface = {},
        .fpMessage        = PrintFfxMessage,
    };

    const size_t scratchBufferSize =
        pfn.ffxGetScratchMemorySizeVK( physDevice, FFX_FSR2_CONTEXT_COUNT );
    m_scratchBuffer.resize( scratchBufferSize );
    std::ranges::fill( m_scratchBuffer, 0 );

    auto contextDevice = VkDeviceContext{
        device,
        physDevice,
        vkGetDeviceProcAddr,
    };

    r = pfn.ffxGetInterfaceVK( &contextDesc.backendInterface,
                               pfn.ffxGetDeviceVK( &contextDevice ),
                               m_scratchBuffer.data(),
                               scratchBufferSize,
                               FFX_FSR2_CONTEXT_COUNT );
    CheckError( r );

    r = pfn.ffxFsr2ContextCreate( m_context, &contextDesc );
    CheckError( r );
}

namespace
{
constexpr RTGL1::FramebufferImageIndex OUTPUT_IMAGE_INDEX = RTGL1::FB_IMAGE_INDEX_UPSCALED_PONG;

FfxResource ToFSRResource( RTGL1::FramebufferImageIndex  fbImage,
                           uint32_t                      frameIndex,
                           FfxFsr2Context*               pCtx,
                           const RTGL1::Framebuffers&    framebuffers,
                           const RTGL1::ResolutionState& resolutionState )
{
    auto [ image, view, format, sz ] =
        framebuffers.GetImageHandles( fbImage, frameIndex, resolutionState );

    auto desc = FfxResourceDescription{
        .type     = FFX_RESOURCE_TYPE_TEXTURE2D,
        .format   = ToFfxFormat( format ),
        .width    = sz.width,
        .height   = sz.height,
        .depth    = 1,
        .mipCount = 1,
        .flags    = FFX_RESOURCE_FLAGS_NONE,
        .usage    = ( fbImage == OUTPUT_IMAGE_INDEX ) ? FFX_RESOURCE_USAGE_UAV
                                                      : FFX_RESOURCE_USAGE_READ_ONLY,
    };

    FfxResourceStates state = ( fbImage == OUTPUT_IMAGE_INDEX )
                                  ? FFX_RESOURCE_STATE_UNORDERED_ACCESS
                                  : FFX_RESOURCE_STATE_COMPUTE_READ;

    wchar_t name[ 64 ];
    wcscpy_s( name, RTGL1::ShFramebuffers_DebugNamesW[ fbImage ] );
    name[ std::size( name ) - 1 ] = '\0';

    return pfn.ffxGetResourceVK( image, desc, name, state );
}

template< size_t N >
void InsertBarriers( VkCommandBuffer            cmd,
                     uint32_t                   frameIndex,
                     const RTGL1::Framebuffers& framebuffers,
                     const RTGL1::FramebufferImageIndex ( &inputsAndOutput )[ N ],
                     bool isBackwards )
{
    assert( std::ranges::contains( inputsAndOutput, OUTPUT_IMAGE_INDEX ) );

    VkImageMemoryBarrier2 barriers[ N ];

    for( size_t i = 0; i < N; i++ )
    {
        barriers[ i ] = {
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask        = VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .srcAccessMask       = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT,
            .dstStageMask        = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .dstAccessMask       = inputsAndOutput[ i ] == OUTPUT_IMAGE_INDEX ? VK_ACCESS_2_SHADER_WRITE_BIT : VK_ACCESS_2_SHADER_READ_BIT,
            .oldLayout           = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout           = inputsAndOutput[ i ] == OUTPUT_IMAGE_INDEX ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = framebuffers.GetImage( inputsAndOutput[ i ], frameIndex ),
            .subresourceRange    = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };

        if( isBackwards )
        {
            auto& b = barriers[ i ];

            std::swap( b.srcStageMask, b.dstStageMask );
            std::swap( b.srcAccessMask, b.dstAccessMask );
            std::swap( b.oldLayout, b.newLayout );
            std::swap( b.srcQueueFamilyIndex, b.dstQueueFamilyIndex );
        }
    }

    VkDependencyInfoKHR dependencyInfo = {
        .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR,
        .imageMemoryBarrierCount = uint32_t( std::size( barriers ) ),
        .pImageMemoryBarriers    = barriers,
    };

    RTGL1::svkCmdPipelineBarrier2KHR( cmd, &dependencyInfo );
}
}

RTGL1::FramebufferImageIndex RTGL1::FSR2::Apply( VkCommandBuffer               cmd,
                                                 uint32_t                      frameIndex,
                                                 const Framebuffers&           framebuffers,
                                                 const RenderResolutionHelper& renderResolution,
                                                 RgFloat2D                     jitterOffset,
                                                 double                        timeDelta,
                                                 float                         nearPlane,
                                                 float                         farPlane,
                                                 float                         fovVerticalRad,
                                                 bool                          resetAccumulation,
                                                 float                         oneGameUnitInMeters )
{
    assert( nearPlane > 0.0f && nearPlane < farPlane );

    using FI = FramebufferImageIndex;

    FI rs[] = {
        FI::FB_IMAGE_INDEX_FINAL,      FI::FB_IMAGE_INDEX_DEPTH_NDC, FI::FB_IMAGE_INDEX_MOTION_DLSS,
        FI::FB_IMAGE_INDEX_REACTIVITY, OUTPUT_IMAGE_INDEX,
    };
    InsertBarriers( cmd, frameIndex, framebuffers, rs, false );

    // clang-format off
    FfxFsr2DispatchDescription info = {
        .commandList                = pfn.ffxGetCommandListVK( cmd ),
        .color                      = ToFSRResource( FI::FB_IMAGE_INDEX_FINAL, frameIndex, m_context, framebuffers, renderResolution.GetResolutionState() ),
        .depth                      = ToFSRResource( FI::FB_IMAGE_INDEX_DEPTH_NDC, frameIndex, m_context, framebuffers, renderResolution.GetResolutionState() ),
        .motionVectors              = ToFSRResource( FI::FB_IMAGE_INDEX_MOTION_DLSS, frameIndex, m_context, framebuffers, renderResolution.GetResolutionState() ),
        .exposure                   = {},
        .reactive                   = ToFSRResource( FI::FB_IMAGE_INDEX_REACTIVITY, frameIndex, m_context, framebuffers, renderResolution.GetResolutionState() ),
        .transparencyAndComposition = {},
        .output                     = ToFSRResource( OUTPUT_IMAGE_INDEX, frameIndex, m_context, framebuffers, renderResolution.GetResolutionState() ),
        .jitterOffset               = { -jitterOffset.data[ 0 ], -jitterOffset.data[ 1 ] },
        .motionVectorScale          = { float( renderResolution.GetResolutionState().renderWidth ), float( renderResolution.GetResolutionState().renderHeight ) },
        .renderSize                 = { renderResolution.GetResolutionState().renderWidth, renderResolution.GetResolutionState().renderHeight },
        .enableSharpening           = renderResolution.IsCASInsideFSR2(),
        .sharpness                  = renderResolution.GetSharpeningIntensity(),
        .frameTimeDelta             = float( timeDelta * 1000.0 ),
        .preExposure                = 1.0f,
        .reset                      = resetAccumulation,
        .cameraNear                 = nearPlane,
        .cameraFar                  = farPlane,
        .cameraFovAngleVertical     = fovVerticalRad,
        .viewSpaceToMetersFactor    = oneGameUnitInMeters,
        .enableAutoReactive         = false,
        .colorOpaqueOnly            = {},
        .autoTcThreshold            = {},
        .autoTcScale                = {},
        .autoReactiveScale          = {},
        .autoReactiveMax            = {},
    };
    // clang-format on

    FfxErrorCode r = pfn.ffxFsr2ContextDispatch( m_context, &info );
    CheckError( r );

    InsertBarriers( cmd, frameIndex, framebuffers, rs, true );

    return OUTPUT_IMAGE_INDEX;
}

RgFloat2D RTGL1::FSR2::GetJitter( const ResolutionState& resolutionState, uint32_t frameId ) const
{
    if( !pfn.ffxFsr2GetJitterPhaseCount || !pfn.ffxFsr2GetJitterOffset )
    {
        assert( 0 );
        return {};
    }

    int32_t id    = int32_t( frameId % uint32_t{ INT32_MAX } );
    int32_t phase = pfn.ffxFsr2GetJitterPhaseCount( int32_t( resolutionState.renderWidth ),
                                                    int32_t( resolutionState.upscaledWidth ) );

    float x = 0, y = 0;

    FfxErrorCode r = pfn.ffxFsr2GetJitterOffset( &x, &y, id, phase );
    assert( r == FFX_OK );

    return { x, y };
}
