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

#include "Fluid.h"

#include "CmdLabel.h"
#include "CommandBufferManager.h"
#include "Matrix.h"
#include "MemoryAllocator.h"
#include "RenderResolutionHelper.h"
#include "Utils.h"

#include "Generated/ShaderCommonC.h"
#include "Generated/ShaderCommonCFramebuf.h"
#include "Shaders/Fluid_Def.h"

#include <glm/glm.hpp>
#include <glm/gtc/packing.hpp>

namespace RTGL1
{
namespace
{
    // simulation

    constexpr uint32_t MAX_PARTICLES_DEFAULT = 1024 * 1024;
    uint32_t           MAX_PARTICLES = MAX_PARTICLES_DEFAULT; // NOTE: was originally constexpr

    // generation

    using IdToSource               = uint8_t;
    constexpr uint32_t MAX_SOURCES = std::numeric_limits< IdToSource >::max();

    // visualization

    constexpr uint32_t QUAD_VERTEX_COUNT = 4;
    constexpr auto     QUAD_TOPOLOGY     = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

    struct VisualizePush_T
    {
        float proj[ 16 ];
        float view[ 16 ];
    };

    // push structs

    using uint = uint32_t;
    using vec3 = RgFloat3D;

    struct PARTICLESPUSH_T;

    struct FluidSmoothPush_T
    {
        uint32_t renderWidth;
        uint32_t renderHeight;
        float    zNear;
        float    zFar;
    };
}

uint32_t RingBuf::length() const
{
    assert( ringBegin < MAX_PARTICLES );
    assert( ringEnd < MAX_PARTICLES );

    if( ringFull )
    {
        return MAX_PARTICLES;
    }

    if( ringEnd < ringBegin )
    {
        return MAX_PARTICLES + ringEnd - ringBegin;
    }
    return ringEnd - ringBegin;
}

auto RingBuf::asRanges() const -> std::array< CopyRange, 2 >
{
    if( ringFull )
    {
        return {
            MakeRangeFromCount( 0, MAX_PARTICLES ),
            CopyRange{},
        };
    }

    if( ringEnd < ringBegin )
    {
        return {
            MakeRangeFromCount( ringBegin, MAX_PARTICLES - ringBegin ),
            MakeRangeFromCount( 0, ringEnd + 1 ),
        };
    }

    return {
        MakeRangeFromCount( ringBegin, ringEnd - ringBegin ),
        CopyRange{},
    };
}

RingBuf makeRing( uint32_t first, uint32_t count )
{
    assert( count <= MAX_PARTICLES );
    count = std::min( count, MAX_PARTICLES );

    return RingBuf{
        .ringBegin = first % MAX_PARTICLES,
        .ringEnd   = ( first + count ) % MAX_PARTICLES,
        .ringFull  = ( count == MAX_PARTICLES ),
    };
}

RingBuf appendRing( const RingBuf& base, const RingBuf& increment )
{
    assert( base.ringEnd == increment.ringBegin );

    if( base.length() + increment.length() >= MAX_PARTICLES )
    {
        return makeRing( base.ringBegin + increment.length(),
                         MAX_PARTICLES );
    }

    return makeRing( base.ringBegin,
                     std::min( MAX_PARTICLES, base.length() + increment.length() ) );
}

RingBuf makeEmptyWithBeginningAtEndOf( const RingBuf& after )
{
    return RingBuf{
        .ringBegin = after.ringEnd,
        .ringEnd   = after.ringEnd,
        .ringFull  = false,
    };
}

void RingBuf::pushCount( uint32_t count )
{
    *this = appendRing( *this, makeRing( this->ringEnd, count ) );
}

} // namespace RTGL1

RTGL1::Fluid::Fluid( VkDevice                                device,
                     std::shared_ptr< CommandBufferManager > cmdManager,
                     std::shared_ptr< MemoryAllocator >&     allocator,
                     std::shared_ptr< Framebuffers >         storageFramebuffer,
                     const ShaderManager&                    shaderManager,
                     VkDescriptorSetLayout                   tlasLayout,
                     uint32_t                                fluidBudget,
                     float                                   particleRadius )
    : m_device{ device }
    , m_storageFramebuffer{ std::move( storageFramebuffer ) }
    , m_cmdManager{ std::move( cmdManager ) }
    , m_generateIdToSource{ allocator }
    , m_sources{ allocator }
    , m_particleRadius{ std::clamp( particleRadius, 0.01f, 1.0f ) }
{
    {
        fluidBudget = std::clamp( fluidBudget, 4096u, MAX_PARTICLES_DEFAULT );
        fluidBudget = Utils::Align( fluidBudget, 4096u );
        MAX_PARTICLES = fluidBudget;
    }

    m_particlesArray.Init( *allocator,
                           MAX_PARTICLES * sizeof( ShParticleDef ),
                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                           "Fluid Particles" );

    m_generateIdToSource.Create( MAX_PARTICLES * sizeof( IdToSource ),
                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                 "Fluid Generate: Particle ID to Source" );

    m_sources.Create( MAX_SOURCES * sizeof( ShParticleSourceDef ),
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                      "Fluid Sources" );
    m_sourcesCached.reserve( MAX_SOURCES );
    m_sourcesCachedCnt.reserve( MAX_SOURCES );

    {
        VkCommandBuffer cmd = m_cmdManager->StartGraphicsCmd();
        {
            assert( m_particlesArray.GetSize() % 4 == 0 );
            vkCmdFillBuffer( cmd, //
                             m_particlesArray.GetBuffer(),
                             0,
                             m_particlesArray.GetSize(),
                             0xFFFFFFFF );
            assert( m_generateIdToSource.GetSize() % 4 == 0 );
            vkCmdFillBuffer( cmd, //
                             m_generateIdToSource.GetDeviceLocal(),
                             0,
                             m_generateIdToSource.GetSize(),
                             0xFFFFFFFF );
        }
        m_cmdManager->Submit( cmd );
        m_cmdManager->WaitGraphicsIdle();
    }

    CreateRenderPass();

    CreateDescriptors();
    UpdateDescriptors();

    CreatePipelineLayouts( tlasLayout );
    CreatePipelines( shaderManager );
}

