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

#include "DLSS3_DX12.h"

#include <filesystem>
#include <regex>

#include "RTGL1/RTGL1.h"
#include "CmdLabel.h"
#include "DynamicSdk.h"
#include "RenderResolutionHelper.h"
#include "Scene.h"
#include "Utils.h"

#include "DX12_CopyFramebuf.h"
#include "DX12_Interop.h"

#include <d3d12.h>
#include <dxgi1_5.h>

#include <sl.h>
#include <sl_consts.h>
#include <sl_helpers.h>
#include <sl_dlss_g.h>
#include <sl_security.h>

namespace
{

auto LoadInterposerDll( const std::filesystem::path& folder ) -> HMODULE
{
    assert( folder.is_absolute() );
    auto dllPath = folder / "sl.interposer.dll";

    if( !sl::security::verifyEmbeddedSignature( dllPath.c_str() ) )
    {
        RTGL1::debug::Error(
            "(NVIDIA Streamline): Failed to verify signature for NVIDIA Streamline: {}",
            dllPath.string() );
        return nullptr;
    }

    HMODULE dll = LoadLibraryW( dllPath.c_str() );
    if( !dll )
    {
        RTGL1::debug::Error( "(NVIDIA Streamline): Failed to load DLL \'{}\'", dllPath.string() );
    }
    return dll;
}

struct StreamlineSdk
{
    constexpr static const char* SdkName() { return "Streamline SDK"; }

    DYNAMICSDK_DECLARE( slAllocateResources );
    DYNAMICSDK_DECLARE( slEvaluateFeature );
    DYNAMICSDK_DECLARE( slFreeResources );
    DYNAMICSDK_DECLARE( slGetFeatureFunction );
    DYNAMICSDK_DECLARE( slGetFeatureRequirements );
    DYNAMICSDK_DECLARE( slGetFeatureVersion );
    DYNAMICSDK_DECLARE( slGetNativeInterface );
    DYNAMICSDK_DECLARE( slGetNewFrameToken );
    DYNAMICSDK_DECLARE( slInit );
    DYNAMICSDK_DECLARE( slIsFeatureLoaded );
    DYNAMICSDK_DECLARE( slIsFeatureSupported );
    DYNAMICSDK_DECLARE( slSetConstants );
    DYNAMICSDK_DECLARE( slSetD3DDevice );
    DYNAMICSDK_DECLARE( slSetFeatureLoaded );
    DYNAMICSDK_DECLARE( slSetTag );
    DYNAMICSDK_DECLARE( slShutdown );
    DYNAMICSDK_DECLARE( slUpgradeInterface );

    DYNAMICSDK_DECLARE( CreateDXGIFactory );
    DYNAMICSDK_DECLARE( CreateDXGIFactory1 );
    DYNAMICSDK_DECLARE( CreateDXGIFactory2 );
    DYNAMICSDK_DECLARE( D3D12CreateDevice );
    DYNAMICSDK_DECLARE( D3D12CreateRootSignatureDeserializer );
    DYNAMICSDK_DECLARE( D3D12CreateVersionedRootSignatureDeserializer );
    DYNAMICSDK_DECLARE( D3D12EnableExperimentalFeatures );
    DYNAMICSDK_DECLARE( D3D12GetDebugInterface );
    DYNAMICSDK_DECLARE( D3D12GetInterface );
    DYNAMICSDK_DECLARE( D3D12SerializeRootSignature );
    DYNAMICSDK_DECLARE( D3D12SerializeVersionedRootSignature );
    DYNAMICSDK_DECLARE( DXGIGetDebugInterface1 );

    DYNAMICSDK_DECLARE( slDLSSGetOptimalSettings );
    DYNAMICSDK_DECLARE( slDLSSGetState );
    DYNAMICSDK_DECLARE( slDLSSSetOptions );

    DYNAMICSDK_DECLARE( slPCLGetState );
    DYNAMICSDK_DECLARE( slPCLSetMarker );

    DYNAMICSDK_DECLARE( slReflexGetState );
    DYNAMICSDK_DECLARE( slReflexSleep );
    DYNAMICSDK_DECLARE( slReflexSetOptions );

