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

#include "Bloom.h"

#include "CmdLabel.h"
#include "RenderResolutionHelper.h"
#include "TextureManager.h"
#include "Utils.h"

#include "Generated/ShaderCommonC.h"

namespace
{
VkPipelineLayout CreatePipelineLayout( VkDevice                           device,
                                       std::span< VkDescriptorSetLayout > setLayouts,
                                       std::string_view                   name )
{
    VkPipelineLayout layout = VK_NULL_HANDLE;

    VkPipelineLayoutCreateInfo info = {
        .sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = static_cast< uint32_t >( setLayouts.size() ),
        .pSetLayouts    = setLayouts.data(),
    };

    VkResult r = vkCreatePipelineLayout( device, &info, nullptr, &layout );
    RTGL1::VK_CHECKERROR( r );

    SET_DEBUG_NAME( device, layout, VK_OBJECT_TYPE_PIPELINE_LAYOUT, name.data() );

    return layout;
}
}

RTGL1::Bloom::Bloom( VkDevice                        _device,
                     std::shared_ptr< Framebuffers > _framebuffers,
                     const ShaderManager&            _shaderManager,
                     const GlobalUniform&            _uniform,
                     const TextureManager&           _textureManager,
                     const Tonemapping&              _tonemapping )
    : device( _device )
    , framebuffers( std::move( _framebuffers ) )
    , pipelineLayout( VK_NULL_HANDLE )
    , downsamplePipelines{}
    , upsamplePipelines{}
    , preloadPipelines{}
    , applyPipelines{}
{
    {
        VkDescriptorSetLayout setLayouts[] = {
            framebuffers->GetDescSetLayout(),
            _uniform.GetDescSetLayout(),
            _tonemapping.GetDescSetLayout(),
            _textureManager.GetDescSetLayout(),
        };
        pipelineLayout = CreatePipelineLayout( device, setLayouts, "Bloom layout" );
    }
    CreatePipelines( &_shaderManager );

    static_assert( StepCount == COMPUTE_BLOOM_STEP_COUNT, "Recheck COMPUTE_BLOOM_STEP_COUNT" );
}

RTGL1::Bloom::~Bloom()
{
    vkDestroyPipelineLayout( device, pipelineLayout, nullptr );
    DestroyPipelines();
}

