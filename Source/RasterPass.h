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

#include "Common.h"
#include "DepthCopying.h"
#include "Framebuffers.h"
#include "RasterizerPipelines.h"

namespace RTGL1
{
class RasterPass : public IShaderDependency
{
public:
    RasterPass( VkDevice                    device,
                VkPhysicalDevice            physDevice,
                VkPipelineLayout            pipelineLayout,
                const ShaderManager&        shaderManager,
                const Framebuffers&         storageFramebuffers,
                const RgInstanceCreateInfo& instanceInfo );
    ~RasterPass() override;

    RasterPass( const RasterPass& other )                = delete;
    RasterPass( RasterPass&& other ) noexcept            = delete;
    RasterPass& operator=( const RasterPass& other )     = delete;
    RasterPass& operator=( RasterPass&& other ) noexcept = delete;

    void PrepareForFinal( VkCommandBuffer     cmd,
                          uint32_t            frameIndex,
                          const Framebuffers& storageFramebuffers,
                          uint32_t            renderWidth,
                          uint32_t            renderHeight );

    void CreateFramebuffers( uint32_t              renderWidth,
                             uint32_t              renderHeight,
                             uint32_t              upscaledWidth,
                             uint32_t              upscaledHeight,
                             const Framebuffers&   storageFramebuffers,
                             MemoryAllocator&      allocator,
                             CommandBufferManager& cmdManager );
    void DestroyFramebuffers();

    void OnShaderReload( const ShaderManager* shaderManager ) override;

    VkRenderPass GetWorldRenderPass() const;
    VkRenderPass GetClassicRenderPass() const;
    VkRenderPass GetSkyRenderPass() const;

    const std::shared_ptr< RasterizerPipelines >& GetRasterPipelines() const;
    const std::shared_ptr< RasterizerPipelines >& GetClassicRasterPipelines() const;
    const std::shared_ptr< RasterizerPipelines >& GetSkyRasterPipelines() const;

    VkFramebuffer GetWorldFramebuffer() const;
    VkFramebuffer GetClassicFramebuffer( FramebufferImageIndex img ) const;
    VkFramebuffer GetSkyFramebuffer() const;

private:
    VkRenderPass CreateWorldRenderPass( VkFormat finalImageFormat,
                                        VkFormat screenEmisionFormat,
                                        VkFormat reactivityFormat,
                                        VkFormat depthImageFormat ) const;
    static VkRenderPass CreateClassicRenderPass( VkDevice device,
                                                 VkFormat colorImageFormat,
                                                 VkFormat depthImageFormat );
    VkRenderPass CreateSkyRenderPass( VkFormat skyFinalImageFormat,
                                      VkFormat depthImageFormat ) const;

    struct DepthBuffer
    {
        VkImage        image{ VK_NULL_HANDLE };
        VkImageView    view{ VK_NULL_HANDLE };
        VkDeviceMemory memory{ VK_NULL_HANDLE };
    };
    [[nodiscard]] static auto CreateDepthBuffers( uint32_t              width,
                                                  uint32_t              height,
                                                  MemoryAllocator&      allocator,
                                                  CommandBufferManager& cmdManager ) -> DepthBuffer;
    static void DestroyDepthBuffers( VkDevice device, DepthBuffer& buf );

private:
    VkDevice device{ VK_NULL_HANDLE };

    VkRenderPass worldRenderPass{ VK_NULL_HANDLE };
    VkRenderPass classicRenderPass{ VK_NULL_HANDLE };
    VkRenderPass skyRenderPass{ VK_NULL_HANDLE };

    std::shared_ptr< RasterizerPipelines > worldPipelines{};
    std::shared_ptr< RasterizerPipelines > classicPipelines{};
    std::shared_ptr< RasterizerPipelines > skyPipelines{};

    VkFramebuffer worldFramebuffer{ VK_NULL_HANDLE };
    VkFramebuffer classicFramebuffer_UpscaledPing{ VK_NULL_HANDLE };
    VkFramebuffer classicFramebuffer_UpscaledPong{ VK_NULL_HANDLE };
    VkFramebuffer classicFramebuffer_Final{ VK_NULL_HANDLE };
    VkFramebuffer skyFramebuffer{ VK_NULL_HANDLE };

    std::shared_ptr< DepthCopying > depthCopying{};

    DepthBuffer renderDepth{};
    DepthBuffer upscaledDepth{};
};

}
