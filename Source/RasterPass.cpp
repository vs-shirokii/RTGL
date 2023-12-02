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

#include "RasterPass.h"

#include "Generated/ShaderCommonCFramebuf.h"
#include "Rasterizer.h"
#include "RgException.h"


constexpr VkFormat    DEPTH_FORMAT      = RTGL1::RASTER_PASS_DEPTH_FORMAT;
constexpr const char* DEPTH_FORMAT_NAME = "VK_FORMAT_D32_SFLOAT";


RTGL1::RasterPass::RasterPass( VkDevice                    _device,
                               VkPhysicalDevice            _physDevice,
                               VkPipelineLayout            _pipelineLayout,
                               const ShaderManager&        _shaderManager,
                               const Framebuffers&         _storageFramebuffers,
                               const RgInstanceCreateInfo& _instanceInfo )
    : device( _device )
{
    {
        VkFormatProperties props = {};
        vkGetPhysicalDeviceFormatProperties( _physDevice, DEPTH_FORMAT, &props );
        if( ( props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT ) == 0 )
        {
            using namespace std::string_literals;
            throw RgException( RG_RESULT_GRAPHICS_API_ERROR,
                               "Depth format is not supported: "s + DEPTH_FORMAT_NAME );
        }
    }

    worldRenderPass =
        CreateWorldRenderPass( ShFramebuffers_Formats[ FB_IMAGE_INDEX_FINAL ],
                               ShFramebuffers_Formats[ FB_IMAGE_INDEX_SCREEN_EMISSION ],
                               ShFramebuffers_Formats[ FB_IMAGE_INDEX_REACTIVITY ],
                               DEPTH_FORMAT );

    // otherwise, we need to create classicRenderPass for different formats
    {
        assert( ShFramebuffers_Formats[ FB_IMAGE_INDEX_FINAL ] ==
                    ShFramebuffers_Formats[ FB_IMAGE_INDEX_UPSCALED_PING ] &&
                ShFramebuffers_Formats[ FB_IMAGE_INDEX_UPSCALED_PING ] ==
                    ShFramebuffers_Formats[ FB_IMAGE_INDEX_UPSCALED_PONG ] );
    }
    classicRenderPass = CreateClassicRenderPass(
        device, ShFramebuffers_Formats[ FB_IMAGE_INDEX_FINAL ], DEPTH_FORMAT );

    skyRenderPass =
        CreateSkyRenderPass( ShFramebuffers_Formats[ FB_IMAGE_INDEX_ALBEDO ], DEPTH_FORMAT );

    worldPipelines =
        std::make_shared< RasterizerPipelines >( device,
                                                 _pipelineLayout,
                                                 worldRenderPass,
                                                 _shaderManager,
                                                 "VertDefault",
                                                 "FragWorld",
                                                 true,
                                                 _instanceInfo.rasterizedVertexColorGamma );
    classicPipelines =
        std::make_shared< RasterizerPipelines >( device,
                                                 _pipelineLayout,
                                                 classicRenderPass,
                                                 _shaderManager,
                                                 "VertDefault",
                                                 "FragWorldClassic",
                                                 false,
                                                 _instanceInfo.rasterizedVertexColorGamma );

    skyPipelines =
        std::make_shared< RasterizerPipelines >( device,
                                                 _pipelineLayout,
                                                 skyRenderPass,
                                                 _shaderManager,
                                                 "VertDefault",
                                                 "FragSky",
                                                 false,
                                                 _instanceInfo.rasterizedVertexColorGamma );

    depthCopying = std::make_shared< DepthCopying >(
        device, DEPTH_FORMAT, _shaderManager, _storageFramebuffers );
}

RTGL1::RasterPass::~RasterPass()
{
    vkDestroyRenderPass( device, worldRenderPass, nullptr );
    vkDestroyRenderPass( device, classicRenderPass, nullptr );
    vkDestroyRenderPass( device, skyRenderPass, nullptr );
    DestroyFramebuffers();
}

void RTGL1::RasterPass::PrepareForFinal( VkCommandBuffer     cmd,
                                         uint32_t            frameIndex,
                                         const Framebuffers& storageFramebuffers,
                                         uint32_t            renderWidth,
                                         uint32_t            renderHeight )
{
    // firstly, copy data from storage buffer to depth buffer,
    // and only after getting correct depth buffer, draw the geometry
    // if no primary rays were traced, just clear depth buffer without copying
    depthCopying->Process( cmd, frameIndex, storageFramebuffers, renderWidth, renderHeight, false );
}

