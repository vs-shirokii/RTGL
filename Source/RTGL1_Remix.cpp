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

#include "RTGL1/RTGL1.h"

#include "Const.h"
#include "Containers.h"
#include "DebugPrint.h"
#include "DrawFrameInfo.h"
#include "GltfImporter.h"
#include "Matrix.h"
#include "SceneMeta.h"
#include "ScratchImmediate.h"
#include "TextureMeta.h"
#include "TextureOverrides.h"
#include "UniqueID.h"
#include "Utils.h"

#include "defer.hpp"

#include <remix/remix_c.h>

#include <future>
#include <fstream>

#define RG_REMIXAPI_FILTER_NEAREST 0
#define RG_REMIXAPI_FILTER_LINEAR  1

#define RG_REMIXAPI_WRAP_CLAMP                 0
#define RG_REMIXAPI_WRAP_REPEAT                1
#define RG_REMIXAPI_WRAP_MIRRORED_REPEAT       2
#define RG_REMIXAPI_WRAP_CLAMP_TO_BORDER_BLACK 3

#define RG_TODO 0 // todo

using namespace RTGL1;

namespace RTGL1::debug::detail
{

DebugPrintFn           g_print{};
RgMessageSeverityFlags g_printSeverity{ 0 };
bool                   g_breakOnError{ true };

}

namespace
{

remixapi_Interface g_remix{};
HMODULE            g_dllremix{};


auto wrapconf = RemixWrapperConfig{};


auto g_texturemeta         = std::unique_ptr< TextureMetaManager >{};
auto g_scenemeta           = std::unique_ptr< SceneMetaManager >{};
auto g_importexport_params = ImportExportParams{};
auto g_imageLoaderKtx      = ImageLoader{};
auto g_imageLoaderRaw      = ImageLoaderDev{};
auto g_scratch             = ScratchImmediate{};


auto c_lightstoclear    = rgl::unordered_set< remixapi_LightHandle >{};
auto c_meshestoclear    = rgl::unordered_set< remixapi_MeshHandle >{};
auto c_materialstoclear = rgl::unordered_set< remixapi_MaterialHandle >{};


auto  g_ovrdfolder                 = std::filesystem::path{};
float g_indexOfRefractionGlass     = 1.52f;
float g_indexOfRefractionWater     = 1.33f;
auto  g_pbrTextureSwizzling        = RgTextureSwizzling{};
bool  g_forceNormalMapFilterLinear = true;
auto  g_skyviewerpos               = RgFloat3D{};
bool  g_framegen_supported         = true;


auto g_hwnd      = HWND{};
auto g_hwnd_size = RgExtent2D{};



constexpr float RG_PI             = 3.1415926535897932384626433f;
constexpr float MIN_SPHERE_RADIUS = 0.005f; // light

// clang-format off
auto   safecstr( const char* in ) -> const char* { return Utils::SafeCstr( in );    }
auto cstr_empty( const char* in ) -> bool        { return Utils::IsCstrEmpty( in ); }

template< typename T > auto saturate( const T& )        -> T     = delete;
template<>             auto saturate( const float& in ) -> float { return Utils::Saturate( in ); };
// clang-format on

void printerror( std ::string_view func, remixapi_ErrorCode r )
{
    debug::Error( "{} fail: {}", func, static_cast< uint32_t >( r ) );
}



// clang-format off
template< typename            T > bool option_issame( const T& a, const T& b ) = delete;
template< std::integral       T > bool option_issame( const T& a, const T& b ) { return a == b; }
template< std::floating_point T > bool option_issame( const T& a, const T& b ) { return std::abs( a - b ) < T( 0.0001 ); }
// clang-format on

template< size_t N >
struct stringliteral_t
{
    char m_cstr[ N ];
    constexpr stringliteral_t( const char ( &cstr )[ N ] ) { std::copy_n( cstr, N, m_cstr ); }
};

// TODO: string
template< stringliteral_t RemixName, typename T >
    requires( !std::is_same_v< T, bool > && //
              !std::is_pointer_v< T > &&    //
              !std::is_array_v< T > )
struct optionstate_t
{
    std::remove_const_t< std::remove_reference_t< T > > m_cachedvalue{};
    bool                                                m_firsttime{ true };

    template< typename U >
    void update( const U ) = delete;

