// Copyright (c) 2021 Sultim Tsyrendashiev
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

#include "GeomInfoManager.h"

#include <algorithm>

#include "DrawFrameInfo.h"
#include "Matrix.h"
#include "VertexCollectorFilterType.h"
#include "Generated/ShaderCommonC.h"
#include "CmdLabel.h"
#include "Utils.h"

#include <ranges>

static_assert( sizeof( RTGL1::ShGeometryInstance ) % 16 == 0,
               "Std430 structs must be aligned by 16 bytes" );

namespace
{

constexpr auto MatchPrevInvalidValue = RTGL1::GeomInfoManager::MatchPrevIndexType{ -1 };

#if !SUPPRESS_TEXLAYERS
uint32_t GetMaterialBlendFlags( const RgTextureLayerBlendType* blend, uint32_t layerIndex )
{
    if( blend == nullptr )
    {
        return 0;
    }
    assert( layerIndex < GEOM_INST_FLAG_BLENDING_LAYER_COUNT );

    uint32_t bitOffset = MATERIAL_BLENDING_TYPE_BIT_COUNT * layerIndex;

    switch( *blend )
    {
        case RG_TEXTURE_LAYER_BLEND_TYPE_OPAQUE: return MATERIAL_BLENDING_TYPE_OPAQUE << bitOffset;
        case RG_TEXTURE_LAYER_BLEND_TYPE_ALPHA: return MATERIAL_BLENDING_TYPE_ALPHA << bitOffset;
        case RG_TEXTURE_LAYER_BLEND_TYPE_ADD: return MATERIAL_BLENDING_TYPE_ADD << bitOffset;
        case RG_TEXTURE_LAYER_BLEND_TYPE_SHADE: return MATERIAL_BLENDING_TYPE_SHADE << bitOffset;
        default: assert( 0 ); return 0;
    }
}

uint32_t GetMaterialBlendFlags( const RgMeshPrimitiveTextureLayersEXT& info, uint32_t layerIndex )
{
    assert( layerIndex <= 3 );

    switch( layerIndex )
    {
        case 0: return RG_TEXTURE_LAYER_BLEND_TYPE_OPAQUE;
        case 1: return GetMaterialBlendFlags( info.pLayer1 ? &info.pLayer1->blend : nullptr, 1 );
        case 2: return GetMaterialBlendFlags( info.pLayer2 ? &info.pLayer1->blend : nullptr, 2 );
        case 3: return GetMaterialBlendFlags( info.pLayer3 ? &info.pLayer1->blend : nullptr, 3 );
        default: assert( 0 ); return 0;
    }
}
#endif

}

bool RTGL1::GeomInfoManager::LayerExists( const RgMeshPrimitiveInfo& info, uint32_t layerIndex )
{
#if !SUPPRESS_TEXLAYERS
    if( auto layers = pnext::find< RgMeshPrimitiveTextureLayersEXT >( &info ) )
    {
        switch( layerIndex )
        {
            case 1: return layers->pLayer1 && layers->pLayer1->pTexCoord;
            case 2: return layers->pLayer2 && layers->pLayer2->pTexCoord;
            case 3: return layers->pLayer3 && layers->pLayer3->pTexCoord;
            default: assert( 0 ); return false;
        }
    }
#endif

    return false;
}

const RgFloat2D* RTGL1::GeomInfoManager::AccessLayerTexCoords( const RgMeshPrimitiveInfo& info,
                                                               uint32_t layerIndex )
{
#if !SUPPRESS_TEXLAYERS
    if( auto layers = pnext::find< RgMeshPrimitiveTextureLayersEXT >( &info ) )
    {
        switch( layerIndex )
        {
            case 1: return layers->pLayer1 ? layers->pLayer1->pTexCoord : nullptr;
            case 2: return layers->pLayer2 ? layers->pLayer2->pTexCoord : nullptr;
            case 3: return layers->pLayer3 ? layers->pLayer3->pTexCoord : nullptr;

            default: assert( 0 ); return nullptr;
        }
    }
#endif

    return nullptr;
}