    DYNAMICSDK_DECLARE( slDLSSGGetState );
    DYNAMICSDK_DECLARE( slDLSSGSetOptions );
};


auto LoadDllFunctions( const std::filesystem::path& folder ) -> DynamicSdk< StreamlineSdk >
{
    auto sdk = DynamicSdk< StreamlineSdk >{};

    if( auto sldll = sdk.Add( LoadInterposerDll( folder ) ) )
    {
        DYNAMICSDK_FETCH( sldll, slAllocateResources );
        DYNAMICSDK_FETCH( sldll, slEvaluateFeature );
        DYNAMICSDK_FETCH( sldll, slFreeResources );
        DYNAMICSDK_FETCH( sldll, slGetFeatureFunction );
        DYNAMICSDK_FETCH( sldll, slGetFeatureRequirements );
        DYNAMICSDK_FETCH( sldll, slGetFeatureVersion );
        DYNAMICSDK_FETCH( sldll, slGetNativeInterface );
        DYNAMICSDK_FETCH( sldll, slGetNewFrameToken );
        DYNAMICSDK_FETCH( sldll, slInit );
        DYNAMICSDK_FETCH( sldll, slIsFeatureLoaded );
        DYNAMICSDK_FETCH( sldll, slIsFeatureSupported );
        DYNAMICSDK_FETCH( sldll, slSetConstants );
        DYNAMICSDK_FETCH( sldll, slSetD3DDevice );
        DYNAMICSDK_FETCH( sldll, slSetFeatureLoaded );
        DYNAMICSDK_FETCH( sldll, slSetTag );
        DYNAMICSDK_FETCH( sldll, slShutdown );
        DYNAMICSDK_FETCH( sldll, slUpgradeInterface );

        DYNAMICSDK_FETCH( sldll, CreateDXGIFactory );
        DYNAMICSDK_FETCH( sldll, CreateDXGIFactory1 );
        DYNAMICSDK_FETCH( sldll, CreateDXGIFactory2 );
        DYNAMICSDK_FETCH( sldll, D3D12CreateDevice );
        DYNAMICSDK_FETCH( sldll, D3D12CreateRootSignatureDeserializer );
        DYNAMICSDK_FETCH( sldll, D3D12CreateVersionedRootSignatureDeserializer );
        DYNAMICSDK_FETCH( sldll, D3D12EnableExperimentalFeatures );
        DYNAMICSDK_FETCH( sldll, D3D12GetDebugInterface );
        DYNAMICSDK_FETCH( sldll, D3D12GetInterface );
        DYNAMICSDK_FETCH( sldll, D3D12SerializeRootSignature );
        DYNAMICSDK_FETCH( sldll, D3D12SerializeVersionedRootSignature );
        DYNAMICSDK_FETCH( sldll, DXGIGetDebugInterface1 );
    }

    return OnlyFullyLoaded( std::move( sdk ) );
}


constexpr RTGL1::FramebufferImageIndex INPUT_IMAGES[] = {
    RTGL1::FB_IMAGE_INDEX_FINAL,
    RTGL1::FB_IMAGE_INDEX_DEPTH_NDC,
    RTGL1::FB_IMAGE_INDEX_MOTION_DLSS,
};

constexpr RTGL1::FramebufferImageIndex OUTPUT_IMAGE = RTGL1::FB_IMAGE_INDEX_UPSCALED_PONG;


struct InitArgs
{
    std::string appGuid{};
};
auto g_initArgs = std::optional< InitArgs >{};


auto pfn = DynamicSdk< StreamlineSdk >{};
}


void RTGL1::DLSS3_DX12::LoadSDK( const char* pAppGuid )
{
    g_initArgs = InitArgs{
        .appGuid = pAppGuid,
    };
    pfn = LoadDllFunctions( Utils::FindBinFolder() );
}

void RTGL1::DLSS3_DX12::UnloadSDK()
{
    pfn.Free();
}