    void update( const T value )
    {
        if( !m_firsttime && option_issame( value, m_cachedvalue ) )
        {
            return;
        }

        std::string val_str = std::format( "{}", value );

        remixapi_ErrorCode r = g_remix.SetConfigVariable( RemixName.m_cstr, val_str.c_str() );
        assert( r == REMIXAPI_ERROR_CODE_SUCCESS );

        m_cachedvalue = value;
        m_firsttime   = false;
    }
};

#define setoption_if( remixname, value )                                             \
    do                                                                               \
    {                                                                                \
        static auto s_optionstate = optionstate_t< remixname, decltype( value ) >{}; \
        s_optionstate.update( value );                                               \
    } while( 0 )



void rgInitData( const RgInstanceCreateInfo& info )
{
    g_ovrdfolder = safecstr( info.pOverrideFolderPath );

    g_texturemeta = std::make_unique< TextureMetaManager >( g_ovrdfolder / DATABASE_FOLDER );
    g_scenemeta =
        std::make_unique< SceneMetaManager >( g_ovrdfolder / DATABASE_FOLDER / "scenes.json" );


    {
        auto tr = Utils::MakeTransform( Utils::Normalize( info.worldUp ), //
                                        Utils::Normalize( info.worldForward ),
                                        info.worldScale );

        g_importexport_params = ImportExportParams{
            .worldTransform                         = tr,
            .oneGameUnitInMeters                    = info.worldScale,
            .importedLightIntensityScaleDirectional = info.importedLightIntensityScaleDirectional,
            .importedLightIntensityScaleSphere      = info.importedLightIntensityScaleSphere,
            .importedLightIntensityScaleSpot        = info.importedLightIntensityScaleSpot,
        };
    }

    g_pbrTextureSwizzling        = info.pbrTextureSwizzling;
    g_forceNormalMapFilterLinear = info.textureSamplerForceNormalMapFilterLinear;

    // so that the user menu would not overwrite dlss options...
    setoption_if( "rtx.defaultToAdvancedUI", 1 );
    // required for first-person weapons
    setoption_if( "rtx.viewModel.enable", 1 );
    // always set LPM Tonemapper
    setoption_if( "rtx.tonemappingMode", 0 /* Global */ );
    setoption_if( "rtx.tonemap.finalizeWithACES", 1 );
    setoption_if( "rtx.tonemap.lpm", 1 );

    setoption_if( "rtx.skyProbeSide", std::clamp( info.rasterizedSkyCubemapSize, 32u, 2048u ) );

    // no need
    setoption_if( "rtx.terrainBaker.enableBaking", 0 );

    // looks ugly with current noshadow handling...
    setoption_if( "rtx.enableStochasticAlphaBlend", 0 );
}

RgResult RGAPI_CALL rgDestroyInstance()
{
    defer
    {
        RTGL1::debug::detail::g_printSeverity = 0;
        RTGL1::debug::detail::g_print         = nullptr;
    };

    if( !g_remix.Shutdown )
    {
        return RG_RESULT_NOT_INITIALIZED;
    }

    remixapi_ErrorCode r = remixapi_lib_shutdownAndUnloadRemixDll( &g_remix, g_dllremix );
    if( r != REMIXAPI_ERROR_CODE_SUCCESS )
    {
        printerror( "remixapi_lib_shutdownAndUnloadRemixDll", r );
    }

    return RG_RESULT_SUCCESS;
}



template< typename T >
auto toremix( const T& v ) = delete;

template<>
auto toremix( const RgFloat3D& v )
{
    return remixapi_Float3D{ v.data[ 0 ], v.data[ 1 ], v.data[ 2 ] };
}
template<>
auto toremix( const RgTransform& src )
{
    remixapi_Transform dst;
    static_assert( sizeof( dst.matrix ) == sizeof( src.matrix ) );
    memcpy( dst.matrix, src.matrix, sizeof( RgTransform ) );
    return dst;
}

auto colorintensity_to_radiance( RgColor4DPacked32 color, float intensity ) -> RgFloat3D
{
    auto c = Utils::UnpackColor4DPacked32< RgFloat3D >( color );
    return RgFloat3D{
        c.data[ 0 ] * intensity,
        c.data[ 1 ] * intensity,
        c.data[ 2 ] * intensity,
    };
}

auto calc_hwnd_size( HWND hwnd ) -> RgExtent2D
{
    auto rect = RECT{};
    GetClientRect( hwnd, &rect );
    return RgExtent2D{
        .width  = static_cast< uint32_t >( rect.right - rect.left ),
        .height = static_cast< uint32_t >( rect.bottom - rect.top ),
    };
}

bool almost_identity( const RgTransform& tr, float eps = 0.00001f )
{
    assert( eps > 0 );
    auto cmp = [ eps ]( float a, float b ) {
        return std::abs( a - b ) < eps;
    };

    const auto& m = tr.matrix;
    // clang-format off
    return
        cmp( m[ 0 ][ 0 ], 1 ) && cmp( m[ 0 ][ 1 ], 0 ) && cmp( m[ 0 ][ 2 ], 0 ) && cmp( m[ 0 ][ 3 ], 0 ) && //
        cmp( m[ 1 ][ 0 ], 0 ) && cmp( m[ 1 ][ 1 ], 1 ) && cmp( m[ 1 ][ 2 ], 0 ) && cmp( m[ 1 ][ 3 ], 0 ) && //
        cmp( m[ 2 ][ 0 ], 0 ) && cmp( m[ 2 ][ 1 ], 0 ) && cmp( m[ 2 ][ 2 ], 1 ) && cmp( m[ 2 ][ 3 ], 0 );
    // clang-format on
}

uint32_t align_to_tri_lower( uint64_t count )
{
    count = count / 3;
    return static_cast< uint32_t >( count * 3 );
}


#pragma warning( push )
#pragma warning( error : 4061 ) // all switch cases must be handled explicitly

/*auto toremix_format( RgFormat src ) -> remixapi_Format
{
    switch( src )
    {
        case RG_FORMAT_R8_UNORM: return REMIXAPI_FORMAT_R8_UNORM;
        case RG_FORMAT_R8_SRGB: return REMIXAPI_FORMAT_R8_UNORM; // TODO REMIXAPI_FORMAT_R8_SRGB
        case RG_FORMAT_R8G8B8A8_UNORM: return REMIXAPI_FORMAT_R8G8B8A8_UNORM;
        case RG_FORMAT_R8G8B8A8_SRGB: return REMIXAPI_FORMAT_R8G8B8A8_SRGB;
        case RG_FORMAT_B8G8R8A8_UNORM: return REMIXAPI_FORMAT_B8G8R8A8_UNORM;
        case RG_FORMAT_B8G8R8A8_SRGB: return REMIXAPI_FORMAT_B8G8R8A8_SRGB;
        case RG_FORMAT_UNDEFINED:
        default: assert( 0 ); return REMIXAPI_FORMAT_UNDEFINED;
    }
}

auto calc_imagedatasize( const RgFormat* src, const RgExtent2D& ext ) -> size_t
{
    RgFormat format = src ? *src : RG_FORMAT_R8G8B8A8_UNORM;

    switch( format )
    {
        case RG_FORMAT_R8_UNORM:
        case RG_FORMAT_R8_SRGB: return 1ull * ext.width * ext.height;
        case RG_FORMAT_R8G8B8A8_UNORM:
        case RG_FORMAT_R8G8B8A8_SRGB:
        case RG_FORMAT_B8G8R8A8_UNORM:
        case RG_FORMAT_B8G8R8A8_SRGB: return 4ull * ext.width * ext.height;
        case RG_FORMAT_UNDEFINED:
        default: assert( 0 ); return 0;
    }
}*/

#define SRGB_HACKHACK 1

VkFormat rgformat_to_vkformat( RgFormat f )
{
    switch( f )
    {
        case RG_FORMAT_UNDEFINED: assert( 0 ); return VK_FORMAT_UNDEFINED;
        case RG_FORMAT_R8_UNORM: return VK_FORMAT_R8_UNORM;
        case RG_FORMAT_R8_SRGB:
            return SRGB_HACKHACK ? VK_FORMAT_R8_UNORM //
                                 : VK_FORMAT_R8_SRGB;
        case RG_FORMAT_R8G8B8A8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
        case RG_FORMAT_R8G8B8A8_SRGB:
            return SRGB_HACKHACK ? VK_FORMAT_R8G8B8A8_UNORM //
                                 : VK_FORMAT_R8G8B8A8_SRGB;
        case RG_FORMAT_B8G8R8A8_UNORM: return VK_FORMAT_B8G8R8A8_UNORM;
        case RG_FORMAT_B8G8R8A8_SRGB:
            return SRGB_HACKHACK ? VK_FORMAT_B8G8R8A8_UNORM //
                                 : VK_FORMAT_B8G8R8A8_SRGB;
        default:
            assert( 0 );
            return SRGB_HACKHACK ? VK_FORMAT_R8G8B8A8_UNORM //
                                 : VK_FORMAT_R8G8B8A8_SRGB;
    }
}

#pragma warning( pop )

VkFormat rgtexture_to_vkformat( const RgOriginalTextureDetailsEXT* details, VkFormat fallback )
{
    if( details )
    {
        return rgformat_to_vkformat( details->format );
    }
}

auto toremix_format_fromvk( VkFormat src ) -> remixapi_Format
{
    switch( src )
    {
        case VK_FORMAT_R8_UINT: return REMIXAPI_FORMAT_R8_UINT;
        case VK_FORMAT_R8_SINT: return REMIXAPI_FORMAT_R8_SINT;
        case VK_FORMAT_R8_UNORM: return REMIXAPI_FORMAT_R8_UNORM;
        case VK_FORMAT_R8_SNORM: return REMIXAPI_FORMAT_R8_SNORM;
        case VK_FORMAT_R8G8_UINT: return REMIXAPI_FORMAT_R8G8_UINT;
        case VK_FORMAT_R8G8_SINT: return REMIXAPI_FORMAT_R8G8_SINT;
        case VK_FORMAT_R8G8_UNORM: return REMIXAPI_FORMAT_R8G8_UNORM;
        case VK_FORMAT_R8G8_SNORM: return REMIXAPI_FORMAT_R8G8_SNORM;
        case VK_FORMAT_R16_UINT: return REMIXAPI_FORMAT_R16_UINT;
        case VK_FORMAT_R16_SINT: return REMIXAPI_FORMAT_R16_SINT;
        case VK_FORMAT_R16_UNORM: return REMIXAPI_FORMAT_R16_UNORM;
        case VK_FORMAT_R16_SNORM: return REMIXAPI_FORMAT_R16_SNORM;
        case VK_FORMAT_R16_SFLOAT: return REMIXAPI_FORMAT_R16_SFLOAT;
        case VK_FORMAT_B4G4R4A4_UNORM_PACK16: return REMIXAPI_FORMAT_B4G4R4A4_UNORM_PACK16;
        case VK_FORMAT_B5G6R5_UNORM_PACK16: return REMIXAPI_FORMAT_B5G6R5_UNORM_PACK16;
        case VK_FORMAT_B5G5R5A1_UNORM_PACK16: return REMIXAPI_FORMAT_B5G5R5A1_UNORM_PACK16;
        case VK_FORMAT_R8G8B8A8_UINT: return REMIXAPI_FORMAT_R8G8B8A8_UINT;
        case VK_FORMAT_R8G8B8A8_SINT: return REMIXAPI_FORMAT_R8G8B8A8_SINT;
        case VK_FORMAT_R8G8B8A8_UNORM: return REMIXAPI_FORMAT_R8G8B8A8_UNORM;
        case VK_FORMAT_R8G8B8A8_SNORM: return REMIXAPI_FORMAT_R8G8B8A8_SNORM;
        case VK_FORMAT_B8G8R8A8_UNORM: return REMIXAPI_FORMAT_B8G8R8A8_UNORM;

        // HACKHACK begin
        case VK_FORMAT_R8G8B8A8_SRGB:
            return REMIXAPI_FORMAT_R8G8B8A8_UNORM; // return REMIXAPI_FORMAT_R8G8B8A8_SRGB;
            // HACKHACK end

        case VK_FORMAT_B8G8R8A8_SRGB: return REMIXAPI_FORMAT_B8G8R8A8_SRGB;
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return REMIXAPI_FORMAT_A2B10G10R10_UNORM_PACK32;
        case VK_FORMAT_B10G11R11_UFLOAT_PACK32: return REMIXAPI_FORMAT_B10G11R11_UFLOAT_PACK32;
        case VK_FORMAT_R16G16_UINT: return REMIXAPI_FORMAT_R16G16_UINT;
        case VK_FORMAT_R16G16_SINT: return REMIXAPI_FORMAT_R16G16_SINT;
        case VK_FORMAT_R16G16_UNORM: return REMIXAPI_FORMAT_R16G16_UNORM;
        case VK_FORMAT_R16G16_SNORM: return REMIXAPI_FORMAT_R16G16_SNORM;
        case VK_FORMAT_R16G16_SFLOAT: return REMIXAPI_FORMAT_R16G16_SFLOAT;
        case VK_FORMAT_R32_UINT: return REMIXAPI_FORMAT_R32_UINT;
        case VK_FORMAT_R32_SINT: return REMIXAPI_FORMAT_R32_SINT;
        case VK_FORMAT_R32_SFLOAT: return REMIXAPI_FORMAT_R32_SFLOAT;
        case VK_FORMAT_R16G16B16A16_UINT: return REMIXAPI_FORMAT_R16G16B16A16_UINT;
        case VK_FORMAT_R16G16B16A16_SINT: return REMIXAPI_FORMAT_R16G16B16A16_SINT;
        case VK_FORMAT_R16G16B16A16_SFLOAT: return REMIXAPI_FORMAT_R16G16B16A16_SFLOAT;
        case VK_FORMAT_R16G16B16A16_UNORM: return REMIXAPI_FORMAT_R16G16B16A16_UNORM;
        case VK_FORMAT_R16G16B16A16_SNORM: return REMIXAPI_FORMAT_R16G16B16A16_SNORM;
        case VK_FORMAT_R32G32_UINT: return REMIXAPI_FORMAT_R32G32_UINT;
        case VK_FORMAT_R32G32_SINT: return REMIXAPI_FORMAT_R32G32_SINT;
        case VK_FORMAT_R32G32_SFLOAT: return REMIXAPI_FORMAT_R32G32_SFLOAT;
        case VK_FORMAT_R32G32B32_UINT: return REMIXAPI_FORMAT_R32G32B32_UINT;
        case VK_FORMAT_R32G32B32_SINT: return REMIXAPI_FORMAT_R32G32B32_SINT;
        case VK_FORMAT_R32G32B32_SFLOAT: return REMIXAPI_FORMAT_R32G32B32_SFLOAT;
        case VK_FORMAT_R32G32B32A32_UINT: return REMIXAPI_FORMAT_R32G32B32A32_UINT;
        case VK_FORMAT_R32G32B32A32_SINT: return REMIXAPI_FORMAT_R32G32B32A32_SINT;
        case VK_FORMAT_R32G32B32A32_SFLOAT: return REMIXAPI_FORMAT_R32G32B32A32_SFLOAT;
        case VK_FORMAT_D16_UNORM: return REMIXAPI_FORMAT_D16_UNORM;
        case VK_FORMAT_D24_UNORM_S8_UINT: return REMIXAPI_FORMAT_D24_UNORM_S8_UINT;
        case VK_FORMAT_D32_SFLOAT: return REMIXAPI_FORMAT_D32_SFLOAT;
        case VK_FORMAT_D32_SFLOAT_S8_UINT: return REMIXAPI_FORMAT_D32_SFLOAT_S8_UINT;
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK: return REMIXAPI_FORMAT_BC1_RGB_UNORM_BLOCK;
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK: return REMIXAPI_FORMAT_BC1_RGB_SRGB_BLOCK;
        case VK_FORMAT_BC2_UNORM_BLOCK: return REMIXAPI_FORMAT_BC2_UNORM_BLOCK;
        case VK_FORMAT_BC2_SRGB_BLOCK: return REMIXAPI_FORMAT_BC2_SRGB_BLOCK;
        case VK_FORMAT_BC3_UNORM_BLOCK: return REMIXAPI_FORMAT_BC3_UNORM_BLOCK;
        case VK_FORMAT_BC3_SRGB_BLOCK: return REMIXAPI_FORMAT_BC3_SRGB_BLOCK;
        case VK_FORMAT_BC4_UNORM_BLOCK: return REMIXAPI_FORMAT_BC4_UNORM_BLOCK;
        case VK_FORMAT_BC4_SNORM_BLOCK: return REMIXAPI_FORMAT_BC4_SNORM_BLOCK;
        case VK_FORMAT_BC5_UNORM_BLOCK: return REMIXAPI_FORMAT_BC5_UNORM_BLOCK;
        case VK_FORMAT_BC5_SNORM_BLOCK: return REMIXAPI_FORMAT_BC5_SNORM_BLOCK;
        case VK_FORMAT_BC6H_UFLOAT_BLOCK: return REMIXAPI_FORMAT_BC6H_UFLOAT_BLOCK;
        case VK_FORMAT_BC6H_SFLOAT_BLOCK: return REMIXAPI_FORMAT_BC6H_SFLOAT_BLOCK;
        case VK_FORMAT_BC7_UNORM_BLOCK: return REMIXAPI_FORMAT_BC7_UNORM_BLOCK;
        case VK_FORMAT_BC7_SRGB_BLOCK: return REMIXAPI_FORMAT_BC7_SRGB_BLOCK;
        case VK_FORMAT_UNDEFINED:
        default: assert( 0 ); return REMIXAPI_FORMAT_UNDEFINED;
    }
}

auto toremix_path( const char* src ) -> std::wstring
{
    if( !src || src[ 0 ] == '\0' )
    {
        return {};
    }

    int len = MultiByteToWideChar( CP_UTF8, 0, src, -1, nullptr, 0 );
    if( len <= 0 )
    {
        assert( 0 );
        return {};
    }

    auto wstr = std::wstring{};
    wstr.resize( static_cast< size_t >( len ) );

    // Convert the multi-byte string to wide character string
    int res = MultiByteToWideChar( CP_UTF8, 0, src, -1, wstr.data(), len );
    if( res <= 0 )
    {
        assert( 0 );
        return {};
    }

    // remove the null terminator included by MultiByteToWideChar
    assert( wstr.ends_with( L'\0' ) );
    wstr.resize( len - 1 );

    return wstr;
}

template< bool WithSeparateFolder = true >
auto MakeGltfPath( const std::filesystem::path& base, std::string_view meshName )
    -> std::filesystem::path
{
    auto exportName = std::string( meshName );

    std::ranges::replace( exportName, '\\', '_' );
    std::ranges::replace( exportName, '/', '_' );

    if( WithSeparateFolder )
    {
        return base / exportName / ( exportName + ".gltf" );
    }
    else
    {
        return base / ( exportName + ".gltf" );
    }
}

auto GetGltfFilesSortedAlphabetically( const std::filesystem::path& folder )
    -> std::set< std::filesystem::path >
{
    using namespace RTGL1;
    namespace fs = std::filesystem;

    std::error_code ec;
    if( folder.empty() || !exists( folder, ec ) || !is_directory( folder ) )
    {
        return {};
    }

    auto gltfs = std::set< std::filesystem::path >{};

    try
    {
        for( const fs::directory_entry& entry : fs::directory_iterator{ folder } )
        {
            if( entry.is_regular_file() && MakeFileType( entry.path() ) == RTGL1::FileType::GLTF )
            {
                gltfs.insert( entry.path() );
            }
        }
    }
    catch( const std::filesystem::filesystem_error& e )
    {
        debug::Error( R"(directory_iterator failure: '{}'. path1: '{}'. path2: '{}')",
                      e.what(),
                      e.path1().string(),
                      e.path2().string() );
        return {};
    }

    return gltfs;
}

auto AnyImageLoader() -> TextureOverrides::Loader
{
    return std::tuple{
        &g_imageLoaderKtx,
        &g_imageLoaderRaw,
    };
}



auto toremix_verts( const RgMeshPrimitiveInfo* pPrimitive )
    -> std::vector< remixapi_HardcodedVertex >
{
    if( !pPrimitive )
    {
        return {};
    }
    auto rverts = std::vector< remixapi_HardcodedVertex >{};
    rverts.reserve( pPrimitive->vertexCount );
    for( uint32_t i = 0; i < pPrimitive->vertexCount; i++ )
    {
        const RgPrimitiveVertex& src        = pPrimitive->pVertices[ i ];
        const RgFloat3D          src_normal = Utils::UnpackNormal( src.normalPacked );

        rverts.push_back( remixapi_HardcodedVertex{
            .position = { src.position[ 0 ], src.position[ 1 ], src.position[ 2 ] },
            .normal   = { src_normal.data[ 0 ], src_normal.data[ 1 ], src_normal.data[ 2 ] },
            .texcoord = { src.texCoord[ 0 ], src.texCoord[ 1 ] },
            .color    = src.color,
        } );
    }
    return rverts;
}

auto relink_as_lightinfo( LightCopy* storage ) -> RgLightInfo*
{
    if( !storage )
    {
        assert( 0 );
        return nullptr;
    }

    storage->base.pNext = nullptr;

    if( storage->additional )
    {
        assert( storage->additional->pNext == nullptr );
        storage->additional->pNext = storage->base.pNext;
        storage->base.pNext        = &storage->additional.value();
    }

    std::visit(
        [ &storage ]< typename T >( T& ext ) {
            assert( ext.pNext == nullptr );
            ext.pNext           = storage->base.pNext;
            storage->base.pNext = &ext;
        },
        storage->extension );

    return &storage->base;
}



template< typename T >
auto hashcombine( size_t seed, const T& v )
{
    return seed ^ ( std::hash< T >{}( v ) + 0x9e3779b9 + ( seed << 6 ) + ( seed >> 2 ) );
}

enum hashspace_e
{
    HASHSPACE_MESH_STATIC,
    HASHSPACE_MESH_REPLACEMENT,
    HASHSPACE_MESH_DYNAMIC,
};

uint64_t remixhash_mesh( std::string_view meshname, hashspace_e space )
{
    return hashcombine( space, meshname );
}

uint64_t remixhash_material( const char*      texturename,
                             std::string_view meshname,
                             hashspace_e      space,
                             uint64_t         uniqueObjectId,
                             uint32_t         primindex )
{
    uint64_t h = 0;

    if( texturename )
    {
        h = hashcombine( h, std::string_view{ texturename } );
    }
    h = hashcombine( h, primindex );
    h = hashcombine( h, meshname );
    h = hashcombine( h, space );
    h = hashcombine( h, uniqueObjectId );

    return h;
}

namespace textures
{
    struct imageset_t
    {
        std::wstring albedo_alpha{};
        std::wstring roughness{};
        std::wstring metallic{};
        std::wstring normal{};
        std::wstring emissive{};
        std::wstring height{};
    };

    auto find_imageset( const char* setname ) -> const imageset_t*;
}
struct HACK_materialprebake_t
{
    using anyext_t = std::variant< remixapi_MaterialInfoOpaqueEXT, //
                                   remixapi_MaterialInfoTranslucentEXT >;