uint32_t RTGL1::GeomInfoManager::GetPrimitiveFlags( const RgMeshInfo*          mesh,
                                                    const RgMeshPrimitiveInfo& info,
                                                    bool                       isDynamicVertexData )
{
    uint32_t f = 0;


#if !SUPPRESS_TEXLAYERS
    if( auto layers = pnext::find< RgMeshPrimitiveTextureLayersEXT >( &info ) )
    {
        f |= LayerExists( info, 1 ) ? GEOM_INST_FLAG_EXISTS_LAYER1 : 0;
        f |= LayerExists( info, 2 ) ? GEOM_INST_FLAG_EXISTS_LAYER2 : 0;
        f |= LayerExists( info, 3 ) ? GEOM_INST_FLAG_EXISTS_LAYER3 : 0;

        f |= GetMaterialBlendFlags( *layers, 0 );
        f |= GetMaterialBlendFlags( *layers, 1 );
        f |= GetMaterialBlendFlags( *layers, 2 );
        f |= GetMaterialBlendFlags( *layers, 3 );
    }
#endif

    if( ( info.flags & RG_MESH_PRIMITIVE_MIRROR ) ||
        ( mesh && ( mesh->flags & RG_MESH_FORCE_MIRROR ) ) )
    {
        f |= GEOM_INST_FLAG_REFLECT;
    }

    if( ( info.flags & RG_MESH_PRIMITIVE_WATER ) ||
        ( mesh && ( mesh->flags & RG_MESH_FORCE_WATER ) ) )
    {
        f |= GEOM_INST_FLAG_MEDIA_TYPE_WATER;
        f |= GEOM_INST_FLAG_REFLECT;
        f |= GEOM_INST_FLAG_REFRACT;
    }

    if( info.flags & RG_MESH_PRIMITIVE_ACID )
    {
        f |= GEOM_INST_FLAG_MEDIA_TYPE_ACID;
        f |= GEOM_INST_FLAG_REFLECT;
        f |= GEOM_INST_FLAG_REFRACT;
    }

    if( ( info.flags & RG_MESH_PRIMITIVE_GLASS ) ||
        ( mesh && ( mesh->flags & RG_MESH_FORCE_GLASS ) ) )
    {
        f |= GEOM_INST_FLAG_MEDIA_TYPE_GLASS;
        f |= GEOM_INST_FLAG_REFLECT;
        f |= GEOM_INST_FLAG_REFRACT;
    }

    if( info.flags & RG_MESH_PRIMITIVE_GLASS_IF_SMOOTH )
    {
        f |= GEOM_INST_FLAG_GLASS_IF_SMOOTH;
    }

    if( info.flags & RG_MESH_PRIMITIVE_MIRROR_IF_SMOOTH )
    {
        f |= GEOM_INST_FLAG_MIRROR_IF_SMOOTH;
    }

    if( ( info.flags & RG_MESH_PRIMITIVE_IGNORE_REFRACT_AFTER ) ||
        ( mesh && ( mesh->flags & RG_MESH_FORCE_IGNORE_REFRACT_AFTER ) ) )
    {
        f |= GEOM_INST_FLAG_IGNORE_REFRACT_AFTER;
    }

    if( !( info.flags & RG_MESH_PRIMITIVE_DONT_GENERATE_NORMALS ) )
    {
        f |= GEOM_INST_FLAG_GENERATE_NORMALS;
    }

    if( info.flags & RG_MESH_PRIMITIVE_FORCE_EXACT_NORMALS )
    {
        f |= GEOM_INST_FLAG_EXACT_NORMALS;
    }

    if( info.flags & RG_MESH_PRIMITIVE_THIN_MEDIA )
    {
        f |= GEOM_INST_FLAG_THIN_MEDIA;
    }

    if( isDynamicVertexData )
    {
        f |= GEOM_INST_FLAG_IS_DYNAMIC;
    }

    return f;
}

