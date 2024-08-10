// Copyright (c) 2023 Sultim Tsyrendashiev
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

#pragma once

#include "InternalExtensions.inl"

namespace RTGL1
{

namespace detail
{
    struct AnyInfoPrototype
    {
        RgStructureType sType;
        void*           pNext;
    };

    template< typename T >
    auto GetStructureType( const T* pInfo ) noexcept -> RgStructureType
    {
        if( pInfo )
        {
            return static_cast< const AnyInfoPrototype* >( pInfo )->sType;
        }
        return RG_STRUCTURE_TYPE_NONE;
    }

    template< typename T >
    auto GetPNext( T* pInfo ) noexcept
    {
        using AnyInfo_T =
            std::conditional_t< std::is_const_v< T >, const AnyInfoPrototype, AnyInfoPrototype >;
        using ReturnType = std::conditional_t< std::is_const_v< T >, const void*, void* >;

        if( pInfo )
        {
            return static_cast< ReturnType >( static_cast< AnyInfo_T* >( pInfo )->pNext );
        }
        return static_cast< ReturnType >( nullptr );
    }

    template< typename Target >
    constexpr auto TypeToStructureType = RgStructureType{ RG_STRUCTURE_TYPE_NONE };

    // clang-format off
    template<> constexpr auto TypeToStructureType< RgStartFrameRenderResolutionParams   > = RG_STRUCTURE_TYPE_START_FRAME_RENDER_RESOLUTION_PARAMS ;
    template<> constexpr auto TypeToStructureType< RgDrawFrameIlluminationParams        > = RG_STRUCTURE_TYPE_DRAW_FRAME_ILLUMINATION_PARAMS       ;
    template<> constexpr auto TypeToStructureType< RgDrawFrameVolumetricParams          > = RG_STRUCTURE_TYPE_DRAW_FRAME_VOLUMETRIC_PARAMS         ;
    template<> constexpr auto TypeToStructureType< RgDrawFrameTonemappingParams         > = RG_STRUCTURE_TYPE_DRAW_FRAME_TONEMAPPING_PARAMS        ;
    template<> constexpr auto TypeToStructureType< RgDrawFrameBloomParams               > = RG_STRUCTURE_TYPE_DRAW_FRAME_BLOOM_PARAMS              ;
    template<> constexpr auto TypeToStructureType< RgDrawFrameReflectRefractParams      > = RG_STRUCTURE_TYPE_DRAW_FRAME_REFLECT_REFRACT_PARAMS    ;
    template<> constexpr auto TypeToStructureType< RgDrawFrameSkyParams                 > = RG_STRUCTURE_TYPE_DRAW_FRAME_SKY_PARAMS                ;
    template<> constexpr auto TypeToStructureType< RgDrawFrameTexturesParams            > = RG_STRUCTURE_TYPE_DRAW_FRAME_TEXTURES_PARAMS           ;
    template<> constexpr auto TypeToStructureType< RgDrawFramePostEffectsParams         > = RG_STRUCTURE_TYPE_DRAW_FRAME_POST_EFFECTS_PARAMS       ;
    template<> constexpr auto TypeToStructureType< RgInstanceCreateInfo                 > = RG_STRUCTURE_TYPE_INSTANCE_CREATE_INFO                 ;
    template<> constexpr auto TypeToStructureType< RgMeshInfo                           > = RG_STRUCTURE_TYPE_MESH_INFO                            ;
    template<> constexpr auto TypeToStructureType< RgMeshPrimitiveInfo                  > = RG_STRUCTURE_TYPE_MESH_PRIMITIVE_INFO                  ;
    template<> constexpr auto TypeToStructureType< RgMeshPrimitivePortalEXT             > = RG_STRUCTURE_TYPE_MESH_PRIMITIVE_PORTAL_EXT            ;
    template<> constexpr auto TypeToStructureType< RgMeshPrimitiveTextureLayersEXT      > = RG_STRUCTURE_TYPE_MESH_PRIMITIVE_TEXTURE_LAYERS_EXT    ;
    template<> constexpr auto TypeToStructureType< RgMeshPrimitivePBREXT                > = RG_STRUCTURE_TYPE_MESH_PRIMITIVE_PBR_EXT               ;
    template<> constexpr auto TypeToStructureType< RgMeshPrimitiveAttachedLightEXT      > = RG_STRUCTURE_TYPE_MESH_PRIMITIVE_ATTACHED_LIGHT_EXT    ;
    template<> constexpr auto TypeToStructureType< RgMeshPrimitiveSwapchainedEXT        > = RG_STRUCTURE_TYPE_MESH_PRIMITIVE_SWAPCHAINED_EXT       ;
    template<> constexpr auto TypeToStructureType< RgLensFlareInfo                      > = RG_STRUCTURE_TYPE_LENS_FLARE_INFO                      ;
    template<> constexpr auto TypeToStructureType< RgLightInfo                          > = RG_STRUCTURE_TYPE_LIGHT_INFO                           ;
    template<> constexpr auto TypeToStructureType< RgLightAdditionalEXT                 > = RG_STRUCTURE_TYPE_LIGHT_ADDITIONAL_EXT                 ;
    template<> constexpr auto TypeToStructureType< RgLightDirectionalEXT                > = RG_STRUCTURE_TYPE_LIGHT_DIRECTIONAL_EXT                ;
    template<> constexpr auto TypeToStructureType< RgLightSphericalEXT                  > = RG_STRUCTURE_TYPE_LIGHT_SPHERICAL_EXT                  ;
    template<> constexpr auto TypeToStructureType< RgLightPolygonalEXT                  > = RG_STRUCTURE_TYPE_LIGHT_POLYGONAL_EXT                  ;
    template<> constexpr auto TypeToStructureType< RgLightSpotEXT                       > = RG_STRUCTURE_TYPE_LIGHT_SPOT_EXT                       ;
    template<> constexpr auto TypeToStructureType< RgOriginalTextureInfo                > = RG_STRUCTURE_TYPE_ORIGINAL_TEXTURE_INFO                ;
    template<> constexpr auto TypeToStructureType< RgStartFrameInfo                     > = RG_STRUCTURE_TYPE_START_FRAME_INFO                     ;
    template<> constexpr auto TypeToStructureType< RgDrawFrameInfo                      > = RG_STRUCTURE_TYPE_DRAW_FRAME_INFO                      ;
    template<> constexpr auto TypeToStructureType< RgCameraInfo                         > = RG_STRUCTURE_TYPE_CAMERA_INFO                          ;
    template<> constexpr auto TypeToStructureType< RgCameraInfoReadbackEXT              > = RG_STRUCTURE_TYPE_CAMERA_INFO_READ_BACK_EXT            ;
    template<> constexpr auto TypeToStructureType< RgOriginalTextureDetailsEXT          > = RG_STRUCTURE_TYPE_ORIGINAL_TEXTURE_DETAILS_EXT         ;
    template<> constexpr auto TypeToStructureType< RgSpawnFluidInfo                     > = RG_STRUCTURE_TYPE_SPAWN_FLUID_INFO                     ;
    template<> constexpr auto TypeToStructureType< RgStartFrameFluidParams              > = RG_STRUCTURE_TYPE_START_FRAME_FLUID_PARAMS             ;
#if RG_USE_REMIX
    template<> constexpr auto TypeToStructureType< RgStartFrameRemixParams              > = RG_STRUCTURE_TYPE_START_FRAME_REMIX_PARAMS             ;
#endif
    // clang-format on

