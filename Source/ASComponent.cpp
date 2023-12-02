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

#include "ASComponent.h"

namespace
{
VkDeviceAddress FetchASAddress( VkDevice device, VkAccelerationStructureKHR as )
{
    assert( device != VK_NULL_HANDLE );
    assert( as != VK_NULL_HANDLE );

    auto addressInfo = VkAccelerationStructureDeviceAddressInfoKHR{
        .sType                 = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
        .accelerationStructure = as,
    };
    return RTGL1::svkGetAccelerationStructureDeviceAddressKHR( device, &addressInfo );
}
}

RTGL1::ASComponent::ASComponent( VkDevice _device, const char* _debugName )
    : device{ _device }, as{ VK_NULL_HANDLE }, asSize{ 0 }, asAddress{ 0 }, debugName{ _debugName }
{
}

RTGL1::BLASComponent::BLASComponent( VkDevice _device, const char* _debugName )
    : ASComponent{ _device, _debugName }
{
}

RTGL1::TLASComponent::TLASComponent( VkDevice _device, const char* _debugName )
    : ASComponent{ _device, _debugName }
{
}

auto RTGL1::ASComponent::CreateAS( VkBuffer buf, VkDeviceSize offset, VkDeviceSize size ) const
    -> VkAccelerationStructureKHR
{
    assert( device != VK_NULL_HANDLE );

    VkAccelerationStructureCreateInfoKHR info = {
        .sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
        .buffer = buf,
        .offset = offset,
        .size   = size,
        .type   = GetType(),
    };
    assert( info.offset % 256 == 0 );

    VkAccelerationStructureKHR resultAS = nullptr;
    VkResult r = svkCreateAccelerationStructureKHR( device, &info, nullptr, &resultAS );
    VK_CHECKERROR( r );

    SET_DEBUG_NAME( device, resultAS, VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR, debugName );

    return resultAS;
}

void RTGL1::ASComponent::DestroyAS()
{
    if( as != VK_NULL_HANDLE )
    {
        svkDestroyAccelerationStructureKHR( device, as, nullptr );
        as = VK_NULL_HANDLE;
    }
    asSize = 0;
    asAddress = 0;
}

bool RTGL1::ASComponent::RecreateIfNotValid(
    const VkAccelerationStructureBuildSizesInfoKHR& buildSizes,
    ChunkedStackAllocator&                          allocator,
    bool                                            resetAllocOnCreate )
{
    if( !as || asSize < buildSizes.accelerationStructureSize )
    {
        // destroy
        DestroyAS();

        if( resetAllocOnCreate )
        {
            allocator.Reset();
        }

        // get range in common buffer, and create
        const auto allocation = allocator.Push( buildSizes.accelerationStructureSize );

        as = CreateAS(
            allocation.buffer, allocation.offsetInBuffer, buildSizes.accelerationStructureSize );
        asSize = buildSizes.accelerationStructureSize;
        asAddress = FetchASAddress( device, as );

        return true;
    }
    return false;
}
