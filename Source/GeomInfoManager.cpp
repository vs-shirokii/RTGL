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
}

bool RTGL1::GeomInfoManager::CopyFromStaging( VkCommandBuffer cmd,
                                              uint32_t        frameIndex,
                                              bool            insertBarrier )
{
    auto label = CmdLabel{ cmd, "Copying geom infos" };

    struct MinMax
    {
        void add( uint32_t x )
        {
            vmin = std::min( x, vmin );
            vmax = std::max( x, vmax );
        }

        uint32_t vmin{ 0 };
        uint32_t vmax{ 0 };
    };

    {
        // for static and dynamic ranges
        MinMax                minmax[ 2 ];
        VkBufferCopy          copyInfos[ 2 ];
        VkBufferMemoryBarrier barriers[ 2 ];

        {
            for( auto& [ uniqueID, info ] : userUniqueIDToGeomFrameInfo[ frameIndex ] )
            {
                minmax[ 0 ].add( info.tlasInstanceID );
            }
        }

        uint32_t infoCount = 0;

        // copy to staging
        {
            MatchPrevIndexType* dst = matchPrev->GetMappedAs< MatchPrevIndexType* >( frameIndex );
            MatchPrevIndexType* src = matchPrevShadow.get();

            memcpy( &dst[ elementsToCopy.elementsOffset ],
                    &src[ elementsToCopy.elementsOffset ],
                    elementsToCopy.elementsCount * sizeof( MatchPrevIndexType ) );
        }

        // copy from staging
        copyInfos[ infoCount ] = {
            .srcOffset = elementsToCopy.elementsOffset * sizeof( MatchPrevIndexType ),
            .dstOffset = elementsToCopy.elementsOffset * sizeof( MatchPrevIndexType ),
            .size      = elementsToCopy.elementsCount * sizeof( MatchPrevIndexType ),
        };

        barriers[ infoCount ] = VkBufferMemoryBarrier{
            .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask       = VK_ACCESS_SHADER_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer              = matchPrev->GetDeviceLocal(),
            .offset              = copyInfos[ infoCount ].dstOffset,
            .size                = copyInfos[ infoCount ].size,
        };

        infoCount++;

        if( infoCount > 0 )
        {
            matchPrev->CopyFromStaging( cmd, frameIndex, copyInfos, infoCount );

            if( insertBarrier )
            {
                vkCmdPipelineBarrier( cmd,
                                      VK_PIPELINE_STAGE_TRANSFER_BIT,
                                      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                                          VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                                      0,
                                      0,
                                      nullptr,
                                      infoCount,
                                      barriers,
                                      0,
                                      nullptr );
            }
        }
    }


    {
        VkBufferCopy          copyInfos[ MAX_TOP_LEVEL_INSTANCE_COUNT ];
        VkBufferMemoryBarrier barriers[ MAX_TOP_LEVEL_INSTANCE_COUNT ];

        uint32_t infoCount = 0;

        VertexCollectorFilterTypeFlags_IterateOverFlags(
            [ & ]( VertexCollectorFilterTypeFlags flags ) {
                //
                const auto groupOffsetInBytes =
                    VertexCollectorFilterTypeFlags_GetOffsetInGlobalArray( flags ) *
                    sizeof( ShGeometryInstance );

                rgl::byte_subspan toCopy = AccessGeometryInstanceGroup( frameIndex, flags )
                                               .resolve_byte_subspan( groupOffsetInBytes );

                if( toCopy.sizeInBytes > 0 )
                {
                    copyInfos[ infoCount ] = VkBufferCopy{

                        .srcOffset = toCopy.offsetInBytes,
                        .dstOffset = toCopy.offsetInBytes,
                        .size      = toCopy.sizeInBytes,
                    };

                    barriers[ infoCount ] = VkBufferMemoryBarrier{
                        .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                        .srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
                        .dstAccessMask       = VK_ACCESS_SHADER_READ_BIT,
                        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .buffer              = buffer->GetDeviceLocal(),
                        .offset              = copyInfos[ infoCount ].dstOffset,
                        .size                = copyInfos[ infoCount ].size,
                    };

                    infoCount++;
                }
            } );

        if( infoCount == 0 )
        {
            return false;
        }

        buffer->CopyFromStaging( cmd, frameIndex, copyInfos, infoCount );

        if( insertBarrier )
        {
            vkCmdPipelineBarrier( cmd,
                                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                                      VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                                  0,
                                  0,
                                  nullptr,
                                  infoCount,
                                  barriers,
                                  0,
                                  nullptr );
        }
    }

    return true;
}

void RTGL1::GeomInfoManager::ResetGroup(
    rgl::unordered_map< PrimitiveUniqueID, GeomFrameInfo >& idToInfo,
    const std::vector< PrimitiveUniqueID >&                 idsToClear )
{
    for( const auto& id : idsToClear )
    {
        auto iter = idToInfo.find( id );
        if( iter == idToInfo.end() )
        {
            assert( 0 );
            continue;
        }

        idToInfo.erase( iter );
    }
}