RTGL1::Fluid::~Fluid()
{
    vkDeviceWaitIdle( m_device );
    vkDestroyDescriptorPool( m_device, m_descPool, nullptr );
    vkDestroyDescriptorSetLayout( m_device, m_descLayout, nullptr );
    vkDestroyPipelineLayout( m_device, m_particlesPipelineLayout, nullptr );
    vkDestroyPipelineLayout( m_device, m_visualizePipelineLayout, nullptr );
    vkDestroyPipelineLayout( m_device, m_smoothPipelineLayout, nullptr );
    vkDestroyRenderPass( m_device, m_renderPass, nullptr );
    DestroyFramebuffers();
    DestroyPipelines();
}

void RTGL1::Fluid::PrepareForFrame( bool reset )
{
    if( reset )
    {
        m_sourcesCached.clear();
        m_sourcesCachedCnt.clear();

        m_active = makeEmptyWithBeginningAtEndOf( m_active );
    }
}

void RTGL1::Fluid::AddSource( const RgSpawnFluidInfo& src )
{
    if( src.count == 0 )
    {
        return;
    }

    if( src.count >= MAX_PARTICLES )
    {
        debug::Error( "Too many particles in a fluid source. Max={}", MAX_PARTICLES );
        return;
    }

    if( m_sourcesCached.size() >= MAX_SOURCES )
    {
        debug::Error( "Too many fluid sources in a frame, ignoring" );
        return;
    }
    
    m_sourcesCached.push_back( ShParticleSourceDef{
        .position_dispersionAngle = glm::packHalf4x16( {
            src.position.data[ 0 ],
            src.position.data[ 1 ],
            src.position.data[ 2 ],
            std::clamp( src.dispersionAngleDegrees / 180.f, 0.f, 1.f ),
        } ),
        .velocity_dispersion      = glm::packHalf4x16( {
            src.velocity.data[ 0 ],
            src.velocity.data[ 1 ],
            src.velocity.data[ 2 ],
            std::clamp( src.dispersionVelocity, 0.f, 1.f ),
        } ),
    } );
    m_sourcesCachedCnt.push_back( src.count );
}

bool RTGL1::Fluid::Active() const
{
    return m_active.length() > 0 || !m_sourcesCached.empty();
}