void RTGL1::RasterPass::CreateFramebuffers( uint32_t              renderWidth,
                                            uint32_t              renderHeight,
                                            uint32_t              upscaledWidth,
                                            uint32_t              upscaledHeight,
                                            const Framebuffers&   storageFramebuffers,
                                            MemoryAllocator&      allocator,
                                            CommandBufferManager& cmdManager )
{
    // validate
    {
        auto sameAtAnyFrameIndex = [ &storageFramebuffers ]( FramebufferImageIndex img ) {
            VkImageView v0 = storageFramebuffers.GetImageView( img, 0 );
            for( uint32_t i = 1; i < MAX_FRAMES_IN_FLIGHT; i++ )
            {
                if( v0 != storageFramebuffers.GetImageView( img, i ) )
                {
                    return false;
                }
            }
            return true;
        };
        // used images must not have FRAMEBUF_FLAGS_STORE_PREV flag,
        // because if an image does, then need to create 2 VkFramebuffer instead of 1
        assert( sameAtAnyFrameIndex( FB_IMAGE_INDEX_FINAL ) );
        assert( sameAtAnyFrameIndex( FB_IMAGE_INDEX_SCREEN_EMISSION ) );
        assert( sameAtAnyFrameIndex( FB_IMAGE_INDEX_REACTIVITY ) );
        assert( sameAtAnyFrameIndex( FB_IMAGE_INDEX_ALBEDO ) );
        assert( sameAtAnyFrameIndex( FB_IMAGE_INDEX_UPSCALED_PING ) );
        assert( sameAtAnyFrameIndex( FB_IMAGE_INDEX_UPSCALED_PONG ) );

        assert( renderDepth.image == VK_NULL_HANDLE );
        assert( renderDepth.view == VK_NULL_HANDLE );
        assert( renderDepth.memory == VK_NULL_HANDLE );
        assert( upscaledDepth.image == VK_NULL_HANDLE );
        assert( upscaledDepth.view == VK_NULL_HANDLE );
        assert( upscaledDepth.memory == VK_NULL_HANDLE );

        assert( worldFramebuffer == VK_NULL_HANDLE );
        assert( skyFramebuffer == VK_NULL_HANDLE );
    }

    renderDepth   = CreateDepthBuffers( renderWidth, renderHeight, allocator, cmdManager );
    upscaledDepth = CreateDepthBuffers( upscaledWidth, upscaledHeight, allocator, cmdManager );

    // world at render size
    {
        VkImageView attchs[] = {
            storageFramebuffers.GetImageView( FB_IMAGE_INDEX_FINAL, 0 ),
            storageFramebuffers.GetImageView( FB_IMAGE_INDEX_SCREEN_EMISSION, 0 ),
            storageFramebuffers.GetImageView( FB_IMAGE_INDEX_REACTIVITY, 0 ),
            renderDepth.view,
        };

        VkFramebufferCreateInfo fbInfo = {
            .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass      = worldRenderPass,
            .attachmentCount = std::size( attchs ),
            .pAttachments    = attchs,
            .width           = renderWidth,
            .height          = renderHeight,
            .layers          = 1,
        };

        VkResult r = vkCreateFramebuffer( device, &fbInfo, nullptr, &worldFramebuffer );
        VK_CHECKERROR( r );

        SET_DEBUG_NAME(
            device, worldFramebuffer, VK_OBJECT_TYPE_FRAMEBUFFER, "Rasterizer raster framebuffer" );
    }
    // world at upscaled size, for classic mode
    {
        struct UpcaledDst
        {
            FramebufferImageIndex color;
            VkFramebuffer*        framebuffer;
            DepthBuffer*          depth;
            bool                  upscaled;
        };

        // clang-format off
        UpcaledDst dsts[] = {
            { FB_IMAGE_INDEX_UPSCALED_PING, &classicFramebuffer_UpscaledPing, &upscaledDepth, true },
            { FB_IMAGE_INDEX_UPSCALED_PONG, &classicFramebuffer_UpscaledPong, &upscaledDepth, true },
            { FB_IMAGE_INDEX_FINAL,         &classicFramebuffer_Final,        &renderDepth,   false },
        };
        // clang-format on

        for( UpcaledDst& d : dsts )
        {
            VkImageView attchs[] = {
                storageFramebuffers.GetImageView( d.color, 0 ),
                d.depth->view,
            };

            VkFramebufferCreateInfo fbInfo = {
                .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                .renderPass      = classicRenderPass,
                .attachmentCount = std::size( attchs ),
                .pAttachments    = attchs,
                .width           = d.upscaled ? upscaledWidth : renderWidth,
                .height          = d.upscaled ? upscaledHeight : renderHeight,
                .layers          = 1,
            };

            VkResult r = vkCreateFramebuffer( device, &fbInfo, nullptr, d.framebuffer );
            VK_CHECKERROR( r );

            SET_DEBUG_NAME( device,
                            *d.framebuffer,
                            VK_OBJECT_TYPE_FRAMEBUFFER,
                            "Rasterizer upscaled framebuffer" );
        }
    }
    // sky at render size
    {
        VkImageView attchs[] = {
            storageFramebuffers.GetImageView( FB_IMAGE_INDEX_ALBEDO, 0 ),
            renderDepth.view,
        };

        VkFramebufferCreateInfo fbInfo = {
            .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass      = skyRenderPass,
            .attachmentCount = std::size( attchs ),
            .pAttachments    = attchs,
            .width           = renderWidth,
            .height          = renderHeight,
            .layers          = 1,
        };

        VkResult r = vkCreateFramebuffer( device, &fbInfo, nullptr, &skyFramebuffer );
        VK_CHECKERROR( r );

        SET_DEBUG_NAME( device,
                        skyFramebuffer,
                        VK_OBJECT_TYPE_FRAMEBUFFER,
                        "Rasterizer raster sky framebuffer" );
    }

    depthCopying->CreateFramebuffers( renderDepth.view, renderWidth, renderHeight );
}

