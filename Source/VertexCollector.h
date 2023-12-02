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

#include <optional>
#include <span>
#include <vector>

#include "Buffer.h"
#include "Common.h"
#include "Material.h"
#include "VertexCollectorFilter.h"
#include "Utils.h"

#include "RTGL1/RTGL1.h"

namespace RTGL1
{

struct ShVertex;

class GeomInfoManager;

// Accumulating vertex data
class VertexCollector
{
public:
    explicit VertexCollector( VkDevice         device,
                              MemoryAllocator& allocator,
                              const size_t ( &maxVertsPerLayer )[ 4 ],
                              const size_t     maxIndices,
                              bool             isDynamic,
                              std::string_view debugName );

    // Create new vertex collector, but with shared device local buffers
    explicit VertexCollector( const VertexCollector& src,
                              MemoryAllocator&       allocator,
                              std::string_view       debugName );

    static auto CreateWithSameDeviceLocalBuffers( const VertexCollector& src,
                                                  MemoryAllocator&       allocator,
                                                  std::string_view       debugName )
        -> std::unique_ptr< VertexCollector >;

    ~VertexCollector() = default;

    VertexCollector( const VertexCollector& other )                = delete;
    VertexCollector( VertexCollector&& other ) noexcept            = delete;
    VertexCollector& operator=( const VertexCollector& other )     = delete;
    VertexCollector& operator=( VertexCollector&& other ) noexcept = delete;


    struct CopyRanges
    {
        CopyRange vertices;
        CopyRange indices;
        CopyRange texCoord1;
        CopyRange texCoord2;
        CopyRange texCoord3;

        static auto Merge( const CopyRanges& a, const CopyRanges& b )
        {
            return CopyRanges{
                .vertices  = CopyRange::merge( a.vertices, b.vertices ),
                .indices   = CopyRange::merge( a.indices, b.indices ),
                .texCoord1 = CopyRange::merge( a.texCoord1, b.texCoord1 ),
                .texCoord2 = CopyRange::merge( a.texCoord2, b.texCoord2 ),
                .texCoord3 = CopyRange::merge( a.texCoord3, b.texCoord3 ),
            };
        }

        static auto RemoveAtStart( const CopyRanges& full, const CopyRanges& toremove )
        {
            return CopyRanges{
                .vertices  = CopyRange::remove_at_start( full.vertices, toremove.vertices ),
                .indices   = CopyRange::remove_at_start( full.indices, toremove.indices ),
                .texCoord1 = CopyRange::remove_at_start( full.texCoord1, toremove.texCoord1 ),
                .texCoord2 = CopyRange::remove_at_start( full.texCoord2, toremove.texCoord2 ),
                .texCoord3 = CopyRange::remove_at_start( full.texCoord3, toremove.texCoord3 ),
            };
        }
    };
    void AllocateStaging( MemoryAllocator& alloc );
    bool CopyFromStaging( VkCommandBuffer cmd, const CopyRanges& ranges );
    bool CopyFromStaging( VkCommandBuffer cmd );
    void DeleteStaging();


    struct UploadResult
    {
        VkAccelerationStructureGeometryKHR       asGeometryInfo;
        VkAccelerationStructureBuildRangeInfoKHR asRange;
        std::optional< uint32_t >                firstIndex;
        uint32_t                                 firstVertex;
        uint32_t                                 firstVertex_Layer1;
        uint32_t                                 firstVertex_Layer2;
        uint32_t                                 firstVertex_Layer3;
    };

    auto Upload( VertexCollectorFilterTypeFlags geomFlags, const RgMeshPrimitiveInfo& prim )
        -> std::optional< UploadResult >;


    void Reset( const CopyRanges* rangeToPreserve );


    CopyRanges GetCurrentRanges() const;
    VkBuffer   GetVertexBuffer() const;
    VkBuffer   GetTexcoordBuffer_Layer1() const;
    VkBuffer   GetTexcoordBuffer_Layer2() const;
    VkBuffer   GetTexcoordBuffer_Layer3() const;
    VkBuffer   GetIndexBuffer() const;
    uint32_t   GetCurrentVertexCount() const;
    uint32_t   GetCurrentIndexCount() const;


