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

#include "GltfImporter.h"

#include "Const.h"
#include "DrawFrameInfo.h"
#include "JsonParser.h"
#include "Matrix.h"
#include "SamplerManager.h"
#include "TextureMeta.h"
#include "Utils.h"

#include "Generated/ShaderCommonC.h"

#if RG_USE_REMIX
#define CGLTF_VALIDATE_ENABLE_ASSERTS 1
#define CGLTF_IMPLEMENTATION
#endif
#include "cgltf/cgltf.h"

#if 0
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#endif

#include <format>
#include <span>

#define NEED_TANGENT 0

namespace RTGL1
{

extern std::filesystem::path g_ovrdFolder; // hack

namespace
{
    RgTransform ColumnsToRows( const float arr[ 16 ] )
    {
#define MAT( i, j ) arr[ ( i )*4 + ( j ) ]

        assert( std::abs( MAT( 0, 3 ) ) < FLT_EPSILON );
        assert( std::abs( MAT( 1, 3 ) ) < FLT_EPSILON );
        assert( std::abs( MAT( 2, 3 ) ) < FLT_EPSILON );
        assert( std::abs( MAT( 3, 3 ) - 1.0f ) < FLT_EPSILON );

        return RgTransform{ {
            { MAT( 0, 0 ), MAT( 1, 0 ), MAT( 2, 0 ), MAT( 3, 0 ) },
            { MAT( 0, 1 ), MAT( 1, 1 ), MAT( 2, 1 ), MAT( 3, 1 ) },
            { MAT( 0, 2 ), MAT( 1, 2 ), MAT( 2, 2 ), MAT( 3, 2 ) },
        } };

#undef MAT
    }

    RgTransform MakeRgTransformGlobal( const cgltf_node& node )
    {
        float mat[ 16 ];
        cgltf_node_transform_world( &node, mat );

        return ColumnsToRows( mat );
    }

    // based on cgltf_node_transform_world
    RgTransform MakeRgTransformRelativeTo( const cgltf_node* target, const cgltf_node* relativeTo )
    {
        // clang-format off
        cgltf_float lm[ 16 ] = {
            1,0,0,0,
            0,1,0,0,
            0,0,1,0,
            0,0,0,1,
        };
        // clang-format on

        const cgltf_node* cur = target;
        while( cur )
        {
            if( cur == relativeTo )
            {
                break;
            }

            float pm[ 16 ];
            cgltf_node_transform_local( cur, pm );

            for( int i = 0; i < 4; ++i )
            {
                float l0 = lm[ i * 4 + 0 ];
                float l1 = lm[ i * 4 + 1 ];
                float l2 = lm[ i * 4 + 2 ];

                float r0 = l0 * pm[ 0 ] + l1 * pm[ 4 ] + l2 * pm[ 8 ];
                float r1 = l0 * pm[ 1 ] + l1 * pm[ 5 ] + l2 * pm[ 9 ];
                float r2 = l0 * pm[ 2 ] + l1 * pm[ 6 ] + l2 * pm[ 10 ];

                lm[ i * 4 + 0 ] = r0;
                lm[ i * 4 + 1 ] = r1;
                lm[ i * 4 + 2 ] = r2;
            }

            lm[ 12 ] += pm[ 12 ];
            lm[ 13 ] += pm[ 13 ];
            lm[ 14 ] += pm[ 14 ];

            cur = cur->parent;
        }

        return ColumnsToRows( lm );
    }

    void TransformFromGltfToWorld( std::span< cgltf_node* > nodes,
                                   const RgTransform&       worldTransform )
    {
        // apply world transform to a normalized (editor) space

        const float gltfMatrixWorld[] = RG_TRANSFORM_TO_GLTF_MATRIX( worldTransform );

        for( cgltf_node* n : nodes )
        {
            if( n )
            {
                float nonWorld[ 16 ];
                cgltf_node_transform_local( n, nonWorld );

                // overwrite matrix
                {
                    n->has_matrix = true;
                    Matrix::Multiply( n->matrix, gltfMatrixWorld, nonWorld );
                }
                // reset others
                {
                    n->has_translation = false;
                    n->has_rotation    = false;
                    n->has_scale       = false;
                    memset( n->translation, 0, sizeof( n->translation ) );
                    memset( n->rotation, 0, sizeof( n->rotation ) );
                    memset( n->scale, 0, sizeof( n->scale ) );
                }
            }
        }
    }
    
    auto Widen( const char* cstr )
    {
        size_t len = std::strlen( cstr );

        auto wstr = std::wstring{};
        wstr.resize( len );
        for( uint32_t i = 0; i < len; i++ )
        {
            wstr[ i ] = static_cast< wchar_t >( cstr[ i ] );
        }
        return wstr;
    }

    cgltf_node* FindMainRootNode( cgltf_data* data )
    {
        if( !data || !data->scene )
        {
            return nullptr;
        }

        for( cgltf_node* n : std::span{ data->scene->nodes, data->scene->nodes_count } )
        {
            if( !Utils::IsCstrEmpty( n->name ) )
            {
                if( std::strcmp( n->name, RTGL1_MAIN_ROOT_NODE ) == 0 )
                {
                    return n;
                }
            }
        }
        return nullptr;
    }

    auto nodeName( const cgltf_node& n )
    {
        if( !Utils::IsCstrEmpty( n.name ) )
        {
            return std::string_view{ n.name };
        }
        return std::string_view{};
    }

    auto nodeName( const cgltf_node* n )
    {
        return n ? nodeName( *n ) : std::string_view{};
    }

    template< typename T >
    size_t hashCombine( size_t seed, const T& v )
    {
        return seed ^ ( std::hash< T >{}( v ) + 0x9e3779b9 + ( seed << 6 ) + ( seed >> 2 ) );
    }

    bool IsAlmostIdentity( const RgTransform& tr )
    {
        auto areclose = []( float a, float b ) {
            return std::abs( a - b ) < 0.000001f;
        };

        for( int i = 0; i < 3; i++ )
        {
            for( int j = 0; j < 4; j++ )
            {
                if( !areclose( tr.matrix[ i ][ j ], i == j ? 1 : 0 ) )
                {
                    return false;
                }
            }
        }
        return true;
    }

    template< bool IsTopLevel = true, typename Func >
    void ForEachChildNodeRecursively( Func&& func, const cgltf_node* src )
    {
        if( src )
        {
            // do not process global parent
            if( !IsTopLevel )
            {
                func( *src );
            }

            for( const cgltf_node* child : std::span{ src->children, src->children_count } )
            {
                ForEachChildNodeRecursively< false >( func, child );
            }
        }
    }