namespace
{

auto FeatureName( sl::Feature f ) -> const char*
{
    switch( f )
    {
        case sl::kFeatureDLSS: return "DLSS Super Resolution";
        case sl::kFeatureNRD: return "NRD";
        case sl::kFeatureReflex: return "Reflex";
        case sl::kFeatureDLSS_G: return "DLSS Frame Generation";
        default: return "<no name>";
    }
}

auto FetchFeatureFunctions( const sl::AdapterInfo& adapter, sl::Feature slFeature ) -> bool
{
    if( !pfn.slGetFeatureFunction )
    {
        assert( 0 );
        return false;
    }

    {
        auto slr = pfn.slIsFeatureSupported( slFeature, adapter );
        if( slr != sl::Result::eOk )
        {
            RTGL1::debug::Warning(
                "[NVIDIA Streamline] SL feature \'{}\' is not supported. Error={}",
                FeatureName( slFeature ),
                int( slr ) );
            return false;
        }
    }
    {
        auto reqs = sl::FeatureRequirements{};
        auto slr  = pfn.slGetFeatureRequirements( slFeature, reqs );
        if( slr != sl::Result::eOk )
        {
            RTGL1::debug::Warning(
                "[NVIDIA Streamline] Failed to fetch requirements for SL feature \'{}\'. Error={}",
                FeatureName( slFeature ),
                int( slr ) );
            return false;
        }

        if( !( reqs.flags & sl::FeatureRequirementFlags::eD3D12Supported ) )
        {
            RTGL1::debug::Warning( "[NVIDIA Streamline] SL feature \'{}\' doesn't support D3D12",
                                   FeatureName( slFeature ) );
            return false;
        }

        if( reqs.osVersionDetected < reqs.osVersionRequired )
        {
            RTGL1::debug::Warning(
                "[NVIDIA Streamline] SL feature \'{}\' requires OS version {}, but detected: {}",
                FeatureName( slFeature ),
                reqs.osVersionRequired.toStr(),
                reqs.osVersionDetected.toStr() );

            {
                auto title = std::format( "NVIDIA {} Fail", FeatureName( slFeature ) );
                auto msg =
                    std::format( "For {}, required Windows version is {}, but you have: {}\n\n"
                                 "Please update your Windows OS",
                                 FeatureName( slFeature ),
                                 reqs.osVersionRequired.toStr(),
                                 reqs.osVersionDetected.toStr() );
                MessageBoxA( nullptr, msg.c_str(), title.c_str(), MB_OK );
            }
            return false;
        }

        if( reqs.driverVersionDetected < reqs.driverVersionRequired )
        {
            RTGL1::debug::Warning( "[NVIDIA Streamline] SL feature \'{}\' requires driver version "
                                   "{}, but detected: {}",
                                   FeatureName( slFeature ),
                                   reqs.driverVersionRequired.toStr(),
                                   reqs.driverVersionDetected.toStr() );

            {
                auto title = std::format( "NVIDIA {} Fail", FeatureName( slFeature ) );
                auto msg   = std::format(
                    "For {}, required NVIDIA driver version is {}, but you have: {}\n\n"
                      "Please update your drivers",
                    FeatureName( slFeature ),
                    reqs.driverVersionRequired.toStr(),
                    reqs.driverVersionDetected.toStr() );
                MessageBoxA( nullptr, msg.c_str(), title.c_str(), MB_OK );
            }
            return false;
        }

        bool forceVsyncOff = ( reqs.flags & sl::FeatureRequirementFlags::eVSyncOffRequired );
        if( !forceVsyncOff )
        {
            if( slFeature == sl::kFeatureDLSS_G )
            {
                RTGL1::debug::Warning(
                    "[NVIDIA Streamline] Expected that DLSS Frame Generation would require"
                    " VSync Off, but the library returned value specifies no such requirement" );
            }
        }

        assert( std::size( INPUT_IMAGES ) + 1 /* OUTPUT_IMAGE */ >= reqs.numRequiredTags );
    }

    auto fetchFeatureInto = [ slFeature ]< typename T >( T* dst, const char* name ) {
        void*      f   = nullptr;
        sl::Result slr = pfn.slGetFeatureFunction( slFeature, name, f );
        if( slr == sl::Result::eOk )
        {
            *dst = reinterpret_cast< T >( f );
            return true;
        }
        RTGL1::debug::Warning( "[NVIDIA Streamline] Failed to fetch SL feature function: {}",
                               name );
        return false;
    };

    switch( slFeature )
    {
        case sl::kFeatureDLSS:
            // clang-format off
            if( !fetchFeatureInto( &pfn.slDLSSGetOptimalSettings , "slDLSSGetOptimalSettings" ) ) { return false; }
            if( !fetchFeatureInto( &pfn.slDLSSGetState           , "slDLSSGetState"           ) ) { return false; }
            if( !fetchFeatureInto( &pfn.slDLSSSetOptions         , "slDLSSSetOptions"         ) ) { return false; }
            // clang-format on
            break;

        case sl::kFeaturePCL:
            // clang-format off
            if( !fetchFeatureInto( &pfn.slPCLGetState      , "slPCLGetState"   ) ) { return false; }
            if( !fetchFeatureInto( &pfn.slPCLSetMarker     , "slPCLSetMarker"  ) ) { return false; }
            // clang-format on
            break;

        case sl::kFeatureReflex:
            // clang-format off
            if( !fetchFeatureInto( &pfn.slReflexGetState   , "slReflexGetState"   ) ) { return false; }
            if( !fetchFeatureInto( &pfn.slReflexSleep      , "slReflexSleep"      ) ) { return false; }
            if( !fetchFeatureInto( &pfn.slReflexSetOptions , "slReflexSetOptions" ) ) { return false; }
            // clang-format on
            break;

        case sl::kFeatureDLSS_G:
            // clang-format off
            if( !fetchFeatureInto( &pfn.slDLSSGGetState    , "slDLSSGGetState"    ) ) { return false; }
            if( !fetchFeatureInto( &pfn.slDLSSGSetOptions  , "slDLSSGSetOptions"  ) ) { return false; }
            // clang-format on
            break;

        default: assert( 0 ); return false;
    }

    return true;
}

sl::FrameToken* MakeFrameToken( uint32_t frameId )
{
    if( !pfn.Valid() || !pfn.slGetNewFrameToken )
    {
        assert( 0 );
        return nullptr;
    }

    sl::FrameToken* frameToken = nullptr;
    if( SL_FAILED( slr, pfn.slGetNewFrameToken( frameToken, &frameId ) ) )
    {
        RTGL1::debug::Error( "[NVIDIA Streamline] slGetNewFrameToken fail. Error code: {}",
                             uint32_t( slr ) );
    }
    if( !frameToken )
    {
        RTGL1::debug::Error(
            "[NVIDIA Streamline] slGetNewFrameToken returned null but it has succeded" );
    }
    return frameToken;
}

}


