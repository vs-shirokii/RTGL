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

#include "Rasterizer.h"

#include "Swapchain.h"
#include "Matrix.h"
#include "Utils.h"
#include "CmdLabel.h"
#include "RenderResolutionHelper.h"

namespace
{

// A duplicate of GLSL's RasterizerFrag_BT
struct RasterizedPushConst
{
    float    vp[ 16 ];
    uint32_t packedColor;
    uint32_t textureIndex;
    uint32_t emissiveTextureIndex;
    float    emissiveMult;
    uint32_t normalTextureIndex;
    uint32_t manualSrgb;

    explicit RasterizedPushConst( const RTGL1::RasterizedDataCollector::DrawInfo& info,
                                  const float*                                    defaultViewProj,
                                  bool                                            _manualSrgb )
        : vp{}
        , packedColor{ info.colorFactor_base }
        , textureIndex( info.texture_base )
        , emissiveTextureIndex( info.texture_base_E )
        , emissiveMult( info.emissive )
        , normalTextureIndex( info.texture_base_N )
        , manualSrgb( _manualSrgb )
    {
        float model[ 16 ] = RG_MATRIX_TRANSPOSED( info.transform );
        RTGL1::Matrix::Multiply(
            vp, model, info.viewProj ? info.viewProj->Get() : defaultViewProj );
    }
};

static_assert( offsetof( RasterizedPushConst, vp ) == 0 );
static_assert( offsetof( RasterizedPushConst, packedColor ) == 64 );
static_assert( offsetof( RasterizedPushConst, textureIndex ) == 68 );
static_assert( offsetof( RasterizedPushConst, emissiveTextureIndex ) == 72 );
static_assert( offsetof( RasterizedPushConst, emissiveMult ) == 76 );
static_assert( offsetof( RasterizedPushConst, normalTextureIndex ) == 80 );
static_assert( sizeof( RasterizedPushConst ) == 88 );

VkPipelineLayout CreatePipelineLayout( VkDevice                           device,
                                       std::span< VkDescriptorSetLayout > descs,
                                       const char*                        name )
{
    const auto pushConst = VkPushConstantRange{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset     = 0,
        .size       = sizeof( RasterizedPushConst ),
    };

    auto layoutInfo = VkPipelineLayoutCreateInfo{
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = static_cast< uint32_t >( descs.size() ),
        .pSetLayouts            = descs.data(),
        .pushConstantRangeCount = 1,
        .pPushConstantRanges    = &pushConst,
    };

    VkPipelineLayout pl = nullptr;
    VkResult         r  = vkCreatePipelineLayout( device, &layoutInfo, nullptr, &pl );
    RTGL1::VK_CHECKERROR( r );

    SET_DEBUG_NAME( device, pl, VK_OBJECT_TYPE_PIPELINE_LAYOUT, name );
    return pl;
}

}

