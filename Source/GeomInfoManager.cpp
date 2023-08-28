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

}

bool RTGL1::GeomInfoManager::LayerExists( const RgMeshPrimitiveInfo& info, uint32_t layerIndex )
{
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

    return false;
}

const RgFloat2D* RTGL1::GeomInfoManager::AccessLayerTexCoords( const RgMeshPrimitiveInfo& info,
                                                               uint32_t layerIndex )
{
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

    return nullptr;
}

uint32_t RTGL1::GeomInfoManager::GetPrimitiveFlags( const RgMeshPrimitiveInfo& info )
{
    uint32_t f = 0;


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

    if( info.flags & RG_MESH_PRIMITIVE_MIRROR )
    {
        f |= GEOM_INST_FLAG_REFLECT;
    }

    if( info.flags & RG_MESH_PRIMITIVE_WATER )
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

    if( info.flags & RG_MESH_PRIMITIVE_GLASS )
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

    if( info.flags & RG_MESH_PRIMITIVE_IGNORE_REFRACT_AFTER )
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
                                              TlasIDToUniqueID&& tlas )
{
    auto label = CmdLabel{ cmd, "Copying geom infos" };

    struct MinMax
    {
        void add( uint32_t x )
        {
            vmin = std::min( x, vmin );
            vmax = std::max( x, vmax );
        }

        uint32_t first() const { return vmin; }
        uint32_t count() const { return vmax - vmin; }
        bool     valid() const { return count() > 0; }

        uint32_t vmin{ 0 };
        uint32_t vmax{ 0 };
    };


    auto matchprev_range = MinMax{};
    auto geominfo_range  = MinMax{};


    auto findTlasInstanceIDInCurrentFrame =
        []( const TlasIDToUniqueID&  cur,
            const PrimitiveUniqueID& targetUniqueID ) -> std::optional< uint32_t > {
        for( const auto& [ curTlasInstanceID, uniqueID ] : cur )
        {
            if( uniqueID == targetUniqueID )
            {
                return curTlasInstanceID;
            }
        }
        return {};
    };


    auto prevIndexToCurIndexArr = static_cast< MatchPrevIndexType* >( matchPrevShadow.get() );
    {
        for( const auto& [ prev, uniqueID ] : tlas_prev )
        {
            MatchPrevIndexType src;
            {
                if( auto cur = findTlasInstanceIDInCurrentFrame( tlas, uniqueID ) )
                {
                    // save index to access ShGeometryInfo using a previous frame's TLAS Instance ID
                    src = MatchPrevIndexType( *cur );
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
        for( const auto& [ tlasInstanceID, uniqueID ] : tlas )
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
            .srcOffset = geominfo_range.first() * sizeof( MatchPrevIndexType ),
            .dstOffset = geominfo_range.first() * sizeof( MatchPrevIndexType ),
            .size      = geominfo_range.count() * sizeof( MatchPrevIndexType ),
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
    }
    staticUniqueIds.clear();
}

void RTGL1::GeomInfoManager::PrepareForFrame( uint32_t frameIndex )
{
    // reset only dynamic
    for( const auto& idToErase : curFrame_dynamicUniqueIds )
    {
        curFrame_IdToInfo.erase( idToErase );
    }
    curFrame_dynamicUniqueIds.clear();
}

void RTGL1::GeomInfoManager::WriteGeomInfo( uint32_t                 frameIndex,
                                            const PrimitiveUniqueID& geomUniqueID,
                                            ShGeometryInstance&      src,
                                            bool                     isStatic )
{
    // must be aligned for per-triangle vertex attributes
    assert( src.baseVertexIndex % 3 == 0 );
    assert( src.baseIndexIndex % 3 == 0 );

    assert( frameIndex < MAX_FRAMES_IN_FLIGHT );

    {
        auto& dstIDs = isStatic ? staticUniqueIds : curFrame_dynamicUniqueIds;

        auto [ iter, isnew ] = dstIDs.insert( geomUniqueID );
        assert( isnew );
    }

    if( auto prev = FindPrevFrameData( geomUniqueID, src, frameIndex ) )
    {
        // copy data from previous frame to current ShGeometryInstance
        src.prevBaseVertexIndex = prev->baseVertexIndex;
        src.prevBaseIndexIndex  = prev->baseIndexIndex;
        memcpy( src.prevModel, prev->model, sizeof( float ) * 16 );
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

auto RTGL1::GeomInfoManager::FindPrevFrameData( const PrimitiveUniqueID&  geomUniqueID,
                                                const ShGeometryInstance& target,
                                                uint32_t frameIndex ) const -> const PrevInfo*
{
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
        static_assert( sizeof dst.model == sizeof( float ) * 16 );
        static_assert( sizeof src.model == sizeof( float ) * 16 );
        memcpy( dst.model, src.model, sizeof( float ) * 16 );
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
    assert( curFrame_IdToInfo.size() == staticUniqueIds.size() + curFrame_dynamicUniqueIds.size() );
    return static_cast< uint32_t >( curFrame_IdToInfo.size() );
}
