/*

Copyright (c) 2024 V.Shirokii

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

#pragma once

#include "Common.h"
#include "Framebuffers.h"
#include "Generated/ShaderCommonCFramebuf.h"
#include "ResolutionState.h"

#include "DX12_Interop.h"

#include <ranges>

namespace RTGL1
{

namespace detail
{
    template< bool BackToOriginal, size_t N, size_t K >
    void InsertBarriersForCopy( VkCommandBuffer cmd,
                                const VkImage ( &src )[ N ],
                                const VkImage ( &dst )[ K ] )
    {
        constexpr auto subres = VkImageSubresourceRange{
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        };

        VkImageMemoryBarrier2 barriers[ N + K ];

        for( size_t i = 0; i < N + K; i++ )
        {
            bool    issrc = i < N;
            VkImage img   = ( issrc ? src[ i ] : dst[ i - N ] );

            barriers[ i ] = VkImageMemoryBarrier2{
                .sType        = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR |
                                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT |
                                 VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                .dstStageMask        = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .dstAccessMask       = issrc ? VK_ACCESS_2_TRANSFER_READ_BIT //
                                             : VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .oldLayout           = VK_IMAGE_LAYOUT_GENERAL,
                .newLayout           = issrc ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL //
                                             : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image               = img,
                .subresourceRange    = subres,
            };

            if( BackToOriginal )
            {
                VkImageMemoryBarrier2& b = barriers[ i ];

                std::swap( b.srcStageMask, b.dstStageMask );
                std::swap( b.srcAccessMask, b.dstAccessMask );
                std::swap( b.oldLayout, b.newLayout );
                std::swap( b.srcQueueFamilyIndex, b.dstQueueFamilyIndex );
            }
        }

        auto dependencyInfo = VkDependencyInfoKHR{
            .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR,
            .imageMemoryBarrierCount = uint32_t( std::size( barriers ) ),
            .pImageMemoryBarriers    = barriers,
        };

        svkCmdPipelineBarrier2KHR( cmd, &dependencyInfo );
    }
}

template< size_t N >
void Framebuf_CopyVkToDX12( VkCommandBuffer     cmd,
                            uint32_t            frameIndex,
                            const Framebuffers& framebuffers,
                            uint32_t            width,
                            uint32_t            height,
                            const FramebufferImageIndex ( &imagesToDX12 )[ N ] )
{
    constexpr auto subres = VkImageSubresourceLayers{
        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
        .mipLevel       = 0,
        .baseArrayLayer = 0,
        .layerCount     = 1,
    };
    const auto region = VkImageCopy{
        .srcSubresource = subres,
        .srcOffset      = {},
        .dstSubresource = subres,
        .dstOffset      = {},
        .extent         = { width, height, 1 },
    };

    VkImage src[ N ];
    VkImage dst[ N ];
    for( size_t i = 0; i < N; i++ )
    {
        src[ i ] = framebuffers.GetImage( imagesToDX12[ i ], frameIndex );
        dst[ i ] = dxgi::Framebuf_GetVkDx12Shared( imagesToDX12[ i ] ).vkimage;
    }


    detail::InsertBarriersForCopy< false >( cmd, src, dst );

    for( auto [ s, d ] : std::views::zip( src, dst ) )
    {
        vkCmdCopyImage( cmd,
                        s,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        d,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        1,
                        &region );
    }

    detail::InsertBarriersForCopy< true >( cmd, src, dst );
}

template< size_t N >
void Framebuf_CopyDX12ToVk( VkCommandBuffer     cmd,
                            uint32_t            frameIndex,
                            const Framebuffers& framebuffers,
                            uint32_t            width,
                            uint32_t            height,
                            const FramebufferImageIndex ( &imagesToVk )[ N ] )
{
    constexpr auto subres = VkImageSubresourceLayers{
        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
        .mipLevel       = 0,
        .baseArrayLayer = 0,
        .layerCount     = 1,
    };
    const auto region = VkImageCopy{
        .srcSubresource = subres,
        .srcOffset      = {},
        .dstSubresource = subres,
        .dstOffset      = {},
        .extent         = { width, height, 1 },
    };

    VkImage src[ N ] = {};
    VkImage dst[ N ] = {};
    for( size_t i = 0; i < N; i++ )
    {
        src[ i ] = dxgi::Framebuf_GetVkDx12Shared( imagesToVk[ i ] ).vkimage;
        dst[ i ] = framebuffers.GetImage( imagesToVk[ i ], frameIndex );
    }


    detail::InsertBarriersForCopy< false >( cmd, src, dst );

    vkCmdCopyImage( cmd,
                    src[ 0 ],
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    dst[ 0 ],
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    1,
                    &region );

    detail::InsertBarriersForCopy< true >( cmd, src, dst );
}

}