void RTGL1::GeomInfoManager::ResetOnlyStatic()
{
    mappedBufferRegionsCount;

    MatchPrevIndexType* prevIndexToCurIndexArr = matchPrevShadow.get();
    for( auto& idToInfo : userUniqueIDToGeomFrameInfo )
    {
        ResetGroup( idToInfo, staticUniqueIds );
    }
    for( const auto& info_prev :
         userUniqueIDToGeomFrameInfo[ Utils::PrevFrame( frameIndex ) ] | std::views::values )
    {
        prevIndexToCurIndexArr[ info_prev.tlasInstanceID ] = -1;
    }
    for( auto& idToInfo : userUniqueIDToGeomFrameInfo )
    {
        ResetGroup( idToInfo, staticUniqueIds );
    }
    staticUniqueIds.clear();
}

void RTGL1::GeomInfoManager::PrepareForFrame( uint32_t frameIndex )
{
    mappedBufferRegionsCount;

    // reset only dynamic
    MatchPrevIndexType* prevIndexToCurIndexArr = matchPrevShadow.get();
    for( const auto& info_prev :
         userUniqueIDToGeomFrameInfo[ Utils::PrevFrame( frameIndex ) ] | std::views::values )
    {
        prevIndexToCurIndexArr[ info_prev.tlasInstanceID ] = -1;
    }
    ResetGroup( userUniqueIDToGeomFrameInfo[ frameIndex ], dynamicUniqueIds );
    dynamicUniqueIds.clear();
}

void RTGL1::GeomInfoManager::WriteGeomInfo( uint32_t                       frameIndex,
                                            const PrimitiveUniqueID&       geomUniqueID,
                                            uint32_t                       tlasInstanceID,
                                            VertexCollectorFilterTypeFlags flags,
                                            ShGeometryInstance&            src )
{
    // must be aligned for per-triangle vertex attributes
    assert( src.baseVertexIndex % 3 == 0 );
    assert( src.baseIndexIndex % 3 == 0 );

    assert( frameIndex < MAX_FRAMES_IN_FLIGHT );

    FillWithPrevFrameData( geomUniqueID, tlasInstanceID, src, frameIndex );

    auto dstInfoArray = buffer->GetMappedAs< ShGeometryInstance* >( frameIndex );
    {
        memcpy( &dstInfoArray[ tlasInstanceID ], &src, sizeof( ShGeometryInstance ) );
    }

    mappedBufferRegionsCount;

    WriteInfoForNextUsage( geomUniqueID, tlasInstanceID, src, frameIndex );
}

void RTGL1::GeomInfoManager::FillWithPrevFrameData( const PrimitiveUniqueID& geomUniqueID,
                                                    uint32_t                 tlasInstanceID,
                                                    ShGeometryInstance&      dst,
                                                    uint32_t                 frameIndex )
{
    auto MarkNoPrevInfo = []( ShGeometryInstance& dst ) {
        dst.prevBaseVertexIndex = UINT32_MAX;
    };
    assert( tlasInstanceID < MAX_GEOM_INFO_COUNT );

    auto& idToInfo_prev = userUniqueIDToGeomFrameInfo[ Utils::PrevFrame( frameIndex ) ];

    auto iter = idToInfo_prev.find( geomUniqueID );
    if( iter == idToInfo_prev.end() )
    {
        MarkNoPrevInfo( dst );
        return;
    }
    auto& prev = iter->second;

    // if counts are not the same
    if( prev.vertexCount != dst.vertexCount || prev.indexCount != dst.indexCount )
    {
        MarkNoPrevInfo( dst );
        return;
    }

    // copy data from previous frame to current ShGeometryInstance
    {
        dst.prevBaseVertexIndex = prev.baseVertexIndex;
        dst.prevBaseIndexIndex  = prev.baseIndexIndex;
        memcpy( dst.prevModel, prev.model, sizeof( float ) * 16 );
    }

    MatchPrevIndexType* prevIndexToCurIndexArr = matchPrevShadow.get();

    // save index to access ShGeometryInfo using previous frame's global geom index
    prevIndexToCurIndexArr[ prev.tlasInstanceID ] = MatchPrevIndexType( tlasInstanceID );
}


void RTGL1::GeomInfoManager::WriteInfoForNextUsage( const PrimitiveUniqueID&  geomUniqueID,
                                                    uint32_t                  tlasInstanceID,
                                                    const ShGeometryInstance& src,
                                                    uint32_t                  frameIndex )
{
    auto& idToInfo_cur = userUniqueIDToGeomFrameInfo[ frameIndex ];

    // IDs must be unique
    assert( !idToInfo_cur.contains( geomUniqueID ) );

    auto& dst = idToInfo_cur[ geomUniqueID ];
    {
        dst = GeomFrameInfo{
            .baseVertexIndex = src.baseVertexIndex,
            .baseIndexIndex  = src.baseIndexIndex,
            .vertexCount     = src.vertexCount,
            .indexCount      = src.indexCount,
            .tlasInstanceID  = tlasInstanceID,
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
    return static_cast< uint32_t >( userUniqueIDToGeomFrameInfo[ frameIndex ].size() );
}