    remixapi_MaterialInfo   base{};
    anyext_t                ext{};
    uint64_t                targethash{};
    remixapi_MaterialHandle targethandle{};
};
auto HACK_updatetextures_on_material = rgl::string_map< std::vector< HACK_materialprebake_t > >{};



auto create_remixmaterial( const RgMeshInfo*          meshinst,
                           const RgMeshPrimitiveInfo& prim,
                           uint64_t                   hash,
                           const rgl::string_set*     hack_trackTextureToReplace = nullptr )
    -> remixapi_MaterialHandle
{
    const bool use_drawcall_alpha_state = false;

    const auto imageset = textures::find_imageset( prim.pTextureName );

    const RgMeshInfoFlags meshflags = ( meshinst ? meshinst->flags : 0 );

    const bool alpha_test  = ( prim.flags & RG_MESH_PRIMITIVE_ALPHA_TESTED );
    const bool alpha_blend = ( prim.flags & RG_MESH_PRIMITIVE_TRANSLUCENT );

    const bool noshadow = ( prim.flags & RG_MESH_PRIMITIVE_NO_SHADOW );

    const auto src_pbr = pnext::find< RgMeshPrimitivePBREXT >( &prim );

    union {
        remixapi_MaterialInfoTranslucentEXT tr;
        remixapi_MaterialInfoOpaqueEXT      op;
    } rext;

    const bool psr = ( prim.flags & RG_MESH_PRIMITIVE_GLASS ) || //
                     ( prim.flags & RG_MESH_PRIMITIVE_WATER ) || //
                     ( meshflags & RG_MESH_FORCE_GLASS ) ||      //
                     ( meshflags & RG_MESH_FORCE_WATER );

    const bool mirror = ( prim.flags & RG_MESH_PRIMITIVE_MIRROR ) || //
                        ( meshflags & RG_MESH_FORCE_MIRROR );

    if( psr )
    {
        const auto refrindex = ( prim.flags & RG_MESH_PRIMITIVE_GLASS )   ? g_indexOfRefractionGlass
                               : ( prim.flags & RG_MESH_PRIMITIVE_WATER ) ? g_indexOfRefractionWater
                                                                          : 1.0f;

        const bool thinwall = ( prim.flags & RG_MESH_PRIMITIVE_THIN_MEDIA );

        rext.tr = remixapi_MaterialInfoTranslucentEXT{
            .sType                            = REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_TRANSLUCENT_EXT,
            .pNext                            = nullptr,
            .transmittanceTexture             = imageset ? imageset->albedo_alpha.c_str() : nullptr,
            .refractiveIndex                  = refrindex,
            .transmittanceColor               = { 0.97f, 0.97f, 0.97f },
            .transmittanceMeasurementDistance = 1.0f,
            .thinWallThickness_hasvalue       = thinwall,
            .thinWallThickness_value          = 0.001f,
            .useDiffuseLayer                  = imageset && !imageset->albedo_alpha.empty(),
        };
    }
    else
    {
        rext.op = remixapi_MaterialInfoOpaqueEXT{
            .sType             = REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_OPAQUE_EXT,
            .pNext             = nullptr,
            .roughnessTexture  = imageset ? imageset->roughness.c_str() : nullptr,
            .metallicTexture   = imageset ? imageset->metallic.c_str() : nullptr,
            .anisotropy        = 0,
            .albedoConstant    = toremix( Utils::UnpackColor4DPacked32< RgFloat3D >( prim.color ) ),
            .opacityConstant   = noshadow      ? wrapconf.noshadow_opacity
                                 : alpha_blend ? Utils::UnpackAlphaFromPacked32( prim.color )
                                               : 1.0f,
            .roughnessConstant = mirror    ? 0.0f
                                 : src_pbr ? src_pbr->roughnessDefault
                                           : 1.0f,
            .metallicConstant  = mirror ? 1.0f
                                 : src_pbr
                                     ? saturate( src_pbr->metallicDefault + wrapconf.metallic_bias )
                                     : 0.0f,
            .thinFilmThickness_hasvalue = false,
            .thinFilmThickness_value    = {},
            .alphaIsThinFilmThickness   = false,
            .heightTexture              = imageset ? imageset->height.c_str() : nullptr,
            .heightTextureStrength      = 1.0f,
            .useDrawCallAlphaState      = use_drawcall_alpha_state,
            .blendType_hasvalue         = alpha_blend || noshadow,
            .blendType_value            = 0, // BlendType::Alpha
            .invertedBlend              = false,
            .alphaTestType              = alpha_test ? 4 /* kGreater */ : 7 /* kAlways */,
            .alphaReferenceValue        = 127,
        };
    }

    auto rinfo = remixapi_MaterialInfo{
        .sType                 = REMIXAPI_STRUCT_TYPE_MATERIAL_INFO,
        .pNext                 = &rext.tr,
        .hash                  = hash,
        .albedoTexture         = imageset ? imageset->albedo_alpha.c_str() : nullptr,
        .normalTexture         = imageset ? imageset->normal.c_str() : nullptr,
        .tangentTexture        = nullptr,
        .emissiveTexture       = imageset ? imageset->emissive.c_str() : nullptr,
        .emissiveIntensity     = imageset && !imageset->emissive.empty() ? 1.0f
                                 : noshadow ? prim.emissive * wrapconf.noshadow_emismult
                                            : prim.emissive,
        .emissiveColorConstant = toremix( Utils::UnpackColor4DPacked32< RgFloat3D >( prim.color ) ),
        .spriteSheetRow        = 0,
        .spriteSheetCol        = 0,
        .spriteSheetFps        = 0,
        .filterMode            = RG_REMIXAPI_FILTER_NEAREST, // linear TODO
        .wrapModeU             = RG_REMIXAPI_WRAP_REPEAT, // repeat TODO
        .wrapModeV             = RG_REMIXAPI_WRAP_REPEAT, // repeat TODO
    };

    remixapi_MaterialHandle rmaterial{};
    remixapi_ErrorCode      r = g_remix.CreateMaterial( &rinfo, &rmaterial );
    if( r != REMIXAPI_ERROR_CODE_SUCCESS )
    {
        printerror( "remixapi_CreateMaterial", r );
        return nullptr;
    }

    // SHIPPING_HACK begin
    if( hack_trackTextureToReplace )
    {
        if( !cstr_empty( prim.pTextureName ) &&
            hack_trackTextureToReplace->contains( prim.pTextureName ) )
        {
            HACK_updatetextures_on_material[ prim.pTextureName ].push_back( HACK_materialprebake_t{
                .base         = rinfo,
                .ext          = psr ? HACK_materialprebake_t::anyext_t{ rext.tr }
                                    : HACK_materialprebake_t::anyext_t{ rext.op },
                .targethash   = hash,
                .targethandle = rmaterial,
            } );
        }
    }
    // SHIPPING_HACK end

    return rmaterial;
}

auto create_remixmesh( std::string_view                    meshname,
                       const WholeModelFile::RawModelData& m,
                       hashspace_e                         space,
                       const rgl::string_set*              hack_trackTextureToReplace = nullptr )
    -> remixapi_MeshHandle
{
    if( m.primitives.empty() )
    {
        return nullptr;
    }

    for( const auto& p : m.primitives )
    {
        if( p.flags & RG_MESH_PRIMITIVE_SKY_VISIBILITY )
        {
            return nullptr;
        }
    }

    auto REMIX_surf = std::vector< remixapi_MeshInfoSurfaceTriangles >{};
    REMIX_surf.reserve( m.primitives.size() );

    for( uint32_t index = 0; index < m.primitives.size(); index++ )
    {
        MakeMeshPrimitiveInfoAndProcess(
            m.primitives[ index ], index, [ & ]( const RgMeshPrimitiveInfo& prim ) {
                remixapi_MaterialHandle REMIX_mat = create_remixmaterial(
                    nullptr,
                    prim,
                    remixhash_material(
                        prim.pTextureName, meshname, space, 0, prim.primitiveIndexInMesh ),
                    hack_trackTextureToReplace );

                REMIX_surf.push_back( remixapi_MeshInfoSurfaceTriangles{
                    // NOTE: for WholeModelFile, MakeMeshPrimitiveInfoAndProcess returns
                    // pVertices as remixapi_HardcodedVertex!!!
                    // this is needed to save up on conversion rg->remix
                    .vertices_values =
                        reinterpret_cast< const remixapi_HardcodedVertex* >( prim.pVertices ),
                    .vertices_count    = prim.vertexCount,
                    .indices_values    = prim.pIndices,
                    .indices_count     = prim.indexCount,
                    .skinning_hasvalue = false,
                    .skinning_value    = {},
                    .material          = REMIX_mat,
                    .flags             = ( prim.flags & RG_MESH_PRIMITIVE_FORCE_EXACT_NORMALS )
                                             ? REMIXAPI_MESH_INFO_SURFACE_TRIANGLES_BIT_USE_TRIANGLE_NORMALS
                                             : 0u,
                } );
            } );
    }


    auto REMIX_info = remixapi_MeshInfo{
        .sType           = REMIXAPI_STRUCT_TYPE_MESH_INFO,
        .pNext           = nullptr,
        .hash            = remixhash_mesh( meshname, space ),
        .surfaces_values = REMIX_surf.data(),
        .surfaces_count  = static_cast< uint32_t >( REMIX_surf.size() ),
    };

    remixapi_MeshHandle REMIX_mesh{};
    remixapi_ErrorCode  r = g_remix.CreateMesh( &REMIX_info, &REMIX_mesh );
    if( r != REMIXAPI_ERROR_CODE_SUCCESS )
    {
        printerror( "remixapi_CreateMesh", r );
        return nullptr;
    }
    return REMIX_mesh;
}

RgResult UploadLightEx( const RgLightInfo* pInfo, const RgTransform* transform );



namespace textures
{
    constexpr const char* REMIX_TEXTURE_NORMAL_postfix    = "_remix_normal";
    constexpr const char* REMIX_TEXTURE_ROUGHNESS_postfix = "_remix_roughness";
    constexpr const char* REMIX_TEXTURE_METALLIC_postfix  = "_remix_metallic";

    enum remixtexindex_e : uint32_t 
    {
        REMIX_TEXINDEX_ALBEDOALPHA = 0,
        REMIX_TEXINDEX_ROUGHNESS   = 1,
        REMIX_TEXINDEX_NORMAL      = 2,
        REMIX_TEXINDEX_EMISSIVE    = 3,
        REMIX_TEXINDEX_HEIGHT      = 4,
        REMIX_TEXINDEX_METALLIC    = 5,

        REMIX_TEXTURES_PER_MAT, // count
    };

    const std::string& postfix( remixtexindex_e index )
    {
        static std::string empty{};

        // clang-format off
        switch( index )
        {
            case REMIX_TEXINDEX_ALBEDOALPHA: return wrapconf.texpostfix_albedo;
            case REMIX_TEXINDEX_ROUGHNESS:   return wrapconf.texpostfix_rough;
            case REMIX_TEXINDEX_NORMAL:      return wrapconf.texpostfix_normal;
            case REMIX_TEXINDEX_EMISSIVE:    return wrapconf.texpostfix_emis;
            case REMIX_TEXINDEX_HEIGHT:      return wrapconf.texpostfix_height;
            case REMIX_TEXINDEX_METALLIC:    return wrapconf.texpostfix_metal;
            case REMIX_TEXTURES_PER_MAT:
            default: assert( 0 ); return empty;
        }
        // clang-format on
    }
    const std::wstring& postfix_w( remixtexindex_e index )
    {
        static std::wstring s_wstr[ REMIX_TEXTURES_PER_MAT ];

        static bool s_firsttime = true;
        if( s_firsttime )
        {
            s_firsttime = false;

            // recalculate
            for( uint32_t i = 0; i < REMIX_TEXTURES_PER_MAT; i++ )
            {
                s_wstr[ i ] = toremix_path( postfix( remixtexindex_e( i ) ).c_str() );
            }
        }

        static std::wstring wempty{};

        assert( index < REMIX_TEXTURES_PER_MAT );
        return index < REMIX_TEXTURES_PER_MAT ? s_wstr[ index ] : wempty;
    }

    constexpr bool PreferExistingMaterials = true;

    enum imported_imageset_type_e
    {
        IMPORTED_IMAGE_SET_FOR_REPLACEMENT,
        IMPORTED_IMAGE_SET_FOR_STATIC,
    };
    struct imported_imageset_t
    {
        imported_imageset_type_e type{};
        imageset_t               images{};
    };


    auto g_imagesets_imported = rgl::string_map< imported_imageset_t >{};
    auto g_imagesets_user     = rgl::string_map< imageset_t >{};


    auto upload_to_remix( const char*                                           setname,
                          std::span< TextureOverrides, REMIX_TEXTURES_PER_MAT > ovrd ) -> imageset_t
    {
        if( cstr_empty( setname ) )
        {
            return {};
        }

        imageset_t imgset{};
        
        const std::wstring baseremixname = toremix_path( setname );

        for( uint32_t i = 0; i < REMIX_TEXTURES_PER_MAT; i++ )
        {
            const auto& info = ovrd[ i ].result;
            if( !info )
            {
                continue;
            }
            if( info->baseSize.width == 0 || info->baseSize.height == 0 )
            {
                debug::Warning( "Incorrect size ({},{}) of one of images in a texture <>",
                                info->baseSize.width,
                                info->baseSize.height );
                continue;
            }

            std::wstring remiximgname = baseremixname + postfix_w( remixtexindex_e( i ) );

            auto rinfo = remixapi_CreateImageInfo{
                .sType     = REMIXAPI_STRUCT_TYPE_CREATE_IMAGE_INFO,
                .pNext     = nullptr,
                .flags     = 0,
                .format    = toremix_format_fromvk( info->format ),
                .imageName = remiximgname.c_str(),
                .pData     = info->pData + info->levelOffsets[ 0 ],
                .dataSize  = info->levelSizes[ 0 ],
                .width     = info->baseSize.width,
                .height    = info->baseSize.height,
            };

            remixapi_ErrorCode r = g_remix.CreateImage( &rinfo );
            if( r != REMIXAPI_ERROR_CODE_SUCCESS )
            {
                printerror( "remixapi_CreateImage", r );
                continue;
            }

            // clang-format off
            switch( i )
            {
                case REMIX_TEXINDEX_ALBEDOALPHA : imgset.albedo_alpha = std::move( remiximgname ); break;
                case REMIX_TEXINDEX_ROUGHNESS   : imgset.roughness    = std::move( remiximgname ); break;
                case REMIX_TEXINDEX_NORMAL      : imgset.normal       = std::move( remiximgname ); break;
                case REMIX_TEXINDEX_EMISSIVE    : imgset.emissive     = std::move( remiximgname ); break;
                case REMIX_TEXINDEX_HEIGHT      : imgset.height       = std::move( remiximgname ); break;
                case REMIX_TEXINDEX_METALLIC    : imgset.metallic     = std::move( remiximgname ); break;
                default: assert( 0 ); break;
            }
            // clang-format on
        }

        return imgset;
    }

    void dealloc_from_remix( const imageset_t& imgset )
    {
        const std::wstring* idx[] = {
            &imgset.albedo_alpha, // REMIX_TEXINDEX_ALBEDOALPHA
            &imgset.roughness,    // REMIX_TEXINDEX_ROUGHNESS
            &imgset.normal,       // REMIX_TEXINDEX_NORMAL
            &imgset.emissive,     // REMIX_TEXINDEX_EMISSIVE
            &imgset.height,       // REMIX_TEXINDEX_HEIGHT
            &imgset.metallic,     // REMIX_TEXINDEX_METALLIC
        };
        static_assert( std::size( idx ) == REMIX_TEXTURES_PER_MAT );
        static_assert( sizeof( imageset_t ) == 192 );

        for( const std::wstring* remiximgname : idx )
        {
            if( remiximgname && !remiximgname->empty() )
            {
                remixapi_ErrorCode r = g_remix.DestroyImage( remiximgname->c_str() );
                if( r != REMIXAPI_ERROR_CODE_SUCCESS )
                {
                    printerror( "remixapi_DestroyImage", r );
                }
            }
        }
    }

