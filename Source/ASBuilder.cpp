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

#include "ASBuilder.h"

#include <algorithm>
#include <utility>

#include "Utils.h"

using namespace RTGL1;

ASBuilder::ASBuilder( VkDevice _device, std::shared_ptr< ScratchBuffer > _commonScratchBuffer )
    : device( _device ), scratchBuffer( std::move( _commonScratchBuffer ) )
{
}

auto ASBuilder::GetBuildSizes( VkAccelerationStructureTypeKHR                  type,
                               std::span< const VkAccelerationStructureGeometryKHR > geometries,
                               std::span< const uint32_t > maxPrimitiveCountPerGeometry,
                               bool fastTrace ) const -> VkAccelerationStructureBuildSizesInfoKHR
{
    assert( !geometries.empty() );
    assert( geometries.size() == maxPrimitiveCountPerGeometry.size() );

    VkBuildAccelerationStructureFlagsKHR flags =
        fastTrace ? VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR
                  : VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;

    // mode, srcAccelerationStructure, dstAccelerationStructure
    // and all VkDeviceOrHostAddressKHR except transformData are ignored
    // in vkGetAccelerationStructureBuildSizesKHR(..)
    auto buildInfo = VkAccelerationStructureBuildGeometryInfoKHR{
        .sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type          = type,
        .flags         = flags,
        .geometryCount = static_cast< uint32_t >( geometries.size() ),
        .pGeometries   = geometries.data(),
        .ppGeometries  = nullptr,
    };

    VkAccelerationStructureBuildSizesInfoKHR sizeInfo = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
    };

    svkGetAccelerationStructureBuildSizesKHR( device,
                                              VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                              &buildInfo,
                                              maxPrimitiveCountPerGeometry.data(),
                                              &sizeInfo );

    return sizeInfo;
}


void ASBuilder::AddBLAS( VkAccelerationStructureKHR                                  as,
                         std::span< const VkAccelerationStructureGeometryKHR >       geometries,
                         std::span< const VkAccelerationStructureBuildRangeInfoKHR > rangeInfos,
                         const VkAccelerationStructureBuildSizesInfoKHR&             buildSizes,
                         bool                                                        fastTrace,
                         bool                                                        update,
                         bool isBLASUpdateable )
{
    // while building bottom level, top level must be not
    assert( topLBuildInfo.geomInfos.empty() && topLBuildInfo.rangeInfos.empty() );

    assert( !geometries.empty() );
    assert( geometries.size() == rangeInfos.size() );

    VkDeviceSize scratchSize =
        std::max( buildSizes.updateScratchSize, buildSizes.buildScratchSize );

    VkBuildAccelerationStructureFlagsKHR flags =
        fastTrace ? VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR
                  : VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;

    if( isBLASUpdateable || update )
    {
        flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    }

    auto buildInfo = VkAccelerationStructureBuildGeometryInfoKHR{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type  = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
        .flags = flags,
        .mode  = update ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR
                        : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        .srcAccelerationStructure = update ? as : VK_NULL_HANDLE,
        .dstAccelerationStructure = as,
        .geometryCount            = static_cast<uint32_t>(geometries.size()),
        .pGeometries              = geometries.data(),
        .ppGeometries             = nullptr,
        .scratchData = {
            .deviceAddress = scratchBuffer->GetScratchAddress( scratchSize ),
        },
    };

    bottomLBuildInfo.geomInfos.push_back( buildInfo );
    bottomLBuildInfo.rangeInfos.push_back( rangeInfos.data() );
}

bool ASBuilder::BuildBottomLevel( VkCommandBuffer cmd )
{
    assert( bottomLBuildInfo.geomInfos.size() == bottomLBuildInfo.rangeInfos.size() );

    if( bottomLBuildInfo.geomInfos.empty() )
    {
        return false;
    }

    // build bottom level
    svkCmdBuildAccelerationStructuresKHR(
        cmd,
        static_cast< uint32_t >( bottomLBuildInfo.geomInfos.size() ),
        bottomLBuildInfo.geomInfos.data(),
        bottomLBuildInfo.rangeInfos.data() );

    bottomLBuildInfo.geomInfos.clear();
    bottomLBuildInfo.rangeInfos.clear();

    return true;
}

void ASBuilder::AddTLAS( VkAccelerationStructureKHR                      as,
                         const VkAccelerationStructureGeometryKHR*       instance,
                         const VkAccelerationStructureBuildRangeInfoKHR* rangeInfo,
                         const VkAccelerationStructureBuildSizesInfoKHR& buildSizes,
                         bool                                            fastTrace,
                         bool                                            update )
{
    // while building top level, bottom level must be not
    assert( bottomLBuildInfo.geomInfos.empty() && bottomLBuildInfo.rangeInfos.empty() );

    VkDeviceSize scratchSize = update ? buildSizes.updateScratchSize : buildSizes.buildScratchSize;

    VkBuildAccelerationStructureFlagsKHR flags =
        fastTrace ? VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR
                  : VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type  = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
        .flags = flags,
        .mode  = update ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR
                        : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        .srcAccelerationStructure = update ? as : VK_NULL_HANDLE,
        .dstAccelerationStructure = as,
        .geometryCount            = 1,
        .pGeometries              = instance,
        .ppGeometries             = nullptr,
        .scratchData = {
            .deviceAddress = scratchBuffer->GetScratchAddress( scratchSize ),
        },
    };

    topLBuildInfo.geomInfos.push_back( buildInfo );
    topLBuildInfo.rangeInfos.push_back( rangeInfo );
}

bool ASBuilder::BuildTopLevel( VkCommandBuffer cmd )
{
    assert( topLBuildInfo.geomInfos.size() == topLBuildInfo.rangeInfos.size() );

    if( topLBuildInfo.geomInfos.empty() )
    {
        return false;
    }

    // build bottom level
    svkCmdBuildAccelerationStructuresKHR( cmd,
                                          topLBuildInfo.geomInfos.size(),
                                          topLBuildInfo.geomInfos.data(),
                                          topLBuildInfo.rangeInfos.data() );

    topLBuildInfo.geomInfos.clear();
    topLBuildInfo.rangeInfos.clear();

    return true;
}

bool ASBuilder::IsEmpty() const
{
    return bottomLBuildInfo.geomInfos.empty() && bottomLBuildInfo.rangeInfos.empty() &&
           topLBuildInfo.geomInfos.empty() && topLBuildInfo.rangeInfos.empty();
}