    template< typename T >
    constexpr bool CheckMembers()
    {
        return offsetof( AnyInfoPrototype, sType ) == offsetof( T, sType ) &&
               sizeof( T::sType ) == sizeof( AnyInfoPrototype::sType ) &&
               offsetof( AnyInfoPrototype, pNext ) == offsetof( T, pNext ) &&
               sizeof( T::pNext ) == sizeof( AnyInfoPrototype::pNext );
    }

    static_assert( CheckMembers< RgStartFrameRenderResolutionParams >() );
    static_assert( CheckMembers< RgDrawFrameIlluminationParams >() );
    static_assert( CheckMembers< RgDrawFrameVolumetricParams >() );
    static_assert( CheckMembers< RgDrawFrameTonemappingParams >() );
    static_assert( CheckMembers< RgDrawFrameBloomParams >() );
    static_assert( CheckMembers< RgDrawFrameReflectRefractParams >() );
    static_assert( CheckMembers< RgDrawFrameSkyParams >() );
    static_assert( CheckMembers< RgDrawFrameTexturesParams >() );
    static_assert( CheckMembers< RgDrawFramePostEffectsParams >() );
    static_assert( CheckMembers< RgInstanceCreateInfo >() );
    static_assert( CheckMembers< RgMeshInfo >() );
    static_assert( CheckMembers< RgMeshPrimitiveInfo >() );
    static_assert( CheckMembers< RgMeshPrimitivePortalEXT >() );
    static_assert( CheckMembers< RgMeshPrimitiveTextureLayersEXT >() );
    static_assert( CheckMembers< RgMeshPrimitivePBREXT >() );
    static_assert( CheckMembers< RgMeshPrimitiveAttachedLightEXT >() );
    static_assert( CheckMembers< RgMeshPrimitiveSwapchainedEXT >() );
    static_assert( CheckMembers< RgLensFlareInfo >() );
    static_assert( CheckMembers< RgLightInfo >() );
    static_assert( CheckMembers< RgLightAdditionalEXT >() );
    static_assert( CheckMembers< RgLightDirectionalEXT >() );
    static_assert( CheckMembers< RgLightSphericalEXT >() );
    static_assert( CheckMembers< RgLightPolygonalEXT >() );
    static_assert( CheckMembers< RgLightSpotEXT >() );
    static_assert( CheckMembers< RgOriginalTextureInfo >() );
    static_assert( CheckMembers< RgStartFrameInfo >() );
    static_assert( CheckMembers< RgDrawFrameInfo >() );
    static_assert( CheckMembers< RgCameraInfo >() );
    static_assert( CheckMembers< RgCameraInfoReadbackEXT >() );
    static_assert( CheckMembers< RgOriginalTextureDetailsEXT >() );
    static_assert( CheckMembers< RgSpawnFluidInfo >() );
    static_assert( CheckMembers< RgStartFrameFluidParams >() );
#if RG_USE_REMIX
    static_assert( CheckMembers< RgStartFrameRemixParams >() );
#endif