    const char* CgltfErrorName( cgltf_result r )
    {
#define RTGL1_CGLTF_RESULT_NAME( x ) \
    case( x ): return "(" #x ")"; break

        switch( r )
        {
            RTGL1_CGLTF_RESULT_NAME( cgltf_result_success );
            RTGL1_CGLTF_RESULT_NAME( cgltf_result_data_too_short );
            RTGL1_CGLTF_RESULT_NAME( cgltf_result_unknown_format );
            RTGL1_CGLTF_RESULT_NAME( cgltf_result_invalid_json );
            RTGL1_CGLTF_RESULT_NAME( cgltf_result_invalid_gltf );
            RTGL1_CGLTF_RESULT_NAME( cgltf_result_invalid_options );
            RTGL1_CGLTF_RESULT_NAME( cgltf_result_file_not_found );
            RTGL1_CGLTF_RESULT_NAME( cgltf_result_io_error );
            RTGL1_CGLTF_RESULT_NAME( cgltf_result_out_of_memory );
            RTGL1_CGLTF_RESULT_NAME( cgltf_result_legacy_gltf );
            RTGL1_CGLTF_RESULT_NAME( cgltf_result_max_enum );
            default: assert( 0 ); return "";
        }

#undef RTGL1_CGLTF_RESULT_NAME
    }

    template< size_t N >
    cgltf_bool cgltf_accessor_read_float_h( const cgltf_accessor* accessor,
                                            cgltf_size            index,
                                            float ( &out )[ N ] )
    {
        return cgltf_accessor_read_float( accessor, index, out, N );
    }

#if !RG_USE_REMIX
    std::vector< RgPrimitiveVertex >
#else
    std::vector< remixapi_HardcodedVertex >
#endif
    GatherVertices( const cgltf_primitive& prim,
                                                     std::string_view       gltfPath,
                                                     std::string_view       dbgNodeName,
                                                     std::string_view       dbgParentNodeName )
    {
        std::span attrSpan( prim.attributes, prim.attributes_count );

        auto debugprintAttr = [ &dbgNodeName, &dbgParentNodeName, &gltfPath ](
                                  const cgltf_attribute& attr, std::string_view msg ) {
            debug::Warning( "Ignoring primitive of ...->{}->{}: Attribute {}: {}. {}",
                            dbgParentNodeName,
                            dbgNodeName,
                            Utils::SafeCstr( attr.name ),
                            msg,
                            gltfPath );
        };

        // check if compatible and find common attribute count
        std::optional< size_t > vertexCount;
        {
            // required
            bool position{}, normal{}, texcoord{};
#if NEED_TANGENT
            bool tangent{};
#endif

            for( const cgltf_attribute& attr : attrSpan )
            {
                if( attr.data->is_sparse )
                {
                    debugprintAttr( attr, "Sparse accessors are not supported" );
                    return {};
                }

                bool color = false;

                switch( attr.type )
                {
                    case cgltf_attribute_type_position:
                        position = true;
                        if( cgltf_num_components( attr.data->type ) != 3 )
                        {
                            debugprintAttr( attr, "Expected VEC3" );
                            return {};
                        }
                        static_assert( std::size( RgPrimitiveVertex{}.position ) == 3 );
                        break;

                    case cgltf_attribute_type_normal:
                        normal = true;
                        if( cgltf_num_components( attr.data->type ) != 3 )
                        {
                            debugprintAttr( attr, "Expected VEC3" );
                            return {};
                        }
                        static_assert( std::is_same_v< decltype( RgPrimitiveVertex{}.normalPacked ),
                                                       RgNormalPacked32 > );
                        break;

#if NEED_TANGENT
                    case cgltf_attribute_type_tangent:
                        tangent = true;
                        if( cgltf_num_components( attr.data->type ) != 4 )
                        {
                            debugprintAttr( attr, "Expected VEC4" );
                            return {};
                        }
                        static_assert( std::size( RgPrimitiveVertex{}.tangent ) == 4 );
                        break;
#endif

                    case cgltf_attribute_type_texcoord:
                        texcoord = true;
                        if( cgltf_num_components( attr.data->type ) != 2 )
                        {
                            debugprintAttr( attr, "Expected VEC2" );
                            return {};
                        }
                        static_assert( std::size( RgPrimitiveVertex{}.texCoord ) == 2 );
                        break;


                    case cgltf_attribute_type_color:
                        color = true;
                        if( cgltf_num_components( attr.data->type ) != 4 )
                        {
                            debugprintAttr( attr, "Expected VEC4" );
                            return {};
                        }
                        static_assert( std::is_same_v< decltype( RgPrimitiveVertex{}.color ),
                                                       RgColor4DPacked32 > );
                        break;

                    default: break;
                }

#if NEED_TANGENT
                if( position || normal || tangent || texcoord || color )
#else
                if( position || normal || texcoord || color )
#endif
                {
                    if( vertexCount )
                    {
                        if( vertexCount.value() != attr.data->count )
                        {
                            debugprintAttr(
                                attr,
                                std::format(
                                    "Mismatch on attributes count (expected {}, but got {})",
                                    *vertexCount,
                                    attr.data->count ) );
                            return {};
                        }
                    }
                    else
                    {
                        vertexCount = attr.data->count;
                    }
                }
            }

#if NEED_TANGENT
            if( !position || !normal || !tangent || !texcoord )
#else
            if( !position || !normal || !texcoord )
#endif
            {
                debug::Warning( "Ignoring primitive of ...->{}->{}: Not all required "
                                "attributes are present. "
                                "POSITION - {}. "
                                "NORMAL - {}. "
#if NEED_TANGENT
                                "TANGENT - {}. "
#endif
                                "TEXCOORD_0 - {}. {}",
                                dbgParentNodeName,
                                dbgNodeName,
                                position,
                                normal,
#if NEED_TANGENT
                                tangent,
#endif
                                texcoord,
                                gltfPath );
                return {};
            }
        }

        if( !vertexCount )
        {
            debug::Warning(
                "{}: Ignoring ...->{}->{}: ", gltfPath, dbgParentNodeName, dbgNodeName );
            return {};
        }


#if !RG_USE_REMIX
        auto primVertices = std::vector< RgPrimitiveVertex >( *vertexCount );
#else
        auto primVertices = std::vector< remixapi_HardcodedVertex >( *vertexCount );
        static_assert( sizeof( remixapi_HardcodedVertex::position ) ==
                       sizeof( RgPrimitiveVertex::position ) );
        static_assert( sizeof( remixapi_HardcodedVertex::texcoord ) ==
                       sizeof( RgPrimitiveVertex::texCoord ) );
#endif
        auto defaultColor = std::optional( Utils::PackColor( 255, 255, 255, 255 ) );

        for( const cgltf_attribute& attr : attrSpan )
        {
            cgltf_bool ok = true;

            switch( attr.type )
            {
                case cgltf_attribute_type_position:
                    for( size_t i = 0; i < primVertices.size(); i++ )
                    {
                        ok &=
                            cgltf_accessor_read_float_h( attr.data, i, primVertices[ i ].position );
                    }
                    break;

                case cgltf_attribute_type_normal:
                    for( size_t i = 0; i < primVertices.size(); i++ )
                    {
                        float n[ 3 ];
                        ok &= cgltf_accessor_read_float_h( attr.data, i, n );

#if !RG_USE_REMIX
                        primVertices[ i ].normalPacked =
                            Utils::PackNormal( n[ 0 ], n[ 1 ], n[ 2 ] );
#else
                        primVertices[ i ].normal[ 0 ] = n[ 0 ];
                        primVertices[ i ].normal[ 1 ] = n[ 1 ];
                        primVertices[ i ].normal[ 2 ] = n[ 2 ];
#endif
                    }
                    break;

#if NEED_TANGENT
                case cgltf_attribute_type_tangent:
                    for( size_t i = 0; i < primVertices.size(); i++ )
                    {
                        ok &=
                            cgltf_accessor_read_float_h( attr.data, i, primVertices[ i ].tangent );
                    }
                    break;
#endif

                case cgltf_attribute_type_texcoord: {
                    int texcoordIndex = attr.index;
                    for( size_t i = 0; i < primVertices.size(); i++ )
                    {
#if !RG_USE_REMIX
                        ok &=
                            cgltf_accessor_read_float_h( attr.data, i, primVertices[ i ].texCoord );
#else
                        ok &=
                            cgltf_accessor_read_float_h( attr.data, i, primVertices[ i ].texcoord );
#endif
                    }
                    break;
                }

                case cgltf_attribute_type_color:
                    defaultColor = std::nullopt;
                    for( size_t i = 0; i < primVertices.size(); i++ )
                    {
                        float c[ 4 ];
                        ok &= cgltf_accessor_read_float_h( attr.data, i, c );

                        primVertices[ i ].color =
                            Utils::PackColorFromFloat( c[ 0 ], c[ 1 ], c[ 2 ], c[ 3 ] );
                    }
                    break;

                default: break;
            }

            if( !ok )
            {
                debugprintAttr( attr, "cgltf_accessor_read_float fail" );
                return {};
            }
        }

        if( defaultColor )
        {
            for( auto& v : primVertices )
            {
                v.color = *defaultColor;
            }
        }

        return primVertices;
    }

