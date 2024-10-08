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

#pragma once

#include "ASBuilder.h"
#include "CommandBufferManager.h"
#include "GlobalUniform.h"
#include "ScratchBuffer.h"
#include "TextureManager.h"
#include "VertexCollector.h"
#include "ASComponent.h"
#include "Token.h"
#include "UniqueID.h"

namespace RTGL1
{

class ASManager
{
public:
    ASManager( VkDevice                                device,
               const PhysicalDevice&                   physDevice,
               std::shared_ptr< MemoryAllocator >      allocator,
               std::shared_ptr< CommandBufferManager > cmdManager,
               std::shared_ptr< GeomInfoManager >      geomInfoManager,
               uint64_t                                maxReplacementsVerts,
               uint64_t                                maxDynamicVerts,
               bool                                    enableTexCoordLayer1,
               bool                                    enableTexCoordLayer2,
               bool                                    enableTexCoordLayer3 );
    ~ASManager();

    ASManager( const ASManager& other )                = delete;
    ASManager( ASManager&& other ) noexcept            = delete;
    ASManager& operator=( const ASManager& other )     = delete;
    ASManager& operator=( ASManager&& other ) noexcept = delete;


    [[nodiscard]] StaticGeometryToken BeginStaticGeometry( bool freeReplacements );
    void                              MarkReplacementsRegionEnd( const StaticGeometryToken& token );
    // Submitting static geometry to the building is a heavy operation
    // with waiting for it to complete.
    void SubmitStaticGeometry( StaticGeometryToken& token, bool buildReplacements );


    [[nodiscard]] DynamicGeometryToken BeginDynamicGeometry( VkCommandBuffer cmd,
                                                             uint32_t        frameIndex );
    void                               SubmitDynamicGeometry( DynamicGeometryToken& token,
                                                              VkCommandBuffer       cmd,
                                                              uint32_t              frameIndex );


    bool AddMeshPrimitive( uint32_t                   frameIndex,
                           const RgMeshInfo&          mesh,
                           const RgMeshPrimitiveInfo& primitive,
                           const PrimitiveUniqueID&   uniqueID,
                           const bool                 isStatic,
                           const bool                 isReplacement,
                           const TextureManager&      textureManager,
                           GeomInfoManager&           geomInfoManager );

    void Hack_PatchTexturesForStaticPrimitive( const PrimitiveUniqueID& uniqueID,
                                               const char*              pTextureName,
                                               const TextureManager&    textureManager );
    void Hack_PatchGeomInfoTransformForStatic( const PrimitiveUniqueID& geomUniqueID,
                                               const RgTransform&       transform );

    void CacheReplacement( std::string_view           meshName,
                           const RgMeshPrimitiveInfo& primitive,
                           uint32_t                   index );


    auto MakeUniqueIDToTlasID( bool disableRTGeometry ) const -> UniqueIDToTlasID;
    void BuildTLAS( VkCommandBuffer cmd,
                    uint32_t        frameIndex,
                    uint32_t        uniformData_rayCullMaskWorld,
                    bool            disableRTGeometry );


    // Copy current dynamic vertex and index data to
    // special buffers for using current frame's data in the next frame.
    void CopyDynamicDataToPrevBuffers( VkCommandBuffer cmd, uint32_t frameIndex );


    void OnVertexPreprocessingBegin( VkCommandBuffer cmd, uint32_t frameIndex, bool onlyDynamic );
    void OnVertexPreprocessingFinish( VkCommandBuffer cmd, uint32_t frameIndex, bool onlyDynamic );


    VkDescriptorSet GetBuffersDescSet( uint32_t frameIndex ) const;
    VkDescriptorSet GetTLASDescSet( uint32_t frameIndex ) const;

    VkDescriptorSetLayout GetBuffersDescSetLayout() const;
    VkDescriptorSetLayout GetTLASDescSetLayout() const;

private:
    void CreateDescriptors();
    void UpdateBufferDescriptors( uint32_t frameIndex );
    void UpdateASDescriptors( uint32_t frameIndex );

    struct BuiltAS
    {
        VertexCollectorFilterTypeFlags flags;
        BLASComponent                  blas;
        VertexCollector::UploadResult  geometry;
    };

    auto UploadAndBuildAS( const RgMeshPrimitiveInfo&     primitive,
                           VertexCollectorFilterTypeFlags geomFlags,
                           VertexCollector&               vertexAlloc,
                           ChunkedStackAllocator&         accelStructAlloc,
                           const bool                     isDynamic ) -> std::unique_ptr< BuiltAS >;

    static auto MakeVkTLAS( const BuiltAS&                 builtAS,
                            uint32_t                       rayCullMaskWorld,
                            const RgTransform&             instanceTransform,
                            VertexCollectorFilterTypeFlags instanceFlags )
        -> std::optional< VkAccelerationStructureInstanceKHR >;

private:
    VkDevice                           device;
    std::shared_ptr< MemoryAllocator > allocator;

    VkFence staticCopyFence;

    // for filling buffers
    std::unique_ptr< VertexCollector > collectorStatic;
    std::unique_ptr< VertexCollector > collectorDynamic[ MAX_FRAMES_IN_FLIGHT ];
    // device-local buffer for storing previous info
    Buffer                             previousDynamicPositions;
    Buffer                             previousDynamicIndices;
    VertexCollector::CopyRanges        collectorStatic_replacements{};

    // building
    std::shared_ptr< ChunkedStackAllocator > scratchBuffer;
    std::unique_ptr< ASBuilder >             asBuilder;

    std::shared_ptr< CommandBufferManager > cmdManager;
    std::shared_ptr< TextureManager >       textureMgr;
    std::shared_ptr< GeomInfoManager >      geomInfoMgr;

    std::unique_ptr< ChunkedStackAllocator > allocTlas[ MAX_FRAMES_IN_FLIGHT ];
    std::unique_ptr< ChunkedStackAllocator > allocReplacementsGeom;
    std::unique_ptr< ChunkedStackAllocator > allocStaticGeom;
    std::unique_ptr< ChunkedStackAllocator > allocDynamicGeom[ MAX_FRAMES_IN_FLIGHT ];

    rgl::string_map< std::vector< std::unique_ptr< BuiltAS > > > builtReplacements;
    std::vector< std::unique_ptr< BuiltAS > >                    builtStaticInstances;
    std::vector< std::unique_ptr< BuiltAS > > builtDynamicInstances[ MAX_FRAMES_IN_FLIGHT ];

    // Exists only in the current frame
    struct Object
    {
        // should be weak_ptr
        BuiltAS*                       builtInstance;
        bool                           isStatic;
        // per instance params
        PrimitiveUniqueID              uniqueID;
        RgTransform                    transform;
        VertexCollectorFilterTypeFlags instanceFlags;
    };
    std::vector< Object > curFrame_objects;

    // top level AS
    std::unique_ptr< AutoBuffer >    instanceBuffer;
    std::unique_ptr< TLASComponent > tlas[ MAX_FRAMES_IN_FLIGHT ];

    // TLAS and buffer descriptors
    VkDescriptorPool descPool;

    VkDescriptorSetLayout buffersDescSetLayout;
    VkDescriptorSet       buffersDescSets[ MAX_FRAMES_IN_FLIGHT ];

    VkDescriptorSetLayout asDescSetLayout;
    VkDescriptorSet       asDescSets[ MAX_FRAMES_IN_FLIGHT ];
};

}
