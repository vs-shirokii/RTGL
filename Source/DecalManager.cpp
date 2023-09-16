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

#include "DecalManager.h"

#include "CmdLabel.h"
#include "Matrix.h"
#include "RasterizedDataCollector.h"
#include "Utils.h"

#include "Generated/ShaderCommonC.h"


namespace
{
constexpr uint32_t DECAL_MAX_COUNT = 4096;

constexpr uint32_t            CUBE_VERTEX_COUNT = 14;
constexpr VkPrimitiveTopology CUBE_TOPOLOGY     = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;


[[nodiscard]] VkPipelineLayout CreatePipelineLayout( VkDevice                           device,
                                                     std::span< VkDescriptorSetLayout > setLayouts )
{
    VkPipelineLayoutCreateInfo info = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = uint32_t( setLayouts.size() ),
        .pSetLayouts            = setLayouts.data(),
        .pushConstantRangeCount = 0,
        .pPushConstantRanges    = nullptr,
    };

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

    VkResult r = vkCreatePipelineLayout( device, &info, nullptr, &pipelineLayout );
    RTGL1::VK_CHECKERROR( r );

    return pipelineLayout;
}
}

RTGL1::DecalManager::DecalManager( VkDevice                           _device,
                                   std::shared_ptr< MemoryAllocator > _allocator,
                                   std::shared_ptr< Framebuffers >    _storageFramebuffers,
                                   const ShaderManager&               _shaderManager,
                                   const GlobalUniform&               _uniform,
                                   VkPipelineLayout&&                 _drawPipelineLayout )
    : device{ _device }
    , storageFramebuffers{ std::move( _storageFramebuffers ) }
    , drawPipelineLayout{ _drawPipelineLayout }
{
    CreateRenderPass();

    {
        VkDescriptorSetLayout setLayouts[] = {
            storageFramebuffers->GetDescSetLayout(),
            _uniform.GetDescSetLayout(),
        };
        copyingPipelineLayout = CreatePipelineLayout( device, setLayouts );
    }

    CreatePipelines( &_shaderManager );
}

RTGL1::DecalManager::~DecalManager()
{
    vkDestroyPipelineLayout( device, drawPipelineLayout, nullptr );
    vkDestroyPipelineLayout( device, copyingPipelineLayout, nullptr );
    DestroyPipelines();

    vkDestroyRenderPass( device, renderPass, nullptr );
    DestroyFramebuffers();
}