    template< typename T >
    struct LinkRootHelper
    {
        using Root = T;
    };

    // clang-format off
    template<> struct LinkRootHelper< RgMeshPrimitivePortalEXT           >{ using Root = RgMeshPrimitiveInfo; };
    template<> struct LinkRootHelper< RgMeshPrimitiveTextureLayersEXT    >{ using Root = RgMeshPrimitiveInfo; };
    template<> struct LinkRootHelper< RgMeshPrimitivePBREXT              >{ using Root = RgMeshPrimitiveInfo; };
    template<> struct LinkRootHelper< RgMeshPrimitiveAttachedLightEXT    >{ using Root = RgMeshPrimitiveInfo; };
    template<> struct LinkRootHelper< RgMeshPrimitiveSwapchainedEXT      >{ using Root = RgMeshPrimitiveInfo; };
    template<> struct LinkRootHelper< RgOriginalTextureDetailsEXT        >{ using Root = RgOriginalTextureInfo; };
    template<> struct LinkRootHelper< RgLightAdditionalEXT               >{ using Root = RgLightInfo; };
    template<> struct LinkRootHelper< RgLightDirectionalEXT              >{ using Root = RgLightInfo; };
    template<> struct LinkRootHelper< RgLightSphericalEXT                >{ using Root = RgLightInfo; };
    template<> struct LinkRootHelper< RgLightPolygonalEXT                >{ using Root = RgLightInfo; };
    template<> struct LinkRootHelper< RgLightSpotEXT                     >{ using Root = RgLightInfo; };
    template<> struct LinkRootHelper< RgCameraInfoReadbackEXT            >{ using Root = RgCameraInfo; };
    template<> struct LinkRootHelper< RgStartFrameRenderResolutionParams >{ using Root = RgStartFrameInfo; };
    template<> struct LinkRootHelper< RgStartFrameFluidParams            >{ using Root = RgStartFrameInfo; };
#if RG_USE_REMIX
    template<> struct LinkRootHelper< RgStartFrameRemixParams            >{ using Root = RgStartFrameInfo; };
#endif
    template<> struct LinkRootHelper< RgDrawFrameIlluminationParams      >{ using Root = RgDrawFrameInfo; };
    template<> struct LinkRootHelper< RgDrawFrameVolumetricParams        >{ using Root = RgDrawFrameInfo; };
    template<> struct LinkRootHelper< RgDrawFrameTonemappingParams       >{ using Root = RgDrawFrameInfo; };
    template<> struct LinkRootHelper< RgDrawFrameBloomParams             >{ using Root = RgDrawFrameInfo; };
    template<> struct LinkRootHelper< RgDrawFrameReflectRefractParams    >{ using Root = RgDrawFrameInfo; };
    template<> struct LinkRootHelper< RgDrawFrameSkyParams               >{ using Root = RgDrawFrameInfo; };
    template<> struct LinkRootHelper< RgDrawFrameTexturesParams          >{ using Root = RgDrawFrameInfo; };
    template<> struct LinkRootHelper< RgDrawFramePostEffectsParams       >{ using Root = RgDrawFrameInfo; };
    // clang-format on