void RTGL1::RasterPass::DestroyFramebuffers()
{
    depthCopying->DestroyFramebuffers();

    DestroyDepthBuffers( device, renderDepth );
    DestroyDepthBuffers( device, upscaledDepth );

    VkFramebuffer* todelete[] = {
        &worldFramebuffer,
        &classicFramebuffer_UpscaledPing,
        &classicFramebuffer_UpscaledPong,
        &classicFramebuffer_Final,
        &skyFramebuffer,
    };

    for( VkFramebuffer* f : todelete )
    {
        if( *f != VK_NULL_HANDLE )
        {
            vkDestroyFramebuffer( device, *f, nullptr );
            *f = VK_NULL_HANDLE;
        }
    }
}

VkRenderPass RTGL1::RasterPass::GetWorldRenderPass() const
{
    return worldRenderPass;
}

VkRenderPass RTGL1::RasterPass::GetClassicRenderPass() const
{
    return classicRenderPass;
}

VkRenderPass RTGL1::RasterPass::GetSkyRenderPass() const
{
    return skyRenderPass;
}

const std::shared_ptr< RTGL1::RasterizerPipelines >& RTGL1::RasterPass::GetRasterPipelines() const
{
    return worldPipelines;
}

const std::shared_ptr< RTGL1::RasterizerPipelines >& RTGL1::RasterPass::GetClassicRasterPipelines()
    const
{
    return classicPipelines;
}

const std::shared_ptr< RTGL1::RasterizerPipelines >& RTGL1::RasterPass::GetSkyRasterPipelines()
    const
{
    return skyPipelines;
}

VkFramebuffer RTGL1::RasterPass::GetWorldFramebuffer() const
{
    return worldFramebuffer;
}

VkFramebuffer RTGL1::RasterPass::GetClassicFramebuffer( FramebufferImageIndex img ) const
{
    switch( img )
    {
        case FB_IMAGE_INDEX_FINAL: return classicFramebuffer_Final;
        case FB_IMAGE_INDEX_UPSCALED_PING: return classicFramebuffer_UpscaledPing;
        case FB_IMAGE_INDEX_UPSCALED_PONG: return classicFramebuffer_UpscaledPong;
        default: assert( 0 ); return nullptr;
    }
}

VkFramebuffer RTGL1::RasterPass::GetSkyFramebuffer() const
{
    return skyFramebuffer;
}

void RTGL1::RasterPass::OnShaderReload( const ShaderManager* shaderManager )
{
    worldPipelines->OnShaderReload( shaderManager );
    classicPipelines->OnShaderReload( shaderManager );
    skyPipelines->OnShaderReload( shaderManager );

    depthCopying->OnShaderReload( shaderManager );
}