RTGL1::GeomInfoManager::GeomInfoManager( VkDevice                            _device,
                                         std::shared_ptr< MemoryAllocator >& _allocator )
    : device( _device )
{
    buffer    = std::make_shared< AutoBuffer >( _allocator );
    matchPrev = std::make_shared< AutoBuffer >( _allocator );

    buffer->Create( MAX_GEOM_INFO_COUNT * sizeof( ShGeometryInstance ),
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                    "Geometry info buffer" );

    matchPrev->Create( MAX_GEOM_INFO_COUNT * sizeof( MatchPrevIndexType ),
                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                       "Match previous Geometry infos buffer" );
    matchPrevShadow = std::make_unique< MatchPrevIndexType[] >( MAX_GEOM_INFO_COUNT );

    for( int i = 0; i < MAX_GEOM_INFO_COUNT; i++ )
    {
        matchPrevShadow[ i ] = MatchPrevInvalidValue;
    }
}

bool RTGL1::GeomInfoManager::CopyFromStaging( VkCommandBuffer    cmd,
                                              uint32_t           frameIndex,
                                              UniqueIDToTlasID&& tlas )
{
    auto label = CmdLabel{ cmd, "Copying geom infos" };


    auto matchprev_range = CopyRange{};
    auto geominfo_range  = CopyRange{};


    auto prevIndexToCurIndexArr = static_cast< MatchPrevIndexType* >( matchPrevShadow.get() );
    {
        for( const auto& [ uniqueID, prev ] : tlas_prev )
        {
            MatchPrevIndexType src;
            {
                auto foundCurrentIdOfPrev = tlas.find( uniqueID );
                if( foundCurrentIdOfPrev != tlas.end() )
                {
                    // save index to access ShGeometryInfo using a previous frame's TLAS Instance ID
                    src = MatchPrevIndexType( foundCurrentIdOfPrev->second );
                }
                else
                {
                    // if existed in prev frame, but not in the current, invalidate
                    src = MatchPrevInvalidValue;
                }
            }

            prevIndexToCurIndexArr[ prev ] = src;
            matchprev_range.add( prev );
        }
    }

    auto geomInfos = buffer->GetMappedAs< ShGeometryInstance* >( frameIndex );
    {
        for( const auto& [ uniqueID, tlasInstanceID ] : tlas )
        {
            const ShGeometryInstance* src;
            {
                auto found = curFrame_IdToInfo.find( uniqueID );
                if( found != curFrame_IdToInfo.end() )
                {
                    static_assert(
                        std::is_same_v< ShGeometryInstance, decltype( found->second ) > );
                    src = &found->second;
                }
                else
                {
                    assert( 0 );
                    debug::Error( "ShGeometryInstance was not registered for {}-{}",
                                  uniqueID.objectId,
                                  uniqueID.primitiveIndex );
                    constexpr static auto null{ ShGeometryInstance{} };
                    src = &null;
                }
            }

            memcpy( &geomInfos[ tlasInstanceID ], src, sizeof( ShGeometryInstance ) );
            geominfo_range.add( tlasInstanceID );
        }
    }


    tlas_prev = std::move( tlas );


    if( matchprev_range.valid() )
    {
        // copy to staging
        {
            auto dst = matchPrev->GetMappedAs< MatchPrevIndexType* >( frameIndex );
            auto src = static_cast< MatchPrevIndexType* >( matchPrevShadow.get() );

            memcpy( &dst[ matchprev_range.first() ],
                    &src[ matchprev_range.first() ],
                    matchprev_range.count() * sizeof( MatchPrevIndexType ) );
        }

        // copy from staging
        auto vcopy = VkBufferCopy{
            .srcOffset = matchprev_range.first() * sizeof( MatchPrevIndexType ),
            .dstOffset = matchprev_range.first() * sizeof( MatchPrevIndexType ),
            .size      = matchprev_range.count() * sizeof( MatchPrevIndexType ),
        };

        // TODO: remove VkBufferMemoryBarrier from CopyFromStaging and add barrier here
        matchPrev->CopyFromStaging( cmd, frameIndex, &vcopy, 1 );
    }

    if( geominfo_range.valid() )
    {
        // copy from staging
        auto vcopy = VkBufferCopy{
            .srcOffset = geominfo_range.first() * sizeof( ShGeometryInstance ),
            .dstOffset = geominfo_range.first() * sizeof( ShGeometryInstance ),
            .size      = geominfo_range.count() * sizeof( ShGeometryInstance ),
        };

        // TODO: remove VkBufferMemoryBarrier from CopyFromStaging and add barrier here
        buffer->CopyFromStaging( cmd, frameIndex, &vcopy, 1 );
    }

    return true;
}

