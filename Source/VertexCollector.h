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
#include "UniqueID.h"

#include "RTGL1/RTGL1.h"

namespace RTGL1
{

struct ShVertex;

class GeomInfoManager;

// The class collects vertex data to buffers with shader struct types.
// Geometries are passed to the class by chunks and the result of collecting
// is a vertex buffer with ready data and infos for acceleration structure creation/building.
class VertexCollector
{
public:
    explicit VertexCollector( VkDevice         device,
                              MemoryAllocator& allocator,
                              const uint32_t ( &maxVertsPerLayer )[ 4 ],
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

    auto Upload( VertexCollectorFilterTypeFlags geomFlags,
                 const RgMeshInfo&              mesh,
                 const RgMeshPrimitiveInfo&     prim ) -> std::optional< UploadResult >;


    // Clear data that was generated while collecting.
    // Should be called when blasGeometries is not needed anymore
    void Reset();
    // Copy buffer from staging and set barrier for processing in compute shader
    // "isStaticVertexData" is required to determine what GLSL struct to use for copying
    bool CopyFromStaging( VkCommandBuffer cmd );


    VkBuffer GetVertexBuffer() const;
    VkBuffer GetTexcoordBuffer_Layer1() const;
    VkBuffer GetTexcoordBuffer_Layer2() const;
    VkBuffer GetTexcoordBuffer_Layer3() const;
    VkBuffer GetIndexBuffer() const;
    uint32_t GetCurrentVertexCount() const;
    uint32_t GetCurrentIndexCount() const;


    // Make sure that copying was done
    void InsertVertexPreprocessBeginBarrier( VkCommandBuffer cmd );
    // Make sure that preprocessing is done, and prepare for use in AS build and in shaders
    void InsertVertexPreprocessFinishBarrier( VkCommandBuffer cmd );

private:
    void CopyVertexDataToStaging( const RgMeshPrimitiveInfo& info,
                                  uint32_t                   vertIndex,
                                  uint32_t                   texcIndex_1,
                                  uint32_t                   texcIndex_2,
                                  uint32_t                   texcIndex_3 );

    bool CopyVertexDataFromStaging( VkCommandBuffer cmd );
    bool CopyTexCoordsFromStaging( VkCommandBuffer cmd, uint32_t layerIndex );
    bool CopyIndexDataFromStaging( VkCommandBuffer cmd );

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

        void Init( std::shared_ptr< Buffer > otherDeviceLocal,
                   MemoryAllocator&          allocator,
                   std::string_view          name )
        {
            deviceLocal = std::move( otherDeviceLocal );
            assert( deviceLocal->GetSize() > 0 );

            staging.Init( allocator,
                          deviceLocal->GetSize(),
                          VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          MakeName( name, true ).c_str() );
            mapped = static_cast< T* >( staging.Map() );
        }

    public:
        explicit SharedDeviceLocal( MemoryAllocator&   allocator,
                                    uint32_t           maxElements,
                                    VkBufferUsageFlags usage,
                                    std::string_view   name )
        {
            if( maxElements > 0 )
            {
                deviceLocal = std::make_shared< Buffer >();
                deviceLocal->Init( allocator,
                                   sizeof( T ) * maxElements,
                                   usage,
                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                   MakeName( name, false ).c_str() );
                Init( deviceLocal, allocator, name );
            }
        }

        explicit SharedDeviceLocal( const SharedDeviceLocal& other,
                                    MemoryAllocator&         allocator,
                                    std::string_view         name )
        {
            if( other.IsInitialized() )
            {
                Init( other.deviceLocal, allocator, name );
            }
        }

        [[nodiscard]] bool IsInitialized() const { return deviceLocal != nullptr; }

        ~SharedDeviceLocal()
        {
            if( IsInitialized() )
            {
                staging.TryUnmap();
            }
        }

        SharedDeviceLocal( const SharedDeviceLocal& )                = delete;
        SharedDeviceLocal( SharedDeviceLocal&& ) noexcept            = delete;
        SharedDeviceLocal& operator=( const SharedDeviceLocal& )     = delete;
        SharedDeviceLocal& operator=( SharedDeviceLocal&& ) noexcept = delete;

        std::shared_ptr< Buffer > deviceLocal{};
        Buffer                    staging{};
        T*                        mapped{ nullptr };
    };


    SharedDeviceLocal< ShVertex >             bufVertices;
    SharedDeviceLocal< uint32_t >             bufIndices;
    SharedDeviceLocal< RgFloat2D >            bufTexcoordLayer1;
    SharedDeviceLocal< RgFloat2D >            bufTexcoordLayer2;
    SharedDeviceLocal< RgFloat2D >            bufTexcoordLayer3;

    uint32_t curVertexCount{ 0 };
    uint32_t curIndexCount{ 0 };
    uint32_t curPrimitiveCount{ 0 };
    uint32_t curTexCoordCount_Layer1{ 0 };
    uint32_t curTexCoordCount_Layer2{ 0 };
    uint32_t curTexCoordCount_Layer3{ 0 };
};

}