/*

Copyright (c) 2024 V.Shirokii
Copyright (c) 2021 Sultim Tsyrendashiev

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

#include "DLSS2.h"

#ifdef RG_USE_NATIVE_DLSS2

#include "RTGL1/RTGL1.h"

#include "CmdLabel.h"
#include "LibraryConfig.h"
#include "RenderResolutionHelper.h"
#include "Utils.h"

#include <nvsdk_ngx_helpers_vk.h>
#include <nvsdk_ngx_helpers.h>

#include <filesystem>

namespace
{

void PrintCallback( const char*             message,
                    NVSDK_NGX_Logging_Level loggingLevel,
                    NVSDK_NGX_Feature       sourceComponent )
{
    RTGL1::debug::Verbose( "DLSS2: NVSDK_NGX_Feature={}: {}", //
                           static_cast< int >( sourceComponent ),
                           message );
}

NVSDK_NGX_PerfQuality_Value ToNGXPerfQuality( RgRenderResolutionMode mode )
{
    switch( mode )
    {
        case RG_RENDER_RESOLUTION_MODE_ULTRA_PERFORMANCE:
            return NVSDK_NGX_PerfQuality_Value::NVSDK_NGX_PerfQuality_Value_UltraPerformance;
        case RG_RENDER_RESOLUTION_MODE_PERFORMANCE:
            return NVSDK_NGX_PerfQuality_Value::NVSDK_NGX_PerfQuality_Value_MaxPerf;
        case RG_RENDER_RESOLUTION_MODE_BALANCED:
            return NVSDK_NGX_PerfQuality_Value::NVSDK_NGX_PerfQuality_Value_Balanced;
        case RG_RENDER_RESOLUTION_MODE_QUALITY:
            return NVSDK_NGX_PerfQuality_Value::NVSDK_NGX_PerfQuality_Value_MaxQuality;
        case RG_RENDER_RESOLUTION_MODE_NATIVE_AA:
            return NVSDK_NGX_PerfQuality_Value::NVSDK_NGX_PerfQuality_Value_DLAA;
        default:
            assert( 0 );
            return NVSDK_NGX_PerfQuality_Value::NVSDK_NGX_PerfQuality_Value_Balanced;
    }
}

}


RTGL1::DLSS2::DLSS2( VkInstance       instance,
                     VkDevice         device,
                     VkPhysicalDevice physDevice,
                     const char*      pAppGuid )
    : m_device{ device }
{
    if( !RequiredVulkanExtensions_Instance() || !RequiredVulkanExtensions_Device( physDevice ) )
    {
        return;
    }

    NVSDK_NGX_Result r{};

    const auto binFolder      = Utils::FindBinFolder();
    const auto dataFolderPath = std::filesystem::path{ L"temp/dlss" };

    if( !exists( binFolder / "nvngx_dlss.dll" ) )
    {
        debug::Warning( "DLSS2: Disabled, as DLL file was not found: {}",
                        ( binFolder / "nvngx_dlss.dll" ).string() );
        return;
    }

    const wchar_t* binFolder_c = binFolder.c_str();

    auto pathsInfo = NVSDK_NGX_PathListInfo{
        .Path   = &binFolder_c,
        .Length = 1,
    };

    auto commonInfo = NVSDK_NGX_FeatureCommonInfo{
        .PathListInfo = pathsInfo,
        .LoggingInfo =
            LibConfig().dlssValidation
                ? NVSDK_NGX_LoggingInfo{ .LoggingCallback     = &PrintCallback,
                                         .MinimumLoggingLevel = NVSDK_NGX_LOGGING_LEVEL_ON }
                : NVSDK_NGX_LoggingInfo{ .LoggingCallback     = nullptr,
                                         .MinimumLoggingLevel = NVSDK_NGX_LOGGING_LEVEL_OFF },
    };

    if( LibConfig().dlssValidation )
    {
        std::error_code ec;
        create_directories( dataFolderPath, ec );
        if( ec )
        {
            debug::Error( "Failed to create DLSSTemp directory. create_directory error: {}",
                          ec.message() );
        }
    }

    r = NVSDK_NGX_VULKAN_Init_with_ProjectID( pAppGuid,
                                              NVSDK_NGX_EngineType::NVSDK_NGX_ENGINE_TYPE_CUSTOM,
                                              RG_RTGL_VERSION_API,
                                              dataFolderPath.c_str(),
                                              instance,
                                              physDevice,
                                              device,
                                              nullptr,
                                              nullptr,
                                              &commonInfo );
    if( NVSDK_NGX_FAILED( r ) )
    {
        debug::Error( "DLSS2: NVSDK_NGX_VULKAN_Init_with_ProjectID fail: {}",
                      static_cast< int >( r ) );
        Destroy();
        return;
    }
    m_initialized = true;

    r = NVSDK_NGX_VULKAN_GetCapabilityParameters( &m_params );
    if( NVSDK_NGX_FAILED( r ) || !m_params )
    {
        debug::Error( "DLSS2: NVSDK_NGX_VULKAN_GetCapabilityParameters fail: {}",
                      static_cast< int >( r ) );
        Destroy();
        return;
    }

    {
        int          needsUpdatedDriver    = 0;
        unsigned int minDriverVersionMajor = 0;
        unsigned int minDriverVersionMinor = 0;

        NVSDK_NGX_Result r_upd = m_params->Get(
            NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &needsUpdatedDriver );
        NVSDK_NGX_Result r_mjr = m_params->Get(
            NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor, &minDriverVersionMajor );
        NVSDK_NGX_Result r_mnr = m_params->Get(
            NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor, &minDriverVersionMinor );

        if( NVSDK_NGX_FAILED( r_upd ) || NVSDK_NGX_FAILED( r_mjr ) || NVSDK_NGX_FAILED( r_mnr ) )
        {
            debug::Error( "DLSS2: Minimum driver version was not reported" );
            Destroy();
            return;
        }

        if( needsUpdatedDriver )
        {
            debug::Error( "DLSS2: Can't load: Outdated driver. Min driver version: {}",
                          minDriverVersionMinor );
            Destroy();
            return;
        }
        debug::Verbose( "DLSS2: Reported Min driver version: {}", minDriverVersionMinor );
    }
    {
        int              isDlssSupported = 0;
        NVSDK_NGX_Result featureInitResult;

        r = m_params->Get( NVSDK_NGX_Parameter_SuperSampling_Available, &isDlssSupported );
        if( NVSDK_NGX_FAILED( r ) || !isDlssSupported )
        {
            // more details about what failed (per feature init result)
            r = NVSDK_NGX_Parameter_GetI( m_params,
                                          NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult,
                                          reinterpret_cast< int* >( &featureInitResult ) );
            if( NVSDK_NGX_SUCCEED( r ) )
            {
                debug::Error(
                    "DLSS2: Not available on this hardware/platform. FeatureInitResult={}",
                    static_cast< int >( featureInitResult ) );
            }
            Destroy();
            return;
        }
    }
}


bool RTGL1::DLSS2::Valid() const
{
    return m_initialized && m_params;
}

RTGL1::DLSS2::~DLSS2()
{
    Destroy();
}


void RTGL1::DLSS2::Destroy()
{
    vkDeviceWaitIdle( m_device );

    if( m_feature )
    {
        NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_ReleaseFeature( m_feature );
        assert( NVSDK_NGX_SUCCEED( r ) );
        m_feature = nullptr;
    }

    if( m_params )
    {
        NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_DestroyParameters( m_params );
        assert( NVSDK_NGX_SUCCEED( r ) );
        m_params = nullptr;
    }

    if( m_initialized )
    {
        NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_Shutdown1( m_device );
        assert( NVSDK_NGX_SUCCEED( r ) );
        m_initialized = false;
    }
}

namespace RTGL1
{
namespace
{
    auto CreateDlssFeature( NVSDK_NGX_Parameter*   params,
                            VkDevice               device,
                            VkCommandBuffer        cmd,
                            const ResolutionState& resolution,
                            NVSDK_NGX_Handle*      oldFeature ) -> NVSDK_NGX_Handle*
    {
        auto dlssParams = NVSDK_NGX_DLSS_Create_Params{
            .Feature =
                NVSDK_NGX_Feature_Create_Params{
                    .InWidth        = resolution.renderWidth,
                    .InHeight       = resolution.renderHeight,
                    .InTargetWidth  = resolution.upscaledWidth,
                    .InTargetHeight = resolution.upscaledHeight,
                    // .InPerfQualityValue = ToNGXPerfQuality( resolution.GetResolutionMode() ),
                },
            .InFeatureCreateFlags   = 0,
            .InEnableOutputSubrects = false,
        };

        // motion vectors are in render resolution, not target resolution
        dlssParams.InFeatureCreateFlags |= NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
        dlssParams.InFeatureCreateFlags |= 0; // NVSDK_NGX_DLSS_Feature_Flags_MVJittered;
        dlssParams.InFeatureCreateFlags |= NVSDK_NGX_DLSS_Feature_Flags_IsHDR;
        dlssParams.InFeatureCreateFlags |= 0; // NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
        dlssParams.InFeatureCreateFlags |= 0; // NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;

        // only one phys device
        uint32_t creationNodeMask   = 1;
        uint32_t visibilityNodeMask = 1;

        // destroy previous one
        if( oldFeature != nullptr )
        {
            vkDeviceWaitIdle( device );

            NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_ReleaseFeature( oldFeature );
            if( NVSDK_NGX_FAILED( r ) )
            {
                debug::Warning( "DLSS2: NVSDK_NGX_VULKAN_ReleaseFeature fail: {}",
                                static_cast< int >( r ) );
            }
        }

        NVSDK_NGX_DLSS_Hint_Render_Preset preset = LibConfig().dlssForceDefaultPreset
                                                       ? NVSDK_NGX_DLSS_Hint_Render_Preset_Default
                                                       : NVSDK_NGX_DLSS_Hint_Render_Preset_E;
        // clang-format off
        NVSDK_NGX_Parameter_SetUI(params, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA, preset );
        NVSDK_NGX_Parameter_SetUI(params, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality, preset );
        NVSDK_NGX_Parameter_SetUI(params, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced, preset );
        NVSDK_NGX_Parameter_SetUI(params, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance, preset );
        NVSDK_NGX_Parameter_SetUI(params, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance, preset );
        NVSDK_NGX_Parameter_SetUI(params, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraQuality, preset );
        // clang-format on

        NVSDK_NGX_Handle* newFeature{ nullptr };
        NVSDK_NGX_Result  r = NGX_VULKAN_CREATE_DLSS_EXT( cmd, //
                                                         creationNodeMask,
                                                         visibilityNodeMask,
                                                         &newFeature,
                                                         params,
                                                         &dlssParams );
        if( NVSDK_NGX_FAILED( r ) )
        {
            debug::Warning( "DLSS2: NGX_VULKAN_CREATE_DLSS_EXT fail: {}", static_cast< int >( r ) );
            return nullptr;
        }
        return newFeature;
    }

    constexpr FramebufferImageIndex INPUT_IMAGES[] = {
        FB_IMAGE_INDEX_FINAL,
        FB_IMAGE_INDEX_DEPTH_NDC,
        FB_IMAGE_INDEX_DEPTH_WORLD,
        FB_IMAGE_INDEX_MOTION_DLSS,
    };
    constexpr FramebufferImageIndex OUTPUT_IMAGE = RTGL1::FB_IMAGE_INDEX_UPSCALED_PONG;

    NVSDK_NGX_Resource_VK ToNGXResource( const Framebuffers&   framebuffers,
                                         uint32_t              frameIndex,
                                         FramebufferImageIndex fbImage,
                                         NVSDK_NGX_Dimensions  size,
                                         bool                  withWriteAccess = false )
    {
        assert( fbImage == OUTPUT_IMAGE || std::ranges::contains( INPUT_IMAGES, fbImage ) );

        auto [ image, view, format ] = framebuffers.GetImageHandles( fbImage, frameIndex );

        auto subresourceRange = VkImageSubresourceRange{
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        };

        return NVSDK_NGX_Create_ImageView_Resource_VK( view, //
                                                       image,
                                                       subresourceRange,
                                                       format,
                                                       size.Width,
                                                       size.Height,
                                                       withWriteAccess );
    }

}
}

auto RTGL1::DLSS2::Apply( VkCommandBuffer               cmd,
                          uint32_t                      frameIndex,
                          Framebuffers&                 framebuffers,
                          const RenderResolutionHelper& renderResolution,
                          RgFloat2D                     jitterOffset,
                          double                        timeDelta,
                          bool                          resetAccumulation ) -> FramebufferImageIndex
{
    auto label = CmdLabel{ cmd, "DLSS2" };

    if( !Valid() )
    {
        debug::Error( "DLSS2: Failed to validate, DLSS will not be applied" );
        assert( 0 );
        return OUTPUT_IMAGE;
    }

    {
        auto newResolution = renderResolution.GetResolutionState();
        if( m_prevResolution != newResolution )
        {
            m_prevResolution = newResolution;
            m_feature = CreateDlssFeature( m_params, m_device, cmd, newResolution, m_feature );

            if( !m_feature )
            {
                assert( 0 );
                return OUTPUT_IMAGE;
            }
        }
    }

    framebuffers.BarrierMultiple( cmd, //
                                  frameIndex,
                                  INPUT_IMAGES,
                                  Framebuffers::BarrierType::Storage );


    auto sourceOffset = NVSDK_NGX_Coordinates{
        0,
        0,
    };
    auto sourceSize = NVSDK_NGX_Dimensions{
        renderResolution.Width(),
        renderResolution.Height(),
    };
    auto targetSize = NVSDK_NGX_Dimensions{
        renderResolution.UpscaledWidth(),
        renderResolution.UpscaledHeight(),
    };

    // clang-format off
    NVSDK_NGX_Resource_VK unresolvedColorResource = ToNGXResource( framebuffers, frameIndex, FB_IMAGE_INDEX_FINAL, sourceSize );
    NVSDK_NGX_Resource_VK resolvedColorResource   = ToNGXResource( framebuffers, frameIndex, OUTPUT_IMAGE, targetSize, true );
    NVSDK_NGX_Resource_VK motionVectorsResource   = ToNGXResource( framebuffers, frameIndex, FB_IMAGE_INDEX_MOTION_DLSS, sourceSize );
    NVSDK_NGX_Resource_VK depthResource           = ToNGXResource( framebuffers, frameIndex, FB_IMAGE_INDEX_DEPTH_NDC, sourceSize );
    NVSDK_NGX_Resource_VK rayLengthResource       = ToNGXResource( framebuffers, frameIndex, FB_IMAGE_INDEX_DEPTH_WORLD, sourceSize );
    // clang-format on


    auto evalParams = NVSDK_NGX_VK_DLSS_Eval_Params{
        .Feature  = { .pInColor = &unresolvedColorResource, .pInOutput = &resolvedColorResource },
        .pInDepth = &depthResource,
        .pInMotionVectors          = &motionVectorsResource,
        .InJitterOffsetX           = jitterOffset.data[ 0 ] * ( -1 ),
        .InJitterOffsetY           = jitterOffset.data[ 1 ] * ( -1 ),
        .InRenderSubrectDimensions = sourceSize,
        .InReset                   = resetAccumulation ? 1 : 0,
        .InMVScaleX                = float( sourceSize.Width ),
        .InMVScaleY                = float( sourceSize.Height ),
        .InColorSubrectBase        = sourceOffset,
        .InDepthSubrectBase        = sourceOffset,
        .InMVSubrectBase           = sourceOffset,
        .InTranslucencySubrectBase = sourceOffset,
        .InPreExposure             = 1.0f,
        .InExposureScale           = 1.0f,
        .InToneMapperType          = NVSDK_NGX_TONEMAPPER_ONEOVERLUMA,
        .InFrameTimeDeltaInMsec    = float( timeDelta * 1000.0 ),
        .pInRayTracingHitDistance  = &rayLengthResource,
    };

    NVSDK_NGX_Result r = NGX_VULKAN_EVALUATE_DLSS_EXT( cmd, m_feature, m_params, &evalParams );

    if( NVSDK_NGX_FAILED( r ) )
    {
        debug::Warning( "DLSS2: NGX_VULKAN_EVALUATE_DLSS_EXT fail: {}", static_cast< int >( r ) );
    }
    return OUTPUT_IMAGE;
}

auto RTGL1::DLSS2::GetOptimalSettings( uint32_t               userWidth,
                                       uint32_t               userHeight,
                                       RgRenderResolutionMode mode ) const
    -> std::pair< uint32_t, uint32_t >
{
    if( !Valid() )
    {
        assert( 0 );
        return { userWidth, userHeight };
    }

    uint32_t renderWidth  = userWidth;
    uint32_t renderHeight = userHeight;
    uint32_t minWidth     = userWidth;
    uint32_t minHeight    = userHeight;
    uint32_t maxWidth     = userWidth;
    uint32_t maxHeight    = userHeight;
    float    sharpness    = 1.0f;

    NVSDK_NGX_Result r = NGX_DLSS_GET_OPTIMAL_SETTINGS( m_params,
                                                        userWidth,
                                                        userHeight,
                                                        ToNGXPerfQuality( mode ),
                                                        &renderWidth,
                                                        &renderHeight,
                                                        &maxWidth,
                                                        &maxHeight,
                                                        &minWidth,
                                                        &minHeight,
                                                        &sharpness );
    if( NVSDK_NGX_FAILED( r ) )
    {
        debug::Warning( "DLSS2: NGX_DLSS_GET_OPTIMAL_SETTINGS fail: {}", static_cast< int >( r ) );
        assert( 0 );
        return { userWidth, userHeight };
    }
    return { renderWidth, renderHeight };
}

auto RTGL1::DLSS2::RequiredVulkanExtensions_Instance()
    -> std::optional< std::vector< const char* > >
{
    auto supported = std::vector< VkExtensionProperties >{};
    {
        VkResult r{};
        uint32_t count = 0;

        r = vkEnumerateInstanceExtensionProperties( nullptr, &count, nullptr );
        if( r != VK_SUCCESS )
        {
            return std::nullopt;
        }

        supported.resize( count );
        r = vkEnumerateInstanceExtensionProperties( nullptr, &count, supported.data() );
        if( r != VK_SUCCESS )
        {
            return std::nullopt;
        }
    }

    auto required = std::vector< const char* >{};
    {
        uint32_t     instanceExtCount;
        const char** ppInstanceExts;
        uint32_t     dummy0;
        const char** dummy1;

        NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_RequiredExtensions( &instanceExtCount, //
                                                                  &ppInstanceExts,
                                                                  &dummy0,
                                                                  &dummy1 );
        if( NVSDK_NGX_SUCCEED( r ) )
        {
            required = std::vector< const char* >{ ppInstanceExts, //
                                                   ppInstanceExts + instanceExtCount };
        }
        else
        {
            debug::Warning( "DLSS2: NVSDK_NGX_VULKAN_RequiredExtensions fail: {}",
                            static_cast< int >( r ) );
            return std::nullopt;
        }
    }

    for( const char* r : required )
    {
        bool isSupported = std::ranges::any_of( supported, [ r ]( const VkExtensionProperties& s ) {
            return strncmp( r, s.extensionName, std::size( s.extensionName ) ) == 0;
        } );

        if( !isSupported )
        {
            debug::Warning(
                "DLSS2: Requires Vulkan instance extension {}, but the system doesn't support it",
                r );
            return std::nullopt;
        }
    }
    return required;
}

auto RTGL1::DLSS2::RequiredVulkanExtensions_Device( VkPhysicalDevice physDevice )
    -> std::optional< std::vector< const char* > >
{
    auto supported = std::vector< VkExtensionProperties >{};
    {
        VkResult r{};
        uint32_t count = 0;

        r = vkEnumerateDeviceExtensionProperties( physDevice, nullptr, &count, nullptr );
        if( r != VK_SUCCESS )
        {
            return std::nullopt;
        }

        supported.resize( count );
        r = vkEnumerateDeviceExtensionProperties( physDevice, nullptr, &count, supported.data() );
        if( r != VK_SUCCESS )
        {
            return std::nullopt;
        }
    }

    auto required = std::vector< const char* >{};
    {
        uint32_t     dummy0;
        const char** dummy1;
        uint32_t     deviceExtCount;
        const char** ppDeviceExts;

        NVSDK_NGX_Result r = NVSDK_NGX_VULKAN_RequiredExtensions( &dummy0, //
                                                                  &dummy1,
                                                                  &deviceExtCount,
                                                                  &ppDeviceExts );
        if( NVSDK_NGX_SUCCEED( r ) )
        {
            required = std::vector< const char* >{ ppDeviceExts, //
                                                   ppDeviceExts + deviceExtCount };
        }
        else
        {
            debug::Warning( "DLSS2: NVSDK_NGX_VULKAN_RequiredExtensions fail: {}",
                            static_cast< int >( r ) );
            return std::nullopt;
        }
    }

    for( const char* r : required )
    {
        bool isSupported = std::ranges::any_of( supported, [ r ]( const VkExtensionProperties& s ) {
            return strncmp( r, s.extensionName, std::size( s.extensionName ) ) == 0;
        } );

        if( !isSupported )
        {
            debug::Warning(
                "DLSS2: Requires Vulkan device extension {}, but the system doesn't support it",
                r );
            return std::nullopt;
        }
    }
    return required;
}

#else

RTGL1::DLSS2::DLSS2( VkInstance       instance,
                     VkDevice         device,
                     VkPhysicalDevice physDevice,
                     const char*      pAppGuid,
                     bool             enableDebug )
{
}

RTGL1::DLSS2::~DLSS2() = default;

auto RTGL1::DLSS2::Apply( VkCommandBuffer,
                          uint32_t,
                          Framebuffers&,
                          const RenderResolutionHelper&,
                          RgFloat2D,
                          double,
                          bool ) -> FramebufferImageIndex
{
    assert( 0 );
    return FB_IMAGE_INDEX_UPSCALED_PONG;
}
auto RTGL1::DLSS2::GetOptimalSettings( uint32_t               userWidth,
                                       uint32_t               userHeight,
                                       RgRenderResolutionMode mode ) const
    -> std::pair< uint32_t, uint32_t >
{
    assert( 0 );
    return { userWidth, userHeight };
}
auto RTGL1::DLSS2::RequiredVulkanExtensions_Instance()
    -> std::optional< std::vector< const char* > >
{
    return std::nullopt;
}
auto RTGL1::DLSS2::RequiredVulkanExtensions_Device( VkPhysicalDevice physDevice )
    -> std::optional< std::vector< const char* > >
{
    return std::nullopt;
}
bool RTGL1::DLSS2::Valid() const
{
    return false;
}
void RTGL1::DLSS2::Destroy()
{
}

#endif