void RTGL1::Fluid::Simulate( VkCommandBuffer  cmd,
                             uint32_t         frameIndex,
                             VkDescriptorSet  tlasDescSet,
                             float            deltaTime,
                             const RgFloat3D& gravity )
{
    if( !Active() )
    {
        return;
    }

    auto label = CmdLabel{ cmd, "Fluid Particles Simulate" };


    // append generated at the end of active
    auto generateIdToSource_copy = makeEmptyWithBeginningAtEndOf( m_active );

    const auto sourceCount = uint32_t( m_sourcesCached.size() );
    if( sourceCount > 0 )
    {
        assert( sourceCount == m_sourcesCachedCnt.size() );
        {
            memcpy( m_sources.GetMapped( frameIndex ), //
                    m_sourcesCached.data(),
                    sizeof( ShParticleSourceDef ) * sourceCount );
        }
        auto* idToSourceArr = m_generateIdToSource.GetMappedAs< IdToSource* >( frameIndex );

        for( uint32_t sourceId = 0; sourceId < sourceCount; sourceId++ )
        {
            const RingBuf newlyAdded =
                makeRing( generateIdToSource_copy.ringEnd, m_sourcesCachedCnt[ sourceId ] );

            for( const CopyRange& r : newlyAdded.asRanges() )
            {
                if( r.count() > 0 )
                {
                    static_assert( sizeof( IdToSource ) == 1, "memset accepts a 255 value" );

                    memset( &idToSourceArr[ r.first() ],
                            static_cast< int >( sourceId ),
                            sizeof( IdToSource ) * r.count() );
                }
            }

            generateIdToSource_copy = appendRing( generateIdToSource_copy, newlyAdded );
            m_active                = appendRing( m_active, newlyAdded );
        }
    }
    m_sourcesCached.clear();
    m_sourcesCachedCnt.clear();


    const bool generate = generateIdToSource_copy.length() > 0 && sourceCount > 0;


    if( generate )
    {
        VkBufferCopy copies[ 2 ];
        uint32_t     cnt = 0;
        for( const CopyRange& r : generateIdToSource_copy.asRanges() )
        {
            if( r.count() > 0 )
            {
                copies[ cnt++ ] = VkBufferCopy{
                    .srcOffset = sizeof( IdToSource ) * r.first(),
                    .dstOffset = sizeof( IdToSource ) * r.first(),
                    .size      = sizeof( IdToSource ) * r.count(),
                };
                assert( cnt <= 2 );
            }
        }
        m_generateIdToSource.CopyFromStaging( cmd, frameIndex, copies, cnt );

        m_sources.CopyFromStaging(
            cmd, frameIndex, sourceCount * sizeof( ShParticleSourceDef ), 0 );
    }


    VkDescriptorSet sets[] = {
        m_descSet,
        tlasDescSet,
    };
    vkCmdBindDescriptorSets( cmd,
                             VK_PIPELINE_BIND_POINT_COMPUTE,
                             m_particlesPipelineLayout,
                             0,
                             std::size( sets ),
                             sets,
                             0,
                             nullptr );

    const auto push = ParticlesPush_T{
        .gravity            = gravity,
        .deltaTime          = deltaTime,
        .activeRingBegin    = m_active.ringBegin,
        .activeRingLength   = m_active.length(),
        .generateRingBegin  = generateIdToSource_copy.ringBegin,
        .generateRingLength = generateIdToSource_copy.length(),
    };
    static_assert( sizeof( push ) == 32 );

    vkCmdPushConstants( cmd, //
                        m_particlesPipelineLayout,
                        VK_SHADER_STAGE_COMPUTE_BIT,
                        0,
                        sizeof( push ),
                        &push );

    if( generate )
    {
        vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_generatePipeline );
        vkCmdDispatch( cmd,
                       Utils::GetWorkGroupCount( push.generateRingLength,
                                                 COMPUTE_FLUID_PARTICLES_GENERATE_GROUP_SIZE_X ),
                       1,
                       1 );

        // sync read access to a grid / particles
        {
            VkBufferMemoryBarrier2 bs[ 2 ];
            uint32_t               cnt = 0;
            for( const CopyRange& r : generateIdToSource_copy.asRanges() )
            {
                if( r.count() > 0 )
                {
                    bs[ cnt++ ] = VkBufferMemoryBarrier2{
                        .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                        .pNext               = nullptr,
                        .srcStageMask        = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        .srcAccessMask       = VK_ACCESS_2_SHADER_WRITE_BIT,
                        .dstStageMask        = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        .dstAccessMask       = VK_ACCESS_2_SHADER_READ_BIT,
                        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .buffer              = m_particlesArray.GetBuffer(),
                        .offset              = r.first() * sizeof( ShParticleDef ),
                        .size                = r.count() * sizeof( ShParticleDef ),
                    };
                }
            }
            auto dpd = VkDependencyInfo{
                .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .pNext                    = nullptr,
                .dependencyFlags          = 0,
                .bufferMemoryBarrierCount = cnt,
                .pBufferMemoryBarriers    = bs,
            };
            svkCmdPipelineBarrier2KHR( cmd, &dpd );
        }
    }

    vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_particlesPipeline );
    vkCmdDispatch(
        cmd,
        Utils::GetWorkGroupCount( m_active.length(), COMPUTE_FLUID_PARTICLES_GROUP_SIZE_X ),
        1,
        1 );
}

