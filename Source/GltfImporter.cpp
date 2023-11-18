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
#include "TextureManager.h"
#include "TextureMeta.h"
#include "Utils.h"

#include "Generated/ShaderCommonC.h"

#include "cgltf/cgltf.h"

#include <format>
#include <span>

#define NEED_TANGENT 0

namespace RTGL1
{
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

    RgTransform MakeRgTransform( const cgltf_node& node )
    {
        float mat[ 16 ];
        cgltf_node_transform_local( &node, mat );

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

    void ApplyInverseWorldTransform( cgltf_node& mainNode, const RgTransform& worldTransform )
    {
        mainNode.has_translation = false;
        mainNode.has_rotation    = false;
        mainNode.has_scale       = false;
        memset( mainNode.translation, 0, sizeof( mainNode.translation ) );
        memset( mainNode.rotation, 0, sizeof( mainNode.rotation ) );
        memset( mainNode.scale, 0, sizeof( mainNode.scale ) );

        mainNode.has_matrix = true;

        // columns
        const float gltfMatrixWorld[] = RG_TRANSFORM_TO_GLTF_MATRIX( worldTransform );

        float inv[ 16 ];
        RTGL1::Matrix::Inverse( inv, gltfMatrixWorld );

        float original[ 16 ];
        cgltf_node_transform_local( &mainNode, original );

        RTGL1::Matrix::Multiply( mainNode.matrix, inv, original );
    }

    cgltf_node* FindMainRootNode( cgltf_data* data )
    {
        if( !data || !data->scene )
        {
            return nullptr;
        }

        std::span ns( data->scene->nodes, data->scene->nodes_count );

        for( cgltf_node* n : ns )
        {
            if( RTGL1::Utils::IsCstrEmpty( n->name ) )
            {
                continue;
            }

            if( std::strcmp( n->name, RTGL1_MAIN_ROOT_NODE ) == 0 )
            {
                return n;
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

    std::vector< RgPrimitiveVertex > GatherVertices( const cgltf_primitive& prim,
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


        auto primVertices = std::vector< RgPrimitiveVertex >( *vertexCount );
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

                        primVertices[ i ].normalPacked =
                            Utils::PackNormal( n[ 0 ], n[ 1 ], n[ 2 ] );
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
                        ok &=
                            cgltf_accessor_read_float_h( attr.data, i, primVertices[ i ].texCoord );
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

    std::string MakePTextureName( const cgltf_material&              mat,
                                  std::span< std::filesystem::path > fallbacks )
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
                    debug::Warning( "Suspicious URI \"{}\" of an image with name \"{}\": "
                                    "If \"name\" field is provided, assumed that it's "
                                    "the original game texture. "
                                    "So expecting URI to start with {}. "
                                    "Texture overloading is disabled for this texture",
                                    t->image->uri,
                                    t->image->name,
                                    TEXTURES_FOLDER_JUNCTION_PREFIX );
                }
            }

            return name;
        }

        for( const auto& f : fallbacks )
        {
            if( !f.empty() )
            {
                return f.string();
            }
        }

        return "";
    }

    struct UploadTexturesResult
    {
        RgColor4DPacked32 color        = Utils::PackColor( 255, 255, 255, 255 );
        float             emissiveMult = 0.0f;
        std::string       pTextureName;
        float             metallicFactor  = 0.0f;
        float             roughnessFactor = 1.0f;
    };

    UploadTexturesResult UploadTextures( VkCommandBuffer              cmd,
                                         uint32_t                     frameIndex,
                                         const cgltf_material*        mat,
                                         TextureManager&              textureManager,
                                         bool                         isReplacement,
                                         const std::filesystem::path& gltfFolder,
                                         std::string_view             gltfPath )
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
        std::filesystem::path fullPaths[] = {
            std::filesystem::path(),
            std::filesystem::path(),
            std::filesystem::path(),
            std::filesystem::path(),
        };
        SamplerManager::Handle samplers[] = {
            SamplerManager::Handle( RG_SAMPLER_FILTER_AUTO, RG_SAMPLER_ADDRESS_MODE_REPEAT, RG_SAMPLER_ADDRESS_MODE_REPEAT ),
            SamplerManager::Handle( RG_SAMPLER_FILTER_AUTO, RG_SAMPLER_ADDRESS_MODE_REPEAT, RG_SAMPLER_ADDRESS_MODE_REPEAT ),
            SamplerManager::Handle( RG_SAMPLER_FILTER_AUTO, RG_SAMPLER_ADDRESS_MODE_REPEAT, RG_SAMPLER_ADDRESS_MODE_REPEAT ),
            SamplerManager::Handle( RG_SAMPLER_FILTER_AUTO, RG_SAMPLER_ADDRESS_MODE_REPEAT, RG_SAMPLER_ADDRESS_MODE_REPEAT ),
        };
        static_assert( std::size( fullPaths ) == TEXTURES_PER_MATERIAL_COUNT );
        static_assert( std::size( samplers ) == TEXTURES_PER_MATERIAL_COUNT );
        // clang-format on


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
                samplers[ index ] = SamplerManager::Handle(
                    makeRgSamplerFilter( txview.texture->sampler->mag_filter ),
                    makeRgSamplerAddrMode( txview.texture->sampler->wrap_s ),
                    makeRgSamplerAddrMode( txview.texture->sampler->wrap_t ) );
            }
        }


        std::string materialName = MakePTextureName( *mat, fullPaths );

        // if fullPaths are empty
        if( !materialName.empty() )
        {
            textureManager.TryCreateImportedMaterial(
                cmd, frameIndex, materialName, fullPaths, samplers, pbrSwizzling, isReplacement );
        }

        if( auto t = mat->pbr_metallic_roughness.metallic_roughness_texture.texture )
        {
            if( t->image )
            {
                if( std::abs( mat->pbr_metallic_roughness.metallic_factor - 1.0f ) > 0.01f ||
                    std::abs( mat->pbr_metallic_roughness.roughness_factor - 1.0f ) > 0.01f )
                {
                    debug::Warning(
                        "{}: Texture with image \"{}\" of material \"{}\" has "
                        "metallic / roughness factors that are not 1.0. These values are "
                        "used by RTGL1 only if surface doesn't have PBR texture",
                        gltfPath,
                        Utils::SafeCstr( t->image->uri ),
                        Utils::SafeCstr( mat->name ) );
                }
            }
        }

        return UploadTexturesResult{
            .color = Utils::PackColorFromFloat( mat->pbr_metallic_roughness.base_color_factor ),
            .emissiveMult    = Utils::Luminance( mat->emissive_factor ),
            .pTextureName    = std::move( materialName ),
            .metallicFactor  = mat->pbr_metallic_roughness.metallic_factor,
            .roughnessFactor = mat->pbr_metallic_roughness.roughness_factor,
        };
    }

    auto ParseNodeAsLight( const cgltf_node*  srcNode,
                           uint64_t           uniqueId,
                           float              oneGameUnitInMeters,
                           const RgTransform& relativeTransform ) -> std::optional< LightCopy >
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

        auto makeExtras = []( const char* extradata ) {
            return json_parser::ReadStringAs< RgLightAdditionalEXT >(
                Utils::SafeCstr( extradata ) );
        };

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
                            .sType                  = RG_STRUCTURE_TYPE_LIGHT_DIRECTIONAL_EXT,
                            .pNext                  = nullptr,
                            .color                  = packedColor,
                            .intensity              = node.light->intensity, // already in lm/m^2
                            .direction              = direction,
                            .angularDiameterDegrees = 0.5f,
                        },
                    .additional = makeExtras( node.light->extras.data ),
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
                                candelaToLuminousFlux( node.light->intensity ), // from lm/sr to lm
                            .position = position,
                            .radius   = 0.05f / oneGameUnitInMeters,
                        },
                    .additional = makeExtras( node.light->extras.data ),
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
                                candelaToLuminousFlux( node.light->intensity ), // from lm/sr to lm
                            .position   = position,
                            .direction  = direction,
                            .radius     = 0.05f / oneGameUnitInMeters,
                            .angleOuter = node.light->spot_outer_cone_angle,
                            .angleInner = node.light->spot_inner_cone_angle,
                        },
                    .additional = makeExtras( node.light->extras.data ),
                };
            }
            case cgltf_light_type_invalid:
            case cgltf_light_type_max_enum: return {};
            default: assert( 0 ); return {};
        }
    }
}
}