RTGL1::Rasterizer::Rasterizer( VkDevice                                _device,
                               VkPhysicalDevice                        _physDevice,
                               const ShaderManager&                    _shaderManager,
                               std::shared_ptr< TextureManager >       _textureManager,
                               const GlobalUniform&                    _uniform,
                               const SamplerManager&                   _samplerManager,
                               const Tonemapping&                      _tonemapping,
                               const Volumetric&                       _volumetric,
                               std::shared_ptr< MemoryAllocator >      _allocator,
                               std::shared_ptr< Framebuffers >         _storageFramebuffers,
                               std::shared_ptr< CommandBufferManager > _cmdManager,
                               const RgInstanceCreateInfo&             _instanceInfo )
    : device( _device )
    , rasterPassPipelineLayout( VK_NULL_HANDLE )
    , swapchainPassPipelineLayout( VK_NULL_HANDLE )
    , allocator( std::move( _allocator ) )
    , cmdManager( std::move( _cmdManager ) )
    , storageFramebuffers( std::move( _storageFramebuffers ) )
{
    collector =
        std::make_shared< RasterizedDataCollector >( device,
                                                     allocator,
                                                     _textureManager,
                                                     _instanceInfo.rasterizedMaxVertexCount,
                                                     _instanceInfo.rasterizedMaxIndexCount );
    {
        VkDescriptorSetLayout ls[] = {
            _textureManager->GetDescSetLayout(),
            _uniform.GetDescSetLayout(),
            _tonemapping.GetDescSetLayout(),
            _volumetric.GetDescSetLayout(),
        };
        rasterPassPipelineLayout =
            CreatePipelineLayout( device, ls, "Raster pass Pipeline layout" );
    }
    {
        VkDescriptorSetLayout ls[] = {
            _textureManager->GetDescSetLayout(),
        };
        swapchainPassPipelineLayout =
            CreatePipelineLayout( device, ls, "Swapchain pass Pipeline layout" );
    }

    rasterPass = std::make_shared< RasterPass >( device,
                                                 _physDevice,
                                                 rasterPassPipelineLayout,
                                                 _shaderManager,
                                                 *storageFramebuffers,
                                                 _instanceInfo );

    swapchainPass = std::make_shared< SwapchainPass >(
        device, swapchainPassPipelineLayout, _shaderManager, _instanceInfo );

    renderCubemap = std::make_shared< RenderCubemap >( device,
                                                       *allocator,
                                                       _shaderManager,
                                                       *_textureManager,
                                                       _uniform,
                                                       _samplerManager,
                                                       *cmdManager,
                                                       _instanceInfo );

    lensFlares = std::make_unique< LensFlares >( device,
                                                 allocator,
                                                 _shaderManager,
                                                 rasterPass->GetWorldRenderPass(),
                                                 _uniform,
                                                 *storageFramebuffers,
                                                 *_textureManager,
                                                 _instanceInfo );

    {
        VkDescriptorSetLayout ls[] = {
            _uniform.GetDescSetLayout(),
            storageFramebuffers->GetDescSetLayout(),
            _textureManager->GetDescSetLayout(),
        };
        auto decalPipelineLayout = CreatePipelineLayout( device, ls, "Decal Pipeline layout" );

        decalManager = std::make_unique< DecalManager >( device,
                                                         allocator,
                                                         storageFramebuffers,
                                                         _shaderManager,
                                                         _uniform,
                                                         std::move( decalPipelineLayout ) );
    }
}

RTGL1::Rasterizer::~Rasterizer()
{
    vkDestroyPipelineLayout( device, rasterPassPipelineLayout, nullptr );
    vkDestroyPipelineLayout( device, swapchainPassPipelineLayout, nullptr );
}

void RTGL1::Rasterizer::PrepareForFrame( uint32_t frameIndex )
{
    collector->Clear( frameIndex );
    lensFlares->PrepareForFrame( frameIndex );
}

void RTGL1::Rasterizer::Upload( uint32_t                   frameIndex,
                                GeometryRasterType         rasterType,
                                const RgTransform&         transform,
                                const RgMeshPrimitiveInfo& info,
                                const float*               pViewProjection,
                                const RgViewport*          pViewport )
{
    collector->AddPrimitive( frameIndex, rasterType, transform, info, pViewProjection, pViewport );
}

void RTGL1::Rasterizer::UploadLensFlare( uint32_t               frameIndex,
                                         const RgLensFlareInfo& info,
                                         float                  emissiveMult,
                                         const TextureManager&  textureManager )
{
    lensFlares->Upload( frameIndex, info, emissiveMult, textureManager );
}

void RTGL1::Rasterizer::SubmitForFrame( VkCommandBuffer cmd, uint32_t frameIndex )
{
    CmdLabel label( cmd, "Copying rasterizer data" );

    collector->CopyFromStaging( cmd, frameIndex );
    lensFlares->SubmitForFrame( cmd, frameIndex );
}

void RTGL1::Rasterizer::DrawSkyToCubemap( VkCommandBuffer       cmd,
                                          uint32_t              frameIndex,
                                          const TextureManager& textureManager,
                                          const GlobalUniform&  uniform )
{
    CmdLabel label( cmd, "Rasterized sky to cubemap" );

    renderCubemap->Draw( cmd, frameIndex, *collector, textureManager, uniform );
}

