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

#include "VertexCollector.h"

#include "DrawFrameInfo.h"
#include "GeomInfoManager.h"
#include "Utils.h"

#include "Generated/ShaderCommonC.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace
{

auto MakeName( std::string_view bufname, std::string_view classname )
{
    return std::format( "VC: {}-{}", bufname, classname );
}

auto MakeUsage( bool isDynamic, bool accelStructureRead )
{
    VkBufferUsageFlags usage =
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    if( isDynamic )
    {
        // dynamic vertices need also be copied to previous frame buffer
        usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    }
    else
    {
        usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }

    if( accelStructureRead )
    {
        usage |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    }

    return usage;
}

}

RTGL1::VertexCollector::VertexCollector( VkDevice         _device,
                                         MemoryAllocator& _allocator,
                                         const uint32_t ( &_maxVertsPerLayer )[ 4 ],
                                         bool             _isDynamic,
                                         std::string_view _debugName )
    : device{ _device }
    , bufVertices{ _allocator,
                   _maxVertsPerLayer[ 0 ],
                   MakeUsage( _isDynamic, true ),
                   MakeName( "Vertices", _debugName ) }
    , bufIndices{ _allocator,
                  MAX_INDEXED_PRIMITIVE_COUNT * 3,
                  MakeUsage( _isDynamic, true ),
                  MakeName( "Indices", _debugName ) }
    , bufTexcoordLayer1{ _allocator,
                         _maxVertsPerLayer[ 1 ],
                         MakeUsage( _isDynamic, false ),
                         MakeName( "Texcoords Layer1", _debugName ) }
    , bufTexcoordLayer2{ _allocator,
                         _maxVertsPerLayer[ 2 ],
                         MakeUsage( _isDynamic, false ),
                         MakeName( "Texcoords Layer2", _debugName ) }
    , bufTexcoordLayer3{ _allocator,
                         _maxVertsPerLayer[ 3 ],
                         MakeUsage( _isDynamic, false ),
                         MakeName( "Texcoords Layer3", _debugName ) }
{
}

// device local buffers are shared with the "src" vertex collector
RTGL1::VertexCollector::VertexCollector( const VertexCollector& _src,
                                         MemoryAllocator&       _allocator,
                                         std::string_view       _debugName )
    : device{ _src.device }
    , bufVertices{ _src.bufVertices, _allocator, MakeName( "Vertices", _debugName ) }
    , bufIndices{ _src.bufIndices, _allocator, MakeName( "Indices", _debugName ) }
    , bufTexcoordLayer1{ _src.bufTexcoordLayer1,
                         _allocator,
                         MakeName( "Texcoords Layer1", _debugName ) }
    , bufTexcoordLayer2{ _src.bufTexcoordLayer2,
                         _allocator,
                         MakeName( "Texcoords Layer2", _debugName ) }
    , bufTexcoordLayer3{ _src.bufTexcoordLayer3,
                         _allocator,
                         MakeName( "Texcoords Layer3", _debugName ) }
{
}

auto RTGL1::VertexCollector::CreateWithSameDeviceLocalBuffers( const VertexCollector& src,
                                                               MemoryAllocator&       allocator,
                                                               std::string_view       debugName )
    -> std::unique_ptr< VertexCollector >
{
    return std::make_unique< VertexCollector >( src, allocator, debugName );
}

namespace
{

uint32_t AlignUpBy3( uint32_t x )
{
    return ( ( x + 2 ) / 3 ) * 3;
}

}

