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

#include "JsonParser.h"

#include "DebugPrint.h"

#include <glaze/glaze.hpp>
#include <glaze/api/impl.hpp>
#include <glaze/api/std/deque.hpp>
#include <glaze/api/std/unordered_set.hpp>
#include <glaze/api/std/span.hpp>
#include <glaze/file/file_ops.hpp>

#include <filesystem>
#include <fstream>
#include <optional>


constexpr auto JSON_OPTS = glz::opts{
    .comments              = true,
    .error_on_unknown_keys = false,
    .prettify              = true,
    .indentation_width     = 4,
};


// clang-format off
#define JSON_TYPE( Type )                           \
template<>                                          \
struct glz::meta< Type >                            \
{                                                   \
    using T = Type;                                 \
    static constexpr std::string_view name = #Type; \
    static constexpr auto value = glz::object(   

#define JSON_TYPE_END                         ); }
// clang-format on



struct Version
{
    int version = -1;
};
// clang-format off
JSON_TYPE( Version )

    "version", &T::version

JSON_TYPE_END;
// clang-format on



namespace
{

template< typename T >
    requires( T::Version,
              std::is_same_v< decltype( T::Version ), const int >,
              T::RequiredVersion,
              std::is_same_v< decltype( T::RequiredVersion ), const int > )
std::optional< T > LoadFileAs( const std::filesystem::path& path )
{
    if( !std::filesystem::exists( path ) )
    {
        return std::nullopt;
    }

    auto buffer = std::string{};
    {
        std::ifstream file( path );

        if( file.is_open() )
        {
            std::stringstream strm{};
            strm << file.rdbuf();

            buffer = strm.str();
        }
        else
        {
            return std::nullopt;
        }
    }

    using namespace RTGL1;

    int version = -1;
    {
        Version value{};

        auto err = glz::read< JSON_OPTS >( value, buffer );
        if( err )
        {
            auto msg = glz::format_error( err, buffer );
            debug::Warning( "Json read fail on {}:\n{}", path.string(), msg );
            return std::nullopt;
        }

        version = value.version;
    }

    if( version < 0 )
    {
        debug::Warning( "Json read fail on {}: Invalid version, or \"version\" field is not set",
                        path.string() );
        return std::nullopt;
    }

    if( version < T::RequiredVersion )
    {
        debug::Warning( "Json data is too old {}: Minimum version is {}, but got {}",
                        path.string(),
                        T::RequiredVersion,
                        version );
        return std::nullopt;
    }

    T value{};
    {
        auto err = glz::read< JSON_OPTS >( value, buffer );
        if( err )
        {
            auto msg = glz::format_error( err, buffer );
            debug::Warning( "Json read fail on {}:\n{}", path.string(), msg );
            return std::nullopt;
        }
    }
    return value;
}

}



// clang-format off
JSON_TYPE( RTGL1::TextureMeta )
      "textureName", &T::textureName
    , "forceIgnore", &T::forceIgnore
    , "forceIgnoreIfRasterized", &T::forceIgnoreIfRasterized
    , "forceAlphaTest", &T::forceAlphaTest
    , "forceTranslucent", &T::forceTranslucent
    , "forceOpaque", &T::forceOpaque
    , "forceGenerateNormals", &T::forceGenerateNormals
    , "forceExactNormals", &T::forceExactNormals
    , "isMirror", &T::isMirror
    , "isWater", &T::isWater
    , "isWaterIfTranslucent", &T::isWaterIfTranslucent
    , "isGlass", &T::isGlass
    , "isGlassIfTranslucent", &T::isGlassIfTranslucent
    , "isAcid", &T::isAcid
    , "isGlassIfSmooth", &T::isGlassIfSmooth
    , "isMirrorIfSmooth", &T::isMirrorIfSmooth
    , "isThinMedia", &T::isThinMedia
    , "metallicDefault", &T::metallicDefault
    , "roughnessDefault", &T::roughnessDefault
    , "emissiveMult", &T::emissiveMult
    , "lightIntensity", &T::attachedLightIntensity
    , "lightColor", &T::attachedLightColor
    , "lightColorHEX", &T::attachedLightColorHEX
    , "lightEvenOnDynamic", &T::attachedLightEvenOnDynamic
    , "noShadow", &T::noShadow
JSON_TYPE_END;
JSON_TYPE( RTGL1::TextureMetaArray )
    "array", &T::array
JSON_TYPE_END;
// clang-format on

auto RTGL1::json_parser::detail::ReadTextureMetaArray( const std::filesystem::path& path )
    -> std::optional< TextureMetaArray >
{
    return LoadFileAs< TextureMetaArray >( path );
}



// clang-format off
JSON_TYPE( RTGL1::SceneMeta )
      "sceneName", &T::sceneName
    , "sky", &T::sky
    , "forceSkyPlainColor", &T::forceSkyPlainColor
    , "scatter", &T::scatter
    , "volumeFar", &T::volumeFar
    , "volumeAssymetry", &T::volumeAssymetry
    , "volumeLightMultiplier", &T::volumeLightMultiplier
    , "volumeAmbient", &T::volumeAmbient
    , "volumeUnderwaterColor", &T::volumeUnderwaterColor
    , "ignoredReplacements", &T::ignoredReplacements
JSON_TYPE_END;
JSON_TYPE( RTGL1::SceneMetaArray )
    "array", &T::array
JSON_TYPE_END;
// clang-format on

auto RTGL1::json_parser::detail::ReadSceneMetaArray( const std::filesystem::path& path )
    -> std::optional< SceneMetaArray >
{
    return LoadFileAs< SceneMetaArray >( path );
}



// clang-format off
JSON_TYPE( RTGL1::LibraryConfig )
      "developerMode", &T::developerMode
    , "vulkanValidation", &T::vulkanValidation
    , "dlssValidation", &T::dlssValidation
    , "dlssForceDefaultPreset", &T::dlssForceDefaultPreset
    , "fpsMonitor", &T::fpsMonitor
    , "fsr3async", &T::fsr3async
    , "dxgiToVkSwapchainSwitchHack", &T::dxgiToVkSwapchainSwitchHack
    , "dx12Validation", &T::dx12Validation
    , "fsrValidation", &T::fsrValidation
JSON_TYPE_END;
// clang-format on
static_assert( sizeof( RTGL1::LibraryConfig ) == 9, "Add definitions to parser" );

auto RTGL1::json_parser::detail::ReadLibraryConfig( const std::filesystem::path& path )
    -> std::optional< LibraryConfig >
{
    return LoadFileAs< LibraryConfig >( path );
}


static constexpr auto set_volumetric = []( RgLightAdditionalEXT& s, const int& input ) -> void {
    if( input != 0 )
    {
        s.flags |= RG_LIGHT_ADDITIONAL_VOLUMETRIC;
    }
    else
    {
        s.flags &= ~RG_LIGHT_ADDITIONAL_VOLUMETRIC;
    }
};
static constexpr auto get_volumetric = []( const RgLightAdditionalEXT& s ) -> int {
    return ( s.flags & RG_LIGHT_ADDITIONAL_VOLUMETRIC ) ? 1 : 0;
};

static constexpr auto set_parentIntensity = []( RgLightAdditionalEXT& s,
                                                const int&            input ) -> void {
    if( input != 0 )
    {
        s.flags |= RG_LIGHT_ADDITIONAL_APPLY_PARENT_MESH_INTENSITY;
    }
    else
    {
        s.flags &= ~RG_LIGHT_ADDITIONAL_APPLY_PARENT_MESH_INTENSITY;
    }
};
static constexpr auto get_parentIntensity = []( const RgLightAdditionalEXT& s ) -> int {
    return ( s.flags & RG_LIGHT_ADDITIONAL_APPLY_PARENT_MESH_INTENSITY ) ? 1 : 0;
};

static constexpr auto set_lightstyle = []( RgLightAdditionalEXT& s, const int& input ) -> void {
    if( input >= 0 )
    {
        s.flags |= RG_LIGHT_ADDITIONAL_LIGHTSTYLE;
        s.lightstyle = input;
    }
    else
    {
        s.flags &= ~RG_LIGHT_ADDITIONAL_LIGHTSTYLE;
        s.lightstyle = -1;
    }
};
static constexpr auto get_lightstyle = []( const RgLightAdditionalEXT& s ) -> int {
    return ( s.flags & RG_LIGHT_ADDITIONAL_LIGHTSTYLE ) ? s.lightstyle : -1;
};

// clang-format off
JSON_TYPE( RgLightAdditionalEXT )
      "lightstyle",         glz::custom< set_lightstyle, get_lightstyle >
    , "isVolumetric",       glz::custom< set_volumetric, get_volumetric >
    , "parentIntensity",    glz::custom< set_parentIntensity, get_parentIntensity>
    , "hashName",           &T::hashName
JSON_TYPE_END;
// clang-format on

auto RTGL1::json_parser::detail::ReadLightExtraInfo( const std::string_view& data )
    -> std::optional< RgLightAdditionalEXT >
{
    if( !data.empty() )
    {
        // default
        auto value = RgLightAdditionalEXT{
            .sType      = RG_STRUCTURE_TYPE_LIGHT_ADDITIONAL_EXT,
            .pNext      = nullptr,
            .flags      = 0,
            .lightstyle = -1,
            .hashName   = "",
        };
        static_assert( sizeof( RgLightAdditionalEXT ) == 64, "Change defaults here" );

        auto err = glz::read< JSON_OPTS >( value, data );
        if( err )
        {
            auto msg = glz::format_error( err, data );
            if( err == glz::error_code::unexpected_end )
            {
                debug::Error( "Json read fail on RgLightExtraInfo:\n"
                              "{}{}" //
                              "NOTE: \'hashName\' field must be at most {} characters!",
                              msg,
                              msg.back() == '\n' ? "" : "\n",
                              int( std::size( value.hashName ) - 1 ) );
            }
            else
            {
                debug::Error( "Json read fail on RgLightExtraInfo:\n{}", msg );
            }
        }

        // extra safety
        value.hashName[ std::size( value.hashName ) - 1 ] = '\0';

        return value;
    }
    return std::nullopt;
}



// clang-format off
JSON_TYPE( RTGL1::PrimitiveExtraInfo )
      "isGlass", &T::isGlass
    , "isMirror", &T::isMirror
    , "isWater", &T::isWater
    , "isSkyVisibility", &T::isSkyVisibility
    , "isAcid", &T::isAcid
    , "isThinMedia", &T::isThinMedia
    , "noShadow", &T::noShadow
JSON_TYPE_END;
// clang-format on

auto RTGL1::json_parser::detail::ReadPrimitiveExtraInfo( const std::string_view& data )
    -> PrimitiveExtraInfo
{
    if( !data.empty() )
    {
        auto value = PrimitiveExtraInfo{};

        auto err = glz::read< JSON_OPTS >( value, data );
        if( err )
        {
            auto msg = glz::format_error( err, data );
            debug::Warning( "Json read fail on gltf's PrimitiveExtraInfo:\n{}", msg );
        }

        return value;
    }
    return PrimitiveExtraInfo{};
}

std::string RTGL1::json_parser::MakeJsonString( const RgLightAdditionalEXT& info )
{
    assert( strnlen( info.hashName, std::size( info.hashName ) ) <=
            std::size( info.hashName ) - 1 );

    std::string str;
    glz::write< JSON_OPTS >( info, str );
    return str;
}

std::string RTGL1::json_parser::MakeJsonString( const PrimitiveExtraInfo& info )
{
    std::string str;
    glz::write< JSON_OPTS >( info, str );
    return str;
}



template< typename T >
static constexpr auto convert_sv_to( std::string_view sv ) -> std::optional< T >
{
    // trim whitespaces
    while( !sv.empty() && std::isspace( sv.front() ) )
    {
        sv.remove_prefix( 1 );
    }
    while( !sv.empty() && std::isspace( sv.back() ) )
    {
        sv.remove_suffix( 1 );
    }

    T value{};

    std::from_chars_result r = std::from_chars( sv.data(), sv.data() + sv.size(), value );
    if( r.ec != std::errc{} )
    {
        return std::nullopt;
    }
    return value;
}

static constexpr auto set_anim_fov_24fps = []( RTGL1::CameraExtraInfo& s,
                                               const std::string_view& input ) -> void {
    assert( s.anim_fov_24fps.empty() );
    s.anim_fov_24fps.clear();

    // input example: "0:27 / 638:27 / 732:19.5"

    for( auto&& part : std::views::split( input, '/' ) )
    {
        auto part_str = std::string_view{ part.begin(), part.end() };
        if( part_str.empty() )
        {
            continue;
        }

        auto fovFrame = RTGL1::CameraExtraInfo::FovAnimFrame{
            .frame24    = -1,
            .fovDegrees = -1,
        };

        // part_str example: "732:19.5"

        uint32_t cnt = 0;
        for( auto&& ff : std::views::split( part_str, ':' ) )
        {
            auto ff_str = std::string_view{ ff.begin(), ff.end() };

            if( cnt == 0 )
            {
                if( auto v = convert_sv_to< int >( ff_str ) )
                {
                    fovFrame.frame24 = *v;
                    static_assert( std::is_same_v< int, decltype( fovFrame.frame24 ) > );
                }
            }
            else if( cnt == 1 )
            {
                if( auto v = convert_sv_to< float >( ff_str ) )
                {
                    fovFrame.fovDegrees = *v;
                    static_assert( std::is_same_v< float, decltype( fovFrame.fovDegrees ) > );
                }
            }
            else
            {
                break;
            }
            cnt++;
        }

        if( cnt != 2 )
        {
            RTGL1::debug::Warning(
                "Failed to read Frame-FOV (expected \'<frame integer>:<fov float>\') pair in {}",
                input );
            continue;
        }
        if( fovFrame.frame24 < 0 || fovFrame.fovDegrees <= 0 )
        {
            RTGL1::debug::Warning( "Incorrect Frame-FOV values in {}", input );
            continue;
        }

        s.anim_fov_24fps.push_back( fovFrame );
    }
};
static constexpr auto get_anim_fov_24fps = []( const RTGL1::CameraExtraInfo& s ) -> std::string {
    // output example: "0:27 / 638:27 / 732:19.5"

    std::string str{};
    for( const RTGL1::CameraExtraInfo::FovAnimFrame& f : s.anim_fov_24fps )
    {
        if( !str.empty() )
        {
            str += " / ";
        }
        str += std::to_string( f.frame24 ) + ':' + std::to_string( f.fovDegrees );
    }
    return str;
};

// clang-format off
JSON_TYPE( RTGL1::CameraExtraInfo )
      "version",               &T::version
    , "anim_cuts_24fps",       &T::anim_cuts_24fps
    , "anim_fov_24fps",        glz::custom< set_anim_fov_24fps, get_anim_fov_24fps >
JSON_TYPE_END;
// clang-format on

auto RTGL1::json_parser::detail::ReadCameraExtraInfo( const std::string_view& data )
          -> CameraExtraInfo
{
    if( !data.empty() )
    {
        auto value = CameraExtraInfo{};

        auto err = glz::read< JSON_OPTS >( value, data );
        if( err )
        {
            auto msg = glz::format_error( err, data );
            debug::Warning( "Json read fail on gltf's CameraExtraInfo:\n{}", msg );
        }

        return value;
    }
    return CameraExtraInfo{};
}

#if RG_USE_REMIX

// clang-format off
JSON_TYPE( RTGL1::RemixWrapperConfig )
    "noshadow_opacity",   &T::noshadow_opacity
,   "noshadow_emismult",  &T::noshadow_emismult
,   "lightmult_sun",      &T::lightmult_sun
,   "lightmult_sphere",   &T::lightmult_sphere
,   "lightmult_spot",     &T::lightmult_spot
,   "metallic_bias",      &T::metallic_bias
,   "spritelight_mult",   &T::spritelight_mult
,   "spritelight_radius", &T::spritelight_radius
,   "texpostfix_albedo",  &T::texpostfix_albedo
,   "texpostfix_rough",   &T::texpostfix_rough
,   "texpostfix_normal",  &T::texpostfix_normal
,   "texpostfix_emis",    &T::texpostfix_emis
,   "texpostfix_height",  &T::texpostfix_height
,   "texpostfix_metal",   &T::texpostfix_metal
,   "skymult",            &T::skymult
,   "emismult",           &T::emismult
JSON_TYPE_END;;
static_assert(sizeof( RTGL1::RemixWrapperConfig) == 232, "Add json entries here");
// clang-format on

auto RTGL1::json_parser::detail::ReadRemixWrapperConfig( const std::filesystem::path& path )
    -> RemixWrapperConfig
{
    return LoadFileAs< RemixWrapperConfig >( path ).value_or( RemixWrapperConfig{} );
}
#endif