void RTGL1::GeomInfoManager::ResetOnlyStatic()
{
    for( const auto& idToErase : staticUniqueIds )
    {
        curFrame_IdToInfo.erase( idToErase );

        // clear history
        for( auto& h : idToPrevInfo )
        {
            h.erase( idToErase );
        }
    }
    assert( std::ranges::any_of( dynamicUniqueIds, [ & ]( const auto& oneof_dynamicUniqueIds ) {
        return curFrame_IdToInfo.size() == oneof_dynamicUniqueIds.size();
    } ) );

    staticUniqueIds.clear();
}

void RTGL1::GeomInfoManager::PrepareForFrame( uint32_t frameIndex )
{
    // reset only dynamic
    for( const auto& idToErase : dynamicUniqueIds[ Utils::PrevFrame( frameIndex ) ] )
    {
        curFrame_IdToInfo.erase( idToErase );
    }
    assert( curFrame_IdToInfo.size() == staticUniqueIds.size() );

    // clear history at N-2
    for( const auto& idToErase : dynamicUniqueIds[ frameIndex ] )
    {
        idToPrevInfo[ frameIndex ].erase( idToErase );
    }
    dynamicUniqueIds[ frameIndex ].clear();
}

void RTGL1::GeomInfoManager::WriteGeomInfo( uint32_t                 frameIndex,
                                            const PrimitiveUniqueID& geomUniqueID,
                                            ShGeometryInstance&      src,
                                            bool                     isStatic,
                                            bool                     noMotionVectors )
{
    // must be aligned for per-triangle vertex attributes
    assert( src.baseVertexIndex % 3 == 0 );
    assert( src.baseIndexIndex % 3 == 0 );

    assert( frameIndex < MAX_FRAMES_IN_FLIGHT );

    {
        auto& dstIDs = isStatic ? staticUniqueIds : dynamicUniqueIds[ frameIndex ];

        auto [ iter, isnew ] = dstIDs.insert( geomUniqueID );
        assert( isnew );
    }

    if( auto prev = FindPrevFrameData( geomUniqueID, src, frameIndex, noMotionVectors ) )
    {
        // copy data from previous frame to current ShGeometryInstance
        src.prevBaseVertexIndex = prev->baseVertexIndex;
        src.prevBaseIndexIndex  = prev->baseIndexIndex;
        static_assert( sizeof( src.prevModel_0 ) == sizeof( float ) * 4 );
        static_assert( sizeof( prev->model_0 ) == sizeof( float ) * 4 );
        memcpy( src.prevModel_0, prev->model_0, sizeof( src.prevModel_0 ) );
        memcpy( src.prevModel_1, prev->model_1, sizeof( src.prevModel_1 ) );
        memcpy( src.prevModel_2, prev->model_2, sizeof( src.prevModel_2 ) );
    }
    else
    {
        // no prev
        src.prevBaseVertexIndex = UINT32_MAX;
    }

    // register
    {
        assert( !curFrame_IdToInfo.contains( geomUniqueID ) );
        curFrame_IdToInfo[ geomUniqueID ] = src;
    }

    WritePrevForNextFrame( geomUniqueID, src, frameIndex );
}

void RTGL1::GeomInfoManager::Hack_PatchGeomInfoTexturesForStatic(
    const PrimitiveUniqueID& geomUniqueID,
    uint32_t                 texture_base,
    uint32_t                 texture_base_ORM,
    uint32_t                 texture_base_N,
    uint32_t                 texture_base_E,
    uint32_t                 texture_base_D )
{
    {
        const auto f = staticUniqueIds.find( geomUniqueID );
        if( f == staticUniqueIds.end() )
        {
            debug::Error( "Failed to patch textures for static geominfo: ID is not for static" );
            return;
        }
    }

    const auto f = curFrame_IdToInfo.find( geomUniqueID );
    if( f == curFrame_IdToInfo.end() )
    {
        debug::Error( "Failed to patch textures for static geominfo: "
                      "info with specified ID was not uploaded" );
        return;
    }

    ShGeometryInstance& dst = f->second;
    {
        dst.texture_base     = texture_base;
        dst.texture_base_ORM = texture_base_ORM;
        dst.texture_base_N   = texture_base_N;
        dst.texture_base_E   = texture_base_E;
        dst.texture_base_D   = texture_base_D;
    }
}