auto RTGL1::VertexCollector::Upload( VertexCollectorFilterTypeFlags geomFlags,
                                     const RgMeshInfo&              mesh,
                                     const RgMeshPrimitiveInfo&     prim )
    -> std::optional< UploadResult >
{
    using FT = VertexCollectorFilterTypeFlagBits;

    const uint32_t vertIndex   = AlignUpBy3( curVertexCount );
    const uint32_t indIndex    = AlignUpBy3( curIndexCount );
    const uint32_t texcIndex_1 = curTexCoordCount_Layer1;
    const uint32_t texcIndex_2 = curTexCoordCount_Layer2;
    const uint32_t texcIndex_3 = curTexCoordCount_Layer3;

    const bool     useIndices    = prim.indexCount != 0 && prim.pIndices != nullptr;
    const uint32_t triangleCount = useIndices ? prim.indexCount / 3 : prim.vertexCount / 3;

    curVertexCount = vertIndex + prim.vertexCount;
    curIndexCount  = indIndex + ( useIndices ? prim.indexCount : 0 );
    curPrimitiveCount += triangleCount;
    curTexCoordCount_Layer1 += GeomInfoManager::LayerExists( prim, 1 ) ? prim.vertexCount : 0;
    curTexCoordCount_Layer2 += GeomInfoManager::LayerExists( prim, 2 ) ? prim.vertexCount : 0;
    curTexCoordCount_Layer3 += GeomInfoManager::LayerExists( prim, 3 ) ? prim.vertexCount : 0;



    if( !( geomFlags & FT::CF_DYNAMIC ) )
    {
        if( curVertexCount >= MAX_STATIC_VERTEX_COUNT )
        {
            debug::Error( "Too many static vertices: the limit is {}", MAX_STATIC_VERTEX_COUNT );
            return {};
        }
    }
    else
    {
        if( curVertexCount >= MAX_DYNAMIC_VERTEX_COUNT )
        {
            debug::Error( "Too many dynamic vertices: the limit is {}", MAX_DYNAMIC_VERTEX_COUNT );
            return {};
        }
    }

    if( curIndexCount >= MAX_INDEXED_PRIMITIVE_COUNT * 3 )
    {
        debug::Error( "Too many indices: the limit is {}", MAX_INDEXED_PRIMITIVE_COUNT * 3 );
        return {};
    }



    // copy data to buffers
    CopyVertexDataToStaging( prim, vertIndex, texcIndex_1, texcIndex_2, texcIndex_3 );

    if( useIndices )
    {
        assert( bufIndices.mapped );
        memcpy(
            &bufIndices.mapped[ indIndex ], prim.pIndices, prim.indexCount * sizeof( uint32_t ) );
    }



    auto triangles = VkAccelerationStructureGeometryTrianglesDataKHR{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
        // vertices
        .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
        .vertexData   = { .deviceAddress = bufVertices.deviceLocal->GetAddress() +
                                           vertIndex * sizeof( ShVertex ) +
                                           offsetof( ShVertex, position ) },
        .vertexStride = sizeof( ShVertex ),
        .maxVertex    = prim.vertexCount,
        // indices
        .indexType = useIndices ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_NONE_KHR,
        .indexData = { .deviceAddress = useIndices ? bufIndices.deviceLocal->GetAddress() +
                                                         indIndex * sizeof( uint32_t )
                                                   : 0 },
    };

    auto geom = VkAccelerationStructureGeometryKHR{
        .sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
        .geometry     = { .triangles = triangles },
        .flags        = geomFlags & FT::PT_OPAQUE
                            ? VkGeometryFlagsKHR( VK_GEOMETRY_OPAQUE_BIT_KHR )
                            : VkGeometryFlagsKHR( VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR )
    };

    auto range = VkAccelerationStructureBuildRangeInfoKHR{
        .primitiveCount  = triangleCount,
        .primitiveOffset = 0,
        .firstVertex     = 0,
        .transformOffset = 0,
    };

    return UploadResult{
        .asGeometryInfo     = geom,
        .asRange            = range,
        .firstIndex         = useIndices ? std::optional{ indIndex } : std::nullopt,
        .firstVertex        = vertIndex,
        .firstVertex_Layer1 = texcIndex_1,
        .firstVertex_Layer2 = texcIndex_2,
        .firstVertex_Layer3 = texcIndex_3,
    };
}

void RTGL1::VertexCollector::CopyVertexDataToStaging( const RgMeshPrimitiveInfo& info,
                                                      uint32_t                   vertIndex,
                                                      uint32_t                   texcIndex_1,
                                                      uint32_t                   texcIndex_2,
                                                      uint32_t                   texcIndex_3 )
{
    {
        assert( bufVertices.mapped );
        assert( ( vertIndex + info.vertexCount ) * sizeof( ShVertex ) <
                bufVertices.staging.GetSize() );

        ShVertex* const pDst = &bufVertices.mapped[ vertIndex ];

        // must be same to copy
        static_assert( std::is_same_v< decltype( info.pVertices ), const RgPrimitiveVertex* > );
        static_assert( sizeof( ShVertex ) == sizeof( RgPrimitiveVertex ) );
        static_assert( offsetof( ShVertex, position ) == offsetof( RgPrimitiveVertex, position ) );
        static_assert( offsetof( ShVertex, normal ) == offsetof( RgPrimitiveVertex, normal ) );
        static_assert( offsetof( ShVertex, tangent ) == offsetof( RgPrimitiveVertex, tangent ) );
        static_assert( offsetof( ShVertex, texCoord ) == offsetof( RgPrimitiveVertex, texCoord ) );
        static_assert( offsetof( ShVertex, color ) == offsetof( RgPrimitiveVertex, color ) );

        memcpy( pDst, info.pVertices, info.vertexCount * sizeof( ShVertex ) );
    }

    {
        struct TdDst
        {
            uint32_t                        layerIndex;
            SharedDeviceLocal< RgFloat2D >* buffer;
            uint32_t                        offset;
        };

        TdDst dsts[] = {
            { .layerIndex = 1, .buffer = &bufTexcoordLayer1, .offset = texcIndex_1 },
            { .layerIndex = 2, .buffer = &bufTexcoordLayer2, .offset = texcIndex_2 },
            { .layerIndex = 3, .buffer = &bufTexcoordLayer3, .offset = texcIndex_3 },
        };

        for( auto& dst : dsts )
        {
            if( const RgFloat2D* src =
                    GeomInfoManager::AccessLayerTexCoords( info, dst.layerIndex ) )
            {
                if( dst.buffer->IsInitialized() && dst.buffer->mapped )
                {
                    memcpy( &dst.buffer->mapped[ dst.offset ],
                            src,
                            info.vertexCount * sizeof( RgFloat2D ) );
                }
                else
                {
                    debug::Error( "Found Layer{} texture coords on a primitive, but buffer was not "
                                  "allocated. "
                                  "Recheck RgInstanceCreateInfo::{}",
                                  dst.layerIndex,
                                  dst.layerIndex == 1   ? "allowTexCoordLayer1"
                                  : dst.layerIndex == 2 ? "allowTexCoordLayer2"
                                  : dst.layerIndex == 3 ? "allowTexCoordLayer3"
                                                        : "<unknown>" );
                }
            }
        }
    }
}