void RTGL1::Fluid::Visualize( VkCommandBuffer               cmd,
                              uint32_t                      frameIndex,
                              const float*                  view,
                              const float*                  proj,
                              const RenderResolutionHelper& renderResolution,
                              float                         znear,
                              float                         zfar )
{
    if( !Active() )
    {
        return;
    }

    auto label = CmdLabel{ cmd, "Fluid Particles Visualize" };

    auto push = VisualizePush_T{};
    {
        // no jittering
        memcpy( push.proj, proj, 64 );
        memcpy( push.view, view, 64 );
    }

    const auto viewport = VkViewport{
        .x        = 0,
        .y        = 0,
        .width    = float( renderResolution.Width() ),
        .height   = float( renderResolution.Height() ),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };

    const auto renderArea = VkRect2D{
        .offset = { .x = 0, .y = 0 },
        .extent = { .width = renderResolution.Width(), .height = renderResolution.Height() },
    };

    constexpr auto clears = std::array{
        VkClearValue{ .color = { .uint32 = 0xFFFFFFFF } },
        VkClearValue{ .depthStencil = { .depth = 1.0f } },
    };

    const auto begin = VkRenderPassBeginInfo{
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .pNext           = nullptr,
        .renderPass      = m_renderPass,
        .framebuffer     = m_passFramebuffer,
        .renderArea      = renderArea,
        .clearValueCount = std::size( clears ),
        .pClearValues    = std::data( clears ),
    };

    vkCmdBeginRenderPass( cmd, &begin, VK_SUBPASS_CONTENTS_INLINE );
    {
        vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_visualizePipeline );

        VkDescriptorSet sets[] = {
            m_descSet,
        };
        vkCmdBindDescriptorSets( cmd,
                                 VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 m_visualizePipelineLayout,
                                 0,
                                 std::size( sets ),
                                 sets,
                                 0,
                                 nullptr );

        vkCmdPushConstants( cmd, //
                            m_visualizePipelineLayout,
                            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                            0,
                            sizeof( push ),
                            &push );

        vkCmdSetScissor( cmd, 0, 1, &renderArea );
        vkCmdSetViewport( cmd, 0, 1, &viewport );
        vkCmdDraw( cmd, QUAD_VERTEX_COUNT, m_active.length(), 0, m_active.ringBegin );
    }
    vkCmdEndRenderPass( cmd );


    // Need to be odd, so the final write is into DepthFluid
    assert( std::size( m_smoothPipelines ) % 2 == 0 );
    {
        auto hlabel = CmdLabel{ cmd, "Fluid Smoothing" };

        VkDescriptorSet sets[] = {
            m_storageFramebuffer->GetDescSet( frameIndex ),
        };
        vkCmdBindDescriptorSets( cmd,
                                 VK_PIPELINE_BIND_POINT_COMPUTE,
                                 m_smoothPipelineLayout,
                                 0,
                                 std::size( sets ),
                                 sets,
                                 0,
                                 nullptr );

        for( uint32_t iter = 0; iter < std::size( m_smoothPipelines ); iter++ )
        {
            FramebufferImageIndex fs[] = {
                iter % 2 == 0 ? FB_IMAGE_INDEX_DEPTH_FLUID : FB_IMAGE_INDEX_DEPTH_FLUID_TEMP,
                iter % 2 == 0 ? FB_IMAGE_INDEX_FLUID_NORMAL : FB_IMAGE_INDEX_FLUID_NORMAL_TEMP,
            };
            m_storageFramebuffer->BarrierMultiple( cmd, frameIndex, fs );

            auto push2 = FluidSmoothPush_T{
                .renderWidth  = renderResolution.Width(),
                .renderHeight = renderResolution.Height(),
                .zNear        = znear,
                .zFar         = zfar,
            };
            vkCmdPushConstants( cmd, //
                                m_smoothPipelineLayout,
                                VK_SHADER_STAGE_COMPUTE_BIT,
                                0,
                                sizeof( push2 ),
                                &push2 );

            vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_smoothPipelines[ iter ] );
            vkCmdDispatch(
                cmd,
                Utils::GetWorkGroupCount( push2.renderWidth, COMPUTE_EFFECT_GROUP_SIZE_X ),
                Utils::GetWorkGroupCount( push2.renderHeight, COMPUTE_EFFECT_GROUP_SIZE_Y ),
                1 );
        }
    }
}

void RTGL1::Fluid::OnShaderReload( const ShaderManager* shaderManager )
{
    DestroyPipelines();
    CreatePipelines( *shaderManager );
}

void RTGL1::Fluid::OnFramebuffersSizeChange( const ResolutionState& resolutionState )
{
    DestroyFramebuffers();
    CreateFramebuffers( resolutionState.renderWidth, resolutionState.renderHeight );
}

void RTGL1::Fluid::CreateDescriptors()
{
    auto r = VkResult{};

    VkDescriptorSetLayoutBinding bindings[] = {
        {
            .binding         = BINDING_FLUID_PARTICLES_ARRAY,
            .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT,
        },
        {
            .binding         = BINDING_FLUID_GENERATE_ID_TO_SOURCE,
            .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT,
        },
        {
            .binding         = BINDING_FLUID_SOURCES,
            .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT,
        },
    };

    auto layoutInfo = VkDescriptorSetLayoutCreateInfo{
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext        = nullptr,
        .flags        = 0,
        .bindingCount = std::size( bindings ),
        .pBindings    = bindings,
    };

    r = vkCreateDescriptorSetLayout( m_device, &layoutInfo, nullptr, &m_descLayout );
    VK_CHECKERROR( r );
    SET_DEBUG_NAME(
        m_device, m_descLayout, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, "Fluid Desc set layout" );

    auto poolSize = VkDescriptorPoolSize{
        .type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = std::size( bindings ),
    };

    auto poolInfo = VkDescriptorPoolCreateInfo{
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets       = MAX_FRAMES_IN_FLIGHT,
        .poolSizeCount = 1,
        .pPoolSizes    = &poolSize,
    };
    r = vkCreateDescriptorPool( m_device, &poolInfo, nullptr, &m_descPool );
    VK_CHECKERROR( r );
    SET_DEBUG_NAME( m_device, m_descPool, VK_OBJECT_TYPE_DESCRIPTOR_POOL, "Fluid Desc pool" );

    {
        auto allocInfo = VkDescriptorSetAllocateInfo{
            .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool     = m_descPool,
            .descriptorSetCount = 1,
            .pSetLayouts        = &m_descLayout,
        };

        r = vkAllocateDescriptorSets( m_device, &allocInfo, &m_descSet );
        VK_CHECKERROR( r );
        SET_DEBUG_NAME( m_device, m_descSet, VK_OBJECT_TYPE_DESCRIPTOR_SET, "Fluid Desc set" );
    }
}