    bool user_imageset_register( const RgOriginalTextureInfo& info )
    {
        if( cstr_empty( info.pTextureName ) )
        {
            debug::Warning(
                "RgOriginalTextureInfo::pTextureName must not be null or an empty string" );
            return false;
        }

        if( info.pPixels == nullptr )
        {
            debug::Warning( "RgOriginalTextureInfo::pPixels must not be null" );
            return false;
        }

        // promote material from 'imported' to 'original',
        // if a game creates a material, so it's not deleted in FreeAllImportedMaterials
        if( g_imagesets_imported.erase( info.pTextureName ) )
        {
            debug::Verbose( R"(Material is promoted from 'Imported' to 'Original': {})",
                            info.pTextureName );
        }

        if( PreferExistingMaterials )
        {
            if( g_imagesets_user.contains( info.pTextureName ) )
            {
                debug::Verbose( "Material with the same name already exists, ignoring new data: {}",
                                info.pTextureName );
                return false;
            }
        }


        auto details = pnext::find< RgOriginalTextureDetailsEXT >( &info );


        // clang-format off
        TextureOverrides ovrd[] = {
            TextureOverrides{ g_ovrdfolder, info.pTextureName, postfix( REMIX_TEXINDEX_ALBEDOALPHA ), info.pPixels, info.size, rgtexture_to_vkformat( details, VK_FORMAT_R8G8B8A8_SRGB ), AnyImageLoader() },
            TextureOverrides{ g_ovrdfolder, info.pTextureName, postfix( REMIX_TEXINDEX_ROUGHNESS   ), nullptr, {}, VK_FORMAT_R8_SRGB, AnyImageLoader() },
            TextureOverrides{ g_ovrdfolder, info.pTextureName, postfix( REMIX_TEXINDEX_NORMAL      ), nullptr, {}, VK_FORMAT_R8G8B8A8_UNORM, AnyImageLoader() },
            TextureOverrides{ g_ovrdfolder, info.pTextureName, postfix( REMIX_TEXINDEX_EMISSIVE    ), nullptr, {}, VK_FORMAT_R8G8B8A8_SRGB, AnyImageLoader() },
            TextureOverrides{ g_ovrdfolder, info.pTextureName, postfix( REMIX_TEXINDEX_HEIGHT      ), nullptr, {}, VK_FORMAT_R8_UNORM, AnyImageLoader() },
            TextureOverrides{ g_ovrdfolder, info.pTextureName, postfix( REMIX_TEXINDEX_METALLIC    ), nullptr, {}, VK_FORMAT_R8_UNORM, AnyImageLoader() },
        };
        // clang-format on
        static_assert( std::size( ovrd ) == REMIX_TEXTURES_PER_MAT );

        
        // clang-format off
        SamplerManager::Handle samplers[] = {
            SamplerManager::Handle{ info.filter, info.addressModeU, info.addressModeV },
            SamplerManager::Handle{ info.filter, info.addressModeU, info.addressModeV },
            SamplerManager::Handle{ g_forceNormalMapFilterLinear ? RG_SAMPLER_FILTER_LINEAR : info.filter, info.addressModeU, info.addressModeV },
            SamplerManager::Handle{ info.filter, info.addressModeU, info.addressModeV },
            SamplerManager::Handle{ RG_SAMPLER_FILTER_LINEAR, info.addressModeU, info.addressModeV },
            SamplerManager::Handle{ info.filter, info.addressModeU, info.addressModeV },
        };
        // clang-format on
        static_assert( std::size( samplers ) == REMIX_TEXTURES_PER_MAT );
        static_assert( REMIX_TEXINDEX_NORMAL == 2 );


        std::optional< RgTextureSwizzling > swizzlings[] = {
            std::nullopt, // REMIX_TEXINDEX_ALBEDOALPHA
            std::nullopt, // REMIX_TEXINDEX_ROUGHNESS
            std::nullopt, // REMIX_TEXINDEX_NORMAL
            std::nullopt, // REMIX_TEXINDEX_EMISSIVE
            std::nullopt, // REMIX_TEXINDEX_HEIGHT
            std::nullopt, // REMIX_TEXINDEX_METALLIC
        };
        static_assert( std::size( swizzlings ) == REMIX_TEXTURES_PER_MAT );


        auto [ iter, isnew ] = g_imagesets_user.emplace( //
            std::string{ info.pTextureName },
            upload_to_remix( info.pTextureName, ovrd ) );
        assert( isnew );

        return true;
    }

    bool user_imageset_delete( const char* setname )
    {
        if( cstr_empty( setname ) )
        {
            return false;
        }

        auto f = g_imagesets_user.find( setname );
        if( f == g_imagesets_user.end() )
        {
            return false;
        }

        dealloc_from_remix( f->second );
        g_imagesets_user.erase( f );

        return true;
    }

    bool importedimageset_register(
        const std::string&                                                     setname,
        std::span< const std::filesystem::path, TEXTURES_PER_MATERIAL_COUNT >  fullPaths,
        std::span< const SamplerManager::Handle, TEXTURES_PER_MATERIAL_COUNT > samplers,
        RgTextureSwizzling                                                     customPbrSwizzling,
        bool                                                                   isReplacement )
    {
        if( setname.empty() )
        {
            return false;
        }

        // check if already uploaded
        {
            auto found = g_imagesets_imported.find( setname );
            if( found != g_imagesets_imported.end() )
            {
                if( isReplacement )
                {
                    // promote to a stronger type
                    found->second.type = IMPORTED_IMAGE_SET_FOR_REPLACEMENT;
                }
                return true;
            }
        }

        if( PreferExistingMaterials )
        {
            if( g_imagesets_user.contains( setname ) )
            {
                debug::Verbose( "Material with the same name already exists, ignoring new data: {}",
                                setname );
                return false;
            }
        }

        // all paths are empty
        if( std::ranges::all_of( fullPaths, []( auto&& p ) { return p.empty(); } ) )
        {
            return false;
        }

        if( std::ranges::none_of(
                fullPaths, []( auto&& p ) { return std::filesystem::is_regular_file( p ); } ) )
        {
            debug::Warning( "Fail to create imported material: none of the paths lead to a file:"
                            "\n  A: {}\n  B: {}\n  C: {}\n  D: {}\n  E: {}\n",
                            fullPaths[ 0 ].string(),
                            fullPaths[ 1 ].string(),
                            fullPaths[ 2 ].string(),
                            fullPaths[ 3 ].string(),
                            fullPaths[ 4 ].string() );
            return false;
        }

        // clang-format off
        TextureOverrides ovrd[] = {
            TextureOverrides{ fullPaths[ TEXTURE_ALBEDO_ALPHA_INDEX     ],  true, AnyImageLoader() }, // REMIX_TEXINDEX_ALBEDOALPHA
            TextureOverrides{ {} /* combined rough-metal not supported */, false, AnyImageLoader() }, // REMIX_TEXINDEX_ROUGHNESS
            TextureOverrides{ fullPaths[ TEXTURE_NORMAL_INDEX           ], false, AnyImageLoader() }, // REMIX_TEXINDEX_NORMAL
            TextureOverrides{ fullPaths[ TEXTURE_EMISSIVE_INDEX         ],  true, AnyImageLoader() },  // REMIX_TEXINDEX_EMISSIVE
            TextureOverrides{ fullPaths[ TEXTURE_HEIGHT_INDEX           ],  true, AnyImageLoader() },  // REMIX_TEXINDEX_HEIGHT
            TextureOverrides{ {} /* combined rough-metal not supported */, false, AnyImageLoader() }, // REMIX_TEXINDEX_METALLIC
        };
        // clang-format on
        static_assert( std::size( ovrd ) == REMIX_TEXTURES_PER_MAT );


        std::optional< RgTextureSwizzling > swizzlings[] = {
            std::nullopt, // REMIX_TEXINDEX_ALBEDOALPHA
            std::nullopt, // REMIX_TEXINDEX_ROUGHNESS
            std::nullopt, // REMIX_TEXINDEX_NORMAL
            std::nullopt, // REMIX_TEXINDEX_EMISSIVE
            std::nullopt, // REMIX_TEXINDEX_HEIGHT
            std::nullopt, // REMIX_TEXINDEX_METALLIC
        };
        static_assert( std::size( swizzlings ) == REMIX_TEXTURES_PER_MAT );
        static_assert( TEXTURE_OCCLUSION_ROUGHNESS_METALLIC_INDEX == 1 );

        // to free later / to prevent export from ExportOriginalMaterialTextures
        auto [ iter, isNew ] = g_imagesets_imported.emplace(
            setname,
            imported_imageset_t{
                .type   = isReplacement ? IMPORTED_IMAGE_SET_FOR_REPLACEMENT
                                        : IMPORTED_IMAGE_SET_FOR_STATIC,
                .images = upload_to_remix( setname.c_str(), ovrd ),
            } );
        assert( isNew );

        return true;
    }

    void importedimageset_freeall( bool withreplacements )
    {
        for( const auto& [ setname, set ] : g_imagesets_imported )
        {
            if( set.type == IMPORTED_IMAGE_SET_FOR_REPLACEMENT )
            {
                if( !withreplacements )
                {
                    continue;
                }
            }

            dealloc_from_remix( set.images );
        }

        if( !withreplacements )
        {
            erase_if( g_imagesets_imported, []( const auto& setname_set ) {
                // do not erase replacements
                return setname_set.second.type != IMPORTED_IMAGE_SET_FOR_REPLACEMENT;
            } );
        }
        else
        {
            g_imagesets_imported.clear();
        }
    }

    auto find_imageset( const char* setname ) -> const imageset_t*
    {
        if( cstr_empty( setname ) )
        {
            return nullptr;
        }

        {
            auto f = g_imagesets_imported.find( setname );
            if( f != g_imagesets_imported.end() )
            {
                return &f->second.images;
            }
        }
        {
            auto f = g_imagesets_user.find( setname );
            if( f != g_imagesets_user.end() )
            {
                return &f->second;
            }
        }
        return nullptr;
    }

} // namespace textures

namespace scene
{
    struct staticinstance_t
    {
        remixapi_MeshHandle remixmesh;
        remixapi_Transform  transform;
        uint64_t            instanceId;
    };

    struct replacement_t
    {
        remixapi_MeshHandle          remixmesh;
        WholeModelFile::RawModelData data;
    };

    auto g_currentmap              = std::string{};
    auto g_statics                 = rgl::string_map< staticinstance_t >{};
    auto g_static_lights           = std::vector< LightCopy >{};
    auto cameraInfo_Imported       = std::optional< RgCameraInfo >{};
    auto m_cameraInfo_ImportedAnim = AnimationData{};

    bool g_reimport_replacements = true;
    auto g_replacements          = rgl::string_map< replacement_t >{};


    float g_staticSceneAnimationTime{ 0 };

    auto g_lightstyles                    = std::vector< uint8_t >{};
    auto g_alreadyReplacedUniqueObjectIDs = rgl::unordered_set< uint64_t >{};


    void load_new_scene( const ImportExportParams&    params,
                         const std::filesystem::path& staticscene_gltf,
                         const std::filesystem::path& replacements_folder,
                         const TextureMetaManager&    textureMeta )
    {
        const bool reimportReplacements = !replacements_folder.empty();


        textures::importedimageset_freeall( reimportReplacements );

        for( const auto& [ meshname, st ] : g_statics )
        {
            remixapi_ErrorCode r = g_remix.DestroyMesh( st.remixmesh );
            assert( r == REMIXAPI_ERROR_CODE_SUCCESS );
        }
        g_statics.clear();
        g_static_lights.clear();


        if( reimportReplacements )
        {
            for( const auto& [ meshname, repl ] : g_replacements )
            {
                remixapi_ErrorCode r = g_remix.DestroyMesh( repl.remixmesh );
                assert( r == REMIXAPI_ERROR_CODE_SUCCESS );
            }
            g_replacements.clear();

            debug::Verbose( "Reading replacements..." );
            const auto gltfs = GetGltfFilesSortedAlphabetically( replacements_folder );

            auto allImported = std::vector< std::future< std::unique_ptr< WholeModelFile > > >{};
            {
                // reverse alphabetical -- last ones have more priority
                for( const auto& p : std::ranges::reverse_view{ gltfs } )
                {
                    allImported.push_back( std::async(
                        std::launch::async,
                        [ & ]( const std::filesystem::path& path )
                            -> std::unique_ptr< WholeModelFile > //
                        {
                            if( auto i = GltfImporter{ path, params, textureMeta, true } )
                            {
                                return std::make_unique< WholeModelFile >( i.Move() );
                            }
                            return {};
                        },
                        p ) );
                }
            }

            for( auto& ff : allImported )
            {
                auto wholeGltf = ff.valid() //
                                     ? ff.get()
                                     : std::unique_ptr< WholeModelFile >{};

                if( !wholeGltf )
                {
                    continue;
                }
                auto path = std::filesystem::path{ "<<GET NAME>>" };

                if( !wholeGltf->lights.empty() )
                {
                    debug::Warning( "Ignoring non-attached lights from \'{}\'", path.string() );
                }

                for( const auto& mat : wholeGltf->materials )
                {
                    textures::importedimageset_register( mat.pTextureName,
                                                         mat.fullPaths,
                                                         mat.samplers,
                                                         mat.pbrSwizzling,
                                                         mat.isReplacement );
                }

                for( auto& [ meshName, meshSrc ] : wholeGltf->models )
                {
                    auto [ iter, isNew ] = g_replacements.emplace( meshName,
                                                                   replacement_t{
                                                                       .remixmesh = nullptr,
                                                                       .data = std::move( meshSrc ),
                                                                   } );

                    if( !isNew )
                    {
                        debug::Warning( "Ignoring a replacement as it was already read "
                                        "from another .gltf file. \'{}\' - \'{}\'",
                                        meshName,
                                        path.string() );
                        continue;
                    }

                    WholeModelFile::RawModelData& m = iter->second.data;

                    if( m.primitives.empty() && m.localLights.empty() )
                    {
                        debug::Warning( "Replacement is empty, it doesn't have "
                                        "any primitives or lights: \'{}\' - \'{}\'",
                                        meshName,
                                        path.string() );
                        continue;
                    }

                    iter->second.remixmesh =
                        create_remixmesh( meshName, m, HASHSPACE_MESH_REPLACEMENT );

                    // save up some memory by not storing - as we uploaded already
                    for( auto& prim : m.primitives )
                    {
                        prim.vertices = {};
                        prim.indices  = {};
                    }
                }
            }
            debug::Verbose( "Replacements are ready" );
        }

        // SHIPPING_HACK begin
        HACK_updatetextures_on_material.clear();
        auto trackTextureToReplace = rgl::string_set{};
        // SHIPPING_HACK end

        if( auto staticScene = GltfImporter{ staticscene_gltf, params, textureMeta, false } )
        {
            WholeModelFile sceneFile = staticScene.Move();

            debug::Verbose( "Starting new static scene..." );

            for( const auto& mat : sceneFile.materials )
            {
                textures::importedimageset_register( mat.pTextureName,
                                                     mat.fullPaths,
                                                     mat.samplers,
                                                     mat.pbrSwizzling,
                                                     mat.isReplacement );

                // SHIPPING_HACK begin
                if( mat.trackOriginalTexture && !mat.pTextureName.empty() )
                {
                    trackTextureToReplace.insert( mat.pTextureName );
                }
                // SHIPPING_HACK end
            }

            for( const auto& [ meshName, m ] : sceneFile.models )
            {
                if( auto remixmesh = create_remixmesh(
                        meshName, m, HASHSPACE_MESH_STATIC, &trackTextureToReplace ) )
                {
                    g_statics.emplace( meshName,
                                       staticinstance_t{
                                           .remixmesh  = remixmesh,
                                           .transform  = toremix( m.meshTransform ),
                                           .instanceId = m.uniqueObjectID,
                                       } );
                }

                for( uint32_t i = 0; i < m.primitives.size(); i++ )
                {
                    if( !m.localLights.empty() )
                    {
                        debug::Warning( "Lights under the scene mesh ({}) are ignored, "
                                        "put them under the root node.",
                                        meshName,
                                        staticScene.FilePath() );
                    }
                }
            }

            // camera
            if( sceneFile.camera )
            {
                cameraInfo_Imported = *( sceneFile.camera );
            }
            if( !IsAnimDataEmpty( sceneFile.animcamera ) )
            {
                m_cameraInfo_ImportedAnim = std::move( sceneFile.animcamera );
            }

            // global lights
            for( const auto& l : sceneFile.lights )
            {
                g_static_lights.push_back( l );
            }

            if( sceneFile.lights.empty() )
            {
                debug::Warning( "Haven't found any lights in {}: "
                                "Original exportable lights will be used",
                                staticScene.FilePath() );
            }
            debug::Verbose( "Static scene is ready" );
        }
        else
        {
            debug::Info( "New scene is empty" );
        }
    }