void RTGL1::VertexCollector::Reset()
{
    curVertexCount          = 0;
    curIndexCount           = 0;
    curPrimitiveCount       = 0;
    curTexCoordCount_Layer1 = 0;
    curTexCoordCount_Layer2 = 0;
    curTexCoordCount_Layer3 = 0;
}

bool RTGL1::VertexCollector::CopyVertexDataFromStaging( VkCommandBuffer cmd )
{
    if( curVertexCount == 0 )
    {
        return false;
    }

    VkBufferCopy info = {
        .srcOffset = 0,
        .dstOffset = 0,
        .size      = curVertexCount * sizeof( ShVertex ),
    };

    vkCmdCopyBuffer(
        cmd, bufVertices.staging.GetBuffer(), bufVertices.deviceLocal->GetBuffer(), 1, &info );

    return true;
}

bool RTGL1::VertexCollector::CopyTexCoordsFromStaging( VkCommandBuffer cmd, uint32_t layerIndex )
{
    std::pair< SharedDeviceLocal< RgFloat2D >*, uint32_t > txc = {};

    switch( layerIndex )
    {
        case 1: txc = { &bufTexcoordLayer1, curTexCoordCount_Layer1 }; break;
        case 2: txc = { &bufTexcoordLayer2, curTexCoordCount_Layer2 }; break;
        case 3: txc = { &bufTexcoordLayer3, curTexCoordCount_Layer3 }; break;
        default: assert( 0 ); return false;
    }

    auto& [ buf, count ] = txc;

    if( count == 0 )
    {
        return false;
    }

    VkBufferCopy info = {
        .srcOffset = 0,
        .dstOffset = 0,
        .size      = count * sizeof( RgFloat2D ),
    };

    vkCmdCopyBuffer( cmd, buf->staging.GetBuffer(), buf->deviceLocal->GetBuffer(), 1, &info );
    return true;
}

bool RTGL1::VertexCollector::CopyIndexDataFromStaging( VkCommandBuffer cmd )
{
    if( curIndexCount == 0 )
    {
        return false;
    }

    VkBufferCopy info = {
        .srcOffset = 0,
        .dstOffset = 0,
        .size      = curIndexCount * sizeof( uint32_t ),
    };

    vkCmdCopyBuffer(
        cmd, bufIndices.staging.GetBuffer(), bufIndices.deviceLocal->GetBuffer(), 1, &info );

    return true;
}