auto RTGL1::DLSS3_DX12::MakeInstance( uint64_t gpuLuid, bool justCheckCompatibility ) //
    ->                                                                                //
    std::expected< DLSS3_DX12*, const char* >
{
    if( !pfn.Valid() || !g_initArgs )
    {
        return std::unexpected{ "Couldn't load NVIDIA DLSS3 libraries. Ensure that DLL files are available in the \'bin/\' folder" };
    }
    const char* pAppGuid = g_initArgs->appGuid.c_str();


    const auto binFolder = Utils::FindBinFolder();

    const wchar_t* pluginFolders[] = {
        binFolder.c_str(),
    };

    sl::Feature features[] = {
        sl::kFeatureDLSS,
        sl::kFeaturePCL,
        sl::kFeatureReflex,
        sl::kFeatureDLSS_G,
    };

    auto pref = sl::Preferences{};
    {
        pref.showConsole = LibConfig().dlssValidation;
        pref.logLevel    = LibConfig().dlssValidation ? sl::LogLevel::eDefault : sl::LogLevel::eOff;
        pref.logMessageCallback = []( sl::LogType type, const char* msg ) {
            RgMessageSeverityFlags rgSeverity =
                ( type == sl::LogType::eError )  ? RG_MESSAGE_SEVERITY_ERROR
                : ( type == sl::LogType::eWarn ) ? RG_MESSAGE_SEVERITY_WARNING
                : ( type == sl::LogType::eInfo ) ? RG_MESSAGE_SEVERITY_INFO
                                                 : RG_MESSAGE_SEVERITY_VERBOSE;
            debug::detail::Print( rgSeverity, msg );
        };
        pref.numPathsToPlugins = std::size( pluginFolders );
        pref.pathsToPlugins    = pluginFolders;
        pref.flags             = sl::PreferenceFlags::eUseManualHooking |
                     sl::PreferenceFlags::eDisableCLStateTracking |
                     sl::PreferenceFlags::eUseDXGIFactoryProxy;
        pref.featuresToLoad    = std::data( features );
        pref.numFeaturesToLoad = std::size( features );
        pref.engineVersion     = RG_RTGL_VERSION_API;
        pref.projectId         = pAppGuid;
        pref.renderAPI         = sl::RenderAPI::eD3D12;
    }

    if( SL_FAILED( slr, pfn.slInit( pref, sl::kSDKVersion ) ) )
    {
        switch( slr )
        {
            case sl::Result::eErrorDriverOutOfDate:
                debug::Warning( "[NVIDIA Streamline] Please, update to the latest drivers" );
                return std::unexpected{ "Out-of-date Drivers" };
            case sl::Result::eErrorOSOutOfDate:
                debug::Warning( "[NVIDIA Streamline] Please, update the Windows OS" );
                return std::unexpected{ "Out-of-date Windows Version" };
            default:
                debug::Warning(
                    "[NVIDIA Streamline] Failed to initialize Streamline. Error code: {}",
                              uint32_t( slr ) );
                return std::unexpected{ "NVIDIA Streamline initialization failure" };
        }
    }

    auto adapter = sl::AdapterInfo{};
    {
        adapter.deviceLUID            = reinterpret_cast< uint8_t* >( &gpuLuid );
        adapter.deviceLUIDSizeInBytes = sizeof( gpuLuid );
    }

    for( auto f : features )
    {
        if( !FetchFeatureFunctions( adapter, f ) )
        {
            debug::Warning( "[NVIDIA Streamline] Failed to fetch {} functions", FeatureName( f ) );
            pfn.slShutdown();
            pfn = {};
            return std::unexpected{ f == sl::kFeatureDLSS     ? "NVIDIA DLSS is not supported"
                                    : f == sl::kFeatureReflex ? "NVIDIA Reflex is not supported"
                                    : f == sl::kFeatureDLSS_G
                                        ? "NVIDIA Frame Generation is not supported"
                                        : "NVIDIA <feature_id> is not supported" };
        }
    }

    if( justCheckCompatibility )
    {
        return static_cast< DLSS3_DX12* >( nullptr );
    }

    if( !dxgi::InitAsDLFG(
            gpuLuid,
            []( void* d3dDevice ) {
                auto r = pfn.slSetD3DDevice( d3dDevice );
                assert( r == sl::Result::eOk );
            },
            []( void** baseInterface ) {
                auto r = pfn.slUpgradeInterface( baseInterface );
                assert( r == sl::Result::eOk );
            },
            []( void* proxyInterface, void** baseInterface ) {
                auto r = pfn.slGetNativeInterface( proxyInterface, baseInterface );
                assert( r == sl::Result::eOk );
            } ) )
    {
        debug::Warning( "[NVIDIA Streamline] Failed to init DX12 for DLSS3" );
        pfn.slShutdown();
        pfn = {};
        return std::unexpected{ "DirectX 12 initialization failed for DLSS3" };
    }

    auto reflexConst = sl::ReflexOptions{};
    {
        reflexConst.mode                 = sl::ReflexMode::eOff;
        reflexConst.frameLimitUs         = 0;
        reflexConst.useMarkersToOptimize = false;
        reflexConst.virtualKey           = VK_F13;
    }
    auto slr        = pfn.slReflexSetOptions( reflexConst );
    auto frameToken = MakeFrameToken( 0 );
    if( slr != sl::Result::eOk || !frameToken )
    {
        debug::Warning( "slReflexSetOptions / slGetNewFrameToken fail" );
        dxgi::Destroy();
        pfn.slShutdown();
        pfn = {};
        return std::unexpected{ "slReflexSetOptions / slGetNewFrameToken failure" };
    }

    auto inst = new DLSS3_DX12{};
    {
        inst->m_frameToken = frameToken;
    }
    return inst;
}