namespace RTGL1
{
namespace
{
    void SetViewportIfNew( VkCommandBuffer                          cmd,
                           const RasterizedDataCollector::DrawInfo& info,
                           const VkViewport&                        defaultViewport,
                           VkViewport&                              curViewport )
    {
        const VkViewport newViewport = info.viewport.value_or( defaultViewport );

        if( !Utils::AreViewportsSame( curViewport, newViewport ) )
        {
            vkCmdSetViewport( cmd, 0, 1, &newViewport );
            curViewport = newViewport;
        }
    }
}
}

namespace RTGL1
{

struct RasterLensFlares
{
    const TextureManager* textureManager;
};

struct RasterDrawParams
{
    RasterizerPipelines* pipelines{ nullptr };
    VkPipeline           standalonePipeline{ nullptr };
    VkPipelineLayout     standalonePipelineLayout{ nullptr };

    std::span< const RasterizedDataCollector::DrawInfo > drawInfos{};

    VkRenderPass                      renderPass{ VK_NULL_HANDLE };
    VkFramebuffer                     framebuffer{ VK_NULL_HANDLE };
    uint32_t                          width{ 0 };
    uint32_t                          height{ 0 };
    VkBuffer                          vertexBuffer{ VK_NULL_HANDLE };
    VkBuffer                          indexBuffer{ VK_NULL_HANDLE };
    std::span< VkDescriptorSet >      descSets{};
    float*                            defaultViewProj{ nullptr };
    // not the best way to optionally draw lens flares with a world pass
    std::optional< RasterLensFlares > flaresParams{};
    std::optional< float >            classic{};
    bool                              manualSrgb{ false };
};

}

void RTGL1::Rasterizer::DrawDecals( VkCommandBuffer               cmd,
                                    uint32_t                      frameIndex,
                                    const GlobalUniform&          uniform,
                                    const TextureManager&         textureManager,
                                    const float*                  view,
                                    const float*                  proj,
                                    const RgFloat2D&              jitter,
                                    const RenderResolutionHelper& renderResolution )
{
    if( collector->GetDrawInfos( GeometryRasterType::DECAL ).empty() )
    {
        return;
    }

    auto label = CmdLabel{ cmd, "Decals" };

    decalManager->CopyRtGBufferToAttachments( cmd, frameIndex, uniform, *storageFramebuffers );

    auto jitterredProj =
        ApplyJitter( proj, jitter, renderResolution.Width(), renderResolution.Height() );

    float defaultViewProj[ 16 ];
    Matrix::Multiply( defaultViewProj, view, jitterredProj.data() );

    VkDescriptorSet sets[] = {
        uniform.GetDescSet( frameIndex ),
        storageFramebuffers->GetDescSet( frameIndex ),
        textureManager.GetDescSet( frameIndex ),
    };

    const RasterDrawParams params = {
        .standalonePipeline       = decalManager->GetDrawPipeline(),
        .standalonePipelineLayout = decalManager->GetDrawPipelineLayout(),
        .drawInfos                = collector->GetDrawInfos( GeometryRasterType::DECAL ),
        .renderPass               = decalManager->GetRenderPass(),
        .framebuffer              = decalManager->GetFramebuffer( frameIndex ),
        .width                    = renderResolution.Width(),
        .height                   = renderResolution.Height(),
        .vertexBuffer             = collector->GetVertexBuffer(),
        .indexBuffer              = collector->GetIndexBuffer(),
        .descSets                 = sets,
        .defaultViewProj          = defaultViewProj,
    };

    Draw( cmd, frameIndex, params );

    decalManager->CopyAttachmentsToRtGBuffer( cmd, frameIndex, uniform, *storageFramebuffers );
}