    std::vector< uint32_t > GatherIndices( const cgltf_primitive& prim,
                                           std::string_view       gltfPath,
                                           std::string_view       dbgNodeName,
                                           std::string_view       dbgParentNodeName )
    {
        if( prim.indices->is_sparse )
        {
            debug::Warning( "Ignoring primitive of ...->{}->{}: "
                            "Indices: Sparse accessors are not supported. {}",
                            dbgParentNodeName,
                            dbgNodeName,
                            gltfPath );
            return {};
        }

        std::vector< uint32_t > primIndices( prim.indices->count );

        for( size_t k = 0; k < prim.indices->count; k++ )
        {
            uint32_t resolved;

            if( !cgltf_accessor_read_uint( prim.indices, k, &resolved, 1 ) )
            {
                debug::Warning( "Ignoring primitive of ...->{}->{}: "
                                "Indices: cgltf_accessor_read_uint fail. {}",
                                dbgParentNodeName,
                                dbgNodeName,
                                gltfPath );
                return {};
            }

            primIndices[ k ] = resolved;
        }

        return primIndices;
    }

    std::string MakePTextureName( const cgltf_material& mat )
    {
        const cgltf_texture* t = mat.pbr_metallic_roughness.base_color_texture.texture;

        if( t && t->image && t->image->name )
        {
            std::string name = t->image->name;

            if( t->image->uri )
            {
                if( !std::string_view( t->image->uri )
                         .starts_with( TEXTURES_FOLDER_JUNCTION_PREFIX ) )
                {
                    debug::Verbose( "Found gltf texture (overloading disabled): \'{}\'",
                                    t->image->uri );
                }
            }

            return name;
        }

        return {};
    }

    struct UploadTexturesResult
    {
        RgColor4DPacked32               color{ Utils::PackColor( 255, 255, 255, 255 ) };
        float                           emissiveMult{ 0.0f };
        float                           metallicFactor{ 0.0f };
        float                           roughnessFactor{ 1.0f };
        WholeModelFile::RawMaterialData toRegister{};
    };

