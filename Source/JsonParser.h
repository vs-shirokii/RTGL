// Copyright (c) 2020-2021 Sultim Tsyrendashiev
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

#include "LibraryConfig.h"

#include <RTGL1/RTGL1.h>

#include <array>
#include <filesystem>
#include <string>
#include <optional>
#include <set>
#include <vector>

namespace RTGL1
{

struct TextureMeta
{
    constexpr static int Version{ 0 };
    constexpr static int RequiredVersion{ 0 };

    std::string textureName = {};

    bool forceIgnore             = false;
    bool forceIgnoreIfRasterized = false;

    bool forceAlphaTest   = false;
    bool forceTranslucent = false;
    bool forceOpaque      = false;

    bool forceGenerateNormals = false;
    bool forceExactNormals    = false;

    bool isMirror             = false;
    bool isWater              = false;
    bool isWaterIfTranslucent = false;
    bool isGlass              = false;
    bool isGlassIfTranslucent = false;
    bool isAcid               = false;

    bool isGlassIfSmooth  = false;
    bool isMirrorIfSmooth = false;

    bool isThinMedia = false;

    float metallicDefault  = 0.0f;
    float roughnessDefault = 1.0f;
    float emissiveMult     = 0.0f;

    float                    attachedLightIntensity     = 0.0f;
    std::array< uint8_t, 3 > attachedLightColor         = { { 255, 255, 255 } };
    char                     attachedLightColorHEX[ 7 ] = "FFFFFF";
    bool                     attachedLightEvenOnDynamic = false;

    bool noShadow = false;
};

struct TextureMetaArray
{
    constexpr static int Version{ 0 };
    constexpr static int RequiredVersion{ 0 };

    std::vector< TextureMeta > array;
};



struct SceneMeta
{
    constexpr static int Version{ 0 };
    constexpr static int RequiredVersion{ 0 };

    std::string sceneName = {};

    std::optional< float > sky;
    std::optional< std::array< float, 3 > > forceSkyPlainColor;

    std::optional< float > scatter;
    std::optional< float > volumeFar;
    std::optional< float > volumeAssymetry;
    std::optional< float > volumeLightMultiplier;

    std::optional< std::array< float, 3 > > volumeAmbient;
    std::optional< std::array< float, 3 > > volumeUnderwaterColor;

    std::set< std::string > ignoredReplacements;
};

struct SceneMetaArray
{
    constexpr static int Version{ 0 };
    constexpr static int RequiredVersion{ 0 };

    std::vector< SceneMeta > array;
};



struct PrimitiveExtraInfo
{
    int isGlass         = 0;
    int isMirror        = 0;
    int isWater         = 0;
    int isSkyVisibility = 0;
    int isAcid          = 0;
    int isThinMedia     = 0;
    int noShadow        = 0;
};



struct CameraExtraInfo
{
    struct FovAnimFrame
    {
        int   frame24{ 0 };
        float fovDegrees{ 0 };
    };

    static constexpr uint32_t LatestVersion = 0;

    uint32_t                    version{ LatestVersion };
    std::vector< int >          anim_cuts_24fps{};
    std::vector< FovAnimFrame > anim_fov_24fps{};
};


#if RG_USE_REMIX
struct RemixWrapperConfig
{
    constexpr static int Version{ 0 };
    constexpr static int RequiredVersion{ 0 };

    float noshadow_opacity   = 0.9f;
    float noshadow_emismult  = 1.0f;
    float lightmult_sun      = 0.4f;
    float lightmult_sphere   = 0.05f;
    float lightmult_spot     = 0.05f;
    float spritelight_mult   = 0.6f;
    float spritelight_radius = 0.05f;
    float metallic_bias      = 0.0f;

    std::string texpostfix_albedo = "";
    std::string texpostfix_rough  = "_remix_roughness";
    std::string texpostfix_normal = "_remix_normal";
    std::string texpostfix_emis   = "_e";
    std::string texpostfix_height = "_h";
    std::string texpostfix_metal  = "_remix_metallic";

    float skymult  = 1.0f;
    float emismult = 1.0f;

    bool check_framegen_support_in_log = true;
};
#endif


namespace json_parser
{
    namespace detail
    {
        auto ReadTextureMetaArray( const std::filesystem::path& path )
            -> std::optional< TextureMetaArray >;

        auto ReadSceneMetaArray( const std::filesystem::path& path )
            -> std::optional< SceneMetaArray >;

        auto ReadLibraryConfig( const std::filesystem::path& path )
            -> std::optional< LibraryConfig >;

        auto ReadLightExtraInfo( const std::string_view& data )
            -> std::optional< RgLightAdditionalEXT >;

        auto ReadPrimitiveExtraInfo( const std::string_view& data ) -> PrimitiveExtraInfo;

        auto ReadCameraExtraInfo( const std::string_view& data ) -> CameraExtraInfo;

#if RG_USE_REMIX
        auto ReadRemixWrapperConfig( const std::filesystem::path& path ) -> RemixWrapperConfig;
#endif
    }

    // clang-format off
    template< typename T > auto ReadFileAs( const std::filesystem::path& path ) = delete;
    template<> inline auto ReadFileAs< TextureMetaArray >( const std::filesystem::path& path ) { return detail::ReadTextureMetaArray( path ); }
    template<> inline auto ReadFileAs< SceneMetaArray   >( const std::filesystem::path& path ) { return detail::ReadSceneMetaArray( path ); }
    template<> inline auto ReadFileAs< LibraryConfig    >( const std::filesystem::path& path ) { return detail::ReadLibraryConfig( path ); }
#if RG_USE_REMIX
    template<> inline auto ReadFileAs< RemixWrapperConfig >( const std::filesystem::path& path ) { return detail::ReadRemixWrapperConfig( path ); }
#endif

    template< typename T > auto ReadStringAs( const std::string_view& str ) = delete;
    template<> inline auto ReadStringAs< RgLightAdditionalEXT >( const std::string_view& data ) { return detail::ReadLightExtraInfo( data ); }
    template<> inline auto ReadStringAs< PrimitiveExtraInfo   >( const std::string_view& data ) { return detail::ReadPrimitiveExtraInfo( data ); }
    template<> inline auto ReadStringAs< CameraExtraInfo   >( const std::string_view& data ) { return detail::ReadCameraExtraInfo( data ); }
    // clang-format on

    std::string MakeJsonString( const RgLightAdditionalEXT& info );
    std::string MakeJsonString( const PrimitiveExtraInfo& info );
}

}
