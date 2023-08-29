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

#pragma once

#include <vector>

#include "Common.h"
#include "Buffer.h"
#include "ScratchBuffer.h"

namespace RTGL1
{

struct ASComponent
{
protected:
    explicit ASComponent( VkDevice device, const char* debugName );

public:
    virtual ~ASComponent() = 0;

    ASComponent( const ASComponent& other )                = delete;
    ASComponent( ASComponent&& other ) noexcept            = delete;
    ASComponent& operator=( const ASComponent& other )     = delete;
    ASComponent& operator=( ASComponent&& other ) noexcept = delete;

    void RecreateIfNotValid( const VkAccelerationStructureBuildSizesInfoKHR& buildSizes,
                             ChunkedStackAllocator&                          allocator );

    VkAccelerationStructureKHR GetAS() const;
    VkDeviceAddress            GetASAddress() const;

protected:
    virtual VkAccelerationStructureTypeKHR GetType() const = 0;

private:
    [[nodiscard]] auto CreateAS( VkBuffer buf, VkDeviceSize offset, VkDeviceSize size ) const
        -> VkAccelerationStructureKHR;
    void DestroyAS();

    VkDeviceAddress GetASAddress( VkAccelerationStructureKHR as ) const;

protected:
    VkDevice                   device;
    VkAccelerationStructureKHR as;
    VkDeviceSize               asSize;

    const char* debugName;
};


struct BLASComponent final : public ASComponent
{
public:
    explicit BLASComponent( VkDevice device, const char* debugName = nullptr );

protected:
    VkAccelerationStructureTypeKHR GetType() const override
    {
        return VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    }
};


struct TLASComponent final : public ASComponent
{
public:
    explicit TLASComponent( VkDevice device, const char* debugName = nullptr );

protected:
    VkAccelerationStructureTypeKHR GetType() const override
    {
        return VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    }
};

}