    auto UploadTextures( const cgltf_material*        mat,
                         bool                         isReplacement,
                         const std::filesystem::path& gltfFolder,
                         std::string_view             gltfPath ) -> UploadTexturesResult
    {
        if( mat == nullptr )
        {
            return {};
        }

        if( !mat->has_pbr_metallic_roughness )
        {
            debug::Warning( "{}: Ignoring material \"{}\":"
                            "Can't find PBR Metallic-Roughness",
                            gltfPath,
                            Utils::SafeCstr( mat->name ) );
            return {};
        }

        // clang-format off
        auto fullPaths = std::array{
            std::filesystem::path{},
            std::filesystem::path{},
            std::filesystem::path{},
            std::filesystem::path{},
            std::filesystem::path{},
        };
        auto samplers = std::array{
            // defaults to overwrite
            WholeModelFile::DefaultSampler,
            WholeModelFile::DefaultSampler,
            WholeModelFile::DefaultSampler,
            WholeModelFile::DefaultSampler,
            WholeModelFile::DefaultSampler,
        };
        auto postfixes = std::array{
            TEXTURE_ALBEDO_ALPHA_POSTFIX                ,
            TEXTURE_OCCLUSION_ROUGHNESS_METALLIC_POSTFIX,
            TEXTURE_NORMAL_POSTFIX                      ,
            TEXTURE_EMISSIVE_POSTFIX                    ,
            TEXTURE_HEIGHT_POSTFIX                      ,
        };
        static_assert(
            TEXTURE_ALBEDO_ALPHA_INDEX                 == 0 &&
            TEXTURE_OCCLUSION_ROUGHNESS_METALLIC_INDEX == 1 &&
            TEXTURE_NORMAL_INDEX                       == 2 &&
            TEXTURE_EMISSIVE_INDEX                     == 3 &&
            TEXTURE_HEIGHT_INDEX                       == 4 );
        static_assert( std::size( fullPaths ) == TEXTURES_PER_MATERIAL_COUNT );
        static_assert( std::size( samplers )  == TEXTURES_PER_MATERIAL_COUNT );
        static_assert( std::size( postfixes ) == TEXTURES_PER_MATERIAL_COUNT );
        // clang-format on


        static constexpr auto nulltexview = cgltf_texture_view{};

        const std::pair< int, const cgltf_texture_view& > txds[] = {
            {
                TEXTURE_ALBEDO_ALPHA_INDEX,
                mat->pbr_metallic_roughness.base_color_texture,
            },
            {
                TEXTURE_OCCLUSION_ROUGHNESS_METALLIC_INDEX,
                mat->pbr_metallic_roughness.metallic_roughness_texture,
            },
            {
                TEXTURE_NORMAL_INDEX,
                mat->normal_texture,
            },
            {
                TEXTURE_EMISSIVE_INDEX,
                mat->emissive_texture,
            },
            {
                TEXTURE_HEIGHT_INDEX,
                nulltexview,
            },
        };
        static_assert( std::size( txds ) == TEXTURES_PER_MATERIAL_COUNT );

        RgTextureSwizzling pbrSwizzling = RG_TEXTURE_SWIZZLING_NULL_ROUGHNESS_METALLIC;
        {
            cgltf_texture* texRM = mat->pbr_metallic_roughness.metallic_roughness_texture.texture;
            cgltf_texture* texO  = mat->occlusion_texture.texture;

            if( texRM && texRM->image )
            {
                if( texO && texO->image )
                {
                    if( texRM->image == texO->image )
                    {
                        pbrSwizzling = RG_TEXTURE_SWIZZLING_OCCLUSION_ROUGHNESS_METALLIC;
                    }
                    else
                    {
                        debug::Warning( "{}: Ignoring occlusion image \"{}\" of material \"{}\": "
                                        "Occlusion should be in the Red channel of "
                                        "Metallic-Roughness image \"{}\"",
                                        gltfPath,
                                        Utils::SafeCstr( texO->image->uri ),
                                        Utils::SafeCstr( mat->name ),
                                        Utils::SafeCstr( texRM->image->uri ) );
                    }
                }
            }
            else
            {
                if( texO && texO->image )
                {
                    debug::Warning( "{}: Ignoring occlusion image \"{}\" of material \"{}\": "
                                    "Occlusion should be in the Red channel of Metallic-Roughness "
                                    "image which doesn't exist on this material",
                                    gltfPath,
                                    Utils::SafeCstr( texO->image->uri ),
                                    Utils::SafeCstr( mat->name ) );
                }
            }
        }


        // https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#_sampler_magfilter
        constexpr auto makeRgSamplerFilter = []( int magFilter ) {
            return magFilter == 9728   ? RG_SAMPLER_FILTER_NEAREST
                   : magFilter == 9729 ? RG_SAMPLER_FILTER_LINEAR
                                       : RG_SAMPLER_FILTER_AUTO;
        };
        constexpr auto makeRgSamplerAddrMode = []( int wrap ) {
            return wrap == 33071 ? RG_SAMPLER_ADDRESS_MODE_CLAMP : RG_SAMPLER_ADDRESS_MODE_REPEAT;
        };

        for( const auto& [ index, txview ] : txds )
        {
            if( !txview.texture || !txview.texture->image )
            {
                continue;
            }

            if( txview.texcoord != 0 )
            {
                debug::Warning(
                    "{}: Ignoring texture {} of material \"{}\":"
                    "Only one layer of texture coordinates supported. Found TEXCOORD_{}",
                    gltfPath,
                    Utils::SafeCstr( txview.texture->name ),
                    Utils::SafeCstr( mat->name ),
                    txview.texcoord );
                continue;
            }

            if( Utils::IsCstrEmpty( txview.texture->image->uri ) )
            {
                debug::Warning( "{}: Ignoring texture {} of material \"{}\": "
                                "Texture's image URI is empty",
                                gltfPath,
                                Utils::SafeCstr( txview.texture->name ),
                                Utils::SafeCstr( mat->name ) );
                continue;
            }

            fullPaths[ index ] = gltfFolder / Utils::SafeCstr( txview.texture->image->uri );

            if( txview.texture->sampler )
            {
                samplers[ index ] = SamplerManager::Handle{
                    makeRgSamplerFilter( txview.texture->sampler->mag_filter ),
                    makeRgSamplerAddrMode( txview.texture->sampler->wrap_s ),
                    makeRgSamplerAddrMode( txview.texture->sampler->wrap_t ),
                };
            }
        }

        if( auto t = mat->pbr_metallic_roughness.metallic_roughness_texture.texture )
        {
            if( t->image )
            {
                if( std::abs( mat->pbr_metallic_roughness.metallic_factor - 1.0f ) > 0.01f ||
                    std::abs( mat->pbr_metallic_roughness.roughness_factor - 1.0f ) > 0.01f )
                {
                    debug::Info( "{}: Texture with image \"{}\" of material \"{}\" has "
                                 "metallic / roughness factors that are not 1.0. These values are "
                                 "used by RTGL1 only if surface doesn't have PBR texture",
                                 gltfPath,
                                 Utils::SafeCstr( t->image->uri ),
                                 Utils::SafeCstr( mat->name ) );
                }
            }
        }

        auto name = MakePTextureName( *mat );

#if 1
        // SHIPPING_HACK: if original game texture referenced in gltf, 
        bool trackOriginalTexture = false;
        if( fullPaths[ TEXTURE_ALBEDO_ALPHA_INDEX ].native().find( TEXTURES_FOLDER_JUNCTION_W ) !=
            std::wstring::npos )
        {
            {
                // e.g. "rt/scenes/myscene/mat_junction/floor.tga"
                const auto &filepath = fullPaths[ TEXTURE_ALBEDO_ALPHA_INDEX ];

                // ignore everything before and including "mat_junction"
                auto relname = std::filesystem::path{};
                bool ok = false;
                for( const auto& part : filepath )
                {
                    if( ok )
                    {
                        relname /= part;
                    }
                    else if( part == TEXTURES_FOLDER_JUNCTION_W )
                    {
                        ok = true;
                    }
                }
                assert( ok );
                relname.replace_extension();

                if( !relname.empty() )
                {
                    name = relname.string();
                    std::ranges::replace( name, '\\', '/' );
                }
            }
            assert( !name.empty() );
            trackOriginalTexture = true;
            fullPaths = {};
        }
        // SHIPPING_HACK
#endif

        if( name.empty() )
        {
            // failure fallback
            name = fullPaths[ 0 ].string();
        }

        return UploadTexturesResult{
            .color = Utils::PackColorFromFloat( mat->pbr_metallic_roughness.base_color_factor ),
            .emissiveMult    = Utils::Luminance( mat->emissive_factor ),
            .metallicFactor  = mat->pbr_metallic_roughness.metallic_factor,
            .roughnessFactor = mat->pbr_metallic_roughness.roughness_factor,
            .toRegister =
                WholeModelFile::RawMaterialData{
                    .isReplacement = isReplacement,
                    .pbrSwizzling  = pbrSwizzling,
                    .pTextureName  = std::move( name ),
                    .fullPaths     = std::move( fullPaths ),
                    .samplers      = samplers,
                    .trackOriginalTexture = trackOriginalTexture,
                },
        };
    }

