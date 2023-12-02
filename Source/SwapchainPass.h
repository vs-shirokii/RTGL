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
#include "Framebuffers.h"
#include "RasterizerPipelines.h"

namespace RTGL1
{

class SwapchainPass final : public IShaderDependency
{
public:
    SwapchainPass( VkDevice                    device,
                   VkPipelineLayout            pipelineLayout,
                   const ShaderManager&        shaderManager,
                   const RgInstanceCreateInfo& instanceInfo );
    ~SwapchainPass() override;

    SwapchainPass( const SwapchainPass& other )     = delete;
    SwapchainPass( SwapchainPass&& other ) noexcept = delete;
    SwapchainPass& operator=( const SwapchainPass& other ) = delete;
    SwapchainPass& operator=( SwapchainPass&& other ) noexcept = delete;

    void CreateFramebuffers( uint32_t      swapchainWidth,
                             uint32_t      swapchainHeight,
                             Framebuffers& storageFramebuffers );
    void DestroyFramebuffers();

    void           OnShaderReload( const ShaderManager* shaderManager ) override;

    auto GetSwapchainRenderPass( FramebufferImageIndex framebufIndex ) const -> VkRenderPass;
    auto GetSwapchainPipelines( FramebufferImageIndex framebufIndex ) const -> RasterizerPipelines*;
    auto GetSwapchainFramebuffer( FramebufferImageIndex framebufIndex ) const -> VkFramebuffer;

private:
    [[nodiscard]] VkRenderPass CreateSwapchainRenderPass( VkFormat format, bool clear ) const;

private:
    VkDevice                               device;

    VkRenderPass                           swapchainRenderPass;
    VkRenderPass                           swapchainRenderPass_hudOnly;
    std::shared_ptr< RasterizerPipelines > swapchainPipelines;
    std::shared_ptr< RasterizerPipelines > swapchainPipelines_hudOnly;

    VkFramebuffer fbPing;
    VkFramebuffer fbPong;
    VkFramebuffer hudOnly;
};

}