    void check_new_map( std::string_view mapname, RgStaticSceneStatusFlags* out_staticSceneStatus )
    {
        const bool reimport_static = ( g_currentmap != mapname );
        if( reimport_static )
        {
            g_currentmap = mapname;
        }

        if( g_reimport_replacements || reimport_static )
        {
            // before importer, as it relies on texture properties
            g_texturemeta->RereadFromFiles( std::string_view{ g_currentmap } );

            auto replacements_folder = g_ovrdfolder / REPLACEMENTS_FOLDER;

            load_new_scene( g_importexport_params,
                            MakeGltfPath( g_ovrdfolder / SCENES_FOLDER, g_currentmap ),
                            g_reimport_replacements ? ( g_ovrdfolder / REPLACEMENTS_FOLDER )
                                                    : std::filesystem::path{},
                            *g_texturemeta );

            g_reimport_replacements = false;
        }

        if( out_staticSceneStatus )
        {
#if RG_TODO
            if( scene.StaticSceneExists() )
            {
                *out_staticSceneStatus =
                    RG_STATIC_SCENE_STATUS_LOADED |
                    ( reimport_static ? RG_STATIC_SCENE_STATUS_NEW_SCENE_STARTED : 0 );
            }
            else
#endif
            {
                *out_staticSceneStatus = 0;
            }
        }
    }

    void upload_static_instances()
    {
        for( const auto& [ name, inst ] : g_statics )
        {
            auto rinstinfo = remixapi_InstanceInfo{
                .sType         = REMIXAPI_STRUCT_TYPE_INSTANCE_INFO,
                .pNext         = nullptr,
                .categoryFlags = 0,
                .mesh          = inst.remixmesh,
                .transform     = inst.transform,
                .doubleSided   = true,
            };

            remixapi_ErrorCode r = g_remix.DrawInstance( &rinstinfo );
            if( r != REMIXAPI_ERROR_CODE_SUCCESS )
            {
                printerror( "remixapi_DrawInstance", r );
            }
        }
    }

    void set_lightstyles( const RgStartFrameInfo& params )
    {
        if( !params.pLightstyleValues8 || params.lightstyleValuesCount == 0 )
        {
            return;
        }

        auto values = std::span{ params.pLightstyleValues8, params.lightstyleValuesCount };
        g_lightstyles.assign( values.begin(), values.end() );
    }

    float calculate_lightstyle( const RgLightAdditionalEXT* extra )
    {
        if( extra && ( extra->flags & RG_LIGHT_ADDITIONAL_LIGHTSTYLE ) )
        {
            if( extra->lightstyle >= 0 && size_t( extra->lightstyle ) < g_lightstyles.size() )
            {
                return float( g_lightstyles[ extra->lightstyle ] ) / 255.0f;
            }
            else
            {
                assert( 0 );
            }
        }
        return 1.0f;
    }

    void upload_static_lights()
    {
        for( LightCopy l : g_static_lights )
        {
            RgResult r = UploadLightEx( relink_as_lightinfo( &l ), nullptr );
            assert( r == RG_RESULT_SUCCESS );
        }
    }