void RTGL1::GeomInfoManager::Hack_PatchGeomInfoTransformForStatic(
    const PrimitiveUniqueID& geomUniqueID, const RgTransform& transform )
{
    {
        const auto f = staticUniqueIds.find( geomUniqueID );
        if( f == staticUniqueIds.end() )
        {
            debug::Warning( "Failed to patch transform for static geominfo: ID is not for static" );
            return;
        }
    }

    const auto f = curFrame_IdToInfo.find( geomUniqueID );
    if( f == curFrame_IdToInfo.end() )
    {
        debug::Error( "Failed to patch transform for static geominfo: "
                      "info with specified ID was not uploaded" );
        return;
    }

    ShGeometryInstance& dst = f->second;
    {
        static_assert( sizeof( dst.model_0 ) == sizeof( transform.matrix[ 0 ] ) );
        static_assert( sizeof( dst.model_1 ) == sizeof( transform.matrix[ 1 ] ) );
        static_assert( sizeof( dst.model_2 ) == sizeof( transform.matrix[ 2 ] ) );

        memcpy( dst.model_0, transform.matrix[ 0 ], 4 * sizeof( float ) );
        memcpy( dst.model_1, transform.matrix[ 1 ], 4 * sizeof( float ) );
        memcpy( dst.model_2, transform.matrix[ 2 ], 4 * sizeof( float ) );
    }
}

auto RTGL1::GeomInfoManager::FindPrevFrameData( const PrimitiveUniqueID&  geomUniqueID,
                                                const ShGeometryInstance& target,
                                                uint32_t                  frameIndex,
                                                bool noMotionVectors ) const -> const PrevInfo*
{
    if( noMotionVectors )
    {
        return nullptr;
    }

    auto& idToInfo_prev = idToPrevInfo[ Utils::PrevFrame( frameIndex ) ];

    // if geomUniqueId existed in prev frame
    auto iter = idToInfo_prev.find( geomUniqueID );
    if( iter == idToInfo_prev.end() )
    {
        return nullptr;
    }
    auto& prev = iter->second;

    // if counts are not the same
    if( prev.vertexCount != target.vertexCount || prev.indexCount != target.indexCount )
    {
        return nullptr;
    }

    return &prev;
}


void RTGL1::GeomInfoManager::WritePrevForNextFrame( const PrimitiveUniqueID&  geomUniqueID,
                                                    const ShGeometryInstance& src,
                                                    uint32_t                  frameIndex )
{
    auto& idToInfo_cur = idToPrevInfo[ frameIndex ];

    // IDs must be unique
    assert( !idToInfo_cur.contains( geomUniqueID ) );

    auto& dst = idToInfo_cur[ geomUniqueID ];
    {
        dst = PrevInfo{
            .baseVertexIndex = src.baseVertexIndex,
            .baseIndexIndex  = src.baseIndexIndex,
            .vertexCount     = src.vertexCount,
            .indexCount      = src.indexCount,
        };
        static_assert( sizeof dst.model_0 == sizeof( float ) * 4 );
        static_assert( sizeof src.model_0 == sizeof( float ) * 4 );
        memcpy( dst.model_0, src.model_0, sizeof( src.model_0 ) );
        memcpy( dst.model_1, src.model_1, sizeof( src.model_1 ) );
        memcpy( dst.model_2, src.model_2, sizeof( src.model_2 ) );
    }
}

VkBuffer RTGL1::GeomInfoManager::GetBuffer() const
{
    return buffer->GetDeviceLocal();
}

VkBuffer RTGL1::GeomInfoManager::GetMatchPrevBuffer() const
{
    return matchPrev->GetDeviceLocal();
}

uint32_t RTGL1::GeomInfoManager::GetCount( uint32_t frameIndex ) const
{
    assert( curFrame_IdToInfo.size() ==
            staticUniqueIds.size() + dynamicUniqueIds[ frameIndex ].size() );
    return static_cast< uint32_t >( curFrame_IdToInfo.size() );
}