RTGL1::DLSS3_DX12::~DLSS3_DX12()
{
    if( !pfn.Valid() )
    {
        return;
    }

    if( SL_FAILED( slr, pfn.slShutdown() ) )
    {
        debug::Warning( "[NVIDIA Streamline] Failed to shutdown Streamline. Error code: {}",
                        uint32_t( slr ) );
    }
}

namespace
{

sl::DLSSMode ToSlPerfQuality( RgRenderResolutionMode mode )
{
    switch( mode )
    {
        case RG_RENDER_RESOLUTION_MODE_ULTRA_PERFORMANCE: return sl::DLSSMode::eUltraPerformance;
        case RG_RENDER_RESOLUTION_MODE_PERFORMANCE: return sl::DLSSMode::eMaxPerformance;
        case RG_RENDER_RESOLUTION_MODE_BALANCED: return sl::DLSSMode::eBalanced;
        case RG_RENDER_RESOLUTION_MODE_QUALITY: return sl::DLSSMode::eMaxQuality;
        case RG_RENDER_RESOLUTION_MODE_NATIVE_AA: return sl::DLSSMode::eDLAA;
        case RG_RENDER_RESOLUTION_MODE_CUSTOM: return sl::DLSSMode::eBalanced;
        default: assert( 0 ); return sl::DLSSMode::eBalanced;
    }
}

sl::Resource ToSlResource( const RTGL1::Framebuffers&   framebuffers,
                           uint32_t                     frameIndex,
                           RTGL1::FramebufferImageIndex fbImage,
                           const RgExtent2D&            size,
                           bool                         withWriteAccess = false )
{
    assert( fbImage == OUTPUT_IMAGE || std::ranges::contains( INPUT_IMAGES, fbImage ) ||
            fbImage == RTGL1::FB_IMAGE_INDEX_HUD_ONLY );

    // if fails, need support for .._Prev framebufs
    assert( framebuffers.GetImage( fbImage, frameIndex ) ==
            framebuffers.GetImage( fbImage, ( frameIndex + 1 ) % RTGL1::MAX_FRAMES_IN_FLIGHT ) );

    auto sharedImage = RTGL1::dxgi::Framebuf_GetVkDx12Shared( fbImage );

    auto r = sl::Resource{};
    {
        r.type              = sl::ResourceType::eTex2d;
        r.native            = sharedImage.d3d12resource;
        r.state             = D3D12_RESOURCE_STATE_COMMON;
        r.width             = size.width;
        r.height            = size.height;
        r.nativeFormat      = DXGI_FORMAT( sharedImage.dxgiformat );
        r.mipLevels         = 1;
        r.arrayLayers       = 1;
        r.gpuVirtualAddress = 0;
    }
    return r;
}

sl::DLSSOptions MakeDlssOptions( uint32_t targetWidth, uint32_t targetHeight, sl::DLSSMode mode )
{
    sl::DLSSPreset preset = RTGL1::LibConfig().dlssForceDefaultPreset ? sl::DLSSPreset::eDefault
                                                                      : sl::DLSSPreset::ePresetE;
    auto opt = sl::DLSSOptions{};
    {
        opt.mode                   = mode;
        opt.outputWidth            = targetWidth;
        opt.outputHeight           = targetHeight;
        opt.sharpness              = 0.0f;
        opt.preExposure            = 1.0f;
        opt.exposureScale          = 1.0f;
        opt.colorBuffersHDR        = sl::Boolean::eTrue;
        opt.useAutoExposure        = sl::Boolean::eFalse;
        opt.dlaaPreset             = preset;
        opt.qualityPreset          = preset;
        opt.balancedPreset         = preset;
        opt.performancePreset      = preset;
        opt.ultraPerformancePreset = preset;
        opt.ultraQualityPreset     = preset;
    }
    return opt;
}

}