    bool static_light_exists( const RgLightInfo* light )
    {
        if( light && light->isExportable )
        {
            // if at least one exportable exists, ignore this light
            return !g_static_lights.empty();
        }
        return false;
    }
}

RgResult RGAPI_CALL rgUploadMeshPrimitive( const RgMeshInfo*          pMesh,
                                           const RgMeshPrimitiveInfo* pPrimitive )
{
    if( !pPrimitive )
    {
        return RG_RESULT_WRONG_FUNCTION_ARGUMENT;
    }
    if( pPrimitive->sType != RG_STRUCTURE_TYPE_MESH_PRIMITIVE_INFO )
    {
        return RG_RESULT_WRONG_STRUCTURE_TYPE;
    }
    if( pPrimitive->vertexCount == 0 || pPrimitive->pVertices == nullptr )
    {
        return RG_RESULT_SUCCESS;
    }

    // hdremix missing functionality
    {
        // ignore decals for now
        if( pPrimitive->flags & RG_MESH_PRIMITIVE_DECAL )
        {
            return RG_RESULT_SUCCESS;
        }
        // skyvis polygons
        if( pPrimitive->flags & RG_MESH_PRIMITIVE_SKY_VISIBILITY )
        {
            return RG_RESULT_SUCCESS;
        }
    }

    remixapi_MeshHandle rmesh{};

    if( pMesh && pMesh->isExportable && pMesh->pMeshName && pMesh->pMeshName[ 0 ] != '\0' )
    {
        if( pMesh->flags & RG_MESH_EXPORT_AS_SEPARATE_FILE )
        {
            auto f = scene::g_replacements.find( std::string_view{ pMesh->pMeshName } );
            if( f != scene::g_replacements.end() )
            {
                rmesh = f->second.remixmesh;


                // multiple primitives can correspond to one mesh instance,
                // if a replacement for a mesh is present, upload it once
                if( scene::g_alreadyReplacedUniqueObjectIDs.contains( pMesh->uniqueObjectID ) )
                {
                    return RG_RESULT_SUCCESS;
                }
                scene::g_alreadyReplacedUniqueObjectIDs.insert( pMesh->uniqueObjectID );


                for( LightCopy localLight : f->second.data.localLights )
                {
                    assert( localLight.base.uniqueID != 0 && localLight.base.isExportable );

                    localLight.base.uniqueID =
                        hashcombine( localLight.base.uniqueID, pMesh->uniqueObjectID );
                    localLight.base.isExportable = false;

                    if( localLight.additional &&
                        ( localLight.additional->flags &
                          RG_LIGHT_ADDITIONAL_APPLY_PARENT_MESH_INTENSITY ) )
                    {
                        std::visit( [ mult = pMesh->localLightsIntensity ](
                                        auto& ext ) { ext.intensity *= mult; },
                            localLight.extension );
                    }

                    RgResult r =
                        UploadLightEx( relink_as_lightinfo( &localLight ), &pMesh->transform );
                    assert( r == RG_RESULT_SUCCESS );
                }
            }
        }
        else
        {
            if( scene::g_statics.contains( std::string_view{ pMesh->pMeshName } ) )
            {
                return RG_RESULT_SUCCESS;
            }
        }
    }

    if( !rmesh )
    {
        if( pPrimitive->flags & RG_MESH_PRIMITIVE_SKY_VISIBILITY )
        {
            return RG_RESULT_SUCCESS;
        }


        auto       rverts     = toremix_verts( pPrimitive );
        const bool useindices = ( pPrimitive->pIndices && pPrimitive->indexCount > 0 );


        auto ui = pnext::find< RgMeshPrimitiveSwapchainedEXT >( pPrimitive );

        const bool sky = ( pPrimitive->flags & RG_MESH_PRIMITIVE_SKY );
        if( sky )
        {
            static const auto emulatesky = RgMeshPrimitiveSwapchainedEXT{
                .sType           = RG_STRUCTURE_TYPE_MESH_PRIMITIVE_SWAPCHAINED_EXT,
                .pNext           = nullptr,
                .flags           = 0,
                .pViewport       = nullptr,
                .pView           = nullptr,
                .pProjection     = nullptr,
                .pViewProjection = nullptr,
            };
            ui = &emulatesky;
        }

        if( ui )
        {
            const auto imagename = toremix_path( pPrimitive->pTextureName );

            constexpr static auto MIDENTITY = remixapi_Matrix{ {
                { 1, 0, 0, 0 },
                { 0, 1, 0, 0 },
                { 0, 0, 1, 0 },
                { 0, 0, 0, 1 },
            } };

            remixapi_Matrix mworld;
            if( pMesh )
            {
                const auto& src = pMesh->transform.matrix;

                mworld = remixapi_Matrix{ {
                    { src[ 0 ][ 0 ], src[ 0 ][ 1 ], src[ 0 ][ 2 ], src[ 0 ][ 3 ] },
                    { src[ 1 ][ 0 ], src[ 1 ][ 1 ], src[ 1 ][ 2 ], src[ 1 ][ 3 ] },
                    { src[ 2 ][ 0 ], src[ 2 ][ 1 ], src[ 2 ][ 2 ], src[ 2 ][ 3 ] },
                    { 0, 0, 0, 1 },
                } };
            }
            else
            {
                mworld = MIDENTITY;
            }

            remixapi_Matrix mview;
            if( ui->pViewProjection )
            {
                mview = MIDENTITY;
            }
            else if( ui->pView )
            {
                memcpy( &mview, ui->pView, sizeof( float ) * 16 );
            }
            else
            {
                mview = MIDENTITY;
            }

            remixapi_Matrix mproj;
            if( ui->pViewProjection )
            {
                const auto& src = ui->pViewProjection;

                // Note: view-projection passed to RTGL1 is with Vulkan's inverted Y axis
                // clang-format off
                mproj = remixapi_Matrix{ {
                    { src[  0 ], src[  1 ], src[  2 ], src[  3 ] },
                    { src[  4 ], src[  5 ], src[  6 ], src[  7 ] },
                    { src[  8 ], src[  9 ], src[ 10 ], src[ 11 ] },
                    { src[ 12 ], src[ 13 ], src[ 14 ], src[ 15 ] },
                } };
                // clang-format on
            }
            else if( ui->pProjection )
            {
                const auto& src = ui->pProjection;

                // Note: view-projection passed to RTGL1 is with Vulkan's inverted Y axis
                // clang-format off
                mproj = remixapi_Matrix{ {
                    { src[  0 ], src[  1 ], src[  2 ], src[  3 ] },
                    { src[  4 ], src[  5 ], src[  6 ], src[  7 ] },
                    { src[  8 ], src[  9 ], src[ 10 ], src[ 11 ] },
                    { src[ 12 ], src[ 13 ], src[ 14 ], src[ 15 ] },
                } };
                // clang-format on
            }
            else
            {
                mproj = MIDENTITY;
            }

            static auto l_transpose = []( remixapi_Matrix& inout ) {
                const remixapi_Matrix copy = inout;
                for( int i = 0; i < 4; i++ )
                {
                    for( int j = 0; j < 4; j++ )
                    {
                        inout.matrix[ i ][ j ] = copy.matrix[ j ][ i ];
                    }
                }
            };
            l_transpose( mworld );


            auto l_rgba_to_argb = []( uint32_t rgba ) {

#define RG_D3DCOLOR_ARGB( a, r, g, b )                                        \
    ( ( uint32_t )( ( ( ( a ) & 0xff ) << 24 ) | ( ( ( r ) & 0xff ) << 16 ) | \
                    ( ( ( g ) & 0xff ) << 8 ) | ( ( b ) & 0xff ) ) )
#define RG_D3DCOLOR_RGBA( r, g, b, a ) RG_D3DCOLOR_ARGB( a, r, g, b )
                //
                return RG_D3DCOLOR_RGBA( rgba, //
                                         rgba >> 8,
                                         rgba >> 16,
                                         rgba >> 24 );
            };


            const auto vp = remixapi_Viewport{
                .x        = ui->pViewport ? ui->pViewport->x : 0,
                .y        = ui->pViewport ? ui->pViewport->y : 0,
                .width    = ui->pViewport ? ui->pViewport->width : float( g_hwnd_size.width ),
                .height   = ui->pViewport ? ui->pViewport->height : float( g_hwnd_size.height ),
                .minDepth = ui->pViewport ? ui->pViewport->minDepth : 0.f,
                .maxDepth = ui->pViewport ? ui->pViewport->maxDepth : 1.f,
            };

#if 0 // NOTE: remixapi doesn't set negative viewport height yet...
            const auto vp = remixapi_Viewport{
                .x        = vp0.x,
                .y        = vp0.y + vp0.height,
                .width    = vp0.width,
                .height   = -vp0.height,
                .minDepth = vp0.minDepth,
                .maxDepth = vp0.maxDepth,
            };
#else

            if( !sky )
            {
                static auto l_multiply = []( const remixapi_Matrix& ma,
                                             const remixapi_Matrix& mb ) -> remixapi_Matrix {
                    remixapi_Matrix result;
                    float*          r = ( float* )result.matrix;
                    const float*    a = ( const float* )ma.matrix;
                    const float*    b = ( const float* )mb.matrix;
                    for( int i = 0; i < 4; i++ )
                    {
                        for( int j = 0; j < 4; j++ )
                        {
                            r[ i * 4 + j ] =
                                a[ i * 4 + 0 ] * b[ 0 * 4 + j ] + a[ i * 4 + 1 ] * b[ 1 * 4 + j ] +
                                a[ i * 4 + 2 ] * b[ 2 * 4 + j ] + a[ i * 4 + 3 ] * b[ 3 * 4 + j ];
                        }
                    }
                    return result;
                };

                auto l_applymat4_to_position = []( const remixapi_Matrix& ma, float( &pos )[ 3 ] ) {
                    const auto& m = ma.matrix;
                    // clang-format off
                    const float out[] = {
                        m[ 0 ][ 0 ] * pos[ 0 ] + 
                        m[ 1 ][ 0 ] * pos[ 1 ] + 
                        m[ 2 ][ 0 ] * pos[ 2 ] +
                        m[ 3 ][ 0 ],

                        m[ 0 ][ 1 ] * pos[ 0 ] + 
                        m[ 1 ][ 1 ] * pos[ 1 ] + 
                        m[ 2 ][ 1 ] * pos[ 2 ] +
                        m[ 3 ][ 1 ],

                        m[ 0 ][ 2 ] * pos[ 0 ] + 
                        m[ 1 ][ 2 ] * pos[ 1 ] + 
                        m[ 2 ][ 2 ] * pos[ 2 ] +
                        m[ 3 ][ 2 ],

                        m[ 0 ][ 3 ] * pos[ 0 ] + 
                        m[ 1 ][ 3 ] * pos[ 1 ] + 
                        m[ 2 ][ 3 ] * pos[ 2 ] +
                        m[ 3 ][ 3 ]
                    };
                    // clang-format on

                    float w  = ( abs( out[ 3 ] ) < FLT_EPSILON ? 1 : out[ 3 ] );
                    pos[ 0 ] = out[ 0 ] / w;
                    pos[ 1 ] = out[ 1 ] / w;
                    pos[ 2 ] = out[ 2 ] / w;
                };

                // combine matrices and apply them on CPU...
                const auto mvp = l_multiply( mproj, l_multiply( mview, mworld ) );
                for( auto& vert : rverts )
                {
                    l_applymat4_to_position( mvp, vert.position );
                    vert.position[ 1 ] = -vert.position[ 1 ];
                }
                mworld = MIDENTITY;
                mview  = MIDENTITY;
                mproj  = MIDENTITY;
            }

            // also convert RGBA to ARGB...
            for( auto& vert : rverts )
            {
                vert.color = l_rgba_to_argb( vert.color );
            }

            // need to change winding...
            auto inds = std::vector< uint32_t >{};
            if( useindices )
            {
                inds = std::vector< uint32_t >{ pPrimitive->pIndices,
                                                pPrimitive->pIndices + pPrimitive->indexCount };

                for( size_t i = 0; i < inds.size(); i += 3 )
                {
                    std::swap( inds[ i ], inds[ i + 2 ] );
                }
            }
#endif

            if( rverts.empty() )
            {
                return RG_RESULT_SUCCESS;
            }

            auto uiinfo = remixapi_UIInstanceInfo{
                .sType       = REMIXAPI_STRUCT_TYPE_UI_INSTANCE_INFO,
                .pNext       = nullptr,
                .flags       = REMIXAPI_RASTERIZED_INSTANCE_CATEGORY_BIT_SKIP_NORMALS,
                .pViewport   = &vp,
                .pWorld      = &mworld,
                .pView       = &mview,
                .pProjection = &mproj,
                .pVertices   = rverts.data(),
                .vertexCount = inds.empty() ? align_to_tri_lower( rverts.size() ) //
                                            : uint32_t( rverts.size() ),
                .pIndices    = inds.empty() ? nullptr : inds.data(),
                .indexCount  = inds.empty() ? 0 : align_to_tri_lower( inds.size() ),
                .imageName   = imagename.c_str(),
                .color       = sky ? l_rgba_to_argb( pPrimitive->color ) : rverts[ 0 ].color,
            };

            if (sky)
            {
                assert( std::abs( g_skyviewerpos.data[ 0 ] ) < FLT_EPSILON &&
                        std::abs( g_skyviewerpos.data[ 1 ] ) < FLT_EPSILON &&
                        std::abs( g_skyviewerpos.data[ 2 ] ) < FLT_EPSILON ); // AT_ORIGIN
                uiinfo.flags |=
                    REMIXAPI_RASTERIZED_INSTANCE_CATEGORY_BIT_FORCE_SKYVIEWER_AT_ORIGIN |
                    REMIXAPI_RASTERIZED_INSTANCE_CATEGORY_BIT_USE_MAINCAMERA_VIEW_PROJECTION |
                    REMIXAPI_RASTERIZED_INSTANCE_CATEGORY_BIT_SKY;
            }

            remixapi_ErrorCode r = g_remix.DrawUIInstance( &uiinfo );
            if( r != REMIXAPI_ERROR_CODE_SUCCESS )
            {
                printerror( "remixapi_CreateMesh", r );
                return RG_RESULT_INTERNAL_ERROR;
            }
            return RG_RESULT_SUCCESS;
        }



        // --------------- //
        // --------------- //

        auto modified               = RgMeshPrimitiveInfo{ *pPrimitive };
        auto modified_attachedLight = std::optional< RgMeshPrimitiveAttachedLightEXT >{};
        auto modified_pbr           = std::optional< RgMeshPrimitivePBREXT >{};
        {
            const auto& mesh = *pMesh;

            // ignore replacement, if the scene requires
            if( mesh.isExportable && ( mesh.flags & RG_MESH_EXPORT_AS_SEPARATE_FILE ) &&
                !cstr_empty( mesh.pMeshName ) )
            {
                if( g_scenemeta->IsReplacementIgnored( scene::g_currentmap, mesh.pMeshName ) )
                {
                    return RG_RESULT_SUCCESS;
                }
            }

            if( auto original = pnext::find< RgMeshPrimitiveAttachedLightEXT >( pPrimitive ) )
            {
                modified_attachedLight = *original;
            }

            if( auto original = pnext::find< RgMeshPrimitivePBREXT >( pPrimitive ) )
            {
                modified_pbr = *original;
            }

            if( mesh.flags & RG_MESH_FORCE_MIRROR )
            {
                modified.flags |= RG_MESH_PRIMITIVE_MIRROR;
            }
            if( mesh.flags & RG_MESH_FORCE_GLASS )
            {
                modified.flags |= RG_MESH_PRIMITIVE_GLASS;
            }
            if( mesh.flags & RG_MESH_FORCE_WATER )
            {
                modified.flags |= RG_MESH_PRIMITIVE_WATER;
            }

            if( !g_texturemeta->Modify( modified, modified_attachedLight, modified_pbr, false ) )
            {
                return RG_RESULT_SUCCESS;
            }

            if( modified_attachedLight )
            {
                // insert
                modified_attachedLight.value().pNext = modified.pNext;
                modified.pNext                       = &modified_attachedLight.value();
            }

            if( modified_pbr )
            {
                // insert
                modified_pbr.value().pNext = modified.pNext;
                modified.pNext             = &modified_pbr.value();
            }
        }
        // swap
        pPrimitive = &modified;



        // --------------- //
        // --------------- //

        remixapi_MaterialHandle rmaterial = create_remixmaterial(
            pMesh,
            *pPrimitive,
            remixhash_material( pPrimitive->pTextureName,
                                pMesh && pMesh->pMeshName ? std::string_view{ pMesh->pMeshName }
                                                          : std::string_view{},
                                HASHSPACE_MESH_DYNAMIC,
                                pMesh ? pMesh->uniqueObjectID : 0ull,
                                pPrimitive->primitiveIndexInMesh ) );
        c_materialstoclear.insert( rmaterial );



        // --------------- //
        // --------------- //
        // legacy way to attach lights
        if( auto attachedLight = pnext::find< RgMeshPrimitiveAttachedLightEXT >( pPrimitive ) )
        {
            const auto& mesh = *pMesh;
            const auto& prim = *pPrimitive;

            bool quad = ( prim.indexCount == 6 && prim.vertexCount == 4 ) ||
                        ( prim.indexCount == 0 && prim.vertexCount == 6 );

            if( attachedLight->evenOnDynamic || quad )
            {
                auto attchLightInstance = std::optional< RgLightSphericalEXT >{};

                if( quad )
                {
                    auto center = RgFloat3D{ 0, 0, 0 };
                    {
                        for( uint32_t v = 0; v < prim.vertexCount; v++ )
                        {
                            center.data[ 0 ] += prim.pVertices[ v ].position[ 0 ];
                            center.data[ 1 ] += prim.pVertices[ v ].position[ 1 ];
                            center.data[ 2 ] += prim.pVertices[ v ].position[ 2 ];
                        }
                        center.data[ 0 ] /= float( prim.vertexCount );
                        center.data[ 1 ] /= float( prim.vertexCount );
                        center.data[ 2 ] /= float( prim.vertexCount );
                    }

                    center.data[ 0 ] += mesh.transform.matrix[ 0 ][ 3 ];
                    center.data[ 1 ] += mesh.transform.matrix[ 1 ][ 3 ];
                    center.data[ 2 ] += mesh.transform.matrix[ 2 ][ 3 ];

                    attchLightInstance = RgLightSphericalEXT{
                        .sType     = RG_STRUCTURE_TYPE_LIGHT_SPHERICAL_EXT,
                        .pNext     = nullptr,
                        .color     = attachedLight->color,
                        .intensity = attachedLight->intensity * wrapconf.spritelight_mult,
                        .position  = center,
                        .radius    = std::max( wrapconf.spritelight_radius, MIN_SPHERE_RADIUS ),
                    };
                }
                else
                {
#if 0
                    GltfExporter::MakeLightsForPrimitiveDynamic( mesh,
                                                                 prim,
                                                                 sceneImportExport->GetWorldScale(),
                                                                 tempStorageInit,
                                                                 tempStorageLights );
#else
                    assert( 0 );
#endif
                }

                auto hashCombine = []< typename T >( uint64_t seed, const T& v ) {
                    seed ^= std::hash< T >{}( v ) + 0x9e3779b9 + ( seed << 6 ) + ( seed >> 2 );
                    return seed;
                };

                static const uint64_t attchSalt =
                    hashCombine( 0, std::string_view{ "attachedlight" } );

                // NOTE: can't use texture / mesh name, as texture can be
                // just 1 frame of animation sequence.. so this is more stable
                uint64_t hashBase{ attchSalt };
                hashBase = hashCombine( hashBase, mesh.uniqueObjectID );
                hashBase = hashCombine( hashBase, prim.primitiveIndexInMesh );

                uint64_t counter = 0;
#if 0
                for( AnyLightEXT& lext : attchLightInstance )
                {
                    std::visit(
                        [ & ]< typename T >( T& specific ) {
                            static_assert( detail::AreLinkable< T, RgLightInfo > );

                            RgLightInfo linfo = {
                                .sType        = RG_STRUCTURE_TYPE_LIGHT_INFO,
                                .pNext        = &specific,
                                .uniqueID     = hashCombine( hashBase, counter ),
                                .isExportable = false,
                            };

                            UploadLight( &linfo );
                        },
                        lext );

                    counter++;
                }
#else
                if( attchLightInstance )
                {
                    auto linfo = RgLightInfo{
                        .sType        = RG_STRUCTURE_TYPE_LIGHT_INFO,
                        .pNext        = &attchLightInstance.value(),
                        .uniqueID     = hashCombine( hashBase, counter ),
                        .isExportable = false,
                    };

                    RgResult r = UploadLightEx( &linfo, nullptr );
                    assert( r == RG_RESULT_SUCCESS );
                }
#endif
            }
        }



        // --------------- //
        // --------------- //

        const uint64_t primhash = std::hash< PrimitiveUniqueID >{}( PrimitiveUniqueID{
            *pMesh,
            *pPrimitive,
        } );
        if( primhash == 0 )
        {
            assert( 0 );
            return RG_RESULT_WRONG_FUNCTION_ARGUMENT;
        }

        auto inds = useindices ? std::span{ pPrimitive->pIndices,
                                            pPrimitive->pIndices + pPrimitive->indexCount }
                               : std::span< uint32_t >{};

        auto rtri = remixapi_MeshInfoSurfaceTriangles{
            .vertices_values   = rverts.data(),
            .vertices_count    = inds.empty() ? align_to_tri_lower( rverts.size() ) : rverts.size(),
            .indices_values    = inds.empty() ? nullptr : inds.data(),
            .indices_count     = inds.empty() ? 0 : align_to_tri_lower( inds.size() ),
            .skinning_hasvalue = false,
            .skinning_value    = {},
            .material          = rmaterial,
            .flags             = ( pPrimitive->flags & RG_MESH_PRIMITIVE_FORCE_EXACT_NORMALS )
                                     ? REMIXAPI_MESH_INFO_SURFACE_TRIANGLES_BIT_USE_TRIANGLE_NORMALS
                                     : 0u,
        };

        auto rinfo = remixapi_MeshInfo{
            .sType           = REMIXAPI_STRUCT_TYPE_MESH_INFO,
            .pNext           = nullptr,
            .hash            = hashcombine( HASHSPACE_MESH_DYNAMIC, primhash ),
            .surfaces_values = &rtri,
            .surfaces_count  = 1,
        };

        {
            remixapi_ErrorCode r = g_remix.CreateMesh( &rinfo, &rmesh );
            if( r != REMIXAPI_ERROR_CODE_SUCCESS )
            {
                printerror( "remixapi_CreateMesh", r );
                return RG_RESULT_INTERNAL_ERROR;
            }
            c_meshestoclear.insert( rmesh );
        }
    }

    if( !rmesh )
    {
        return RG_RESULT_INTERNAL_ERROR;
    }

    auto l_toremix_instanceflags = []( RgMeshInfoFlags src ) {
        remixapi_InstanceCategoryFlags dst = 0;
        if( src & RG_MESH_FIRST_PERSON_VIEWER )
        {
            dst |= REMIXAPI_INSTANCE_CATEGORY_BIT_THIRD_PERSON_PLAYER_MODEL;
        }
        if (src & RG_MESH_FIRST_PERSON)
        {
            dst |= REMIXAPI_INSTANCE_CATEGORY_BIT_FIRST_PERSON;
        }
        return dst;
    };

    auto rinstinfo = remixapi_InstanceInfo{
        .sType         = REMIXAPI_STRUCT_TYPE_INSTANCE_INFO,
        .pNext         = nullptr,
        .categoryFlags = l_toremix_instanceflags( pMesh->flags ),
        .mesh          = rmesh, // TODO: override material, for spectres
        .transform     = toremix( pMesh->transform ),
        .doubleSided   = true,
    };

    remixapi_ErrorCode r = g_remix.DrawInstance( &rinstinfo );
    if( r != REMIXAPI_ERROR_CODE_SUCCESS )
    {
        printerror( "remixapi_DrawInstance", r );
        return RG_RESULT_INTERNAL_ERROR;
    }
    return RG_RESULT_SUCCESS;
}

RgResult RGAPI_CALL rgUploadLensFlare( const RgLensFlareInfo* pInfo )
{
    return RG_RESULT_SUCCESS;
}

RgResult RGAPI_CALL rgSpawnFluid( const RgSpawnFluidInfo* pInfo )
{
    return RG_RESULT_SUCCESS;
}

RgResult RGAPI_CALL rgUploadCamera( const RgCameraInfo* pInfo )
{
    if( !pInfo )
    {
        return RG_RESULT_WRONG_FUNCTION_ARGUMENT;
    }

    auto rext = remixapi_CameraInfoParameterizedEXT{
        .sType         = REMIXAPI_STRUCT_TYPE_CAMERA_INFO_PARAMETERIZED_EXT,
        .pNext         = nullptr,
        .position      = toremix( pInfo->position ),
        .forward       = toremix( Utils::Cross( pInfo->up, pInfo->right ) ),
        .up            = toremix( pInfo->up ),
        .right         = toremix( pInfo->right ),
        .fovYInDegrees = Utils::RadToDeg( pInfo->fovYRadians ),
        .aspect        = pInfo->aspect,
        .nearPlane     = pInfo->cameraNear,
        .farPlane      = pInfo->cameraFar,
    };

    auto rinfo = remixapi_CameraInfo{
        .sType      = REMIXAPI_STRUCT_TYPE_CAMERA_INFO,
        .pNext      = &rext,
        .type       = REMIXAPI_CAMERA_TYPE_WORLD,
        .view       = {},
        .projection = {},
    };

    remixapi_ErrorCode r = g_remix.SetupCamera( &rinfo );
    if( r != REMIXAPI_ERROR_CODE_SUCCESS )
    {
        printerror( "remixapi_SetupCamera", r );
        return RG_RESULT_INTERNAL_ERROR;
    }

    if( auto readback = pnext::find< RgCameraInfoReadbackEXT >( pInfo ) )
    {
#if 0
        memcpy( ( void* )readback->view, &rinfo.view, sizeof( rinfo.view ) );
        memcpy( ( void* )readback->projection, &rinfo.projection, sizeof( rinfo.projection ) );
        static_assert( sizeof( readback->view ) == sizeof( rinfo.view ) );
        static_assert( sizeof( readback->projection ) == sizeof( rinfo.projection ) );
#else
        Matrix::MakeViewMatrix( ( float* )readback->view, //
                                pInfo->position,
                                pInfo->right,
                                pInfo->up );
        Matrix::MakeProjectionMatrix( ( float* )readback->projection,
                                      pInfo->aspect,
                                      pInfo->fovYRadians,
                                      pInfo->cameraNear,
                                      pInfo->cameraFar );
#endif

        Matrix::Inverse( ( float* )readback->viewInverse, readback->view );
        Matrix::Inverse( ( float* )readback->projectionInverse, readback->projection );
    }

    // duplicate for view model
    {
        rinfo.type = REMIXAPI_CAMERA_TYPE_VIEW_MODEL;

        remixapi_ErrorCode r = g_remix.SetupCamera( &rinfo );
        if( r != REMIXAPI_ERROR_CODE_SUCCESS )
        {
            printerror( "remixapi_SetupCamera ViewModel", r );
            return RG_RESULT_INTERNAL_ERROR;
        }
    }

    return RG_RESULT_SUCCESS;
}

RgResult UploadLightEx( const RgLightInfo* pInfo, const RgTransform* transform )
{
    if( !pInfo )
    {
        return RG_RESULT_WRONG_FUNCTION_ARGUMENT;
    }

    const float lightstyle = scene::calculate_lightstyle( pnext::find< RgLightAdditionalEXT >( pInfo ) );

    RgFloat3D radiance;

    union {
        remixapi_LightInfoDistantEXT distant;
        remixapi_LightInfoSphereEXT  sphere;
    } rext;

    if( auto dir = pnext::find< RgLightDirectionalEXT >( pInfo ) )
    {
        rext.distant = remixapi_LightInfoDistantEXT{
            .sType     = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DISTANT_EXT,
            .pNext     = nullptr,
            .direction = toremix( Utils::SafeNormalize( dir->direction, { 0, -1, 0 } ) ),
            .angularDiameterDegrees = dir->angularDiameterDegrees,
        };
        radiance = colorintensity_to_radiance(
            dir->color, dir->intensity * lightstyle * wrapconf.lightmult_sun );
    }
    else if( auto sph = pnext::find< RgLightSphericalEXT >( pInfo ) )
    {
        auto position = ApplyTransformToPosition( transform, sph->position );

        rext.sphere = remixapi_LightInfoSphereEXT{
            .sType            = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_SPHERE_EXT,
            .pNext            = nullptr,
            .position         = toremix( position ),
            .radius           = sph->radius,
            .shaping_hasvalue = false,
            .shaping_value    = {},
        };
        float radius = std::max( MIN_SPHERE_RADIUS, sph->radius );
        float area   = RG_PI * radius * radius;

        radiance = colorintensity_to_radiance(
            sph->color, sph->intensity / area * lightstyle * wrapconf.lightmult_sphere );
    }
    else if( auto spot = pnext::find< RgLightSpotEXT >( pInfo ) )
    {
        auto position = ApplyTransformToPosition( transform, spot->position );
        auto direction =
            ApplyTransformToDirection( transform, Utils::Normalize( spot->direction ) );

        rext.sphere = remixapi_LightInfoSphereEXT{
            .sType            = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_SPHERE_EXT,
            .pNext            = nullptr,
            .position         = toremix( position ),
            .radius           = spot->radius,
            .shaping_hasvalue = true,
            .shaping_value =
                remixapi_LightInfoLightShaping{
                    .direction        = toremix( direction ),
                    .coneAngleDegrees = Utils::RadToDeg( spot->angleOuter ),
                    .coneSoftness     = 1.f, // TODO
                    .focusExponent    = 0.f,
                },
        };
        float radius = std::max( MIN_SPHERE_RADIUS, spot->radius );
        float area   = RG_PI * radius * radius;

        radiance = colorintensity_to_radiance(
            spot->color, spot->intensity / area * lightstyle * wrapconf.lightmult_spot );
    }
    else
    {
        return RG_RESULT_WRONG_FUNCTION_ARGUMENT;
    }

    assert( pInfo->uniqueID != UINT64_MAX );

    auto rinfo = remixapi_LightInfo{
        .sType    = REMIXAPI_STRUCT_TYPE_LIGHT_INFO,
        .pNext    = &rext,
        .hash     = 1 + pInfo->uniqueID,
        .radiance = toremix( radiance ),
    };

    remixapi_LightHandle rlight{};
    {
        remixapi_ErrorCode r = g_remix.CreateLight( &rinfo, &rlight );
        if( r != REMIXAPI_ERROR_CODE_SUCCESS )
        {
            printerror( "remixapi_CreateLight", r );
            return RG_RESULT_INTERNAL_ERROR;
        }
        c_lightstoclear.insert( rlight );
    }
    {
        remixapi_ErrorCode r = g_remix.DrawLightInstance( rlight );
        if( r != REMIXAPI_ERROR_CODE_SUCCESS )
        {
            printerror( "remixapi_DrawLightInstance", r );
            return RG_RESULT_INTERNAL_ERROR;
        }
    }
    return RG_RESULT_SUCCESS;
}

RgResult RGAPI_CALL rgUploadLight( const RgLightInfo* pInfo )
{
    if( scene::static_light_exists( pInfo ) )
    {
        return RG_RESULT_SUCCESS;
    }
    return UploadLightEx( pInfo, nullptr );
}

RgResult RGAPI_CALL rgProvideOriginalTexture( const RgOriginalTextureInfo* pInfo )
{
    if( !pInfo )
    {
        return RG_RESULT_WRONG_FUNCTION_ARGUMENT;
    }
    if( !pInfo->pTextureName || pInfo->pTextureName[ 0 ] == '\0' )
    {
        return RG_RESULT_SUCCESS;
    }

    if( !textures::user_imageset_register( *pInfo ) )
    {
        return RG_RESULT_INTERNAL_ERROR;
    }

    // SHIPPING_HACK begin
    auto f = HACK_updatetextures_on_material.find( pInfo->pTextureName );
    if( f != HACK_updatetextures_on_material.end() )
    {
        if( const auto imageset = textures::find_imageset( pInfo->pTextureName ) )
        {
            for( HACK_materialprebake_t& preb : f->second )
            {
                // destroy
                {
                    g_remix.DestroyMaterial( preb.targethandle );
                    preb.targethandle = nullptr;
                }
                // relink, fix pointers
                {
                    std::visit( //
                        ext::overloaded{ [ & ]( remixapi_MaterialInfoOpaqueEXT& ext ) {
                                            ext.roughnessTexture = imageset->roughness.c_str();
                                            ext.metallicTexture  = imageset->metallic.c_str();
                                            ext.heightTexture    = imageset->height.c_str();
                                            preb.base.pNext      = &ext;
                                        },
                                         [ & ]( remixapi_MaterialInfoTranslucentEXT& ext ) {
                                             ext.transmittanceTexture = nullptr;
                                             preb.base.pNext          = &ext;
                                         } },
                        preb.ext );
                    preb.base.albedoTexture   = imageset->albedo_alpha.c_str();
                    preb.base.normalTexture   = imageset->normal.c_str();
                    preb.base.tangentTexture  = nullptr;
                    preb.base.emissiveTexture = imageset->emissive.c_str();
                    preb.base.hash            = preb.targethash;
                }
                // recreate
                remixapi_MaterialHandle rmaterial{};
                {
                    remixapi_ErrorCode r = g_remix.CreateMaterial( &preb.base, &rmaterial );
                    if( r == REMIXAPI_ERROR_CODE_SUCCESS )
                    {
                        printerror( "remixapi_CreateMaterial", r );
                        continue;
                    }
                }
                preb.targethandle = rmaterial;
            }
        }
    }
    // SHIPPING_HACK end

    return RG_RESULT_SUCCESS;
}

RgResult RGAPI_CALL rgMarkOriginalTextureAsDeleted( const char* pTextureName )
{
    if( !pTextureName || pTextureName[ 0 ] == '\0' )
    {
        return RG_RESULT_SUCCESS;
    }

    return textures::user_imageset_delete( pTextureName ) ? RG_RESULT_SUCCESS
                                                          : RG_RESULT_INTERNAL_ERROR;
}

RgResult RGAPI_CALL rgStartFrame( const RgStartFrameInfo* pInfo )
{
    if( !pInfo )
    {
        return RG_RESULT_WRONG_FUNCTION_ARGUMENT;
    }

    g_hwnd_size = calc_hwnd_size( g_hwnd );

    scene::check_new_map( safecstr( pInfo->pMapName ), nullptr );
    scene::set_lightstyles( *pInfo );
    scene::upload_static_instances();
    scene::upload_static_lights();
    scene::g_alreadyReplacedUniqueObjectIDs.clear();
    scene::g_staticSceneAnimationTime = pInfo->staticSceneAnimationTime;


    bool vsync = pInfo->vsync;
    bool reflex;

    const auto& remixparams = pnext::get< RgStartFrameRemixParams >( *pInfo );
    {
        setoption_if( "rtx.enableRayReconstruction", remixparams.rayReconstruction );
        reflex = remixparams.reflex;
    }
    const auto& resol = pnext::get< RgStartFrameRenderResolutionParams >( *pInfo );
    {
        if (resol.frameGeneration)
        {
            reflex = true;
            vsync  = false;
        }
        setoption_if( "rtx.dlfg.enable",
                      resol.frameGeneration == RG_FRAME_GENERATION_MODE_ON ? 1 : 0 );

        // UpscalerType enum
        setoption_if( "rtx.upscalerType",
                      resol.upscaleTechnique == RG_RENDER_UPSCALE_TECHNIQUE_NVIDIA_DLSS ? 1
                      : remixparams.taa                                                 ? 3
                      : remixparams.nis                                                 ? 2
                                                                                        : 0 );
        if( resol.resolutionMode != RG_RENDER_RESOLUTION_MODE_CUSTOM )
        {
            setoption_if( "rtx.qualityDLSS",
                          resol.resolutionMode == RG_RENDER_RESOLUTION_MODE_ULTRA_PERFORMANCE ? 0
                          : resol.resolutionMode == RG_RENDER_RESOLUTION_MODE_PERFORMANCE     ? 1
                          : resol.resolutionMode == RG_RENDER_RESOLUTION_MODE_BALANCED        ? 2
                          : resol.resolutionMode == RG_RENDER_RESOLUTION_MODE_QUALITY         ? 3
                          : resol.resolutionMode == RG_RENDER_RESOLUTION_MODE_NATIVE_AA       ? 5
                                                                                              : 2 );
            setoption_if( "rtx.nisPreset",
                          resol.resolutionMode == RG_RENDER_RESOLUTION_MODE_ULTRA_PERFORMANCE ? 0
                          : resol.resolutionMode == RG_RENDER_RESOLUTION_MODE_PERFORMANCE     ? 0
                          : resol.resolutionMode == RG_RENDER_RESOLUTION_MODE_BALANCED        ? 1
                          : resol.resolutionMode == RG_RENDER_RESOLUTION_MODE_QUALITY         ? 2
                          : resol.resolutionMode == RG_RENDER_RESOLUTION_MODE_NATIVE_AA       ? 3
                                                                                              : 1 );
            setoption_if( "rtx.taauPreset",
                          resol.resolutionMode == RG_RENDER_RESOLUTION_MODE_ULTRA_PERFORMANCE ? 0
                          : resol.resolutionMode == RG_RENDER_RESOLUTION_MODE_PERFORMANCE     ? 0
                          : resol.resolutionMode == RG_RENDER_RESOLUTION_MODE_BALANCED        ? 1
                          : resol.resolutionMode == RG_RENDER_RESOLUTION_MODE_QUALITY         ? 2
                          : resol.resolutionMode == RG_RENDER_RESOLUTION_MODE_NATIVE_AA       ? 3
                                                                                              : 1 );
        }

        auto l_percentage = [ & ] {
            switch( resol.resolutionMode )
            {
                case RG_RENDER_RESOLUTION_MODE_CUSTOM:
                    return std::clamp( float( resol.customRenderSize.height ) /
                                           float( std::max( 1u, g_hwnd_size.height ) ),
                                       0.0f,
                                       1.0f );
                case RG_RENDER_RESOLUTION_MODE_ULTRA_PERFORMANCE: return 0.4f;
                case RG_RENDER_RESOLUTION_MODE_PERFORMANCE: return 0.5f;
                case RG_RENDER_RESOLUTION_MODE_BALANCED: return 0.66f;
                case RG_RENDER_RESOLUTION_MODE_QUALITY: return 0.75f;
                case RG_RENDER_RESOLUTION_MODE_NATIVE_AA: return 1.0f;
                default: return 1.0f;
            }
        };
        setoption_if( "rtx.resolutionScale", l_percentage() );
    }
    setoption_if( "rtx.reflexMode", reflex ? 1 : 0 );
    setoption_if( "rtx.enableVsync", vsync ? 1 : 0 );


    return RG_RESULT_SUCCESS;
}

RgResult RGAPI_CALL rgDrawFrame( const RgDrawFrameInfo* pInfo )
{
    if( !pInfo )
    {
        return RG_RESULT_WRONG_FUNCTION_ARGUMENT;
    }

    auto modified_volume = pnext::get< RgDrawFrameVolumetricParams >( *pInfo );
    auto modified_sky    = pnext::get< RgDrawFrameSkyParams >( *pInfo );
    g_scenemeta->Modify( scene::g_currentmap, modified_volume, modified_sky );

    {
        const auto &refrrefl = pnext::get< RgDrawFrameReflectRefractParams >( *pInfo );

        g_indexOfRefractionGlass = refrrefl.indexOfRefractionGlass;
        g_indexOfRefractionWater = refrrefl.indexOfRefractionWater;
    }
    {
        const auto& texparams = pnext::get< RgDrawFrameTexturesParams >( *pInfo );
        setoption_if( "rtx.emissiveIntensity",
                      std::max( 0.0f, texparams.emissionMapBoost ) / 25 * wrapconf.emismult );
        setoption_if( "rtx.opaqueMaterial.normalIntensity", texparams.normalMapStrength );
        setoption_if( "rtx.translucentMaterial.normalIntensity", texparams.normalMapStrength );
    }
    {
        setoption_if( "rtx.skyBrightness",
                      modified_sky.skyColorMultiplier / 25 * wrapconf.skymult );
        g_skyviewerpos = modified_sky.skyViewerPosition; // there will be a one frame latency...
    }
    {
        const auto& bloom = pnext::get< RgDrawFrameBloomParams >( *pInfo );
        setoption_if( "rtx.bloom.burnIntensity", std::max( 0.0f, bloom.bloomIntensity ) );
    }

    auto rinfo = remixapi_PresentInfo{
        .sType        = REMIXAPI_STRUCT_TYPE_PRESENT_INFO,
        .pNext        = nullptr,
        .hwndOverride = nullptr,
    };

    remixapi_ErrorCode r = g_remix.Present( &rinfo );
    if( r != REMIXAPI_ERROR_CODE_SUCCESS )
    {
        printerror( "remixapi_Present", r );
        return RG_RESULT_INTERNAL_ERROR;
    }

    {
        for( const remixapi_LightHandle& h : c_lightstoclear )
        {
            r = g_remix.DestroyLight( h );
            assert( r == REMIXAPI_ERROR_CODE_SUCCESS );
        }
        c_lightstoclear.clear();

        for( const remixapi_MaterialHandle& h : c_materialstoclear )
        {
            r = g_remix.DestroyMaterial( h );
            assert( r == REMIXAPI_ERROR_CODE_SUCCESS );
        }
        c_materialstoclear.clear();

        for( const remixapi_MeshHandle& h : c_meshestoclear )
        {
            r = g_remix.DestroyMesh( h );
            assert( r == REMIXAPI_ERROR_CODE_SUCCESS );
        }
        c_meshestoclear.clear();
    }
    return RG_RESULT_SUCCESS;
}

RgPrimitiveVertex* RGAPI_CALL rgUtilScratchAllocForVertices( uint32_t vertexCount )
{
    return new RgPrimitiveVertex[ vertexCount ];
}
void RGAPI_CALL rgUtilScratchFree( const RgPrimitiveVertex* pPointer )
{
    delete[] pPointer;
}
void RGAPI_CALL rgUtilScratchGetIndices( RgUtilImScratchTopology topology,
                                         uint32_t                vertexCount,
                                         const uint32_t**        ppOutIndices,
                                         uint32_t*               pOutIndexCount )
{
    const auto indices = g_scratch.GetIndices( topology, vertexCount );
    *ppOutIndices      = indices.data();
    *pOutIndexCount    = uint32_t( indices.size() );
}
void RGAPI_CALL rgUtilImScratchClear()
{
    g_scratch.Clear();
}
void RGAPI_CALL rgUtilImScratchStart( RgUtilImScratchTopology topology )
{
    g_scratch.StartPrimitive( topology );
}
void RGAPI_CALL rgUtilImScratchEnd()
{
    g_scratch.EndPrimitive();
}
void RGAPI_CALL rgUtilImScratchVertex( float x, float y, float z )
{
    g_scratch.Vertex( x, y, z );
}
void RGAPI_CALL rgUtilImScratchNormal( float x, float y, float z )
{
    g_scratch.Normal( x, y, z );
}
void RGAPI_CALL rgUtilImScratchTexCoord( float u, float v )
{
    g_scratch.TexCoord( u, v );
}
void RGAPI_CALL rgUtilImScratchTexCoord_Layer1( float u, float v )
{
    g_scratch.TexCoord_Layer1( u, v );
}
void RGAPI_CALL rgUtilImScratchTexCoord_Layer2( float u, float v )
{
    g_scratch.TexCoord_Layer2( u, v );
}
void RGAPI_CALL rgUtilImScratchTexCoord_Layer3( float u, float v )
{
    g_scratch.TexCoord_Layer3( u, v );
}
void RGAPI_CALL rgUtilImScratchColor( RgColor4DPacked32 color )
{
    g_scratch.Color( color );
}
void RGAPI_CALL rgUtilImScratchSetToPrimitive( RgMeshPrimitiveInfo* pTarget )
{
    g_scratch.SetToPrimitive( pTarget );
}

RgBool32 RGAPI_CALL rgUtilIsUpscaleTechniqueAvailable( RgRenderUpscaleTechnique technique,
                                                       RgFrameGenerationMode    frameGeneration,
                                                       const char**             ppFailureReason )
{
    switch( technique )
    {
        case RG_RENDER_UPSCALE_TECHNIQUE_NEAREST:
        case RG_RENDER_UPSCALE_TECHNIQUE_LINEAR:
        case RG_RENDER_UPSCALE_TECHNIQUE_NVIDIA_DLSS: {
            return frameGeneration ? g_framegen_supported : true;
        }
        case RG_RENDER_UPSCALE_TECHNIQUE_AMD_FSR2: {
            if( ppFailureReason )
            {
                *ppFailureReason = "Remix doesn't support AMD FSR";
            }
            return false;
        }
        default: {
            if( ppFailureReason )
            {
                *ppFailureReason = "Invalid RgRenderUpscaleTechnique";
            }
            assert( 0 );
            return false;
        }
    }
}

RgBool32 RGAPI_CALL rgUtilDXGIAvailable( const char** ppFailureReason )
{
    if( ppFailureReason )
    {
        *ppFailureReason = "Remix controls the presentation mode";
    }
    return false;
}

RgFeatureFlags RGAPI_CALL rgUtilGetSupportedFeatures()
{
    return 0;
}

RgUtilMemoryUsage RGAPI_CALL rgUtilRequestMemoryUsage()
{
    return RgUtilMemoryUsage{ .vramUsed = 0, .vramTotal = 0 };
}

const char* RGAPI_CALL rgUtilGetResultDescription( RgResult result )
{
#define case_RG_RESULT_TO_STR( r ) \
    case r: return #r

#pragma warning( push )
#pragma warning( error : 4061 ) // switch must contain all cases
    switch( result )
    {
        case_RG_RESULT_TO_STR( RG_RESULT_SUCCESS );
        case_RG_RESULT_TO_STR( RG_RESULT_SUCCESS_FOUND_MESH );
        case_RG_RESULT_TO_STR( RG_RESULT_SUCCESS_FOUND_TEXTURE );
        case_RG_RESULT_TO_STR( RG_RESULT_CANT_FIND_DYNAMIC_LIBRARY );
        case_RG_RESULT_TO_STR( RG_RESULT_CANT_FIND_ENTRY_FUNCTION_IN_DYNAMIC_LIBRARY );
        case_RG_RESULT_TO_STR( RG_RESULT_NOT_INITIALIZED );
        case_RG_RESULT_TO_STR( RG_RESULT_ALREADY_INITIALIZED );
        case_RG_RESULT_TO_STR( RG_RESULT_GRAPHICS_API_ERROR );
        case_RG_RESULT_TO_STR( RG_RESULT_CANT_FIND_SUPPORTED_PHYSICAL_DEVICE );
        case_RG_RESULT_TO_STR( RG_RESULT_FRAME_WASNT_STARTED );
        case_RG_RESULT_TO_STR( RG_RESULT_FRAME_WASNT_ENDED );
        case_RG_RESULT_TO_STR( RG_RESULT_WRONG_FUNCTION_CALL );
        case_RG_RESULT_TO_STR( RG_RESULT_WRONG_FUNCTION_ARGUMENT );
        case_RG_RESULT_TO_STR( RG_RESULT_ERROR_CANT_FIND_HARDCODED_RESOURCES );
        case_RG_RESULT_TO_STR( RG_RESULT_ERROR_CANT_FIND_SHADER );
        case_RG_RESULT_TO_STR( RG_RESULT_INTERNAL_ERROR );
        case_RG_RESULT_TO_STR( RG_RESULT_WRONG_STRUCTURE_TYPE );
        case_RG_RESULT_TO_STR( RG_RESULT_ERROR_MEMORY_ALIGNMENT );
        case_RG_RESULT_TO_STR( RG_RESULT_ERROR_NO_VULKAN_EXTENSION );
        default: assert( 0 ); return "Unknown RgResult";
    }
#pragma warning( pop )
#undef case_RG_RESULT_TO_STR
}

RgColor4DPacked32 RGAPI_CALL rgUtilPackColorByte4D( uint8_t r, uint8_t g, uint8_t b, uint8_t a )
{
    return Utils::PackColor( r, g, b, a );
}

RgColor4DPacked32 RGAPI_CALL rgUtilPackColorFloat4D( float r, float g, float b, float a )
{
    return Utils::PackColorFromFloat( r, g, b, a );
}

RgColor4DPacked32 RGAPI_CALL rgUtilPackNormal( float x, float y, float z )
{
    return Utils::PackNormal( x, y, z );
}

void RGAPI_CALL rgUtilExportAsTGA( const void* pPixels,
                                   uint32_t    width,
                                   uint32_t    height,
                                   const char* pPath )
{
}

}