RTGL1::GltfImporter::GltfImporter( const std::filesystem::path& _gltfPath,
                                   const RgTransform&           _worldTransform,
                                   float                        _oneGameUnitInMeters )
    : data( nullptr )
    , gltfPath( _gltfPath.string() )
    , gltfFolder( _gltfPath.parent_path() )
    , oneGameUnitInMeters( _oneGameUnitInMeters )
{
    cgltf_result  r{ cgltf_result_success };
    cgltf_options options{};
    cgltf_data*   parsedData{ nullptr };

    struct FreeIfFail
    {
        cgltf_data** data;
        cgltf_data** parsedData;
        ~FreeIfFail()
        {
            if( *data == nullptr )
            {
                cgltf_free( *parsedData );
            }
        }
    } tmp = { &data, &parsedData };

    r = cgltf_parse_file( &options, gltfPath.c_str(), &parsedData );
    if( r == cgltf_result_file_not_found )
    {
        debug::Warning( "{}: Can't find a file, no static scene will be present", gltfPath );
        return;
    }
    else if( r != cgltf_result_success )
    {
        debug::Warning(
            "{}: cgltf_parse_file. Error code: {} {}", gltfPath, int( r ), CgltfErrorName( r ) );
        return;
    }

    r = cgltf_load_buffers( &options, parsedData, gltfPath.c_str() );
    if( r != cgltf_result_success )
    {
        debug::Warning(
            "{}: cgltf_load_buffers. Error code: {} {}. URI-s for .bin buffers might be incorrect",
            gltfPath,
            int( r ),
            CgltfErrorName( r ) );
        return;
    }

    r = cgltf_validate( parsedData );
    if( r != cgltf_result_success )
    {
        debug::Warning(
            "{}: cgltf_validate. Error code: {} {}", gltfPath, int( r ), CgltfErrorName( r ) );
        return;
    }

    if( parsedData->scenes_count == 0 )
    {
        debug::Warning( "{}: {}", gltfPath, "No scenes found" );
        return;
    }

    if( parsedData->scene == nullptr )
    {
        debug::Warning( "{}: {}", gltfPath, "No default scene, using first" );
        parsedData->scene = &parsedData->scenes[ 0 ];
    }

    cgltf_node* mainNode = FindMainRootNode( parsedData );

    if( !mainNode )
    {
        debug::Warning(
            "{}: {}", gltfPath, "No \"" RTGL1_MAIN_ROOT_NODE "\" node in the default scene" );
        return;
    }

    ApplyInverseWorldTransform( *mainNode, _worldTransform );

    data = parsedData;
}