void RTGL1::DLSS3_DX12::CopyVkInputsToDX12( VkCommandBuffer        cmd,
                                            uint32_t               frameIndex,
                                            const Framebuffers&    framebuffers,
                                            const ResolutionState& resolution )
{
    Framebuf_CopyVkToDX12( cmd,
                           frameIndex,
                           framebuffers,
                           resolution.renderWidth,
                           resolution.renderHeight,
                           INPUT_IMAGES );
}

void RTGL1::DLSS3_DX12::CopyDX12OutputToVk( VkCommandBuffer        cmd,
                                            uint32_t               frameIndex,
                                            const Framebuffers&    framebuffers,
                                            const ResolutionState& resolution )
{
    FramebufferImageIndex outputs[] = { OUTPUT_IMAGE };
    Framebuf_CopyDX12ToVk( cmd,
                           frameIndex,
                           framebuffers,
                           resolution.upscaledWidth,
                           resolution.upscaledHeight,
                           outputs );
}

auto RTGL1::DLSS3_DX12::Apply( ID3D12CommandList*            dx12cmd,
                               uint32_t                      frameIndex,
                               Framebuffers&                 framebuffers,
                               const RenderResolutionHelper& renderResolution,
                               RgFloat2D                     jitterOffset,
                               double                        timeDelta,
                               bool                          resetAccumulation,
                               const Camera&                 camera,
                               uint32_t                      frameId,
                               bool skipGeneratedFrame ) -> std::optional< FramebufferImageIndex >
{
    if( !dx12cmd )
    {
        debug::Warning( "DLSS3_DX12::Apply() was ignored, as ID3D12CommandList failed" );
        return {};
    }

    if( !pfn.Valid() )
    {
        assert( 0 );
        return {};
    }

    if( !m_frameToken )
    {
        debug::Warning( "DLSS3_DX12::m_frameToken is empty. Skipping DLSS3 frame" );
        return {};
    }

    /*
    if( AreEqual( m_prevDlssFeatureValues, renderResolution.GetResolutionState() ) )
    {
        // do not update the parameters?
    }
    m_prevDlssFeatureValues = renderResolution.GetResolutionState();
    */

    const auto sourceSize = RgExtent2D{
        renderResolution.Width(),
        renderResolution.Height(),
    };
    const auto targetSize = RgExtent2D{
        renderResolution.UpscaledWidth(),
        renderResolution.UpscaledHeight(),
    };

    {
        // clang-format off
        auto colorIn   = ToSlResource( framebuffers, frameIndex, FB_IMAGE_INDEX_FINAL,       sourceSize );
        auto colorOut  = ToSlResource( framebuffers, frameIndex, OUTPUT_IMAGE,               targetSize, true);
        auto depth     = ToSlResource( framebuffers, frameIndex, FB_IMAGE_INDEX_DEPTH_NDC,   sourceSize );
        auto motion    = ToSlResource( framebuffers, frameIndex, FB_IMAGE_INDEX_MOTION_DLSS, sourceSize );
        // auto rayLength = ToSlResource( framebuffers, frameIndex, FB_IMAGE_INDEX_DEPTH_WORLD, sourceSize );
        // auto hud       = ToSlResource( framebuffers, frameIndex, FB_IMAGE_INDEX_HUD_ONLY,    targetSize );

         // backbuffer subrect info to run FG on.
        auto backBufferSubrectInfo = sl::Extent{ 0, 0, targetSize.width, targetSize.height };

        // eValidUntilPresent, as those are the copies on DX12 side
        sl::ResourceTag inputs[] = {
            sl::ResourceTag{ nullptr,    sl::kBufferTypeBackbuffer,         sl::ResourceLifecycle{}, &backBufferSubrectInfo },
            sl::ResourceTag{ &colorIn,   sl::kBufferTypeScalingInputColor,  sl::ResourceLifecycle::eValidUntilPresent },
            sl::ResourceTag{ &colorOut,  sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eValidUntilPresent },
            sl::ResourceTag{ &depth,     sl::kBufferTypeDepth,              sl::ResourceLifecycle::eValidUntilPresent },
            sl::ResourceTag{ &motion,    sl::kBufferTypeMotionVectors,      sl::ResourceLifecycle::eValidUntilPresent },
            // sl::ResourceTag{ &rayLength, sl::kBufferTypeRaytracingDistance, sl::ResourceLifecycle::eOnlyValidNow      },
            sl::ResourceTag{ &colorOut,  sl::kBufferTypeHUDLessColor,    sl::ResourceLifecycle::eValidUntilPresent },
            // sl::ResourceTag{ &hud,       sl::kBufferTypeUIColorAndAlpha,    sl::ResourceLifecycle::eValidUntilEvaluate },
        };
        // clang-format on

        if( SL_FAILED(
                slr,
                pfn.slSetTag( sl::ViewportHandle{ 0 }, inputs, std::size( inputs ), dx12cmd ) ) )
        {
            debug::Error( "slDLSSSetOptions fail. Error code: {}", uint32_t( slr ) );
            return {};
        }
    }

    {
        sl::DLSSOptions dlssOptions =
            MakeDlssOptions( targetSize.width,
                             targetSize.height,
                             ToSlPerfQuality( renderResolution.GetResolutionMode() ) );

        if( SL_FAILED( slr, pfn.slDLSSSetOptions( sl::ViewportHandle{ 0 }, dlssOptions ) ) )
        {
            debug::Error( "slDLSSSetOptions fail. Error code: {}", uint32_t( slr ) );
            return {};
        }
    }

    {
        auto dlssgConst = sl::DLSSGOptions{};
        {
            dlssgConst.mode            = skipGeneratedFrame ? sl::DLSSGMode::eOff : sl::DLSSGMode::eOn;
            dlssgConst.mvecDepthWidth  = sourceSize.width;
            dlssgConst.mvecDepthHeight = sourceSize.height;
            dlssgConst.colorWidth      = targetSize.width;
            dlssgConst.colorHeight     = targetSize.height;
            dlssgConst.onErrorCallback = []( const sl::APIError& lastError ) {
                assert( SUCCEEDED( lastError.hres ) );
            };
        }
        if( SL_FAILED( slr, pfn.slDLSSGSetOptions( sl::ViewportHandle{ 0 }, dlssgConst ) ) )
        {
            debug::Error( "slDLSSGSetOptions fail. Error code: {}", uint32_t( slr ) );
            return {};
        }
    }

    {
        auto reflexConst = sl::ReflexOptions{};
        {
            reflexConst.mode                 = sl::ReflexMode::eLowLatency;
            reflexConst.frameLimitUs         = 0;
            reflexConst.useMarkersToOptimize = false;
            reflexConst.virtualKey           = VK_F13;
        }
        if( SL_FAILED( slr, pfn.slReflexSetOptions( reflexConst ) ) )
        {
            debug::Error( "slReflexSetOptions fail. Error code: {}", uint32_t( slr ) );
            return {};
        }
    }

    {
        auto consts = sl::Constants{};
        {
            const static auto identity = sl::float4x4{ {
                { 1, 0, 0, 0 },
                { 0, 1, 0, 0 },
                { 0, 0, 1, 0 },
                { 0, 0, 0, 1 },
            } };

            auto toSlMatrix = []( const float* m ) {
                auto slm = sl::float4x4{};
                for( int i = 0; i < 4; i++ )
                {
                    slm.row[ i ].x = m[ 0 * 4 + i ];
                    slm.row[ i ].y = m[ 1 * 4 + i ];
                    slm.row[ i ].z = m[ 2 * 4 + i ];
                    slm.row[ i ].w = m[ 3 * 4 + i ];
                }
                return slm;
            };

            consts.cameraViewToClip = toSlMatrix( camera.projection );
            consts.clipToCameraView = toSlMatrix( camera.projectionInverse );

            consts.clipToLensClip = identity;

            // assume that the projection matrix is constant
            consts.clipToPrevClip = identity;
            consts.prevClipToClip = identity;

            consts.jitterOffset = {
                jitterOffset.data[ 0 ] * ( -1 ),
                jitterOffset.data[ 1 ] * ( -1 ),
            };
            consts.mvecScale = { 1, 1 };

            consts.cameraPos = {
                camera.viewInverse[ 12 ],
                camera.viewInverse[ 13 ],
                camera.viewInverse[ 14 ],
            };
            consts.cameraUp = {
                camera.viewInverse[ 4 ],
                camera.viewInverse[ 5 ],
                camera.viewInverse[ 6 ],
            };
            consts.cameraRight = {
                camera.viewInverse[ 0 ],
                camera.viewInverse[ 1 ],
                camera.viewInverse[ 2 ],
            };
            consts.cameraFwd = {
                camera.viewInverse[ 8 ],
                camera.viewInverse[ 9 ],
                camera.viewInverse[ 10 ],
            };

            consts.cameraPinholeOffset = { 0, 0 };
            consts.cameraNear          = camera.cameraNear;
            consts.cameraFar           = camera.cameraFar;
            consts.cameraFOV           = camera.fovYRadians;
            consts.cameraAspectRatio   = camera.aspect;

            consts.cameraMotionIncluded = sl::Boolean::eTrue;

            consts.motionVectorsInvalidValue = sl::INVALID_FLOAT;
            consts.depthInverted             = sl::Boolean::eFalse;
            consts.motionVectors3D           = sl::Boolean::eFalse;
            consts.reset = resetAccumulation ? sl::Boolean::eTrue : sl::Boolean::eFalse;
            consts.orthographicProjection = sl::Boolean::eFalse;
            consts.motionVectorsDilated   = sl::Boolean::eFalse;
            consts.motionVectorsJittered  = sl::Boolean::eFalse;
        }

        if( SL_FAILED( slr, pfn.slSetConstants( consts, *m_frameToken, sl::ViewportHandle{ 0 } ) ) )
        {
            debug::Error( "slDLSSSetOptions fail. Error code: {}", uint32_t( slr ) );
            return {};
        }
    }

    {
        auto viewportHandle = sl::ViewportHandle{ 0 };

        const sl::BaseStructure* inputs[] = { &viewportHandle };

        if( SL_FAILED(
                slr,
                pfn.slEvaluateFeature(
                    sl::kFeatureDLSS, *m_frameToken, inputs, std::size( inputs ), dx12cmd ) ) )
        {
            debug::Error( "slEvaluateFeature for DLSS has failed. Error code: {}",
                          uint32_t( slr ) );
        }
        else
        {
            // TODO: restore state from Streamline? this note is from SDK:
            // Host is responsible for restoring state on the command list used restoreState( cmd );
        }
    }

    return OUTPUT_IMAGE;
}

