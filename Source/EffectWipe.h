// Copyright (c) 2022 Sultim Tsyrendashiev
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

#include "EffectBase.h"
#include "Utils.h"

namespace RTGL1
{

struct EffectWipe final : EffectBase
{
    struct PushConst
    {
        uint32_t stripWidthInPixels;
        uint32_t startFrameId;
        float beginTime;
        float endTime;
    };

    explicit EffectWipe( VkDevice             device,
                         const Framebuffers&  _framebuffers,
                         const GlobalUniform& _uniform,
                         const BlueNoise&     _blueNoise,
                         const ShaderManager& _shaderManager,
                         bool                 _effectWipeIsUsed )
        : EffectBase{ device }, push{}, effectWipeIsUsed{ _effectWipeIsUsed }
    {
        VkDescriptorSetLayout setLayouts[] = {
            _framebuffers.GetDescSetLayout(),
            _uniform.GetDescSetLayout(),
            _blueNoise.GetDescSetLayout(),
        };

        InitBase( _shaderManager, setLayouts, PushConst() );
    }

    void CopyToWipeEffectSourceIfNeeded( VkCommandBuffer         cmd,
                                         uint32_t                frameIndex,
                                         Framebuffers&           framebuffers,
                                         FramebufferImageIndex   previouslyPresented,
                                         const ResolutionState&  resolution,
                                         const RgPostEffectWipe* params )
    {
        constexpr auto subres = VkImageSubresourceLayers{
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel       = 0,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        };
        constexpr auto subresRange = VkImageSubresourceRange{
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        };

        static auto l_offsetFromExtent = []( const VkExtent2D& e ) {
            return VkOffset3D{ static_cast< int32_t >( e.width ),
                               static_cast< int32_t >( e.height ),
                               1 };
        };

        if( !params || !params->beginNow )
        {
            return;
        }

        VkImage srcImage = framebuffers.GetImage( previouslyPresented, frameIndex );
        VkImage dstImage = framebuffers.GetImage( FB_IMAGE_INDEX_WIPE_EFFECT_SOURCE, frameIndex );
        if( !srcImage )
        {
            debug::Warning( "Suppressed wipe effect: Prev image is invalid" );
            return;
        }
        if( !dstImage )
        {
            debug::Warning( "Suppressed wipe effect: WIPE_EFFECT_SOURCE is invalid" );
            return;
        }

        const auto srcSize = framebuffers.GetFramebufSize( resolution, //
                                                           previouslyPresented );
        const auto dstSize = framebuffers.GetFramebufSize( resolution, //
                                                           FB_IMAGE_INDEX_WIPE_EFFECT_SOURCE );

        {
            VkImageMemoryBarrier2 bs[] = {
                {
                    .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2_KHR,
                    .srcStageMask        = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    .srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT,
                    .dstStageMask        = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    .dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT,
                    .oldLayout           = VK_IMAGE_LAYOUT_GENERAL,
                    .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image               = srcImage,
                    .subresourceRange    = subresRange,
                },
                {
                    .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2_KHR,
                    .srcStageMask        = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    .srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT,
                    .dstStageMask        = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    .dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
                    .oldLayout           = VK_IMAGE_LAYOUT_GENERAL,
                    .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image               = dstImage,
                    .subresourceRange    = subresRange,
                },
            };

            auto dep = VkDependencyInfo{
                .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .imageMemoryBarrierCount = std::size( bs ),
                .pImageMemoryBarriers    = bs,
            };

            svkCmdPipelineBarrier2KHR( cmd, &dep );
        }

        if( srcSize.width == dstSize.width && //
            srcSize.height == dstSize.height &&
            ShFramebuffers_Formats[ previouslyPresented ] ==
                ShFramebuffers_Formats[ FB_IMAGE_INDEX_WIPE_EFFECT_SOURCE ] )
        {
            auto region = VkImageCopy{
                .srcSubresource = subres,
                .srcOffset      = { 0, 0, 0 },
                .dstSubresource = subres,
                .dstOffset      = { 0, 0, 0 },
                .extent         = { srcSize.width, srcSize.height, 1 },
            };

            vkCmdCopyImage( cmd,
                            srcImage,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            dstImage,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            1,
                            &region );
        }
        else
        {
            auto region = VkImageBlit{
                .srcSubresource = subres,
                .srcOffsets     = { { 0, 0, 0 }, l_offsetFromExtent( srcSize ) },
                .dstSubresource = subres,
                .dstOffsets     = { { 0, 0, 0 }, l_offsetFromExtent( dstSize ) },
            };

            vkCmdBlitImage( cmd,
                            srcImage,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            dstImage,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            1,
                            &region,
                            VK_FILTER_NEAREST );
        }

        {
            VkImageMemoryBarrier2 bs[] = {
                {
                    .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2_KHR,
                    .srcStageMask        = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    .srcAccessMask       = VK_ACCESS_TRANSFER_READ_BIT,
                    .dstStageMask        = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    .dstAccessMask       = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
                    .oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    .newLayout           = VK_IMAGE_LAYOUT_GENERAL,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image               = srcImage,
                    .subresourceRange    = subresRange,
                },
                {
                    .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2_KHR,
                    .srcStageMask        = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    .srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
                    .dstStageMask        = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    .dstAccessMask       = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
                    .oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    .newLayout           = VK_IMAGE_LAYOUT_GENERAL,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image               = dstImage,
                    .subresourceRange    = subresRange,
                },
            };

            auto dep = VkDependencyInfo{
                .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .imageMemoryBarrierCount = std::size( bs ),
                .pImageMemoryBarriers    = bs,
            };

            svkCmdPipelineBarrier2KHR( cmd, &dep );
        }
    }

    bool Setup( const CommonnlyUsedEffectArguments& args,
                const RgPostEffectWipe*             params,
                uint32_t                            currentFrameId )
    {
        if (params == nullptr)
        {
            return false;
        }

        if( !effectWipeIsUsed )
        {
            // if this effect is not used, params must be null
            assert( params == nullptr );
            return false;
        }
        
        push.stripWidthInPixels = (uint32_t)((float)args.width * clamp(params->stripWidth, 0.0f, 1.0f));

        if (params->beginNow)
        {
            push.startFrameId = currentFrameId;
            push.beginTime = args.currentTime;
            push.endTime = args.currentTime + params->duration;
        }

        if (push.stripWidthInPixels == 0 || 
            push.beginTime >= push.endTime ||
            args.currentTime >= push.endTime)
        {
            return false;
        }

        return true;
    }

    FramebufferImageIndex Apply( const CommonnlyUsedEffectArguments& args,
                                 const BlueNoise&                    blueNoise,
                                 FramebufferImageIndex               inputFramebuf )
    {
        VkDescriptorSet descSets[] =
        {
            args.framebuffers->GetDescSet(args.frameIndex),
            args.uniform->GetDescSet(args.frameIndex),
            blueNoise.GetDescSet(),
        };

        return Dispatch(args.cmd, args.frameIndex, args.framebuffers, args.width, args.height, inputFramebuf, descSets);
    }

protected:
    const char *GetShaderName() const override
    {
        return "EffectWipe";
    }

    bool GetPushConstData(uint8_t (&pData)[128], uint32_t *pDataSize) const override
    {
        memcpy(pData, &push, sizeof(push));
        *pDataSize = sizeof(push);
        return true;
    }

private:
    PushConst push;
    bool      effectWipeIsUsed;
};

}