void RTGL1::Rasterizer::DrawSkyToAlbedo( VkCommandBuffer               cmd,
                                         uint32_t                      frameIndex,
                                         const TextureManager&         textureManager,
                                         const float*                  view,
                                         const RgFloat3D&              skyViewerPos,
                                         const float*                  proj,
                                         const RgFloat2D&              jitter,
                                         const RenderResolutionHelper& renderResolution )
{
    auto label = CmdLabel{ cmd, "Rasterized sky to albedo framebuf" };


    using FI = FramebufferImageIndex;
    storageFramebuffers->BarrierOne( cmd, frameIndex, FI::FB_IMAGE_INDEX_ALBEDO );


    float skyView[ 16 ];
    Matrix::SetNewViewerPosition( skyView, view, skyViewerPos.data );

    auto jitterredProj =
        ApplyJitter( proj, jitter, renderResolution.Width(), renderResolution.Height() );

    float defaultSkyViewProj[ 16 ];
    Matrix::Multiply( defaultSkyViewProj, skyView, jitterredProj.data() );


    VkDescriptorSet sets[] = {
        textureManager.GetDescSet( frameIndex ),
    };

    const RasterDrawParams params = {
        .pipelines       = rasterPass->GetSkyRasterPipelines().get(),
        .drawInfos       = collector->GetDrawInfos( GeometryRasterType::SKY ),
        .renderPass      = rasterPass->GetSkyRenderPass(),
        .framebuffer     = rasterPass->GetSkyFramebuffer(),
        .width           = renderResolution.Width(),
        .height          = renderResolution.Height(),
        .vertexBuffer    = collector->GetVertexBuffer(),
        .indexBuffer     = collector->GetIndexBuffer(),
        .descSets        = sets,
        .defaultViewProj = defaultSkyViewProj,
    };

    Draw( cmd, frameIndex, params );
}

void RTGL1::Rasterizer::DrawToFinalImage( VkCommandBuffer               cmd,
                                          uint32_t                      frameIndex,
                                          const TextureManager&         textureManager,
                                          const GlobalUniform&          uniform,
                                          const Tonemapping&            tonemapping,
                                          const Volumetric&             volumetric,
                                          const float*                  view,
                                          const float*                  proj,
                                          const RgFloat2D&              jitter,
                                          const RenderResolutionHelper& renderResolution,
                                          float                         lightmapScreenCoverage )
{
    auto label = CmdLabel{ cmd, "Rasterized to final framebuf" };
    using FI   = FramebufferImageIndex;


    FI fs[] = {
        FI::FB_IMAGE_INDEX_DEPTH_NDC,
        FI::FB_IMAGE_INDEX_FINAL,
    };
    storageFramebuffers->BarrierMultiple( cmd, frameIndex, fs );


    // prepare lens flares draw commands
    lensFlares->Cull( cmd, frameIndex, uniform, *storageFramebuffers );


    // copy depth buffer
    rasterPass->PrepareForFinal( cmd,
                                 frameIndex,
                                 *storageFramebuffers,
                                 renderResolution.Width(),
                                 renderResolution.Height() );


    auto jitterredProj =
        ApplyJitter( proj, jitter, renderResolution.Width(), renderResolution.Height() );

    float defaultViewProj[ 16 ];
    Matrix::Multiply( defaultViewProj, view, jitterredProj.data() );

    VkDescriptorSet sets[] = {
        textureManager.GetDescSet( frameIndex ),
        uniform.GetDescSet( frameIndex ),
        tonemapping.GetDescSet(),
        volumetric.GetDescSet( frameIndex ),
    };

    const RasterDrawParams params = {
        .pipelines       = rasterPass->GetRasterPipelines().get(),
        .drawInfos       = collector->GetDrawInfos( GeometryRasterType::WORLD ),
        .renderPass      = rasterPass->GetWorldRenderPass(),
        .framebuffer     = rasterPass->GetWorldFramebuffer(),
        .width           = renderResolution.Width(),
        .height          = renderResolution.Height(),
        .vertexBuffer    = collector->GetVertexBuffer(),
        .indexBuffer     = collector->GetIndexBuffer(),
        .descSets        = sets,
        .defaultViewProj = defaultViewProj,
        .flaresParams    = RasterLensFlares{ .textureManager = &textureManager },
        .classic         = -lightmapScreenCoverage,
    };

    Draw( cmd, frameIndex, params );
}