auto RTGL1::DLSS3_DX12::GetOptimalSettings( uint32_t               userWidth,
                                            uint32_t               userHeight,
                                            RgRenderResolutionMode mode ) const
    -> std::pair< uint32_t, uint32_t >
{
    if( !pfn.Valid() )
    {
        assert( 0 );
        return { userWidth, userHeight };
    }

    sl::DLSSOptions input = MakeDlssOptions( userWidth, userHeight, ToSlPerfQuality( mode ) );

    auto optimal = sl::DLSSOptimalSettings{};
    if( SL_FAILED( slr, pfn.slDLSSGetOptimalSettings( input, optimal ) ) )
    {
        debug::Warning( "slDLSSGetOptimalSettings has failed. Error code: {}", uint32_t( slr ) );
        assert( 0 );
        return { userWidth, userHeight };
    }
    return { optimal.optimalRenderWidth, optimal.optimalRenderHeight };
}

namespace
{
void ReflexSetMarker( const sl::FrameToken* frame, sl::PCLMarker marker )
{
    if( !pfn.Valid() || !pfn.slPCLSetMarker )
    {
        assert( 0 );
        return;
    }
    if( !frame )
    {
        assert( 0 );
        return;
    }

    if( SL_FAILED( slr, pfn.slPCLSetMarker( marker, *frame ) ) )
    {
        RTGL1::debug::Warning( "slPCLSetMarker fail. Error code: {}", uint32_t( slr ) );
        assert( 0 );
    }
}
}