VkRenderPass RTGL1::RasterPass::CreateWorldRenderPass( VkFormat finalImageFormat,
                                                       VkFormat screenEmisionFormat,
                                                       VkFormat reactivityFormat,
                                                       VkFormat depthImageFormat ) const
{
    const VkAttachmentDescription attchs[] = {
        {
            // final image attachment
            .format         = finalImageFormat,
            .samples        = VK_SAMPLE_COUNT_1_BIT,
            .loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout  = VK_IMAGE_LAYOUT_GENERAL,
            .finalLayout    = VK_IMAGE_LAYOUT_GENERAL,
        },
        {
            // screen emission image attachment
            .format         = screenEmisionFormat,
            .samples        = VK_SAMPLE_COUNT_1_BIT,
            .loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout  = VK_IMAGE_LAYOUT_GENERAL,
            .finalLayout    = VK_IMAGE_LAYOUT_GENERAL,
        },
        {
            .format         = reactivityFormat,
            .samples        = VK_SAMPLE_COUNT_1_BIT,
            .loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout  = VK_IMAGE_LAYOUT_GENERAL,
            .finalLayout    = VK_IMAGE_LAYOUT_GENERAL,
        },
        {
            .format  = depthImageFormat,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            // load depth data from depthCopying
            .loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            // depth image was already transitioned
            // by depthCopying for rasterRenderPass
            .initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .finalLayout   = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        },
    };

    VkAttachmentReference colorRefs[] = {
        {
            .attachment = 0,
            .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        },
        {
            .attachment = 1,
            .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        },
        {
            .attachment = 2,
            .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        },
    };

    VkAttachmentReference depthRef = {
        .attachment = 3,
        .layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass = {
        .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount    = std::size( colorRefs ),
        .pColorAttachments       = colorRefs,
        .pDepthStencilAttachment = &depthRef,
    };

    VkSubpassDependency dependency = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    };

    VkRenderPassCreateInfo passInfo = {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = std::size( attchs ),
        .pAttachments    = attchs,
        .subpassCount    = 1,
        .pSubpasses      = &subpass,
        .dependencyCount = 1,
        .pDependencies   = &dependency,
    };

    VkRenderPass pass;
    VkResult     r = vkCreateRenderPass( device, &passInfo, nullptr, &pass );
    VK_CHECKERROR( r );

    SET_DEBUG_NAME( device, pass, VK_OBJECT_TYPE_RENDER_PASS, "Rasterizer raster render pass" );

    return pass;
}