void RTGL1::Fluid::UpdateDescriptors()
{
    VkDescriptorBufferInfo bufs[] = {
        {
            .buffer = m_particlesArray.GetBuffer(),
            .offset = 0,
            .range  = VK_WHOLE_SIZE,
        },
        {
            .buffer = m_generateIdToSource.GetDeviceLocal(),
            .offset = 0,
            .range  = VK_WHOLE_SIZE,
        },
        {
            .buffer = m_sources.GetDeviceLocal(),
            .offset = 0,
            .range  = VK_WHOLE_SIZE,
        },
    };
    static_assert( MAX_FRAMES_IN_FLIGHT == 2 );

    VkWriteDescriptorSet wrts[] = {
        {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = m_descSet,
            .dstBinding      = BINDING_FLUID_PARTICLES_ARRAY,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo     = &bufs[ BINDING_FLUID_PARTICLES_ARRAY ],
        },
        {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = m_descSet,
            .dstBinding      = BINDING_FLUID_GENERATE_ID_TO_SOURCE,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo     = &bufs[ BINDING_FLUID_GENERATE_ID_TO_SOURCE ],
        },
        {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = m_descSet,
            .dstBinding      = BINDING_FLUID_SOURCES,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo     = &bufs[ BINDING_FLUID_SOURCES ],
        },
    };
    static_assert( std::size( wrts ) == std::size( bufs ) );

    vkUpdateDescriptorSets( m_device, std::size( wrts ), wrts, 0, nullptr );
}

void RTGL1::Fluid::CreatePipelineLayouts( VkDescriptorSetLayout asLayout )
{
    assert( m_particlesPipelineLayout == VK_NULL_HANDLE );
    {
        VkDescriptorSetLayout sets[] = {
            m_descLayout,
            asLayout,
        };

        VkPushConstantRange pushs[] = {
            {
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                .offset     = 0,
                .size       = sizeof( ParticlesPush_T ),
            },
        };

        auto info = VkPipelineLayoutCreateInfo{
            .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pNext                  = nullptr,
            .flags                  = 0,
            .setLayoutCount         = std::size( sets ),
            .pSetLayouts            = sets,
            .pushConstantRangeCount = std::size( pushs ),
            .pPushConstantRanges    = pushs,
        };

        VkResult r = vkCreatePipelineLayout( m_device, &info, nullptr, &m_particlesPipelineLayout );
        VK_CHECKERROR( r );

        SET_DEBUG_NAME( m_device,
                        m_particlesPipelineLayout,
                        VK_OBJECT_TYPE_PIPELINE_LAYOUT,
                        "Fluid Particles pipeline layout" );
    }

    assert( m_visualizePipelineLayout == VK_NULL_HANDLE );
    {
        VkDescriptorSetLayout sets[] = {
            m_descLayout,
        };

        VkPushConstantRange pushs[] = {
            {
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                .offset     = 0,
                .size       = sizeof( VisualizePush_T ),
            },
        };

        auto info = VkPipelineLayoutCreateInfo{
            .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pNext                  = nullptr,
            .flags                  = 0,
            .setLayoutCount         = std::size( sets ),
            .pSetLayouts            = sets,
            .pushConstantRangeCount = std::size( pushs ),
            .pPushConstantRanges    = pushs,
        };

        VkResult r = vkCreatePipelineLayout( m_device, &info, nullptr, &m_visualizePipelineLayout );
        VK_CHECKERROR( r );

        SET_DEBUG_NAME( m_device,
                        m_visualizePipelineLayout,
                        VK_OBJECT_TYPE_PIPELINE_LAYOUT,
                        "Fluid Visualize pipeline layout" );
    }

    assert( m_smoothPipelineLayout == VK_NULL_HANDLE );
    {
        VkDescriptorSetLayout sets[] = {
            m_storageFramebuffer->GetDescSetLayout(),
        };

        VkPushConstantRange pushs[] = {
            {
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                .offset     = 0,
                .size       = sizeof( FluidSmoothPush_T ),
            },
        };

        auto info = VkPipelineLayoutCreateInfo{
            .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pNext                  = nullptr,
            .flags                  = 0,
            .setLayoutCount         = std::size( sets ),
            .pSetLayouts            = sets,
            .pushConstantRangeCount = std::size( pushs ),
            .pPushConstantRanges    = pushs,
        };

        VkResult r = vkCreatePipelineLayout( m_device, &info, nullptr, &m_smoothPipelineLayout );
        VK_CHECKERROR( r );

        SET_DEBUG_NAME( m_device,
                        m_smoothPipelineLayout,
                        VK_OBJECT_TYPE_PIPELINE_LAYOUT,
                        "Fluid Smoothing pipeline layout" );
    }
}