extern "C"
{
RGAPI RgResult RGCONV RGAPI_CALL rgCreateInstance( const RgInstanceCreateInfo* pInfo,
                                                   RgInterface*                pInterface )
{
    if( pInfo == nullptr || pInterface == nullptr )
    {
        return RG_RESULT_WRONG_FUNCTION_ARGUMENT;
    }

    if( g_remix.Shutdown )
    {
        return RG_RESULT_ALREADY_INITIALIZED;
    }

    wrapconf = json_parser::ReadFileAs< RemixWrapperConfig >(
        std::filesystem::path{ safecstr( pInfo->pOverrideFolderPath ) } / "RTGL1_Remix.json" );

    auto logpath = std::filesystem::path{};
    {
        std::error_code ec;
        logpath = std::filesystem::absolute( safecstr( pInfo->pOverrideFolderPath ), ec );
        logpath /= "bin_remix/";
        if( !ec )
        {
            if( !logpath.empty() )
            {
                SetEnvironmentVariableW( L"DXVK_LOG_PATH", logpath.c_str() );
                SetEnvironmentVariableW( L"DXVK_STATE_CACHE_PATH", logpath.c_str() );
            }
        }
    }

    // logger
    {
        RTGL1::debug::detail::g_printSeverity = pInfo->allowedMessages;
        RTGL1::debug::detail::g_print =
            [ pfnPrint       = pInfo->pfnPrint,
              pUserPrintData = pInfo->pUserPrintData ]( std::string_view       msg, //
                                                        RgMessageSeverityFlags severity ) {
                if( pfnPrint )
                {
                    assert( RTGL1::debug::detail::g_printSeverity & severity );
                    pfnPrint( msg.data(), severity, pUserPrintData );
                }
            };
    }

    {
        auto interf = RgInterface{
            .rgCreateInstance                  = rgCreateInstance,
            .rgDestroyInstance                 = rgDestroyInstance,
            .rgStartFrame                      = rgStartFrame,
            .rgUploadCamera                    = rgUploadCamera,
            .rgUploadMeshPrimitive             = rgUploadMeshPrimitive,
            .rgUploadLensFlare                 = rgUploadLensFlare,
            .rgUploadLight                     = rgUploadLight,
            .rgProvideOriginalTexture          = rgProvideOriginalTexture,
            .rgMarkOriginalTextureAsDeleted    = rgMarkOriginalTextureAsDeleted,
            .rgDrawFrame                       = rgDrawFrame,
            .rgUtilScratchAllocForVertices     = rgUtilScratchAllocForVertices,
            .rgUtilScratchFree                 = rgUtilScratchFree,
            .rgUtilScratchGetIndices           = rgUtilScratchGetIndices,
            .rgUtilImScratchClear              = rgUtilImScratchClear,
            .rgUtilImScratchStart              = rgUtilImScratchStart,
            .rgUtilImScratchVertex             = rgUtilImScratchVertex,
            .rgUtilImScratchNormal             = rgUtilImScratchNormal,
            .rgUtilImScratchTexCoord           = rgUtilImScratchTexCoord,
            .rgUtilImScratchTexCoord_Layer1    = rgUtilImScratchTexCoord_Layer1,
            .rgUtilImScratchTexCoord_Layer2    = rgUtilImScratchTexCoord_Layer2,
            .rgUtilImScratchTexCoord_Layer3    = rgUtilImScratchTexCoord_Layer3,
            .rgUtilImScratchColor              = rgUtilImScratchColor,
            .rgUtilImScratchEnd                = rgUtilImScratchEnd,
            .rgUtilImScratchSetToPrimitive     = rgUtilImScratchSetToPrimitive,
            .rgUtilIsUpscaleTechniqueAvailable = rgUtilIsUpscaleTechniqueAvailable,
            .rgUtilDXGIAvailable               = rgUtilDXGIAvailable,
            .rgUtilRequestMemoryUsage          = rgUtilRequestMemoryUsage,
            .rgUtilGetResultDescription        = rgUtilGetResultDescription,
            .rgUtilPackColorByte4D             = rgUtilPackColorByte4D,
            .rgUtilPackColorFloat4D            = rgUtilPackColorFloat4D,
            .rgUtilPackNormal                  = rgUtilPackNormal,
            .rgUtilExportAsTGA                 = rgUtilExportAsTGA,
            .rgUtilGetSupportedFeatures        = rgUtilGetSupportedFeatures,
            .rgSpawnFluid                      = rgSpawnFluid,
        };

        // error if DLL has less functionality, otherwise, warning
        if( pInfo->sizeOfRgInterface > sizeof( RgInterface ) )
        {
            RTGL1::debug::Error( "RTGL1.dll was compiled with sizeof(RgInterface)={}, "
                                 "but the application requires sizeof(RgInterface)={}. "
                                 "Some of the features might not work correctly",
                                 sizeof( RgInterface ),
                                 pInfo->sizeOfRgInterface );
        }
        else if( pInfo->sizeOfRgInterface < sizeof( RgInterface ) )
        {
            RTGL1::debug::Warning( "RTGL1.dll was compiled with sizeof(RgInterface)={}, "
                                   "but the application requires sizeof(RgInterface)={}",
                                   sizeof( RgInterface ),
                                   pInfo->sizeOfRgInterface );
        }

        memcpy( pInterface, &interf, std::min( sizeof( RgInterface ), pInfo->sizeOfRgInterface ) );


        {
            auto dllpath = std::filesystem::path{ safecstr( pInfo->pOverrideFolderPath ) } /
                           "bin_remix" / "d3d9.dll";

            remixapi_ErrorCode r =
                remixapi_lib_loadRemixDllAndInitialize( dllpath.c_str(), &g_remix, &g_dllremix );

            if( r != REMIXAPI_ERROR_CODE_SUCCESS )
            {
                return RG_RESULT_CANT_FIND_DYNAMIC_LIBRARY;
            }
        }
        {
            auto rxinfo = remixapi_StartupInfo{
                .sType = REMIXAPI_STRUCT_TYPE_STARTUP_INFO,
                .pNext = nullptr,
                .hwnd  = pInfo->pWin32SurfaceInfo->hwnd,
            };

            remixapi_ErrorCode r = g_remix.Startup( &rxinfo );
            if( r != REMIXAPI_ERROR_CODE_SUCCESS )
            {
                return RG_RESULT_INTERNAL_ERROR;
            }

            g_hwnd = rxinfo.hwnd;
        }

        // HACKHACK i don't have time.. TODO: supported features from remixapi 
        if( wrapconf.check_framegen_support_in_log )
        {
            if( auto file = std::ifstream{ logpath / "gzdoom_d3d9.log" } )
            {
                std::string ln;
                int         i = 0;
                while( std::getline( file, ln ) )
                {
                    if( i++ > 2000 )
                    {
                        break;
                    }
                    if( ln.contains( "Frame Generation not available" ) )
                    {
                        g_framegen_supported = false;
                        break;
                    }
                }
            }
        }
        // HACKHACK

        rgInitData( *pInfo );
    }

    return RG_RESULT_SUCCESS;
}

} // extern "C"
