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

#include "AutoBuffer.h"
#include "Framebuffers.h"
#include "InternalExtensions.inl"
#include "ShaderManager.h"
#include "Utils.h"

struct ShParticleSourceDef;

namespace RTGL1
{

class RenderResolutionHelper;


struct RingBuf
{
    uint32_t ringBegin{ 0 };
    uint32_t ringEnd{ 0 };
    uint32_t ringFull{ 0 };

    auto length() const -> uint32_t;
    auto asRanges() const -> std::array< CopyRange, 2 >;

    void pushCount( uint32_t count );
};


class Fluid
    : public IShaderDependency
    , public IFramebuffersDependency
{
public:
    Fluid( VkDevice                                device,
           std::shared_ptr< CommandBufferManager > cmdManager,
           std::shared_ptr< MemoryAllocator >&     allocator,
           std::shared_ptr< Framebuffers >         storageFramebuffer,
           const ShaderManager&                    shaderManager,
           VkDescriptorSetLayout                   tlasLayout,
           uint32_t                                fluidBudget,
           float                                   particleRadius );
    ~Fluid() override;

    Fluid( const Fluid& )                = delete;
    Fluid( Fluid&& ) noexcept            = delete;
    Fluid& operator=( const Fluid& )     = delete;
    Fluid& operator=( Fluid&& ) noexcept = delete;

    void PrepareForFrame( bool reset );

    void AddSource( const RgSpawnFluidInfo& src );

    void Simulate( VkCommandBuffer  cmd,
                   uint32_t         frameIndex,
                   VkDescriptorSet  tlasDescSet,
                   float            deltaTime,
                   const RgFloat3D& gravity );
    void Visualize( VkCommandBuffer               cmd,
                    uint32_t                      frameIndex,
                    const float*                  view,
                    const float*                  proj,
                    const RenderResolutionHelper& renderResolution,
                    float                         znear,
                    float                         zfar );

    void OnShaderReload( const ShaderManager* shaderManager ) override;
    void OnFramebuffersSizeChange( const ResolutionState& resolutionState ) override;

    bool Active() const;

private:
    void CreateDescriptors();
    void UpdateDescriptors();

    void CreatePipelineLayouts( VkDescriptorSetLayout asLayout );
    void CreatePipelines( const ShaderManager& shaderManager );
    void DestroyPipelines();

    void CreateFramebuffers( uint32_t width, uint32_t height );
    void DestroyFramebuffers();
    void CreateRenderPass();

private:
    struct VolumeDef
    {
        VkImage        image{ VK_NULL_HANDLE };
        VkImageView    view{ VK_NULL_HANDLE };
        VkDeviceMemory memory{ VK_NULL_HANDLE };
    };
    struct AliasedDef
    {
        VkImage     image{ VK_NULL_HANDLE };
        VkImageView view{ VK_NULL_HANDLE };
    };

private:
    VkDevice m_device{ VK_NULL_HANDLE };

    std::shared_ptr< Framebuffers >         m_storageFramebuffer{};
    std::shared_ptr< CommandBufferManager > m_cmdManager{};

    Buffer     m_particlesArray{};
    AutoBuffer m_generateIdToSource;
    AutoBuffer m_sources;
    std::vector< ShParticleSourceDef > m_sourcesCached; // because sources can be added out-of-frame
    std::vector< uint32_t >            m_sourcesCachedCnt;

    VkDescriptorPool      m_descPool{ VK_NULL_HANDLE };
    VkDescriptorSetLayout m_descLayout{ VK_NULL_HANDLE };
    VkDescriptorSet       m_descSet{ VK_NULL_HANDLE };

    VkPipelineLayout m_particlesPipelineLayout{ VK_NULL_HANDLE };
    VkPipeline       m_generatePipeline{ VK_NULL_HANDLE };
    VkPipeline       m_particlesPipeline{ VK_NULL_HANDLE };

    VkPipelineLayout m_visualizePipelineLayout{ VK_NULL_HANDLE };
    VkPipeline       m_visualizePipeline{ VK_NULL_HANDLE };
    VkRenderPass     m_renderPass{ VK_NULL_HANDLE };
    VkFramebuffer    m_passFramebuffer{};

    VkPipelineLayout m_smoothPipelineLayout{ VK_NULL_HANDLE };
    VkPipeline       m_smoothPipelines[ 2 ]{};

    AliasedDef m_depth{};

    RingBuf m_active{};

    float m_particleRadius{ 0.1f };
};

}