void RTGL1::Fluid::CreatePipelines( const ShaderManager& shaderManager )
{
    struct Data
    {
        uint32_t m_maxPrticleCount;
        float    m_particleRadius;
    };
    const auto tableValues = Data{
        .m_maxPrticleCount = MAX_PARTICLES,
        .m_particleRadius  = m_particleRadius,
    };
    constexpr VkSpecializationMapEntry tableSpecEntries[] = {
        { .constantID = 0,
          .offset     = offsetof( Data, m_maxPrticleCount ),
          .size       = sizeof( Data::m_maxPrticleCount ) },
        { .constantID = 1,
          .offset     = offsetof( Data, m_particleRadius ),
          .size       = sizeof( Data::m_particleRadius ) },
    };
    const auto tableSpec = VkSpecializationInfo{
        .mapEntryCount = std::size( tableSpecEntries ),
        .pMapEntries   = std::data( tableSpecEntries ),
        .dataSize      = sizeof( tableValues ),
        .pData         = &tableValues,
    };

    assert( m_particlesPipelineLayout != VK_NULL_HANDLE );
    assert( m_particlesPipeline == VK_NULL_HANDLE );
    {
        auto info = VkComputePipelineCreateInfo{
            .sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .pNext  = nullptr,
            .flags  = 0,
            .stage  = shaderManager.GetStageInfo( "Fluid_Particles" ),
            .layout = m_particlesPipelineLayout,
        };
        info.stage.pSpecializationInfo = &tableSpec;

        VkResult r = vkCreateComputePipelines(
            m_device, VK_NULL_HANDLE, 1, &info, nullptr, &m_particlesPipeline );
        VK_CHECKERROR( r );

        SET_DEBUG_NAME(
            m_device, m_particlesPipeline, VK_OBJECT_TYPE_PIPELINE, "Fluid Particles pipeline" );
    }

    assert( m_particlesPipelineLayout != VK_NULL_HANDLE );
    assert( m_generatePipeline == VK_NULL_HANDLE );
    {
        auto info = VkComputePipelineCreateInfo{
            .sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .pNext  = nullptr,
            .flags  = 0,
            .stage  = shaderManager.GetStageInfo( "Fluid_Generate" ),
            .layout = m_particlesPipelineLayout,
        };
        info.stage.pSpecializationInfo = &tableSpec;

        VkResult r = vkCreateComputePipelines(
            m_device, VK_NULL_HANDLE, 1, &info, nullptr, &m_generatePipeline );
        VK_CHECKERROR( r );

        SET_DEBUG_NAME(
            m_device, m_generatePipeline, VK_OBJECT_TYPE_PIPELINE, "Fluid Generate pipeline" );
    }

    assert( m_smoothPipelineLayout != VK_NULL_HANDLE );
    for( uint32_t iter = 0; iter < std::size( m_smoothPipelines ); iter++ )
    {
        assert( m_smoothPipelines[ iter ] == VK_NULL_HANDLE );

        constexpr auto iterEntry = VkSpecializationMapEntry{
            .constantID = 0,
            .offset     = 0,
            .size       = sizeof( iter ),
        };
        const auto iterSpec = VkSpecializationInfo{
            .mapEntryCount = 1,
            .pMapEntries   = &iterEntry,
            .dataSize      = sizeof( iter ),
            .pData         = &iter,
        };

        auto info = VkComputePipelineCreateInfo{
            .sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .pNext  = nullptr,
            .flags  = 0,
            .stage  = shaderManager.GetStageInfo( "Fluid_DepthSmooth" ),
            .layout = m_smoothPipelineLayout,
        };
        info.stage.pSpecializationInfo = &iterSpec;

        VkResult r = vkCreateComputePipelines(
            m_device, VK_NULL_HANDLE, 1, &info, nullptr, &m_smoothPipelines[ iter ] );
        VK_CHECKERROR( r );

        SET_DEBUG_NAME( m_device,
                        m_smoothPipelines[ iter ],
                        VK_OBJECT_TYPE_PIPELINE,
                        "Fluid Smoothing pipeline" );
    }

    assert( m_visualizePipelineLayout != VK_NULL_HANDLE );
    assert( m_visualizePipeline == VK_NULL_HANDLE );
    assert( m_renderPass != VK_NULL_HANDLE );
    {
        VkPipelineShaderStageCreateInfo stages[] = {
            shaderManager.GetStageInfo( "Fluid_VisualizeVert" ),
            shaderManager.GetStageInfo( "Fluid_VisualizeFrag" ),
        };
        stages[ 0 ].pSpecializationInfo = &tableSpec;

        auto vi = VkPipelineVertexInputStateCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .vertexBindingDescriptionCount   = 0,
            .pVertexBindingDescriptions      = nullptr,
            .vertexAttributeDescriptionCount = 0,
            .pVertexAttributeDescriptions    = nullptr,
        };

        auto ia = VkPipelineInputAssemblyStateCreateInfo{
            .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology               = QUAD_TOPOLOGY,
            .primitiveRestartEnable = VK_FALSE,
        };

        auto vp = VkPipelineViewportStateCreateInfo{
            .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .pViewports    = nullptr, // dynamic state
            .scissorCount  = 1,
            .pScissors     = nullptr, // dynamic state
        };

        auto rs = VkPipelineRasterizationStateCreateInfo{
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

        auto ds = VkPipelineDepthStencilStateCreateInfo{
            .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable       = VK_TRUE, // must be true, if depthWrite is true
            .depthWriteEnable      = VK_TRUE,
            .depthCompareOp        = VK_COMPARE_OP_LESS_OR_EQUAL,
            .depthBoundsTestEnable = VK_FALSE,
            .stencilTestEnable     = VK_FALSE,
            .front                 = VkStencilOpState{},
            .back                  = VkStencilOpState{},
            .minDepthBounds        = 0, // ignored
            .maxDepthBounds        = 0, // ignored
        };

        auto ms = VkPipelineMultisampleStateCreateInfo{
            .sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .pNext                 = nullptr,
            .flags                 = 0,
            .rasterizationSamples  = VK_SAMPLE_COUNT_1_BIT,
            .sampleShadingEnable   = VK_FALSE,
            .minSampleShading      = 0,
            .pSampleMask           = nullptr,
            .alphaToCoverageEnable = 0,
            .alphaToOneEnable      = 0,
        };

        auto attch = VkPipelineColorBlendAttachmentState{
            .blendEnable         = VK_FALSE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .colorBlendOp        = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .alphaBlendOp        = VK_BLEND_OP_ADD,
            .colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        };

        auto bld = VkPipelineColorBlendStateCreateInfo{
            .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .logicOpEnable   = VK_FALSE,
            .attachmentCount = 1,
            .pAttachments    = &attch,
        };

        VkDynamicState dynamicStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };

        auto dyn = VkPipelineDynamicStateCreateInfo{
            .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = std::size( dynamicStates ),
            .pDynamicStates    = dynamicStates,
        };

        auto info = VkGraphicsPipelineCreateInfo{
            .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .stageCount          = std::size( stages ),
            .pStages             = std::data( stages ),
            .pVertexInputState   = &vi,
            .pInputAssemblyState = &ia,
            .pTessellationState  = nullptr,
            .pViewportState      = &vp,
            .pRasterizationState = &rs,
            .pMultisampleState   = &ms,
            .pDepthStencilState  = &ds,
            .pColorBlendState    = &bld,
            .pDynamicState       = &dyn,
            .layout              = m_visualizePipelineLayout,
            .renderPass          = m_renderPass,
            .subpass             = 0,
            .basePipelineHandle  = VK_NULL_HANDLE,
            .basePipelineIndex   = 0,
        };

        VkResult r =
            vkCreateGraphicsPipelines( m_device, nullptr, 1, &info, nullptr, &m_visualizePipeline );
        VK_CHECKERROR( r );

        SET_DEBUG_NAME(
            m_device, m_visualizePipeline, VK_OBJECT_TYPE_PIPELINE, "Fluid Visualize pipeline" );
    }
}