RTGL1::FramebufferImageIndex RTGL1::Bloom::Apply( VkCommandBuffer       cmd,
                                                  uint32_t              frameIndex,
                                                  const GlobalUniform&  uniform,
                                                  const Tonemapping&    tonemapping,
                                                  const TextureManager& textureManager,
                                                  uint32_t              upscaledWidth,
                                                  uint32_t              upscaledHeight,
                                                  FramebufferImageIndex inputFramebuf )
{
    auto blabel = CmdLabel{ cmd, "Bloom" };

    // SHIPPING_HACK - this barrier is too strict, but at some point,
    //                 there were bugs with incorrect sync between bloom passes
    auto memoryBarrier = VkMemoryBarrier2KHR{
        .sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2_KHR,
        .srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
        .srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
        .dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
        .dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT_KHR | VK_ACCESS_2_SHADER_READ_BIT_KHR,
    };
    auto dependencyInfo = VkDependencyInfoKHR{
        .sType              = VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR,
        .memoryBarrierCount = 1,
        .pMemoryBarriers    = &memoryBarrier,
    };

    // bind desc sets
    VkDescriptorSet sets[] = {
        framebuffers->GetDescSet( frameIndex ),
        uniform.GetDescSet( frameIndex ),
        tonemapping.GetDescSet(),
        textureManager.GetDescSet( frameIndex ),
    };

    vkCmdBindDescriptorSets( cmd,
                             VK_PIPELINE_BIND_POINT_COMPUTE,
                             pipelineLayout,
                             0,
                             std::size( sets ),
                             sets,
                             0,
                             nullptr );


    {
        const auto src = inputFramebuf;
        const auto dst = FB_IMAGE_INDEX_BLOOM;
        const auto sz  = MakeSize( upscaledWidth, upscaledHeight, dst );

        assert( src == FB_IMAGE_INDEX_UPSCALED_PING || src == FB_IMAGE_INDEX_UPSCALED_PONG );
        bool isSourcePing = ( src == FB_IMAGE_INDEX_UPSCALED_PING ? 1 : 0 );

        framebuffers->BarrierOne( cmd, frameIndex, src );

        vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, preloadPipelines[ isSourcePing ] );
        vkCmdDispatch( cmd,
                       Utils::GetWorkGroupCount( sz.width, COMPUTE_BLOOM_APPLY_GROUP_SIZE_X ),
                       Utils::GetWorkGroupCount( sz.height, COMPUTE_BLOOM_APPLY_GROUP_SIZE_Y ),
                       1 );
    }


    svkCmdPipelineBarrier2KHR( cmd, &dependencyInfo );


    for( int i = 0; i < COMPUTE_BLOOM_STEP_COUNT; i++ )
    {
        auto label = CmdLabel{ cmd, "Bloom downsample" };

        // clang-format off
        auto src = FramebufferImageIndex{};
        auto dst = FramebufferImageIndex{};
        switch( i )
        {
            case 0: src = FB_IMAGE_INDEX_BLOOM;      dst = FB_IMAGE_INDEX_BLOOM_MIP1; break;
            case 1: src = FB_IMAGE_INDEX_BLOOM_MIP1; dst = FB_IMAGE_INDEX_BLOOM_MIP2; break;
            case 2: src = FB_IMAGE_INDEX_BLOOM_MIP2; dst = FB_IMAGE_INDEX_BLOOM_MIP3; break;
            case 3: src = FB_IMAGE_INDEX_BLOOM_MIP3; dst = FB_IMAGE_INDEX_BLOOM_MIP4; break;
            case 4: src = FB_IMAGE_INDEX_BLOOM_MIP4; dst = FB_IMAGE_INDEX_BLOOM_MIP5; break;
            case 5: src = FB_IMAGE_INDEX_BLOOM_MIP5; dst = FB_IMAGE_INDEX_BLOOM_MIP6; break;
            case 6: src = FB_IMAGE_INDEX_BLOOM_MIP6; dst = FB_IMAGE_INDEX_BLOOM_MIP7; break;
            default: assert( 0 );
        }
        // clang-format on
        const auto sz = MakeSize( upscaledWidth, upscaledHeight, dst );

        framebuffers->BarrierOne( cmd, frameIndex, src );

        vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, downsamplePipelines[ i ] );
        vkCmdDispatch( cmd,
                       Utils::GetWorkGroupCount( sz.width, COMPUTE_BLOOM_DOWNSAMPLE_GROUP_SIZE_X ),
                       Utils::GetWorkGroupCount( sz.height, COMPUTE_BLOOM_DOWNSAMPLE_GROUP_SIZE_Y ),
                       1 );
    }


    svkCmdPipelineBarrier2KHR( cmd, &dependencyInfo );


    // start from the other side
    for( int i = COMPUTE_BLOOM_STEP_COUNT - 1; i >= 0; i-- )
    {
        auto label = CmdLabel{ cmd, "Bloom upsample" };

        // clang-format off
        auto src = FramebufferImageIndex{};
        auto dst = FramebufferImageIndex{};
        switch( i )
        {
            case 6: src = FB_IMAGE_INDEX_BLOOM_MIP7; dst = FB_IMAGE_INDEX_BLOOM_MIP6; break;
            case 5: src = FB_IMAGE_INDEX_BLOOM_MIP6; dst = FB_IMAGE_INDEX_BLOOM_MIP5; break;
            case 4: src = FB_IMAGE_INDEX_BLOOM_MIP5; dst = FB_IMAGE_INDEX_BLOOM_MIP4; break;
            case 3: src = FB_IMAGE_INDEX_BLOOM_MIP4; dst = FB_IMAGE_INDEX_BLOOM_MIP3; break;
            case 2: src = FB_IMAGE_INDEX_BLOOM_MIP3; dst = FB_IMAGE_INDEX_BLOOM_MIP2; break;
            case 1: src = FB_IMAGE_INDEX_BLOOM_MIP2; dst = FB_IMAGE_INDEX_BLOOM_MIP1; break;
            case 0: src = FB_IMAGE_INDEX_BLOOM_MIP1; dst = FB_IMAGE_INDEX_BLOOM; break;
            default: assert( 0 );
        }
        // clang-format on
        const auto sz = MakeSize( upscaledWidth, upscaledHeight, dst );

        framebuffers->BarrierOne( cmd, frameIndex, src );

        vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, upsamplePipelines[ i ] );
        vkCmdDispatch( cmd,
                       Utils::GetWorkGroupCount( sz.width, COMPUTE_BLOOM_UPSAMPLE_GROUP_SIZE_X ),
                       Utils::GetWorkGroupCount( sz.height, COMPUTE_BLOOM_UPSAMPLE_GROUP_SIZE_Y ),
                       1 );
    }


    svkCmdPipelineBarrier2KHR( cmd, &dependencyInfo );


    FramebufferImageIndex result;
    {
        uint32_t isSourcePing = ( inputFramebuf == FB_IMAGE_INDEX_UPSCALED_PING ? 1 : 0 );

        vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, applyPipelines[ isSourcePing ] );

        FramebufferImageIndex fs[] = {
            inputFramebuf,
            FB_IMAGE_INDEX_BLOOM,
        };
        framebuffers->BarrierMultiple( cmd, frameIndex, fs );

        vkCmdDispatch( cmd,
                       Utils::GetWorkGroupCount( upscaledWidth, COMPUTE_BLOOM_APPLY_GROUP_SIZE_X ),
                       Utils::GetWorkGroupCount( upscaledHeight, COMPUTE_BLOOM_APPLY_GROUP_SIZE_Y ),
                       1 );
        result = isSourcePing ? FB_IMAGE_INDEX_UPSCALED_PONG : FB_IMAGE_INDEX_UPSCALED_PING;
    }
    return result;
}

