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

#include "ScratchBuffer.h"

#include <algorithm>

#include "RgException.h"
#include "Utils.h"

RTGL1::ChunkedStackAllocator::ChunkedStackAllocator( std::shared_ptr< MemoryAllocator >& _allocator,
                                                     VkBufferUsageFlags                  _usage,
                                                     VkDeviceSize     _initialChunkSize,
                                                     VkDeviceSize     _alignment,
                                                     std::string_view _debugName )
    : allocator{ _allocator }
    , usage{ _usage }
    , chunkAllocSize{ Utils::Align( _initialChunkSize, _alignment ) }
    , alignment{ _alignment }
    , debugName{ _debugName }
{
}

auto RTGL1::ChunkedStackAllocator::Push( VkDeviceSize size ) -> PushResult
{
    const VkDeviceSize alignedSize = Utils::Align( size, alignment );

    // find fitting chunk
    for( auto& c : chunks )
    {
        if( c.currentOffset + alignedSize < c.buffer.GetSize() )
        {
            const auto result = PushResult{
                .address        = c.buffer.GetAddress() + c.currentOffset,
                .buffer         = c.buffer.GetBuffer(),
                .offsetInBuffer = c.currentOffset,
            };

            assert( result.offsetInBuffer % alignment == 0 );
            assert( result.address % alignment == 0 );

            c.currentOffset += alignedSize;

            return result;
        }
    }

    // couldn't find chunk, create new one
    return AllocateChunk( alignedSize );
}

void RTGL1::ChunkedStackAllocator::Reset()
{
    for( auto& c : chunks )
    {
        c.currentOffset = 0;
    }
}

auto RTGL1::ChunkedStackAllocator::AllocateChunk( VkDeviceSize size ) -> PushResult
{
    const auto chunkSize = std::max( chunkAllocSize, Utils::Align( size, alignment ) );

    if( const auto alloc = allocator.lock() )
    {
        auto& c = chunks.emplace_back();
        c.buffer.Init( *alloc,
                       chunkSize,
                       VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | usage,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                       debugName.c_str() );

        if( c.buffer.GetAddress() % alignment != 0 )
        {
            throw RgException( RG_RESULT_ERROR_MEMORY_ALIGNMENT,
                               "Allocated VkBuffer's address was not aligned" );
        }

        const auto result = PushResult{
            .address        = c.buffer.GetAddress(),
            .buffer         = c.buffer.GetBuffer(),
            .offsetInBuffer = 0,
        };
        assert( result.address % alignment == 0 );

        // initial offset
        c.currentOffset = 0;
        c.currentOffset += Utils::Align( size, alignment );

        return result;
    }

    assert( 0 );
    return {};
}