    auto ParseNodeAsLight( uint64_t                  fileNameHash,
                           const cgltf_node*         srcNode,
                           uint64_t                  uniqueId,
                           const RgTransform&        relativeTransform,
                           const ImportExportParams& params ) -> std::optional< LightCopy >
    {
        if( !srcNode || !srcNode->light )
        {
            return {};
        }

        if( srcNode->children_count > 0 )
        {
            debug::Warning( "Ignoring child nodes on the light: \'{}\'", srcNode->name );
        }

        const cgltf_node& node = *srcNode;

        constexpr auto candelaToLuminousFlux = []( float lumensPerSteradian ) {
            // to lumens
            return lumensPerSteradian * ( 4 * float( Utils::M_PI ) );
        };


        const auto additional = json_parser::ReadStringAs< RgLightAdditionalEXT >(
            Utils::SafeCstr( node.light->extras.data ) );

        if( additional )
        {
            if( !Utils::IsCstrEmpty( additional->hashName ) )
            {
                uniqueId = hashCombine( fileNameHash, std::string_view{ additional->hashName } );
            }
        }


        const auto position = RgFloat3D{
            relativeTransform.matrix[ 0 ][ 3 ],
            relativeTransform.matrix[ 1 ][ 3 ],
            relativeTransform.matrix[ 2 ][ 3 ],
        };

        const auto direction = RgFloat3D{
            -relativeTransform.matrix[ 0 ][ 2 ],
            -relativeTransform.matrix[ 1 ][ 2 ],
            -relativeTransform.matrix[ 2 ][ 2 ],
        };

        RgColor4DPacked32 packedColor =
            Utils::PackColorFromFloat( RG_ACCESS_VEC3( node.light->color ), 1.0f );

        switch( node.light->type )
        {
            case cgltf_light_type_directional: {
                return LightCopy{
                    .base =
                        RgLightInfo{
                            .sType        = RG_STRUCTURE_TYPE_LIGHT_INFO,
                            .pNext        = nullptr,
                            .uniqueID     = uniqueId,
                            .isExportable = true,
                        },
                    .extension =
                        RgLightDirectionalEXT{
                            .sType     = RG_STRUCTURE_TYPE_LIGHT_DIRECTIONAL_EXT,
                            .pNext     = nullptr,
                            .color     = packedColor,
                            .intensity = params.importedLightIntensityScaleDirectional *
                                         node.light->intensity, // already in lm/m^2
                            .direction              = direction,
                            .angularDiameterDegrees = 0.5f,
                        },
                    .additional = additional,
                };
            }
            case cgltf_light_type_point: {
                return LightCopy{
                    .base =
                        RgLightInfo{
                            .sType        = RG_STRUCTURE_TYPE_LIGHT_INFO,
                            .pNext        = nullptr,
                            .uniqueID     = uniqueId,
                            .isExportable = true,
                        },
                    .extension =
                        RgLightSphericalEXT{
                            .sType = RG_STRUCTURE_TYPE_LIGHT_SPHERICAL_EXT,
                            .pNext = nullptr,
                            .color = packedColor,
                            .intensity =
                                params.importedLightIntensityScaleSphere *
                                candelaToLuminousFlux( node.light->intensity ), // from lm/sr to lm
                            .position = position,
                            .radius   = 0.05f / params.oneGameUnitInMeters,
                        },
                    .additional = additional,
                };
            }
            case cgltf_light_type_spot: {
                return LightCopy{
                    .base =
                        RgLightInfo{
                            .sType        = RG_STRUCTURE_TYPE_LIGHT_INFO,
                            .pNext        = nullptr,
                            .uniqueID     = uniqueId,
                            .isExportable = true,
                        },
                    .extension =
                        RgLightSpotEXT{
                            .sType = RG_STRUCTURE_TYPE_LIGHT_SPOT_EXT,
                            .pNext = nullptr,
                            .color = packedColor,
                            .intensity =
                                params.importedLightIntensityScaleSpot *
                                candelaToLuminousFlux( node.light->intensity ), // from lm/sr to lm
                            .position   = position,
                            .direction  = direction,
                            .radius     = 0.05f / params.oneGameUnitInMeters,
                            .angleOuter = node.light->spot_outer_cone_angle,
                            .angleInner = node.light->spot_inner_cone_angle,
                        },
                    .additional = additional,
                };
            }
            case cgltf_light_type_invalid:
            case cgltf_light_type_max_enum: return {};
            default: assert( 0 ); return {};
        }
    }

    auto ParseNodeAsCamera( const cgltf_node* srcNode, const RgTransform& relativeTransform )
        -> std::optional< RgCameraInfo >
    {
        if( !srcNode || !srcNode->camera || srcNode->camera->type != cgltf_camera_type_perspective )
        {
            return {};
        }

        const cgltf_camera_perspective& src = srcNode->camera->data.perspective;

        auto getColumn = []( const RgTransform& t, int column ) {
            return RgFloat3D{
                t.matrix[ 0 ][ column ],
                t.matrix[ 1 ][ column ],
                t.matrix[ 2 ][ column ],
            };
        };

        return RgCameraInfo{
            .sType       = RG_STRUCTURE_TYPE_CAMERA_INFO,
            .pNext       = nullptr,
            .flags       = 0,
            .position    = getColumn( relativeTransform, 3 ),
            .up          = getColumn( relativeTransform, 1 ),
            .right       = getColumn( relativeTransform, 0 ),
            .fovYRadians = std::clamp( src.yfov, Utils::DegToRad( 1 ), Utils::DegToRad( 179 ) ),
            .aspect = src.has_aspect_ratio && src.aspect_ratio > 0 ? src.aspect_ratio : 16.f / 9.f,
            .cameraNear = 0.1f,
            .cameraFar  = 1000.f,
        };
    }

    template< typename T >
    auto MakeAnimationChannel( cgltf_interpolation_type    interp,
                               const std::vector< float >& timepoints,
                               const std::vector< T >&     values ) -> AnimationChannel< T >
    {
        static auto l_getinterp = []( cgltf_interpolation_type interp ) -> AnimationInterpolation {
            switch( interp )
            {
                case cgltf_interpolation_type_linear: return ANIMATION_INTERPOLATION_LINEAR;
                case cgltf_interpolation_type_step: return ANIMATION_INTERPOLATION_STEP;
                case cgltf_interpolation_type_cubic_spline: return ANIMATION_INTERPOLATION_CUBIC;
                default: assert( 0 ); return ANIMATION_INTERPOLATION_LINEAR;
            }
        };

        if( timepoints.size() != values.size() )
        {
            debug::Warning( "gltf animation channel has {} time keys, but {} values",
                            timepoints.size(),
                            values.size() );
            return {};
        }
        auto frames = std::vector< AnimationFrame< T > >{};
        {
            frames.resize( timepoints.size() );
            for( size_t fr = 0; fr < timepoints.size(); fr++ )
            {
                frames[ fr ] = AnimationFrame< T >{
                    .value         = values[ fr ],
                    .seconds       = timepoints[ fr ],
                    .interpolation = l_getinterp( interp ),
                };
            }
        }
        return AnimationChannel< T >{
            .frames = std::move( frames ),
        };
    }

