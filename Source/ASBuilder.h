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

#include "Common.h"
#include "ScratchBuffer.h"

#include <vector>
#include <span>

namespace RTGL1
{

class ASBuilder
{
public:
    explicit ASBuilder( VkDevice device, std::shared_ptr< ScratchBuffer > commonScratchBuffer );
    ~ASBuilder() = default;

    ASBuilder( const ASBuilder& other )     = delete;
    ASBuilder( ASBuilder&& other ) noexcept = delete;
    ASBuilder& operator=( const ASBuilder& other ) = delete;
    ASBuilder& operator=( ASBuilder&& other ) noexcept = delete;


    // pGeometries is a pointer to an array of size "geometryCount",
    // pRangeInfos is an array of size "geometryCount".
    // All pointers must be valid until BuildBottomLevel is called
    void AddBLAS( VkAccelerationStructureKHR                                  as,
                  std::span< const VkAccelerationStructureGeometryKHR >       geometries,
                  std::span< const VkAccelerationStructureBuildRangeInfoKHR > rangeInfos,
                  const VkAccelerationStructureBuildSizesInfoKHR&             buildSizes,
                  bool                                                        fastTrace,
                  bool                                                        update,
                  bool                                                        isBLASUpdateable );

    bool BuildBottomLevel( VkCommandBuffer cmd );


    // pGeometry is a pointer to one AS geometry,
    // pRangeInfo is a pointer to build range info.
    // All pointers must be valid until BuildTopLevel is called
    void AddTLAS( VkAccelerationStructureKHR                      as,
                  const VkAccelerationStructureGeometryKHR*       instance,
                  const VkAccelerationStructureBuildRangeInfoKHR* rangeInfo,
                  const VkAccelerationStructureBuildSizesInfoKHR& buildSizes,
                  bool                                            fastTrace,
                  bool                                            update );

    bool BuildTopLevel( VkCommandBuffer cmd );


private:
    auto GetBuildSizes( VkAccelerationStructureTypeKHR                  type,
                        std::span< const VkAccelerationStructureGeometryKHR > geometries,
                        std::span< const uint32_t > maxPrimitiveCountPerGeometry,
                        bool fastTrace ) const -> VkAccelerationStructureBuildSizesInfoKHR;

public:
    auto GetBottomBuildSizes( std::span< const VkAccelerationStructureGeometryKHR > geometries,
                              std::span< const uint32_t > maxPrimitiveCountPerGeometry,
                              bool fastTrace ) const -> VkAccelerationStructureBuildSizesInfoKHR
    {
        return GetBuildSizes( VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
                              geometries,
                              maxPrimitiveCountPerGeometry,
                              fastTrace );
    }


    auto GetTopBuildSizes( const VkAccelerationStructureGeometryKHR& instance,
                           uint32_t                                  maxPrimitiveCountInInstance,
                           bool                                      fastTrace ) const
    {
        return GetBuildSizes( VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
                              { &instance, 1 },
                              { &maxPrimitiveCountInInstance, 1 },
                              fastTrace );
    }

    bool IsEmpty() const;

private:
    VkDevice                         device;
    std::shared_ptr< ScratchBuffer > scratchBuffer;

    struct BuildInfo
    {
        std::vector< VkAccelerationStructureBuildGeometryInfoKHR >     geomInfos;
        std::vector< const VkAccelerationStructureBuildRangeInfoKHR* > rangeInfos;
    };

    BuildInfo bottomLBuildInfo;
    BuildInfo topLBuildInfo;
};

}