void RTGL1::Rasterizer::DrawClassic( VkCommandBuffer               cmd,
                                     uint32_t                      frameIndex,
                                     FramebufferImageIndex         destination,
                                     const TextureManager&         textureManager,
                                     const GlobalUniform&          uniform,
                                     const Tonemapping&            tonemapping,
                                     const Volumetric&             volumetric,
                                     const float*                  view,
                                     const float*                  proj,
                                     const RenderResolutionHelper& renderResolution,
                                     float                         lightmapScreenCoverage,
                                     const RgFloat3D&              skyViewerPos )
{
    auto label = CmdLabel{ cmd, "Rasterized classic" };

    assert( destination == FB_IMAGE_INDEX_UPSCALED_PING ||
            destination == FB_IMAGE_INDEX_UPSCALED_PONG || destination == FB_IMAGE_INDEX_FINAL );
    const bool upscaled = ( destination != FB_IMAGE_INDEX_FINAL );

    storageFramebuffers->BarrierOne( cmd, frameIndex, destination );

    VkDescriptorSet sets[] = {
        textureManager.GetDescSet( frameIndex ),
        uniform.GetDescSet( frameIndex ),
        tonemapping.GetDescSet(),
        volumetric.GetDescSet( frameIndex ),
    };

    // sky
    {
        float skyView[ 16 ];
        Matrix::SetNewViewerPosition( skyView, view, skyViewerPos.data );

        float defaultSkyViewProj[ 16 ];
        Matrix::Multiply( defaultSkyViewProj, skyView, proj );
        
        const RasterDrawParams params = {
            .pipelines   = rasterPass->GetClassicRasterPipelines().get(),
            .drawInfos   = collector->GetDrawInfos( GeometryRasterType::SKY ),
            .renderPass  = rasterPass->GetClassicRenderPass(),
            .framebuffer = rasterPass->GetClassicFramebuffer( destination ),
            .width       = upscaled ? renderResolution.UpscaledWidth() : renderResolution.Width(),
            .height      = upscaled ? renderResolution.UpscaledHeight() : renderResolution.Height(),
            .vertexBuffer    = collector->GetVertexBuffer(),
            .indexBuffer     = collector->GetIndexBuffer(),
            .descSets        = sets,
            .defaultViewProj = defaultSkyViewProj,
            .flaresParams    = {},
            .classic         = lightmapScreenCoverage,
        };

        Draw( cmd, frameIndex, params );
    }

    float defaultViewProj[ 16 ];
    Matrix::Multiply( defaultViewProj, view, proj );

    // lightmapScreenCoverage is quantized by 1/renderSize rather than 1/upscaledSize
    {
        auto l_quantize = []( float value, float by ) {
            return std::ceil( value / by ) * by;
        };

        float eps = std::max( 0.0f, 1.0f / float( renderResolution.Width() ) - 0.00001f );

        lightmapScreenCoverage =
            l_quantize( lightmapScreenCoverage + eps, 1.0f / float( renderResolution.Width() ) );
    }

    const RasterDrawParams params = {
        .pipelines   = rasterPass->GetClassicRasterPipelines().get(),
        .drawInfos   = collector->GetDrawInfos( GeometryRasterType::WORLD_CLASSIC ),
        .renderPass  = rasterPass->GetClassicRenderPass(),
        .framebuffer = rasterPass->GetClassicFramebuffer( destination ),
        .width  = upscaled ? renderResolution.UpscaledWidth() : renderResolution.Width(),
        .height = upscaled ? renderResolution.UpscaledHeight() : renderResolution.Height(),
        .vertexBuffer    = collector->GetVertexBuffer(),
        .indexBuffer     = collector->GetIndexBuffer(),
        .descSets        = sets,
        .defaultViewProj = defaultViewProj,
        .flaresParams    = {},
        .classic         = lightmapScreenCoverage,
    };

    Draw( cmd, frameIndex, params );
}