    // begin=0: Make sure that copying was done
    // begin=1: Make sure that preprocessing is done, and prepare for use in AS build and in shaders
    void InsertVertexPreprocessBarrier( VkCommandBuffer cmd, bool begin );

private:
    void CopyDataToStaging( const RgMeshPrimitiveInfo& info,
                            uint32_t                   vertIndex,
                            std::optional< uint32_t >  indIndex,
                            uint32_t                   texcIndex_1,
                            uint32_t                   texcIndex_2,
                            uint32_t                   texcIndex_3 );

private:
    VkDevice device;


    template< typename T >
    class SharedDeviceLocal
    {
    private:
        static auto MakeName( std::string_view basename, bool isStaging )
        {
            return std::format( "{}{}", basename, isStaging ? " (staging)" : "" );
        }

    public:
        void InitStaging( MemoryAllocator& allocator )
        {
            if( !staging.IsInitted() )
            {
                if( deviceLocal && deviceLocal->GetSize() > 0 )
                {
                    staging.Init( allocator,
                                  deviceLocal->GetSize(),
                                  VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  MakeName( debugName, true ).c_str() );
                    assert( !mapped );
                    mapped = static_cast< T* >( staging.Map() );
                }
            }
        }

        void DestroyStaging()
        {
            if( mapped )
            {
                staging.TryUnmap();
                mapped = nullptr;
            }
            staging.Destroy();
        }

        explicit SharedDeviceLocal( MemoryAllocator&   allocator,
                                    size_t             maxElements,
                                    VkBufferUsageFlags usage,
                                    std::string_view   name )
            : debugName{ name }
        {
            if( maxElements > 0 )
            {
                deviceLocal = std::make_shared< Buffer >();
                deviceLocal->Init( allocator,
                                   sizeof( T ) * maxElements,
                                   usage,
                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                   MakeName( debugName, false ).c_str() );
            }
        }

        explicit SharedDeviceLocal( const SharedDeviceLocal& other,
                                    MemoryAllocator&         allocator,
                                    std::string_view         name )
            : deviceLocal{ other.deviceLocal }, debugName{ name }
        {
        }

        [[nodiscard]] bool IsInitialized() const { return deviceLocal != nullptr; }
        [[nodiscard]] auto ElementCount() const
        {
            assert( !staging.IsInitted() || deviceLocal->GetSize() == staging.GetSize() );
            assert( deviceLocal->GetSize() % sizeof( T ) == 0 );
            return deviceLocal->GetSize() / sizeof( T );
        }

        ~SharedDeviceLocal() { DestroyStaging(); }

        SharedDeviceLocal( const SharedDeviceLocal& )                = delete;
        SharedDeviceLocal( SharedDeviceLocal&& ) noexcept            = delete;
        SharedDeviceLocal& operator=( const SharedDeviceLocal& )     = delete;
        SharedDeviceLocal& operator=( SharedDeviceLocal&& ) noexcept = delete;

        std::shared_ptr< Buffer > deviceLocal{};
        Buffer                    staging{};
        T*                        mapped{ nullptr };
        std::string               debugName{};
    };


    SharedDeviceLocal< ShVertex >  bufVertices;
    SharedDeviceLocal< uint32_t >  bufIndices;
    SharedDeviceLocal< RgFloat2D > bufTexcoordLayer1;
    SharedDeviceLocal< RgFloat2D > bufTexcoordLayer2;
    SharedDeviceLocal< RgFloat2D > bufTexcoordLayer3;

    struct Count
    {
        uint32_t vertex{ 0 };
        uint32_t index{ 0 };
        uint32_t texCoord_Layer1{ 0 };
        uint32_t texCoord_Layer2{ 0 };
        uint32_t texCoord_Layer3{ 0 };
    };

    Count count{};

    // need to track offset, because Reset can be called with rangeToPreserve,
    // so need to copy from staging to device local considering offsets
    Count stagingOffset{};

    // we can't have both in one barrier, so delay:
    // VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR |
    // VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
    struct
    {
        VkBufferMemoryBarrier2 barriers[ 2 ]{};
        uint32_t               barriers_count{ 0 };
    } afterBuild{};
};

}