void RTGL1::DecalManager::CopyRtGBufferToAttachments( VkCommandBuffer      cmd,
                                                      uint32_t             frameIndex,
                                                      const GlobalUniform& uniform,
                                                      Framebuffers&        framebuffers )
{
    auto label = CmdLabel{ cmd, "CopyRtGBufferToAttachments" };

    {
        FramebufferImageIndex fs[] = {
            FB_IMAGE_INDEX_ALBEDO,          FB_IMAGE_INDEX_SURFACE_POSITION,
            FB_IMAGE_INDEX_NORMAL,          FB_IMAGE_INDEX_METALLIC_ROUGHNESS,
            FB_IMAGE_INDEX_SCREEN_EMIS_R_T,
        };

        framebuffers.BarrierMultiple( cmd, frameIndex, fs );
    }

    // copy normals from G-buffer to attachment
    {
        VkDescriptorSet sets[] = {
            framebuffers.GetDescSet( frameIndex ),
            uniform.GetDescSet( frameIndex ),
        };
        vkCmdBindDescriptorSets( cmd,
                                 VK_PIPELINE_BIND_POINT_COMPUTE,
                                 copyingPipelineLayout,
                                 0,
                                 std::size( sets ),
                                 sets,
                                 0,
                                 nullptr );
        vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, copyNormalsToAttachment );
        vkCmdDispatch( cmd,
                       Utils::GetWorkGroupCount( uniform.GetData()->renderWidth,
                                                 COMPUTE_DECAL_APPLY_GROUP_SIZE_X ),
                       Utils::GetWorkGroupCount( uniform.GetData()->renderHeight,
                                                 COMPUTE_DECAL_APPLY_GROUP_SIZE_X ),
                       1 );

        VkImageMemoryBarrier2KHR bs[] = {
            {
                .sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .pNext         = nullptr,
                .srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                .dstStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .dstAccessMask =
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,
                .oldLayout           = VK_IMAGE_LAYOUT_GENERAL,
                .newLayout           = VK_IMAGE_LAYOUT_GENERAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = framebuffers.GetImage( FB_IMAGE_INDEX_NORMAL_DECAL, frameIndex ),
                .subresourceRange = { .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                      .baseMipLevel   = 0,
                                      .levelCount     = 1,
                                      .baseArrayLayer = 0,
                                      .layerCount     = 1 },
            },
            // RT normal for manual blending with decal normal
            {
                .sType        = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .pNext        = nullptr,
                .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .srcAccessMask =
                    VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                .dstStageMask        = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                .dstAccessMask       = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                .oldLayout           = VK_IMAGE_LAYOUT_GENERAL,
                .newLayout           = VK_IMAGE_LAYOUT_GENERAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image               = framebuffers.GetImage( FB_IMAGE_INDEX_NORMAL, frameIndex ),
                .subresourceRange    = { .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                         .baseMipLevel   = 0,
                                         .levelCount     = 1,
                                         .baseArrayLayer = 0,
                                         .layerCount     = 1 },
            },
            {
                .sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .pNext         = nullptr,
                .srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                .dstStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .dstAccessMask =
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,
                .oldLayout           = VK_IMAGE_LAYOUT_GENERAL,
                .newLayout           = VK_IMAGE_LAYOUT_GENERAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = framebuffers.GetImage( FB_IMAGE_INDEX_SCREEN_EMISSION, frameIndex ),
                .subresourceRange = { .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                      .baseMipLevel   = 0,
                                      .levelCount     = 1,
                                      .baseArrayLayer = 0,
                                      .layerCount     = 1 },
            },
        };

        VkDependencyInfoKHR info = {
            .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR,
            .imageMemoryBarrierCount = std::size( bs ),
            .pImageMemoryBarriers    = bs,
        };

        svkCmdPipelineBarrier2KHR( cmd, &info );
    }
}

void RTGL1::DecalManager::CopyAttachmentsToRtGBuffer( VkCommandBuffer      cmd,
                                                      uint32_t             frameIndex,
                                                      const GlobalUniform& uniform,
                                                      const Framebuffers&  framebuffers )
{
    auto label = CmdLabel{ cmd, "CopyAttachmentsToRtGBuffer" };

    // copy normals back from attachment to G-buffer

    {
        VkImageMemoryBarrier2KHR bs[] = {
            {
                .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .pNext               = nullptr,
                .srcStageMask        = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .srcAccessMask       = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                .dstStageMask        = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .dstAccessMask       = VK_ACCESS_2_SHADER_READ_BIT,
                .oldLayout           = VK_IMAGE_LAYOUT_GENERAL,
                .newLayout           = VK_IMAGE_LAYOUT_GENERAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = framebuffers.GetImage( FB_IMAGE_INDEX_NORMAL_DECAL, frameIndex ),
                .subresourceRange = { .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                      .baseMipLevel   = 0,
                                      .levelCount     = 1,
                                      .baseArrayLayer = 0,
                                      .layerCount     = 1 },
            },
            {
                .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .pNext               = nullptr,
                .srcStageMask        = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .srcAccessMask       = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                .dstStageMask        = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .dstAccessMask       = VK_ACCESS_2_SHADER_READ_BIT,
                .oldLayout           = VK_IMAGE_LAYOUT_GENERAL,
                .newLayout           = VK_IMAGE_LAYOUT_GENERAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = framebuffers.GetImage( FB_IMAGE_INDEX_SCREEN_EMISSION, frameIndex ),
                .subresourceRange = { .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                      .baseMipLevel   = 0,
                                      .levelCount     = 1,
                                      .baseArrayLayer = 0,
                                      .layerCount     = 1 },
            },
        };

        VkDependencyInfoKHR info = {
            .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR,
            .imageMemoryBarrierCount = std::size( bs ),
            .pImageMemoryBarriers    = bs,
        };

        svkCmdPipelineBarrier2KHR( cmd, &info );
    }

    VkDescriptorSet sets[] = {
        framebuffers.GetDescSet( frameIndex ),
        uniform.GetDescSet( frameIndex ),
    };
    vkCmdBindDescriptorSets( cmd,
                             VK_PIPELINE_BIND_POINT_COMPUTE,
                             copyingPipelineLayout,
                             0,
                             std::size( sets ),
                             sets,
                             0,
                             nullptr );
    vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, copyNormalsToGbuffer );
    vkCmdDispatch( cmd,
                   Utils::GetWorkGroupCount( uniform.GetData()->renderWidth,
                                             COMPUTE_DECAL_APPLY_GROUP_SIZE_X ),
                   Utils::GetWorkGroupCount( uniform.GetData()->renderHeight,
                                             COMPUTE_DECAL_APPLY_GROUP_SIZE_X ),
                   1 );

    {
        VkImageMemoryBarrier2KHR bs[] = {
            {
                .sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .pNext         = nullptr,
                .srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                .dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                                VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                .dstAccessMask =
                    VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                .oldLayout           = VK_IMAGE_LAYOUT_GENERAL,
                .newLayout           = VK_IMAGE_LAYOUT_GENERAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image               = framebuffers.GetImage( FB_IMAGE_INDEX_NORMAL, frameIndex ),
                .subresourceRange    = { .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                         .baseMipLevel   = 0,
                                         .levelCount     = 1,
                                         .baseArrayLayer = 0,
                                         .layerCount     = 1 },
            },
            {
                .sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .pNext         = nullptr,
                .srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                .dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                                VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
                .dstAccessMask =
                    VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                .oldLayout           = VK_IMAGE_LAYOUT_GENERAL,
                .newLayout           = VK_IMAGE_LAYOUT_GENERAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = framebuffers.GetImage( FB_IMAGE_INDEX_SCREEN_EMIS_R_T, frameIndex ),
                .subresourceRange = { .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                      .baseMipLevel   = 0,
                                      .levelCount     = 1,
                                      .baseArrayLayer = 0,
                                      .layerCount     = 1 },
            },
        };

        VkDependencyInfoKHR info = {
            .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR,
            .imageMemoryBarrierCount = std::size( bs ),
            .pImageMemoryBarriers    = bs,
        };

        svkCmdPipelineBarrier2KHR( cmd, &info );
    }
}