    template< typename T >
    using LinkRoot = typename LinkRootHelper< T >::Root;

    template< typename T >
    using ClearType = std::remove_pointer_t< std::remove_reference_t< std::remove_cv_t< T > > >;

    template< typename T, typename Base >
    constexpr bool AreLinkable = std::is_same_v< LinkRoot< ClearType< T > >, ClearType< Base > >;
}


namespace pnext
{
    template< typename T, typename I >
        requires( detail::TypeToStructureType< T > != RG_STRUCTURE_TYPE_NONE )
    const T* cast( const I* pInfo ) noexcept
    {
        if( pInfo )
        {
            if( detail::GetStructureType( pInfo ) == detail::TypeToStructureType< T > )
            {
                return static_cast< const T* >( pInfo );
            }
        }
        return nullptr;
    }

    template< typename T, typename SourceType >
        requires( detail::TypeToStructureType< T > != RG_STRUCTURE_TYPE_NONE &&
                  detail::AreLinkable< T, SourceType > )
    auto find( SourceType* listStart ) noexcept
    {
        using ReturnType = std::conditional_t< std::is_const_v< SourceType >, const T*, T* >;
        using PNextType  = std::conditional_t< std::is_const_v< SourceType >, const void*, void* >;

        auto next = static_cast< PNextType >( listStart );

        while( next )
        {
            RgStructureType sType = detail::GetStructureType( next );

            if( sType == detail::TypeToStructureType< T > )
            {
                return static_cast< ReturnType >( next );
            }

            if( sType == RG_STRUCTURE_TYPE_NONE )
            {
                debug::Error( "Found sType=RG_STRUCTURE_TYPE_NONE on {:#x}", uint64_t( next ) );
            }

            next = detail::GetPNext( next );
        }

        return static_cast< ReturnType >( nullptr );
    }
}

namespace detail
{
    template< typename T >
    struct DefaultParams
    {
    };

    template<>
    struct DefaultParams< RgStartFrameRenderResolutionParams >
    {
        constexpr static auto sType =
            detail::TypeToStructureType< RgStartFrameRenderResolutionParams >;

        constexpr static RgStartFrameRenderResolutionParams value = {
            .sType                     = sType,
            .pNext                     = nullptr,
            .upscaleTechnique          = RG_RENDER_UPSCALE_TECHNIQUE_AMD_FSR2,
            .resolutionMode            = RG_RENDER_RESOLUTION_MODE_QUALITY,
            .frameGeneration           = RG_FRAME_GENERATION_MODE_OFF,
            .preferDxgiPresent         = false,
            .sharpenTechnique          = RG_RENDER_SHARPEN_TECHNIQUE_NONE,
            .customRenderSize          = {},
            .pixelizedRenderSizeEnable = false,
            .pixelizedRenderSize       = {},
        };
    };