bool RTGL1::VertexCollector::CopyFromStaging( VkCommandBuffer cmd )
{
    bool copiedAny = false;

    // just prepare for preprocessing - so no AS as the destination for this moment
    {
        std::array< VkBufferMemoryBarrier, 2 > barriers     = {};
        uint32_t                               barrierCount = 0;

        if( CopyVertexDataFromStaging( cmd ) )
        {
            barriers[ barrierCount++ ] = {
                .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                .srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
                .dstAccessMask       = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer              = bufVertices.deviceLocal->GetBuffer(),
                .offset              = 0,
                .size                = curVertexCount * sizeof( ShVertex ),
            };
            copiedAny = true;
        }

        if( CopyIndexDataFromStaging( cmd ) )
        {
            barriers[ barrierCount++ ] = {
                .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                .srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
                .dstAccessMask       = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer              = bufIndices.deviceLocal->GetBuffer(),
                .offset              = 0,
                .size                = curIndexCount * sizeof( uint32_t ),
            };
            copiedAny = true;
        }

        if( barrierCount > 0 )
        {
            vkCmdPipelineBarrier( cmd,
                                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                                      VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                                  0,
                                  0,
                                  nullptr,
                                  barrierCount,
                                  barriers.data(),
                                  0,
                                  nullptr );
        }
    }

    // copy for read-only
    {
        std::array< VkBufferMemoryBarrier, 3 > barriers     = {};
        uint32_t                               barrierCount = 0;

        for( uint32_t layerIndex : { 1, 2, 3 } )
        {
            std::pair< SharedDeviceLocal< RgFloat2D >*, uint32_t /* elem count */ > txc = {};

            switch( layerIndex )
            {
                case 1: txc = { &bufTexcoordLayer1, curTexCoordCount_Layer1 }; break;
                case 2: txc = { &bufTexcoordLayer2, curTexCoordCount_Layer2 }; break;
                case 3: txc = { &bufTexcoordLayer3, curTexCoordCount_Layer3 }; break;
                default: assert( 0 ); continue;
            }

            if( CopyTexCoordsFromStaging( cmd, layerIndex ) )
            {
                barriers[ barrierCount++ ] = {
                    .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                    .srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
                    .dstAccessMask       = VK_ACCESS_SHADER_READ_BIT,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .buffer              = txc.first->deviceLocal->GetBuffer(),
                    .offset              = 0,
                    .size                = txc.second * sizeof( RgFloat2D ),
                };
                copiedAny = true;
            }
        }

        if( barrierCount > 0 )
        {
            vkCmdPipelineBarrier( cmd,
                                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                  0,
                                  0,
                                  nullptr,
                                  barrierCount,
                                  barriers.data(),
                                  0,
                                  nullptr );
        }
    }

    return copiedAny;
}

VkBuffer RTGL1::VertexCollector::GetVertexBuffer() const
{
    return bufVertices.deviceLocal->GetBuffer();
}

VkBuffer RTGL1::VertexCollector::GetTexcoordBuffer_Layer1() const
{
    return bufTexcoordLayer1.IsInitialized() ? bufTexcoordLayer1.deviceLocal->GetBuffer()
                                             : VK_NULL_HANDLE;
}

VkBuffer RTGL1::VertexCollector::GetTexcoordBuffer_Layer2() const
{
    return bufTexcoordLayer2.IsInitialized() ? bufTexcoordLayer2.deviceLocal->GetBuffer()
                                             : VK_NULL_HANDLE;
}

VkBuffer RTGL1::VertexCollector::GetTexcoordBuffer_Layer3() const
{
    return bufTexcoordLayer3.IsInitialized() ? bufTexcoordLayer3.deviceLocal->GetBuffer()
                                             : VK_NULL_HANDLE;
}

VkBuffer RTGL1::VertexCollector::GetIndexBuffer() const
{
    return bufIndices.deviceLocal->GetBuffer();
}

void RTGL1::VertexCollector::InsertVertexPreprocessBeginBarrier( VkCommandBuffer cmd )
{
    // barriers were already inserted in CopyFromStaging()
}

void RTGL1::VertexCollector::InsertVertexPreprocessFinishBarrier( VkCommandBuffer cmd )
{
    std::array< VkBufferMemoryBarrier, 5 > barriers     = {};
    uint32_t                               barrierCount = 0;

    if( curVertexCount > 0 )
    {
        barriers[ barrierCount++ ] = {
            .sType         = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            .dstAccessMask =
                VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer              = bufVertices.deviceLocal->GetBuffer(),
            .offset              = 0,
            .size                = curVertexCount * sizeof( ShVertex ),
        };
    }

    if( curIndexCount > 0 )
    {
        barriers[ barrierCount++ ] = {
            .sType         = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            .dstAccessMask =
                VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer              = bufIndices.deviceLocal->GetBuffer(),
            .offset              = 0,
            .size                = curIndexCount * sizeof( uint32_t ),
        };
    }

    if( barrierCount == 0 )
    {
        return;
    }

    vkCmdPipelineBarrier( cmd,
                          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                          VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR |
                              VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                          0,
                          0,
                          nullptr,
                          barrierCount,
                          barriers.data(),
                          0,
                          nullptr );
}
uint32_t RTGL1::VertexCollector::GetCurrentVertexCount() const
{
    return curVertexCount;
}

uint32_t RTGL1::VertexCollector::GetCurrentIndexCount() const
{
    return curIndexCount;
}