void RTGL1::DecalManager::OnShaderReload( const ShaderManager* shaderManager )
{
    DestroyPipelines();
    CreatePipelines( shaderManager );
}

void RTGL1::DecalManager::OnFramebuffersSizeChange( const ResolutionState& resolutionState )
{
    DestroyFramebuffers();
    CreateFramebuffers( resolutionState.renderWidth, resolutionState.renderHeight );
}

void RTGL1::DecalManager::CreateRenderPass()
{
    VkAttachmentDescription colorAttchs[] = {
        {
            .format         = ShFramebuffers_Formats[ FB_IMAGE_INDEX_ALBEDO ],
            .samples        = VK_SAMPLE_COUNT_1_BIT,
            .loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout  = VK_IMAGE_LAYOUT_GENERAL,
            .finalLayout    = VK_IMAGE_LAYOUT_GENERAL,
        },
        {
            .format         = ShFramebuffers_Formats[ FB_IMAGE_INDEX_NORMAL_DECAL ],
            .samples        = VK_SAMPLE_COUNT_1_BIT,
            .loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout  = VK_IMAGE_LAYOUT_GENERAL,
            .finalLayout    = VK_IMAGE_LAYOUT_GENERAL,
        },
        {
            .format         = ShFramebuffers_Formats[ FB_IMAGE_INDEX_SCREEN_EMISSION ],
            .samples        = VK_SAMPLE_COUNT_1_BIT,
            .loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout  = VK_IMAGE_LAYOUT_GENERAL,
            .finalLayout    = VK_IMAGE_LAYOUT_GENERAL,
        },
    };

    VkAttachmentReference colorRefs[] = {
        {
            .attachment = 0,
            .layout     = VK_IMAGE_LAYOUT_GENERAL,
        },
        {
            .attachment = 1,
            .layout     = VK_IMAGE_LAYOUT_GENERAL,
        },
        {
            .attachment = 2,
            .layout     = VK_IMAGE_LAYOUT_GENERAL,
        },
    };

    VkSubpassDescription subpass = {
        .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .inputAttachmentCount    = 0,
        .pInputAttachments       = nullptr,
        .colorAttachmentCount    = uint32_t( std::size( colorRefs ) ),
        .pColorAttachments       = colorRefs,
        .pResolveAttachments     = nullptr,
        .pDepthStencilAttachment = nullptr,
        .preserveAttachmentCount = 0,
        .pPreserveAttachments    = nullptr,
    };

    VkSubpassDependency dependency = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask =
            VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, // imageStore
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    };

    VkRenderPassCreateInfo info = {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .flags           = 0,
        .attachmentCount = std::size( colorAttchs ),
        .pAttachments    = colorAttchs,
        .subpassCount    = 1,
        .pSubpasses      = &subpass,
        .dependencyCount = 1,
        .pDependencies   = &dependency,
    };

    VkResult r = vkCreateRenderPass( device, &info, nullptr, &renderPass );
    VK_CHECKERROR( r );
}