void RTGL1::Fluid::CreateRenderPass()
{
    const auto attch = std::array{
        VkAttachmentDescription{
            .flags          = 0,
            .format         = ShFramebuffers_Formats[ FB_IMAGE_INDEX_FLUID_NORMAL ],
            .samples        = VK_SAMPLE_COUNT_1_BIT,
            .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout  = VK_IMAGE_LAYOUT_GENERAL,
            .finalLayout    = VK_IMAGE_LAYOUT_GENERAL,
        },
        VkAttachmentDescription{
            .flags          = 0,
            .format         = RASTER_PASS_DEPTH_FORMAT,
            .samples        = VK_SAMPLE_COUNT_1_BIT,
            .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout  = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        },
    };

    auto normalRef = VkAttachmentReference{
        .attachment = 0,
        .layout     = VK_IMAGE_LAYOUT_GENERAL,
    };

    auto depthRef = VkAttachmentReference{
        .attachment = 1,
        .layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };

    auto subpass = VkSubpassDescription{
        .flags                   = 0,
        .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .inputAttachmentCount    = 0,
        .pInputAttachments       = nullptr,
        .colorAttachmentCount    = 1,
        .pColorAttachments       = &normalRef,
        .pResolveAttachments     = nullptr,
        .pDepthStencilAttachment = &depthRef,
        .preserveAttachmentCount = 0,
        .pPreserveAttachments    = nullptr,
    };

    // after DrawToFinalImage
    auto dep = VkSubpassDependency{
        .srcSubpass      = VK_SUBPASS_EXTERNAL,
        .dstSubpass      = 0,
        .srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dependencyFlags = 0,
    };

    auto info = VkRenderPassCreateInfo{
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .pNext           = nullptr,
        .flags           = 0,
        .attachmentCount = std::size( attch ),
        .pAttachments    = std::data( attch ),
        .subpassCount    = 1,
        .pSubpasses      = &subpass,
        .dependencyCount = 1,
        .pDependencies   = &dep,
    };

    assert( m_renderPass == VK_NULL_HANDLE );

    VkResult r = vkCreateRenderPass( m_device, &info, nullptr, &m_renderPass );
    VK_CHECKERROR( r );
}

