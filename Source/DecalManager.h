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

#include "AutoBuffer.h"
#include "Framebuffers.h"
#include "GlobalUniform.h"
#include "ShaderManager.h"
#include "TextureManager.h"

namespace RTGL1
{

class DecalManager
    : public IShaderDependency
    , public IFramebuffersDependency
{
public:
    DecalManager( VkDevice                           device,
                  std::shared_ptr< MemoryAllocator > allocator,
                  std::shared_ptr< Framebuffers >    storageFramebuffers,
                  const ShaderManager&               shaderManager,
                  const GlobalUniform&               uniform,
                  VkPipelineLayout&&                 drawPipelineLayout );
    ~DecalManager() override;

    DecalManager( const DecalManager& other )                = delete;
    DecalManager( DecalManager&& other ) noexcept            = delete;
    DecalManager& operator=( const DecalManager& other )     = delete;
    DecalManager& operator=( DecalManager&& other ) noexcept = delete;

    void CopyRtGBufferToAttachments( VkCommandBuffer      cmd,
                                     uint32_t             frameIndex,
                                     const GlobalUniform& uniform,
                                     Framebuffers&        framebuffers );
    void CopyAttachmentsToRtGBuffer( VkCommandBuffer      cmd,
                                     uint32_t             frameIndex,
                                     const GlobalUniform& uniform,
                                     const Framebuffers&  framebuffers );

    VkRenderPass  GetRenderPass() { return renderPass; }
    VkFramebuffer GetFramebuffer( uint32_t frameIndex ) { return passFramebuffers[ frameIndex ]; }
    VkPipeline    GetDrawPipeline() { return pipeline; }
    VkPipelineLayout GetDrawPipelineLayout() { return drawPipelineLayout; }

    void OnShaderReload( const ShaderManager* shaderManager ) override;
    void OnFramebuffersSizeChange( const ResolutionState& resolutionState ) override;

private:
    void CreateRenderPass();
    void CreateFramebuffers( uint32_t width, uint32_t height );
    void DestroyFramebuffers();
    void CreatePipelines( const ShaderManager* shaderManager );
    void DestroyPipelines();

private:
    VkDevice                        device;
    std::shared_ptr< Framebuffers > storageFramebuffers;

    VkRenderPass  renderPass{ VK_NULL_HANDLE };
    VkFramebuffer passFramebuffers[ MAX_FRAMES_IN_FLIGHT ]{};

    VkPipelineLayout drawPipelineLayout{ VK_NULL_HANDLE };
    VkPipeline       pipeline{ VK_NULL_HANDLE };

    VkPipelineLayout copyingPipelineLayout{ VK_NULL_HANDLE };
    VkPipeline       copyNormalsToAttachment{ VK_NULL_HANDLE };
    VkPipeline       copyNormalsToGbuffer{ VK_NULL_HANDLE };
};

}