void RTGL1::DecalManager::CreateFramebuffers( uint32_t width, uint32_t height )
{
    for( uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++ )
    {
        assert( passFramebuffers[ i ] == VK_NULL_HANDLE );

        VkImageView vs[] = {
            storageFramebuffers->GetImageView( FB_IMAGE_INDEX_ALBEDO, i ),
            storageFramebuffers->GetImageView( FB_IMAGE_INDEX_NORMAL_DECAL, i ),
            storageFramebuffers->GetImageView( FB_IMAGE_INDEX_SCREEN_EMISSION, i ),
        };

        VkFramebufferCreateInfo info = {
            .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass      = renderPass,
            .attachmentCount = std::size( vs ),
            .pAttachments    = vs,
            .width           = width,
            .height          = height,
            .layers          = 1,
        };

        VkResult r = vkCreateFramebuffer( device, &info, nullptr, &passFramebuffers[ i ] );
        VK_CHECKERROR( r );
    }
}

void RTGL1::DecalManager::DestroyFramebuffers()
{
    for( uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++ )
    {
        if( passFramebuffers[ i ] != VK_NULL_HANDLE )
        {
            vkDestroyFramebuffer( device, passFramebuffers[ i ], nullptr );
            passFramebuffers[ i ] = VK_NULL_HANDLE;
        }
    }
}