RTGL1::GltfImporter::~GltfImporter()
{
    cgltf_free( data );
}

auto RTGL1::GltfImporter::ParseFile( VkCommandBuffer           cmdForTextures,
                                     uint32_t                  frameIndex,
                                     TextureManager&           textureManager,
                                     bool                      isReplacement,
                                     const TextureMetaManager& textureMeta ) const -> WholeModelFile
{
    cgltf_node* mainNode = FindMainRootNode( data );
    if( !mainNode )
    {
        return {};
    }

    if( mainNode->mesh || mainNode->light )
    {
        debug::Warning(
            "Main node ({}) should not have meshes / lights. {}", gltfPath, nodeName( mainNode ) );
    }


    auto hashCombine = []< typename T >( size_t seed, const T& v ) {
        return seed ^ ( std::hash< T >{}( v ) + 0x9e3779b9 + ( seed << 6 ) + ( seed >> 2 ) );
    };
    const auto fileNameHash = hashCombine( 0, gltfPath );


    auto result = WholeModelFile{};

    for( cgltf_node* srcNode : std::span{ mainNode->children, mainNode->children_count } )
    {
        if( !srcNode )
        {
            continue;
        }

        if( nodeName( srcNode ).empty() )
        {
            debug::Warning( "Ignoring a node with null name: a child of {}->{}",
                            gltfPath,
                            nodeName( mainNode ) );
            continue;
        }


        const auto srcNodeHash = hashCombine( fileNameHash, nodeName( srcNode ) );


        // global lights
        if( auto l = ParseNodeAsLight(
                srcNode, srcNodeHash, oneGameUnitInMeters, MakeRgTransform( *srcNode ) ) )
        {
            result.lights.push_back( *l );
            continue;
        }


        // make model
        if( result.models.contains( nodeName( srcNode ) ) )
        {
            debug::Warning( "Ignoring duplicates: multiple nodes with the same name: {}->{}->{}",
                            gltfPath,
                            nodeName( mainNode ),
                            nodeName( srcNode ) );
            continue;
        }

        const auto [ iter, isNew ] =
            result.models.emplace( nodeName( srcNode ),
                                   WholeModelFile::RawModelData{
                                       .uniqueObjectID = srcNodeHash,
                                       .meshTransform  = MakeRgTransform( *srcNode ),
                                       .primitives     = {},
                                       .localLights    = {},
                                   } );
        assert( isNew );
        auto& result_dstModel = iter->second;


        // mesh
        auto AppendMeshPrimitives =
            [ this, &cmdForTextures, &frameIndex, &textureManager, &isReplacement, & textureMeta ](
                std::vector< WholeModelFile::RawModelData::RawPrimitiveData >& target,
                const cgltf_node*                                              atnode,
                const RgTransform*                                             transform ) {
                if( !atnode || !atnode->mesh )
                {
                    return;
                }

                const auto primitiveExtra = json_parser::ReadStringAs< PrimitiveExtraInfo >(
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
                            v.normalPacked = Utils::PackNormal( ApplyTransformToDirection(
                                transform, Utils::UnpackNormal( v.normalPacked ) ) );
                        }
                    }


                    auto indices = GatherIndices(
                        srcPrim, gltfPath, nodeName( atnode ), nodeName( atnode->parent ) );
                    if( indices.empty() )
                    {
                        continue;
                    }


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


                    auto matinfo = UploadTextures( cmdForTextures,
                                                   frameIndex,
                                                   srcPrim.material,
                                                   textureManager,
                                                   isReplacement,
                                                   gltfFolder,
                                                   gltfPath );

                    auto dstPrim = RgMeshPrimitiveInfo{
                        .sType                = RG_STRUCTURE_TYPE_MESH_PRIMITIVE_INFO,
                        .pNext                = nullptr,
                        .flags                = dstFlags,
                        .primitiveIndexInMesh = UINT32_MAX,
                        .pVertices            = vertices.data(),
                        .vertexCount          = uint32_t( vertices.size() ),
                        .pIndices             = indices.empty() ? nullptr : indices.data(),
                        .indexCount           = uint32_t( indices.size() ),
                        .pTextureName         = matinfo.pTextureName.c_str(),
                        .textureFrame         = 0,
                        .color                = matinfo.color,
                        .emissive             = matinfo.emissiveMult,
                    };


                    auto extAttachedLight = std::optional< RgMeshPrimitiveAttachedLightEXT >{};
                    auto extPbr           = std::optional< RgMeshPrimitivePBREXT >{};

                    // use texture meta as fallback
                    {
                        textureMeta.Modify( dstPrim, extAttachedLight, extPbr, true );
                    }

                    // gltf info has a higher priority, so overwrite
                    {
                        extPbr = RgMeshPrimitivePBREXT{
                            .sType            = RG_STRUCTURE_TYPE_MESH_PRIMITIVE_PBR_EXT,
                            .pNext            = nullptr,
                            .metallicDefault  = matinfo.metallicFactor,
                            .roughnessDefault = matinfo.roughnessFactor,
                        };

                        if( primitiveExtra.isGlass )
                        {
                            dstPrim.flags |= RG_MESH_PRIMITIVE_GLASS;
                        }

                        if( primitiveExtra.isMirror )
                        {
                            dstPrim.flags |= RG_MESH_PRIMITIVE_MIRROR;
                        }

                        if( primitiveExtra.isWater )
                        {
                            dstPrim.flags |= RG_MESH_PRIMITIVE_WATER;
                        }

                        if( primitiveExtra.isSkyVisibility )
                        {
                            dstPrim.flags |= RG_MESH_PRIMITIVE_SKY_VISIBILITY;
                        }

                        if( primitiveExtra.isAcid )
                        {
                            dstPrim.flags |= RG_MESH_PRIMITIVE_ACID;
                        }

                        if( primitiveExtra.isThinMedia )
                        {
                            dstPrim.flags |= RG_MESH_PRIMITIVE_THIN_MEDIA;
                        }
                    }


                    target.push_back( WholeModelFile::RawModelData::RawPrimitiveData{
                        .vertices      = std::move( vertices ),
                        .indices       = std::move( indices ),
                        .flags         = dstPrim.flags,
                        .textureName   = Utils::SafeCstr( dstPrim.pTextureName ),
                        .color         = dstPrim.color,
                        .emissive      = dstPrim.emissive,
                        .attachedLight = extAttachedLight,
                        .pbr           = extPbr,
                        .portal        = {},
                    } );
                }
            };

        AppendMeshPrimitives( result_dstModel.primitives, srcNode, nullptr );

        ForEachChildNodeRecursively(
            [ & ]( const cgltf_node& child ) {
                const auto childHash         = hashCombine( srcNodeHash, nodeName( child ) );
                const auto relativeTransform = MakeRgTransformRelativeTo( &child, srcNode );

                // child meshes
                AppendMeshPrimitives( result_dstModel.primitives,
                                      &child,
                                      IsAlmostIdentity( relativeTransform ) ? nullptr
                                                                            : &relativeTransform );

                // local lights
                if( auto l = ParseNodeAsLight(
                        &child, childHash, oneGameUnitInMeters, relativeTransform ) )
                {
                    result_dstModel.localLights.push_back( *l );
                }
            },
            srcNode );
    }

    return result;
}

RTGL1::GltfImporter::operator bool() const
{
    return data != nullptr;
}