    AnimationData ParseNodeAnim( const cgltf_data* data, const cgltf_node* targetnode )
    {
        if( !data || !targetnode )
        {
            return {};
        }

        AnimationData result{};

        for( size_t a = 0; a < data->animations_count; a++ )
        {
            const cgltf_animation& anim = data->animations[ a ];
            {
                bool hasthisnode = false;
                for( size_t c = 0; c < anim.channels_count; c++ )
                {
                    if( targetnode == anim.channels[ c ].target_node && anim.channels[ c ].sampler )
                    {
                        hasthisnode = true;
                        break;
                    }
                }
                if( !hasthisnode )
                {
                    continue;
                }
            }

            for( size_t c = 0; c < anim.channels_count; c++ )
            {
                const cgltf_animation_channel& chan = anim.channels[ c ];

                if( !chan.target_node || !chan.sampler )
                {
                    continue;
                }

                const cgltf_animation_sampler& samp = *chan.sampler;
                if( !samp.input || !samp.output )
                {
                    assert( 0 );
                    continue;
                }

                if( samp.input->count == 0 || samp.output->count == 0 || //
                    samp.input->count != samp.output->count )
                {
                    debug::Warning(
                        "Input/output samplers in gltf animation must have same count" );
                    assert( 0 );
                    continue;
                }

                if( samp.input->component_type != cgltf_component_type_r_32f ||
                    samp.input->type != cgltf_type_scalar || samp.input->is_sparse ||
                    !samp.input->buffer_view )
                {
                    assert( 0 );
                    continue;
                }
                if( chan.target_path == cgltf_animation_path_type_translation )
                {
                    if( samp.output->component_type != cgltf_component_type_r_32f ||
                        samp.output->type != cgltf_type_vec3 || samp.output->is_sparse ||
                        !samp.output->buffer_view )
                    {
                        debug::Warning( "Expected Vector3 for position in gltf animation" );
                        assert( 0 );
                        continue;
                    }
                }
                if( chan.target_path == cgltf_animation_path_type_rotation )
                {
                    if( samp.output->component_type != cgltf_component_type_r_32f ||
                        samp.output->type != cgltf_type_vec4 || samp.output->is_sparse ||
                        !samp.output->buffer_view )
                    {
                        debug::Warning( "Expected quaternion for rotation in gltf animation" );
                        assert( 0 );
                        continue;
                    }
                }

                if( targetnode != chan.target_node )
                {
                    continue;
                }

                if( chan.target_path != cgltf_animation_path_type_translation &&
                    chan.target_path != cgltf_animation_path_type_rotation )
                {
                    continue;
                }

                size_t framecount = samp.input->count;

                std::vector< float > timekeys{};
                {
                    timekeys.resize( framecount );

                    auto r = cgltf_accessor_unpack_floats(
                        samp.input, timekeys.data(), timekeys.size() );
                    if( !r )
                    {
                        assert( 0 );
                        continue;
                    }
                }

                std::vector< RgFloat3D > positions{};
                if( chan.target_path == cgltf_animation_path_type_translation )
                {
                    positions.resize( framecount );

                    static_assert( sizeof( RgFloat3D ) / sizeof( float ) == 3 );
                    auto r = cgltf_accessor_unpack_floats(
                        samp.output,
                        reinterpret_cast< float* >( positions.data() ),
                        3 * positions.size() );
                    if( !r )
                    {
                        assert( 0 );
                        continue;
                    }
                }

                std::vector< RgQuaternion > quaternions{};
                if( chan.target_path == cgltf_animation_path_type_rotation )
                {
                    quaternions.resize( framecount );

                    static_assert( sizeof( RgQuaternion ) / sizeof( float ) == 4 );
                    auto r = cgltf_accessor_unpack_floats(
                        samp.output,
                        reinterpret_cast< float* >( quaternions.data() ),
                        4 * quaternions.size() );
                    if( !r )
                    {
                        assert( 0 );
                        continue;
                    }
                }

                // check t[N] <= t[N+1] fot interpolation
                for( size_t fr = 0; fr < framecount - 1; fr++ )
                {
                    if( timekeys[ fr ] > timekeys[ fr + 1 ] )
                    {
                        debug::Warning( "Time keys are not sorted, expect "
                                        "incorrect gltf animation interpolation" );
                        assert( 0 );
                    }
                }

                if( !positions.empty() )
                {
                    result.position = MakeAnimationChannel( samp.interpolation, //
                                                            timekeys,
                                                            positions );
                }
                if( !quaternions.empty() )
                {
                    result.quaternion = MakeAnimationChannel( samp.interpolation, //
                                                              timekeys,
                                                              quaternions );
                }
            }
        }

        return result;
    }
}
}

RTGL1::GltfImporter::GltfImporter( const std::filesystem::path& _gltfPath,
                                   const ImportExportParams&    _params,
                                   const TextureMetaManager&    _textureMeta,
                                   bool                         _isReplacement )
    : gltfPath{ _gltfPath.string() }
    , gltfFolder{ _gltfPath.parent_path() }
    , params{ _params }
    , parsedModel{}
    , isParsed{ false }
{
    cgltf_result  r{ cgltf_result_success };
    cgltf_options options{};
    cgltf_data*   parsedData{ nullptr };

    struct FreeIfFail
    {
        cgltf_data** parsedData;
        ~FreeIfFail()
        {
            if( *parsedData == nullptr )
            {
                cgltf_free( *parsedData );
            }
        }
    } tmp = { &parsedData };

    r = cgltf_parse_file( &options, gltfPath.c_str(), &parsedData );
    if( r == cgltf_result_file_not_found )
    {
        debug::Warning( "Can't find a file, no static scene will be present: {}", gltfPath );
        return;
    }
    else if( r != cgltf_result_success )
    {
        debug::Warning( "cgltf_parse_file error {}: {}", CgltfErrorName( r ), gltfPath );
        return;
    }

    r = cgltf_load_buffers( &options, parsedData, gltfPath.c_str() );
    if( r != cgltf_result_success )
    {
        debug::Warning(
            "cgltf_load_buffers error {} (URI-s for .bin buffers might be incorrect): {}",
            CgltfErrorName( r ),
            gltfPath );
        return;
    }

    r = cgltf_validate( parsedData );
    if( r != cgltf_result_success )
    {
        debug::Warning( "cgltf_validate error {}: {}", CgltfErrorName( r ), gltfPath );
        return;
    }

    if( parsedData->scenes_count == 0 )
    {
        debug::Warning( "Ignoring gltf: No scenes found: {}", gltfPath );
        return;
    }

    if( parsedData->scene == nullptr )
    {
        debug::Warning( "No default scene, using first: {}", gltfPath );
        parsedData->scene = &parsedData->scenes[ 0 ];
    }

    cgltf_node* mainNode = FindMainRootNode( parsedData );

    if( !mainNode )
    {
        debug::Warning( "No \'" RTGL1_MAIN_ROOT_NODE "\' node in the default scene: ", gltfPath );
        return;
    }

    TransformFromGltfToWorld( std::span{ &mainNode, 1 }, params.worldTransform );
    
    ParseFile( parsedData, _isReplacement, _textureMeta );
}