VkRenderPass RTGL1::RasterPass::CreateClassicRenderPass( VkDevice device,
                                                         VkFormat colorImageFormat,
                                                         VkFormat depthImageFormat )
{
    const VkAttachmentDescription attchs[] = {
        {
            // color image attachment
            .format         = colorImageFormat,
            .samples        = VK_SAMPLE_COUNT_1_BIT,
            .loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout  = VK_IMAGE_LAYOUT_GENERAL,
            .finalLayout    = VK_IMAGE_LAYOUT_GENERAL,
        },
        {
            .format  = depthImageFormat,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            // clear data, don't use depthCopying
            .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            // depth image was already transitioned
            // by depthCopying for rasterRenderPass
            .initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .finalLayout   = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        },
    };

    VkAttachmentReference colorRef = {
        .attachment = 0,
        .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkAttachmentReference depthRef = {
        .attachment = 1,
        .layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass = {
        .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount    = 1,
        .pColorAttachments       = &colorRef,
        .pDepthStencilAttachment = &depthRef,
    };

    VkSubpassDependency dependency = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    };

    VkRenderPassCreateInfo passInfo = {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = std::size( attchs ),
        .pAttachments    = attchs,
        .subpassCount    = 1,
        .pSubpasses      = &subpass,
        .dependencyCount = 1,
        .pDependencies   = &dependency,
    };

    VkRenderPass pass;
    VkResult     r = vkCreateRenderPass( device, &passInfo, nullptr, &pass );
    VK_CHECKERROR( r );

    SET_DEBUG_NAME( device, pass, VK_OBJECT_TYPE_RENDER_PASS, "Rasterizer classic render pass" );

    return pass;
}

VkRenderPass RTGL1::RasterPass::CreateSkyRenderPass( VkFormat skyFinalImageFormat,
                                                     VkFormat depthImageFormat ) const
{
    const VkAttachmentDescription attchs[] = {
        {
            // sky attachment
            .format         = skyFinalImageFormat,
            .samples        = VK_SAMPLE_COUNT_1_BIT,
            .loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout  = VK_IMAGE_LAYOUT_GENERAL,
            .finalLayout    = VK_IMAGE_LAYOUT_GENERAL,
        },
        {
            .format  = depthImageFormat,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            // clear for sky
            .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            // depth image was already transitioned
            // manually for rasterSkyRenderPass
            .initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .finalLayout   = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        },
    };

    VkAttachmentReference colorRefs[] = { {
        .attachment = 0,
        .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    } };

    VkAttachmentReference depthRef = {
        .attachment = 1,
        .layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass = {
        .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount    = std::size( colorRefs ),
        .pColorAttachments       = colorRefs,
        .pDepthStencilAttachment = &depthRef,
    };

    VkSubpassDependency dependency = {
        .srcSubpass    = VK_SUBPASS_EXTERNAL,
        .dstSubpass    = 0,
        .srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    };

    VkRenderPassCreateInfo passInfo = {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = std::size( attchs ),
        .pAttachments    = attchs,
        .subpassCount    = 1,
        .pSubpasses      = &subpass,
        .dependencyCount = 1,
        .pDependencies   = &dependency,
    };

    VkRenderPass pass;
    VkResult     r = vkCreateRenderPass( device, &passInfo, nullptr, &pass );
    VK_CHECKERROR( r );

    SET_DEBUG_NAME( device, pass, VK_OBJECT_TYPE_RENDER_PASS, "Rasterizer raster sky render pass" );

    return pass;
}

auto RTGL1::RasterPass::CreateDepthBuffers( uint32_t              width,
                                            uint32_t              height,
                                            MemoryAllocator&      allocator,
                                            CommandBufferManager& cmdManager ) -> DepthBuffer
{
    VkDevice device = allocator.GetDevice();

    auto result = DepthBuffer{};
    {
        VkImageCreateInfo imageInfo = {
            .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .flags         = 0,
            .imageType     = VK_IMAGE_TYPE_2D,
            .format        = DEPTH_FORMAT,
            .extent        = { width, height, 1 },
            .mipLevels     = 1,
            .arrayLayers   = 1,
            .samples       = VK_SAMPLE_COUNT_1_BIT,
            .tiling        = VK_IMAGE_TILING_OPTIMAL,
            .usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };

        VkResult r = vkCreateImage( device, &imageInfo, nullptr, &result.image );

        VK_CHECKERROR( r );
        SET_DEBUG_NAME(
            device, result.image, VK_OBJECT_TYPE_IMAGE, "Rasterizer raster pass depth image" );
    }
    {
        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements( device, result.image, &memReqs );

        result.memory = allocator.AllocDedicated( memReqs,
                                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                  MemoryAllocator::AllocType::DEFAULT,
                                                  "Rasterizer raster pass depth memory" );

        if( result.memory == VK_NULL_HANDLE )
        {
            vkDestroyImage( device, result.image, nullptr );
            return {};
        }

        VkResult r = vkBindImageMemory( device, result.image, result.memory, 0 );
        VK_CHECKERROR( r );
    }
    {
        VkImageViewCreateInfo viewInfo = {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image            = result.image,
            .viewType         = VK_IMAGE_VIEW_TYPE_2D,
            .format           = DEPTH_FORMAT,
            .subresourceRange = { .aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT,
                                  .baseMipLevel   = 0,
                                  .levelCount     = 1,
                                  .baseArrayLayer = 0,
                                  .layerCount     = 1 },
        };

        VkResult r = vkCreateImageView( device, &viewInfo, nullptr, &result.view );

        VK_CHECKERROR( r );
        SET_DEBUG_NAME( device,
                        result.view,
                        VK_OBJECT_TYPE_IMAGE_VIEW,
                        "Rasterizer raster pass depth image view" );
    }

    // make transition from undefined manually,
    // so depthAttch.initialLayout can be specified as DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    VkCommandBuffer cmd = cmdManager.StartGraphicsCmd();

    VkImageMemoryBarrier imageBarrier = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
        .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout           = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = result.image,
        .subresourceRange    = { .aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT,
                                 .baseMipLevel   = 0,
                                 .levelCount     = 1,
                                 .baseArrayLayer = 0,
                                 .layerCount     = 1 }
    };

    vkCmdPipelineBarrier( cmd,
                          VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                          VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                          0,
                          0,
                          nullptr,
                          0,
                          nullptr,
                          1,
                          &imageBarrier );

    cmdManager.Submit( cmd );
    cmdManager.WaitGraphicsIdle();

    return result;
}

void RTGL1::RasterPass::DestroyDepthBuffers( VkDevice device, DepthBuffer& buf )
{
    assert( ( buf.image && buf.view && buf.memory ) || ( !buf.image && !buf.view && !buf.memory ) );

    if( buf.image != VK_NULL_HANDLE )
    {
        vkDestroyImage( device, buf.image, nullptr );
        vkDestroyImageView( device, buf.view, nullptr );
        MemoryAllocator::FreeDedicated( device, buf.memory );

        buf.image  = VK_NULL_HANDLE;
        buf.view   = VK_NULL_HANDLE;
        buf.memory = VK_NULL_HANDLE;
    }

    buf = {};
}