void RTGL1::DLSS3_DX12::Reflex_SimStart( uint32_t frameId )
{
    if( !pfn.Valid() || !pfn.slReflexSleep )
    {
        assert( 0 );
        return;
    }

    m_frameToken = MakeFrameToken( frameId );
    if( !m_frameToken )
    {
        assert( 0 );
        return;
    }

    if( SL_FAILED( slr, pfn.slReflexSleep( *m_frameToken ) ) )
    {
        debug::Error( "slReflexSleep fail. Error code: {}", uint32_t( slr ) );
    }

    ReflexSetMarker( m_frameToken, sl::PCLMarker::eSimulationStart );
}

void RTGL1::DLSS3_DX12::Reflex_SimEnd()
{
    ReflexSetMarker( m_frameToken, sl::PCLMarker::eSimulationEnd );
}

void RTGL1::DLSS3_DX12::Reflex_RenderStart()
{
    ReflexSetMarker( m_frameToken, sl::PCLMarker::eRenderSubmitStart );
}

void RTGL1::DLSS3_DX12::Reflex_RenderEnd()
{
    ReflexSetMarker( m_frameToken, sl::PCLMarker::eRenderSubmitEnd );
}

void RTGL1::DLSS3_DX12::Reflex_PresentStart()
{
    ReflexSetMarker( m_frameToken, sl::PCLMarker::ePresentStart );
}

void RTGL1::DLSS3_DX12::Reflex_PresentEnd()
{
    ReflexSetMarker( m_frameToken, sl::PCLMarker::ePresentEnd );
}