    template<>
    struct DefaultParams< RgStartFrameFluidParams >
    {
        constexpr static auto sType = detail::TypeToStructureType< RgStartFrameFluidParams >;

        constexpr static RgStartFrameFluidParams value = {
            .sType          = sType,
            .pNext          = nullptr,
            .enabled        = true,
            .reset          = false,
            .gravity        = { 0, -9.8f, 0 },
            .color          = { 1, 1, 1 },
            .particleBudget = 64 * 1024,
            .particleRadius = 0.1f,
        };
    };

    template<>
    struct DefaultParams< RgDrawFrameIlluminationParams >
    {
        constexpr static auto sType = detail::TypeToStructureType< RgDrawFrameIlluminationParams >;

        constexpr static RgDrawFrameIlluminationParams value = {
            .sType                                       = sType,
            .pNext                                       = nullptr,
            .maxBounceShadows                            = 2,
            .enableSecondBounceForIndirect               = true,
            .cellWorldSize                               = 1.0f,
            .directDiffuseSensitivityToChange            = 0.5f,
            .indirectDiffuseSensitivityToChange          = 0.2f,
            .specularSensitivityToChange                 = 0.5f,
            .polygonalLightSpotlightFactor               = 2.0f,
            .lightUniqueIdIgnoreFirstPersonViewerShadows = nullptr,
        };
    };

    template<>
    struct DefaultParams< RgDrawFrameVolumetricParams >
    {
        constexpr static auto sType = detail::TypeToStructureType< RgDrawFrameVolumetricParams >;

        constexpr static RgDrawFrameVolumetricParams value = {
            .sType                   = sType,
            .pNext                   = nullptr,
            .enable                  = true,
            .useSimpleDepthBased     = false,
            .volumetricFar           = std::numeric_limits< float >::max(),
            .ambientColor            = { 0.8f, 0.85f, 1.0f },
            .scaterring              = 0.2f,
            .assymetry               = 0.75f,
            .useIlluminationVolume   = false,
            .fallbackSourceColor     = { 0, 0, 0 },
            .fallbackSourceDirection = { 0, -1, 0 },
            .lightMultiplier         = 1.0f,
        };
    };

    template<>
    struct DefaultParams< RgDrawFrameTonemappingParams >
    {
        constexpr static auto sType = detail::TypeToStructureType< RgDrawFrameTonemappingParams >;

        constexpr static RgDrawFrameTonemappingParams value = {
            .sType                = sType,
            .pNext                = nullptr,
            .disableEyeAdaptation = false,
            .ev100Min             = 0.0f,
            .ev100Max             = 8.0f,
            .luminanceWhitePoint  = 10.0f,
            .saturation           = { 0, 0, 0 },
            .crosstalk            = { 1, 1, 1 },
            .contrast             = 0.1f,
            .hdrBrightness        = 1.0f,
            .hdrContrast          = 0.1f,
            .hdrSaturation        = { 0.25f, 0.25f, 0.25f },
        };
    };

    template<>
    struct DefaultParams< RgDrawFrameBloomParams >
    {
        constexpr static auto sType = detail::TypeToStructureType< RgDrawFrameBloomParams >;

        constexpr static RgDrawFrameBloomParams value = {
            .sType             = sType,
            .pNext             = nullptr,
            .inputEV           = 6.0f,
            .inputThreshold    = 16.0f,
            .bloomIntensity    = 1.0f,
            .lensDirtIntensity = 1.0f,
        };
    };

    template<>
    struct DefaultParams< RgDrawFrameReflectRefractParams >
    {
        constexpr static auto sType =
            detail::TypeToStructureType< RgDrawFrameReflectRefractParams >;