void RTGL1::Fluid::CreateFramebuffers( uint32_t width, uint32_t height )
{
    constexpr auto depthSubres = VkImageSubresourceRange{
        .aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT,
        .baseMipLevel   = 0,
        .levelCount     = 1,
        .baseArrayLayer = 0,
        .layerCount     = 1,
    };

    {
        VkCommandBuffer cmd = m_cmdManager->StartGraphicsCmd();

        assert( !m_depth.image && !m_depth.view );
        assert( m_storageFramebuffer->GetImageView( FB_IMAGE_INDEX_DEPTH_FLUID, 0 ) ==
                m_storageFramebuffer->GetImageView( FB_IMAGE_INDEX_DEPTH_FLUID, 1 ) );

        auto [ format, mem ] =
            m_storageFramebuffer->GetImageForAlias( FB_IMAGE_INDEX_DEPTH_FLUID, //
                                                    0 );

        // assuming that width, height match!
        {
            const auto info = VkImageCreateInfo{
                .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                .imageType     = VK_IMAGE_TYPE_2D,
                .format        = RASTER_PASS_DEPTH_FORMAT,
                .extent        = VkExtent3D{ .width = width, .height = height, .depth = 1 },
                .mipLevels     = 1,
                .arrayLayers   = 1,
                .samples       = VK_SAMPLE_COUNT_1_BIT,
                .tiling        = VK_IMAGE_TILING_OPTIMAL,
                .usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            };

            VkResult r = vkCreateImage( m_device, &info, nullptr, &m_depth.image );
            VK_CHECKERROR( r );

            SET_DEBUG_NAME( m_device,
                            m_depth.image,
                            VK_OBJECT_TYPE_IMAGE,
                            "DepthFluid - Aliased image for raster pass" );
        }
        // alias already allocated float32 memory
        {
            assert( format == VK_FORMAT_R32_SFLOAT &&
                    RASTER_PASS_DEPTH_FORMAT == VK_FORMAT_D32_SFLOAT );

            VkResult r = vkBindImageMemory( m_device, m_depth.image, mem, 0 );
            VK_CHECKERROR( r );
        }
        {
            const auto viewInfo = VkImageViewCreateInfo{
                .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image            = m_depth.image,
                .viewType         = VK_IMAGE_VIEW_TYPE_2D,
                .format           = RASTER_PASS_DEPTH_FORMAT,
                .subresourceRange = depthSubres,
            };

            VkResult r = vkCreateImageView( m_device, &viewInfo, nullptr, &m_depth.view );
            VK_CHECKERROR( r );

            SET_DEBUG_NAME( m_device,
                            m_depth.view,
                            VK_OBJECT_TYPE_IMAGE_VIEW,
                            "DepthFluid - Aliased view for raster pass" );
        }

        // to general layout
        Utils::BarrierImage( cmd,
                             m_depth.image,
                             0,
                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                 VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
                             VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                             depthSubres );

        m_cmdManager->Submit( cmd );
        m_cmdManager->WaitGraphicsIdle();
    }

    {
        assert( m_passFramebuffer == VK_NULL_HANDLE );
        assert( m_renderPass != VK_NULL_HANDLE );
        assert( m_storageFramebuffer->GetImageView( FB_IMAGE_INDEX_FLUID_NORMAL, 0 ) ==
                m_storageFramebuffer->GetImageView( FB_IMAGE_INDEX_FLUID_NORMAL, 1 ) );

        VkImageView vs[] = {
            m_storageFramebuffer->GetImageView( FB_IMAGE_INDEX_FLUID_NORMAL, 0 ),
            m_depth.view,
        };

        VkFramebufferCreateInfo info = {
            .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass      = m_renderPass,
            .attachmentCount = std::size( vs ),
            .pAttachments    = vs,
            .width           = width,
            .height          = height,
            .layers          = 1,
        };

        VkResult r = vkCreateFramebuffer( m_device, &info, nullptr, &m_passFramebuffer );
        VK_CHECKERROR( r );
    }
}

void RTGL1::Fluid::DestroyFramebuffers()
{
    if( m_depth.view != VK_NULL_HANDLE )
    {
        vkDestroyImageView( m_device, m_depth.view, nullptr );
        vkDestroyImage( m_device, m_depth.image, nullptr );
        m_depth.view  = VK_NULL_HANDLE;
        m_depth.image = VK_NULL_HANDLE;
    }

    if( m_passFramebuffer != VK_NULL_HANDLE )
    {
        vkDestroyFramebuffer( m_device, m_passFramebuffer, nullptr );
        m_passFramebuffer = VK_NULL_HANDLE;
    }
}

void RTGL1::Fluid::DestroyPipelines()
{
    vkDestroyPipeline( m_device, m_particlesPipeline, nullptr );
    m_particlesPipeline = VK_NULL_HANDLE;
    vkDestroyPipeline( m_device, m_generatePipeline, nullptr );
    m_generatePipeline = VK_NULL_HANDLE;
    vkDestroyPipeline( m_device, m_visualizePipeline, nullptr );
    m_visualizePipeline = VK_NULL_HANDLE;
    for( auto& p : m_smoothPipelines )
    {
        vkDestroyPipeline( m_device, p, nullptr );
        p = VK_NULL_HANDLE;
    }
}