void RTGL1::GltfImporter::ParseFile( cgltf_data*               data,
                                     bool                      isReplacement,
                                     const TextureMetaManager& textureMeta )
{
    assert( data && data->scene );

    cgltf_node* mainNode = FindMainRootNode( data );
    if( !mainNode )
    {
        return;
    }

    for( cgltf_node* n : std::span{ data->scene->nodes, data->scene->nodes_count } )
    {
        if( !Utils::IsCstrEmpty( n->name ) && std::strcmp( n->name, RTGL1_MAIN_ROOT_NODE ) != 0 )
        {
            debug::Warning( "Ignoring top-level node \'{}\'. {}", n->name, gltfPath );
        }
    }

    if( mainNode->mesh || mainNode->light )
    {
        debug::Warning( "Main node (\'{}\') should not have meshes / lights, ignoring them. {}",
                        nodeName( mainNode ),
                        gltfPath );
    }

    const auto fileNameHash = hashCombine( 0, gltfPath );

    assert( parsedModel.models.empty() && parsedModel.lights.empty() &&
            parsedModel.materials.empty() );
    WholeModelFile& result = parsedModel;

    const cgltf_node* anim_camnode = nullptr;


    for( cgltf_node* srcNode : std::span{ mainNode->children, mainNode->children_count } )
    {
        if( !srcNode )
        {
            continue;
        }

        if( nodeName( srcNode ).empty() )
        {
            debug::Warning( "Ignoring a node with null name: a child of node \'{}\'. {}",
                            nodeName( mainNode ),
                            gltfPath );
            continue;
        }


        const auto srcNodeHash            = hashCombine( fileNameHash, nodeName( srcNode ) );
        const auto srcNodeGlobalTransform = MakeRgTransformGlobal( *srcNode );


        // camera
        if( auto c = ParseNodeAsCamera( srcNode, srcNodeGlobalTransform ) )
        {
            if( !result.camera )
            {
                result.camera = c;
                anim_camnode  = srcNode;
            }
            else
            {
                debug::Warning( "Found multiple cameras, using only one. Ignoring: \'{}\'. {}",
                                nodeName( srcNode ),
                                gltfPath );
            }
        }


        // global lights
        if( auto l = ParseNodeAsLight(
                fileNameHash, srcNode, srcNodeHash, srcNodeGlobalTransform, params ) )
        {
            result.lights.push_back( *l );
            continue;
        }


        // make model
        if( result.models.contains( nodeName( srcNode ) ) )
        {
            debug::Warning( "Ignoring duplicates: multiple nodes with the same name: "
                            "\'{}\'->\'{}\'. {}",
                            nodeName( srcNode->parent ),
                            nodeName( srcNode ),
                            gltfPath );
            continue;
        }

        const auto [ iter, isNew ] =
            result.models.emplace( nodeName( srcNode ),
                                   WholeModelFile::RawModelData{
                                       .uniqueObjectID = srcNodeHash,
                                       .meshTransform  = srcNodeGlobalTransform,
                                       .primitives     = {},
                                       .localLights    = {},
                                       .animobj        = ParseNodeAnim( data, srcNode ),
                                   } );
        assert( isNew );
        auto& result_dstModel = iter->second;


        // mesh
        auto AppendMeshPrimitives =
            [ this, &isReplacement, &textureMeta ](
                std::vector< WholeModelFile::RawPrimitiveData >& target,
                std::vector< WholeModelFile::RawMaterialData >&  targetMaterials,
                const cgltf_node*                                atnode,
                const RgTransform*                               transform ) {
                if( !atnode || !atnode->mesh )
                {
                    return;
                }

                const auto primitiveExtra_node = json_parser::ReadStringAs< PrimitiveExtraInfo >(
                    Utils::SafeCstr( atnode->extras.data ) );

                // primitives
                for( uint32_t i = 0; i < atnode->mesh->primitives_count; i++ )
                {
                    const cgltf_primitive& srcPrim = atnode->mesh->primitives[ i ];


                    auto vertices = GatherVertices(
                        srcPrim, gltfPath, nodeName( atnode ), nodeName( atnode->parent ) );
                    if( vertices.empty() )
                    {
                        continue;
                    }
                    if( transform )
                    {
                        for( auto& v : vertices )
                        {
                            ApplyTransformToPosition( transform, v.position );
#if !RG_USE_REMIX
                            v.normalPacked = Utils::PackNormal( ApplyTransformToDirection(
                                transform, Utils::UnpackNormal( v.normalPacked ) ) );
#else
                            ApplyTransformToDirection( transform, v.normal );
#endif
                        }
                    }


                    auto indices = GatherIndices(
                        srcPrim, gltfPath, nodeName( atnode ), nodeName( atnode->parent ) );
                    if( indices.empty() )
                    {
                        continue;
                    }


                    const auto primitiveExtra_prim =
                        json_parser::ReadStringAs< PrimitiveExtraInfo >(
                            Utils::SafeCstr( srcPrim.extras.data ) );


                    RgMeshPrimitiveFlags dstFlags = 0;

                    if( srcPrim.material )
                    {
                        if( srcPrim.material->alpha_mode == cgltf_alpha_mode_mask )
                        {
                            dstFlags |= RG_MESH_PRIMITIVE_ALPHA_TESTED;
                        }
                        else if( srcPrim.material->alpha_mode == cgltf_alpha_mode_blend )
                        {
                            debug::Warning(
                                "Ignoring primitive of ...->{}->{}: Found blend material, "
                                "so it requires to be uploaded each frame, and not once on load. "
                                "{}",
                                nodeName( atnode->parent ),
                                nodeName( atnode ),
                                gltfPath );
                            continue;
                            dstFlags |= RG_MESH_PRIMITIVE_TRANSLUCENT;
                        }
                    }


                    auto matinfo = UploadTextures( srcPrim.material, //
                                                   isReplacement,
                                                   gltfFolder,
                                                   gltfPath );

                    // dummy to get flags, color, texture
                    auto dummy = RgMeshPrimitiveInfo{
                        .sType        = RG_STRUCTURE_TYPE_MESH_PRIMITIVE_INFO,
                        .flags        = dstFlags,
                        .pTextureName = matinfo.toRegister.pTextureName.c_str(),
                        .color        = matinfo.color,
                        .emissive     = matinfo.emissiveMult,
                    };


                    auto extAttachedLight = std::optional< RgMeshPrimitiveAttachedLightEXT >{};
                    auto extPbr           = std::optional< RgMeshPrimitivePBREXT >{};

                    // use texture meta as fallback
                    {
                        textureMeta.Modify( dummy, extAttachedLight, extPbr, true );
                    }

                    // gltf info has a higher priority, so overwrite
                    {
                        extPbr = RgMeshPrimitivePBREXT{
                            .sType            = RG_STRUCTURE_TYPE_MESH_PRIMITIVE_PBR_EXT,
                            .pNext            = nullptr,
                            .metallicDefault  = matinfo.metallicFactor,
                            .roughnessDefault = matinfo.roughnessFactor,
                        };

                        if( primitiveExtra_node.isGlass || primitiveExtra_prim.isGlass )
                        {
                            dummy.flags |= RG_MESH_PRIMITIVE_GLASS;
                        }

                        if( primitiveExtra_node.isMirror || primitiveExtra_prim.isMirror )
                        {
                            dummy.flags |= RG_MESH_PRIMITIVE_MIRROR;
                        }

                        if( primitiveExtra_node.isWater || primitiveExtra_prim.isWater )
                        {
                            dummy.flags |= RG_MESH_PRIMITIVE_WATER;
                        }

                        if( primitiveExtra_node.isSkyVisibility || primitiveExtra_prim.isSkyVisibility )
                        {
                            dummy.flags |= RG_MESH_PRIMITIVE_SKY_VISIBILITY;
                        }

                        if( primitiveExtra_node.isAcid || primitiveExtra_prim.isAcid )
                        {
                            dummy.flags |= RG_MESH_PRIMITIVE_ACID;
                        }

                        if( primitiveExtra_node.isThinMedia || primitiveExtra_prim.isThinMedia )
                        {
                            dummy.flags |= RG_MESH_PRIMITIVE_THIN_MEDIA;
                        }

                        if( primitiveExtra_node.noShadow || primitiveExtra_prim.noShadow )
                        {
                            dummy.flags |= RG_MESH_PRIMITIVE_NO_SHADOW;
                        }
                    }


                    target.push_back( WholeModelFile::RawPrimitiveData{
                        .vertices      = std::move( vertices ),
                        .indices       = std::move( indices ),
                        .flags         = dummy.flags,
                        .textureName   = Utils::SafeCstr( dummy.pTextureName ),
                        .color         = dummy.color,
                        .emissive      = dummy.emissive,
                        .attachedLight = extAttachedLight,
                        .pbr           = extPbr,
                        .portal        = {},
                    } );
                    targetMaterials.push_back( std::move( matinfo.toRegister ) );
                }
            };

        AppendMeshPrimitives( result_dstModel.primitives, //
                              result.materials,
                              srcNode,
                              nullptr );

        ForEachChildNodeRecursively(
            [ & ]( const cgltf_node& child ) {
                const auto childHash         = hashCombine( srcNodeHash, nodeName( child ) );
                const auto relativeTransform = MakeRgTransformRelativeTo( &child, srcNode );

                // child meshes
                AppendMeshPrimitives( result_dstModel.primitives,
                                      result.materials,
                                      &child,
                                      IsAlmostIdentity( relativeTransform ) ? nullptr
                                                                            : &relativeTransform );

                // local lights
                if( auto l = ParseNodeAsLight(
                        fileNameHash, &child, childHash, relativeTransform, params ) )
                {
                    result_dstModel.localLights.push_back( *l );
                }
            },
            srcNode );
    }


    if( anim_camnode )
    {
        result.animcamera = ParseNodeAnim( data, anim_camnode );

        if( anim_camnode )
        {
            auto l_frame24totime = []( int frame_24fps ) -> float {
                return float( frame_24fps ) / 24.0f;
            };

            const char* animExtraStr =
                !Utils::IsCstrEmpty( anim_camnode->extras.data ) ? anim_camnode->extras.data
                : anim_camnode->camera && !Utils::IsCstrEmpty( anim_camnode->camera->extras.data )
                    ? anim_camnode->camera->extras.data
                    : nullptr;

            if( !Utils::IsCstrEmpty( animExtraStr ) )
            {
                auto animExtra = json_parser::ReadStringAs< CameraExtraInfo >( animExtraStr );


                auto fovYRadians_channel = AnimationChannel< float >{};
                for( const CameraExtraInfo::FovAnimFrame& fv : animExtra.anim_fov_24fps )
                {
                    fovYRadians_channel.frames.push_back( AnimationFrame< float >{
                        .value         = Utils::DegToRad( fv.fovDegrees ),
                        .seconds       = l_frame24totime( fv.frame24 ),
                        .interpolation = ANIMATION_INTERPOLATION_LINEAR,
                    } );
                }
                // sort
                {
                    auto l_sortf = []( const AnimationFrame< float >& a,
                                       const AnimationFrame< float >& b ) {
                        return a.seconds < b.seconds;
                    };
                    std::ranges::sort( fovYRadians_channel.frames, l_sortf );
                }
                assert( result.animcamera.fovYRadians.frames.empty() );
                result.animcamera.fovYRadians = std::move( fovYRadians_channel );


                for( int cut_frame24 : animExtra.anim_cuts_24fps )
                {
                    static auto l_insert_cutframe =
                        []< typename T >( AnimationChannel< T >& dst_channel, float cut_timekey ) {
                            static auto l_findfr = []( const AnimationFrame< T >& fr,
                                                       float                      timekey ) {
                                return fr.seconds < timekey;
                            };

                            // find a frame that is '>= cut_timekey'
                            auto kv = std::lower_bound( dst_channel.frames.begin(),
                                                        dst_channel.frames.end(),
                                                        cut_timekey,
                                                        l_findfr );
                            if( kv == dst_channel.frames.end() )
                            {
                                return;
                            }

                            // make a cut frame with a value from the actual frame
                            auto vcut = AnimationFrame< T >{
                                .value         = kv->value,
                                .seconds       = cut_timekey,
                                .interpolation = ANIMATION_INTERPOLATION_STEP,
                            };

                            // insert a cut frame just before the actual frame
                            dst_channel.frames.insert( kv, vcut );
                        };

                    const float cutframe_timekey = l_frame24totime( cut_frame24 );

                    l_insert_cutframe( result.animcamera.position, cutframe_timekey );
                    l_insert_cutframe( result.animcamera.quaternion, cutframe_timekey );
                    l_insert_cutframe( result.animcamera.fovYRadians, cutframe_timekey );
#ifndef NDEBUG
                    static_assert( sizeof( AnimationData ) == 96,
                                   "if adding a new AnimationChannel, add it also here" );
#endif
                }
            }
        }
    }


    isParsed = true;
}