void RTGL1::Bloom::OnShaderReload( const ShaderManager* shaderManager )
{
    DestroyPipelines();
    CreatePipelines( shaderManager );
}

void RTGL1::Bloom::CreatePipelines( const ShaderManager* shaderManager )
{
    CreateStepPipelines( shaderManager );
    CreateApplyPipelines( shaderManager );
}

void RTGL1::Bloom::CreateStepPipelines( const ShaderManager* shaderManager )
{
    assert( pipelineLayout != VK_NULL_HANDLE );

    for( uint32_t i = 0; i < COMPUTE_BLOOM_STEP_COUNT; i++ )
    {
        assert( downsamplePipelines[ i ] == VK_NULL_HANDLE );
        assert( upsamplePipelines[ i ] == VK_NULL_HANDLE );

        auto specEntry = VkSpecializationMapEntry{
            .constantID = 0,
            .offset     = 0,
            .size       = sizeof( uint32_t ),
        };
        auto specInfo = VkSpecializationInfo{
            .mapEntryCount = 1,
            .pMapEntries   = &specEntry,
            .dataSize      = sizeof( uint32_t ),
            .pData         = &i,
        };

        {
            auto info = VkComputePipelineCreateInfo{
                .sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                .stage  = shaderManager->GetStageInfo( "CBloomDownsample" ),
                .layout = pipelineLayout,
            };
            info.stage.pSpecializationInfo = &specInfo;

            VkResult r = vkCreateComputePipelines(
                device, VK_NULL_HANDLE, 1, &info, nullptr, &downsamplePipelines[ i ] );

            VK_CHECKERROR( r );
            SET_DEBUG_NAME( device,
                            downsamplePipelines[ i ],
                            VK_OBJECT_TYPE_PIPELINE,
                            std::format( "Bloom downsample ({})", i ).c_str() );
        }

        {
            auto info = VkComputePipelineCreateInfo{
                .sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                .stage  = shaderManager->GetStageInfo( "CBloomUpsample" ),
                .layout = pipelineLayout,
            };
            info.stage.pSpecializationInfo = &specInfo;

            VkResult r = vkCreateComputePipelines(
                device, VK_NULL_HANDLE, 1, &info, nullptr, &upsamplePipelines[ i ] );

            VK_CHECKERROR( r );
            SET_DEBUG_NAME( device,
                            upsamplePipelines[ i ],
                            VK_OBJECT_TYPE_PIPELINE,
                            std::format( "Bloom upsample ({})", i ).c_str() );
        }
    }
}