void RTGL1::DecalManager::CreatePipelines( const ShaderManager* shaderManager )
{
    assert( pipeline == VK_NULL_HANDLE && copyNormalsToAttachment == VK_NULL_HANDLE &&
            copyNormalsToGbuffer == VK_NULL_HANDLE );
    assert( renderPass != VK_NULL_HANDLE );
    assert( drawPipelineLayout != VK_NULL_HANDLE && copyingPipelineLayout != VK_NULL_HANDLE );

    {
        uint32_t copyFromDecalToGbuffer = 0;

        VkSpecializationMapEntry entry = {
            .constantID = 0,
            .offset     = 0,
            .size       = sizeof( copyFromDecalToGbuffer ),
        };

        VkSpecializationInfo spec = {
            .mapEntryCount = 1,
            .pMapEntries   = &entry,
            .dataSize      = sizeof( copyFromDecalToGbuffer ),
            .pData         = &copyFromDecalToGbuffer,
        };

        VkComputePipelineCreateInfo copyingInfo = {
            .sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .pNext  = nullptr,
            .flags  = 0,
            .stage  = shaderManager->GetStageInfo( "DecalNormalsCopy" ),
            .layout = copyingPipelineLayout,
        };
        copyingInfo.stage.pSpecializationInfo = &spec;

        {
            copyFromDecalToGbuffer = 0;

            VkResult r = vkCreateComputePipelines(
                device, VK_NULL_HANDLE, 1, &copyingInfo, nullptr, &copyNormalsToAttachment );

            VK_CHECKERROR( r );
            SET_DEBUG_NAME( device,
                            copyNormalsToAttachment,
                            VK_OBJECT_TYPE_PIPELINE,
                            "Decal normals copy: Gbuffer to Attch" );
        }
        {
            copyFromDecalToGbuffer = 1;

            VkResult r = vkCreateComputePipelines(
                device, VK_NULL_HANDLE, 1, &copyingInfo, nullptr, &copyNormalsToGbuffer );

            VK_CHECKERROR( r );
            SET_DEBUG_NAME( device,
                            copyNormalsToGbuffer,
                            VK_OBJECT_TYPE_PIPELINE,
                            "Decal normals copy: Attch to Gbuffer" );
        }
    }

    VkPipelineShaderStageCreateInfo stages[] = {
        shaderManager->GetStageInfo( "VertDecal" ),
        shaderManager->GetStageInfo( "FragDecal" ),
    };


    VkVertexInputBindingDescription vertBinding = {
        .binding   = 0,
        .stride    = RasterizedDataCollector::GetVertexStride(),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };

    auto attrs = RasterizedDataCollector::GetVertexLayout();

    VkPipelineVertexInputStateCreateInfo vertexInput = {
        .sType                         = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions    = &vertBinding,
        .vertexAttributeDescriptionCount = attrs.size(),
        .pVertexAttributeDescriptions    = attrs.data(),
    };

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology               = CUBE_TOPOLOGY,
        .primitiveRestartEnable = VK_FALSE,
    };

    VkPipelineViewportStateCreateInfo viewportState = {
        .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports    = nullptr, // dynamic state
        .scissorCount  = 1,
        .pScissors     = nullptr, // dynamic state
    };

    VkPipelineRasterizationStateCreateInfo raster = {
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable        = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode             = VK_POLYGON_MODE_FILL,
        .cullMode                = VK_CULL_MODE_NONE,
        .frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable         = VK_FALSE,
        .depthBiasConstantFactor = 0,
        .depthBiasClamp          = 0,
        .depthBiasSlopeFactor    = 0,
        .lineWidth               = 1.0f,
    };

    VkPipelineMultisampleStateCreateInfo multisampling = {
        .sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable  = VK_FALSE,
    };

    VkPipelineDepthStencilStateCreateInfo depthStencil = {
        .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable       = VK_FALSE, // must be true, if depthWrite is true
        .depthWriteEnable      = VK_FALSE,
        .depthCompareOp        = VK_COMPARE_OP_LESS_OR_EQUAL,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable     = VK_FALSE,
    };

    VkPipelineColorBlendAttachmentState colorBlendAttchs[] = {
        // albedo
        {
            .blendEnable         = VK_TRUE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .colorBlendOp        = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .alphaBlendOp        = VK_BLEND_OP_ADD,
            .colorWriteMask =
                VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | 0,
        },
        // normal
        {
            .blendEnable    = VK_FALSE,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT,
        },
        // screenEmission
        {
            .blendEnable         = VK_TRUE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
            .colorBlendOp        = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .alphaBlendOp        = VK_BLEND_OP_ADD,
            .colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        },
    };

    VkPipelineColorBlendStateCreateInfo colorBlendState = {
        .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable   = VK_FALSE,
        .attachmentCount = std::size( colorBlendAttchs ),
        .pAttachments    = colorBlendAttchs,
    };

    VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };

    VkPipelineDynamicStateCreateInfo dynamicInfo = {
        .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = std::size( dynamicStates ),
        .pDynamicStates    = dynamicStates,
    };

    VkGraphicsPipelineCreateInfo info = {
        .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount          = std::size( stages ),
        .pStages             = stages,
        .pVertexInputState   = &vertexInput,
        .pInputAssemblyState = &inputAssembly,
        .pTessellationState  = nullptr,
        .pViewportState      = &viewportState,
        .pRasterizationState = &raster,
        .pMultisampleState   = &multisampling,
        .pDepthStencilState  = &depthStencil,
        .pColorBlendState    = &colorBlendState,
        .pDynamicState       = &dynamicInfo,
        .layout              = drawPipelineLayout,
        .renderPass          = renderPass,
        .subpass             = 0,
        .basePipelineHandle  = VK_NULL_HANDLE,
    };

    VkResult r = vkCreateGraphicsPipelines( device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline );
    VK_CHECKERROR( r );
}

void RTGL1::DecalManager::DestroyPipelines()
{
    assert( pipeline != VK_NULL_HANDLE );

    vkDestroyPipeline( device, pipeline, nullptr );
    pipeline = VK_NULL_HANDLE;
    vkDestroyPipeline( device, copyNormalsToGbuffer, nullptr );
    copyNormalsToGbuffer = VK_NULL_HANDLE;
    vkDestroyPipeline( device, copyNormalsToAttachment, nullptr );
    copyNormalsToAttachment = VK_NULL_HANDLE;
}