        constexpr static RgDrawFrameReflectRefractParams value = {
            .sType                                 = sType,
            .pNext                                 = nullptr,
            .maxReflectRefractDepth                = 2,
            .typeOfMediaAroundCamera               = RgMediaType::RG_MEDIA_TYPE_VACUUM,
            .indexOfRefractionGlass                = 1.52f,
            .indexOfRefractionWater                = 1.33f,
            .thinMediaWidth                        = 0.1f,
            .waterWaveSpeed                        = 1.0f,
            .waterWaveNormalStrength               = 1.0f,
            .waterColor                            = { 0.3f, 0.73f, 0.63f },
            .acidColor                             = { 0.0f, 0.66f, 0.55f },
            .acidDensity                           = 10.0f,
            .waterWaveTextureDerivativesMultiplier = 1.0f,
            .waterTextureAreaScale                 = 1.0f,
            .portalNormalTwirl                     = false,
        };
    };

    template<>
    struct DefaultParams< RgDrawFrameSkyParams >
    {
        constexpr static auto sType = detail::TypeToStructureType< RgDrawFrameSkyParams >;

        constexpr static RgDrawFrameSkyParams value = {
            .sType                       = sType,
            .pNext                       = nullptr,
            .skyType                     = RgSkyType::RG_SKY_TYPE_COLOR,
            .skyColorDefault             = { 199 / 255.0f, 233 / 255.0f, 255 / 255.0f },
            .skyColorMultiplier          = 1000.0f,
            .skyColorSaturation          = 1.0f,
            .skyViewerPosition           = {},
            .pSkyCubemapTextureName      = nullptr,
            .skyCubemapRotationTransform = {},
        };
    };

    template<>
    struct DefaultParams< RgDrawFrameTexturesParams >
    {
        constexpr static auto sType = detail::TypeToStructureType< RgDrawFrameTexturesParams >;

        constexpr static RgDrawFrameTexturesParams value = {
            .sType                  = sType,
            .pNext                  = nullptr,
            .dynamicSamplerFilter   = RG_SAMPLER_FILTER_LINEAR,
            .normalMapStrength      = 1.0f,
            .emissionMapBoost       = 100.0f,
            .emissionMaxScreenColor = 1.5f,
            .minRoughness           = 0.0f,
            .heightMapDepth         = 0.02f,
        };
    };

    template<>
    struct DefaultParams< RgDrawFramePostEffectsParams >
    {
        constexpr static auto sType = detail::TypeToStructureType< RgDrawFramePostEffectsParams >;

        constexpr static RgDrawFramePostEffectsParams value = {
            .sType                 = sType,
            .pNext                 = nullptr,
            .pWipe                 = nullptr,
            .pRadialBlur           = nullptr,
            .pChromaticAberration  = nullptr,
            .pInverseBlackAndWhite = nullptr,
            .pHueShift             = nullptr,
            .pDistortedSides       = nullptr,
            .pWaves                = nullptr,
            .pColorTint            = nullptr,
            .pTeleport             = nullptr,
            .pCRT                  = nullptr,
            .pVHS                  = nullptr,
            .pDither               = nullptr,
        };
    };

#if RG_USE_REMIX
    template<>
    struct DefaultParams< RgStartFrameRemixParams >
    {
        constexpr static auto sType = detail::TypeToStructureType< RgStartFrameRemixParams >;

        constexpr static RgStartFrameRemixParams value = {
            .sType             = sType,
            .pNext             = nullptr,
            .rayReconstruction = false,
            .taa               = true,
            .nis               = false,
            .reflex            = true,
        };
        static_assert( sizeof( RgStartFrameRemixParams ) == 32, "Change defaults here" );
    };
#endif

    template< typename T >
    concept HasDefaultParams = requires( DefaultParams< T > t ) { t.value; };
}


namespace pnext
{
    template< detail::HasDefaultParams T, typename SourceType >
        requires( detail::AreLinkable< T, SourceType > )
    const T& get( const SourceType& listStart ) noexcept
    {
        if( auto p = pnext::find< T >( &listStart ) )
        {
            return *p;
        }
        // default if not found
        return detail::DefaultParams< T >::value;
    }
}

}