void RTGL1::Bloom::CreateApplyPipelines( const ShaderManager* shaderManager )
{
    assert( pipelineLayout != VK_NULL_HANDLE );

    for( VkPipeline t : preloadPipelines )
    {
        assert( t == VK_NULL_HANDLE );
    }
    for( VkPipeline t : applyPipelines )
    {
        assert( t == VK_NULL_HANDLE );
    }

    for( uint32_t isSourcePing = 0; isSourcePing <= 1; isSourcePing++ )
    {
        auto specEntry = VkSpecializationMapEntry{
            .constantID = 0,
            .offset     = 0,
            .size       = sizeof( isSourcePing ),
        };
        auto specInfo = VkSpecializationInfo{
            .mapEntryCount = 1,
            .pMapEntries   = &specEntry,
            .dataSize      = sizeof( isSourcePing ),
            .pData         = &isSourcePing,
        };

        {
            auto info = VkComputePipelineCreateInfo{
                .sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                .stage  = shaderManager->GetStageInfo( "CBloomPreload" ),
                .layout = pipelineLayout,
            };
            info.stage.pSpecializationInfo = &specInfo;

            VkResult r = vkCreateComputePipelines(
                device, VK_NULL_HANDLE, 1, &info, nullptr, &preloadPipelines[ isSourcePing ] );

            VK_CHECKERROR( r );
            SET_DEBUG_NAME(
                device,
                preloadPipelines[ isSourcePing ],
                VK_OBJECT_TYPE_PIPELINE,
                std::format( "Bloom Preload from {}", isSourcePing ? "Ping" : "Pong" ).c_str() );
        }
        {
            auto info = VkComputePipelineCreateInfo{
                .sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                .stage  = shaderManager->GetStageInfo( "CBloomApply" ),
                .layout = pipelineLayout,
            };
            info.stage.pSpecializationInfo = &specInfo;

            VkResult r = vkCreateComputePipelines(
                device, VK_NULL_HANDLE, 1, &info, nullptr, &applyPipelines[ isSourcePing ] );

            VK_CHECKERROR( r );
            SET_DEBUG_NAME(
                device,
                applyPipelines[ isSourcePing ],
                VK_OBJECT_TYPE_PIPELINE,
                std::format( "Bloom Apply from {}", isSourcePing ? "Ping" : "Pong" ).c_str() );
        }
    }
}

void RTGL1::Bloom::DestroyPipelines()
{
    for( VkPipeline& p : downsamplePipelines )
    {
        vkDestroyPipeline( device, p, nullptr );
        p = VK_NULL_HANDLE;
    }

    for( VkPipeline& p : upsamplePipelines )
    {
        vkDestroyPipeline( device, p, nullptr );
        p = VK_NULL_HANDLE;
    }

    for( VkPipeline& t : applyPipelines )
    {
        vkDestroyPipeline( device, t, nullptr );
        t = VK_NULL_HANDLE;
    }

    for( VkPipeline& t : preloadPipelines )
    {
        vkDestroyPipeline( device, t, nullptr );
        t = VK_NULL_HANDLE;
    }
}


VkExtent2D RTGL1::Bloom::MakeSize( uint32_t              upscaledWidth,
                                   uint32_t              upscaledHeight,
                                   FramebufferImageIndex index )
{
    assert( ShFramebuffers_Flags[ index ] & FB_IMAGE_FLAGS_FRAMEBUF_FLAGS_FORCE_SIZE_BLOOM );

    auto l_downscaleBy = []( const uint32_t w, uint32_t h, uint32_t div ) {
        return VkExtent2D{
            .width  = ( w + div - 1 ) / div,
            .height = ( h + div - 1 ) / div,
        };
    };

    switch( index )
    {
        case FB_IMAGE_INDEX_BLOOM: return l_downscaleBy( upscaledWidth, upscaledHeight, 2 );
        case FB_IMAGE_INDEX_BLOOM_MIP1: return l_downscaleBy( upscaledWidth, upscaledHeight, 4 );
        case FB_IMAGE_INDEX_BLOOM_MIP2: return l_downscaleBy( upscaledWidth, upscaledHeight, 8 );
        case FB_IMAGE_INDEX_BLOOM_MIP3: return l_downscaleBy( upscaledWidth, upscaledHeight, 16 );
        case FB_IMAGE_INDEX_BLOOM_MIP4: return l_downscaleBy( upscaledWidth, upscaledHeight, 32 );
        case FB_IMAGE_INDEX_BLOOM_MIP5: return l_downscaleBy( upscaledWidth, upscaledHeight, 64 );
        case FB_IMAGE_INDEX_BLOOM_MIP6: return l_downscaleBy( upscaledWidth, upscaledHeight, 128 );
        case FB_IMAGE_INDEX_BLOOM_MIP7: return l_downscaleBy( upscaledWidth, upscaledHeight, 256 );
        default: assert( 0 ); return { upscaledWidth, upscaledHeight };
    }
}