void RTGL1::Rasterizer::DrawToSwapchain( VkCommandBuffer       cmd,
                                         uint32_t              frameIndex,
                                         FramebufferImageIndex imageToDrawIn,
                                         const TextureManager& textureManager,
                                         const float*          view,
                                         const float*          proj,
                                         uint32_t              swapchainWidth,
                                         uint32_t              swapchainHeight,
                                         bool                  isHdr )
{
    auto label = CmdLabel{ cmd, "Rasterized to swapchain" };


    float defaultViewProj[ 16 ];
    Matrix::Multiply( defaultViewProj, view, proj );


    VkDescriptorSet sets[] = {
        textureManager.GetDescSet( frameIndex ),
    };

    const RasterDrawParams params = {
        .pipelines       = swapchainPass->GetSwapchainPipelines( imageToDrawIn ),
        .drawInfos       = collector->GetDrawInfos( GeometryRasterType::SWAPCHAIN ),
        .renderPass      = swapchainPass->GetSwapchainRenderPass( imageToDrawIn ),
        .framebuffer     = swapchainPass->GetSwapchainFramebuffer( imageToDrawIn ),
        .width           = swapchainWidth,
        .height          = swapchainHeight,
        .vertexBuffer    = collector->GetVertexBuffer(),
        .indexBuffer     = collector->GetIndexBuffer(),
        .descSets        = sets,
        .defaultViewProj = defaultViewProj,
        .manualSrgb      = ( imageToDrawIn == FB_IMAGE_INDEX_HUD_ONLY && !isHdr ),
    };

    Draw( cmd, frameIndex, params );
}

void RTGL1::Rasterizer::Draw( VkCommandBuffer         cmd,
                              uint32_t                frameIndex,
                              const RasterDrawParams& drawParams )
{
    assert( drawParams.framebuffer != VK_NULL_HANDLE );

    const bool draw           = !drawParams.drawInfos.empty();
    const bool drawLensFlares = drawParams.flaresParams && lensFlares->GetCullingInputCount() > 0;

    if( !draw && !drawLensFlares )
    {
        return;
    }

    if( drawLensFlares )
    {
        lensFlares->SyncForDraw( cmd, frameIndex );
    }

    const auto defaultViewport = VkViewport{
        .x        = 0,
        .y        = 0,
        .width    = static_cast< float >( drawParams.width ),
        .height   = static_cast< float >( drawParams.height ),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };

    auto defaultRenderArea = VkRect2D{
        .offset = { 0, 0 },
        .extent = { drawParams.width, drawParams.height },
    };

    if( drawParams.classic )
    {
        const bool  left = drawParams.classic.value() > 0;
        const float c    = Utils::Saturate( std::abs( drawParams.classic.value() ) );
        const float w    = static_cast< float >( drawParams.width );

        if( left )
        {
            defaultRenderArea = VkRect2D{
                .offset = { 0, 0 },
                .extent = { uint32_t( w * c ), drawParams.height },
            };
        }
        else
        {
            defaultRenderArea = VkRect2D{
                .offset = { int( w * c ), 0 },
                .extent = { uint32_t( w * ( 1 - c ) ), drawParams.height },
            };
        }
    }

    constexpr VkClearValue clear[] = {
        {
            // NOTE: alpha=0 denotes no HUD for framegen in a swapchain pass
            .color = { .float32 = { 0.0f, 0.0f, 0.0f, 0.0f } },
        },
        {
            .depthStencil = { .depth = 1.0f },
        },
    };

    VkRenderPassBeginInfo beginInfo = {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass      = drawParams.renderPass,
        .framebuffer     = drawParams.framebuffer,
        .renderArea      = defaultRenderArea,
        .clearValueCount = std::size( clear ),
        .pClearValues    = clear,
    };

    vkCmdBeginRenderPass( cmd, &beginInfo, VK_SUBPASS_CONTENTS_INLINE );


    if( draw )
    {
        auto curPipeline = VkPipeline{ nullptr };

        if( drawParams.pipelines )
        {
            curPipeline = drawParams.pipelines->BindPipelineIfNew(
                cmd, VK_NULL_HANDLE, drawParams.drawInfos[ 0 ].pipelineState );
        }
        else
        {
            vkCmdBindPipeline(
                cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, drawParams.standalonePipeline );
        }

        vkCmdBindDescriptorSets( cmd,
                                 VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 drawParams.pipelines ? drawParams.pipelines->GetPipelineLayout()
                                                      : drawParams.standalonePipelineLayout,
                                 0,
                                 uint32_t( drawParams.descSets.size() ),
                                 drawParams.descSets.data(),
                                 0,
                                 nullptr );

        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers( cmd, 0, 1, &drawParams.vertexBuffer, &offset );
        vkCmdBindIndexBuffer( cmd, drawParams.indexBuffer, offset, VK_INDEX_TYPE_UINT32 );


        vkCmdSetScissor( cmd, 0, 1, &defaultRenderArea );
        vkCmdSetViewport( cmd, 0, 1, &defaultViewport );
        VkViewport curViewport = defaultViewport;


        for( const auto& info : drawParams.drawInfos )
        {
            SetViewportIfNew( cmd, info, defaultViewport, curViewport );

            if( drawParams.pipelines )
            {
                curPipeline =
                    drawParams.pipelines->BindPipelineIfNew( cmd, curPipeline, info.pipelineState );
            }

            // push const
            {
                auto push =
                    RasterizedPushConst{ info, drawParams.defaultViewProj, drawParams.manualSrgb };

                vkCmdPushConstants( cmd,
                                    drawParams.pipelines ? drawParams.pipelines->GetPipelineLayout()
                                                         : drawParams.standalonePipelineLayout,
                                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                    0,
                                    sizeof( push ),
                                    &push );
            }

            // draw
            if( info.indexCount > 0 )
            {
                vkCmdDrawIndexed(
                    cmd, info.indexCount, 1, info.firstIndex, int32_t( info.firstVertex ), 0 );
            }
            else
            {
                vkCmdDraw( cmd, info.vertexCount, 1, info.firstVertex, 0 );
            }
        }
    }


    if( drawLensFlares )
    {
        vkCmdSetScissor( cmd, 0, 1, &defaultRenderArea );
        vkCmdSetViewport( cmd, 0, 1, &defaultViewport );

        lensFlares->Draw(
            cmd, frameIndex, *drawParams.flaresParams->textureManager, drawParams.defaultViewProj );
    }


    vkCmdEndRenderPass( cmd );
}

const std::shared_ptr< RTGL1::RenderCubemap >& RTGL1::Rasterizer::GetRenderCubemap() const
{
    return renderCubemap;
}

void RTGL1::Rasterizer::OnShaderReload( const ShaderManager* shaderManager )
{
    rasterPass->OnShaderReload( shaderManager );
    swapchainPass->OnShaderReload( shaderManager );
    renderCubemap->OnShaderReload( shaderManager );
    lensFlares->OnShaderReload( shaderManager );
    decalManager->OnShaderReload( shaderManager );
}

void RTGL1::Rasterizer::OnFramebuffersSizeChange( const ResolutionState& resolutionState )
{
    decalManager->OnFramebuffersSizeChange( resolutionState );

    rasterPass->DestroyFramebuffers();
    swapchainPass->DestroyFramebuffers();

    rasterPass->CreateFramebuffers( resolutionState.renderWidth,
                                    resolutionState.renderHeight,
                                    resolutionState.upscaledWidth,
                                    resolutionState.upscaledHeight,
                                    *storageFramebuffers,
                                    *allocator,
                                    *cmdManager );

    swapchainPass->CreateFramebuffers(
        resolutionState.upscaledWidth, resolutionState.upscaledHeight, *storageFramebuffers );
}
