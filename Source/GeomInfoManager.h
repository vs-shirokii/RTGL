// Copyright (c) 2021-2022 Sultim Tsyrendashiev
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
#include "Common.h"
#include "Containers.h"
#include "Material.h"
#include "MemoryAllocator.h"
#include "SpanCounted.h"
#include "Utils.h"
#include "VertexCollectorFilterType.h"
#include "UniqueID.h"

#include "Generated/ShaderCommonC.h"

#include <vector>

namespace RTGL1
{

// LocalGeomIndex -- geometry index in its filter's space
// GlobalGeomIndex = ToOffset(geomType) * MAX_BLAS_GEOMS + geomLocalIndex
class GeomInfoManager
{
public:
    explicit GeomInfoManager( VkDevice device, std::shared_ptr< MemoryAllocator >& allocator );
    ~GeomInfoManager() = default;

    GeomInfoManager( const GeomInfoManager& other )                = delete;
    GeomInfoManager( GeomInfoManager&& other ) noexcept            = delete;
    GeomInfoManager& operator=( const GeomInfoManager& other )     = delete;
    GeomInfoManager& operator=( GeomInfoManager&& other ) noexcept = delete;


    void PrepareForFrame( uint32_t frameIndex );
    void ResetOnlyStatic();


    // Save instance for copying into buffer and fill previous frame's data.
    // For dynamic geometry it should be called every frame,
    // and for static geometry -- only when whole static scene was changed.
    void WriteGeomInfo( uint32_t                 frameIndex,
                        const PrimitiveUniqueID& geomUniqueID,
                        ShGeometryInstance&      src,
                        bool                     isStatic );


    bool CopyFromStaging( VkCommandBuffer cmd, uint32_t frameIndex, TlasIDToUniqueID&& tlas );


    VkBuffer GetBuffer() const;
    VkBuffer GetMatchPrevBuffer() const;


    uint32_t GetCount( uint32_t frameIndex ) const;


    static bool     LayerExists( const RgMeshPrimitiveInfo& info, uint32_t layerIndex );
    static uint32_t GetPrimitiveFlags( const RgMeshPrimitiveInfo& info );

    static const RgFloat2D* AccessLayerTexCoords( const RgMeshPrimitiveInfo& info,
                                                  uint32_t                   layerIndex );

    using MatchPrevIndexType = int32_t;

private:
    struct PrevInfo
    {
        float    model[ 16 ];
        uint32_t baseVertexIndex;
        uint32_t baseIndexIndex;
        uint32_t vertexCount;
        uint32_t indexCount;
    };

private:
    auto FindPrevFrameData( const PrimitiveUniqueID&  geomUniqueID,
                            const ShGeometryInstance& target,
                            uint32_t                  frameIndex ) const -> const PrevInfo*;

    void WritePrevForNextFrame( const PrimitiveUniqueID&  geomUniqueID,
                                const ShGeometryInstance& src,
                                uint32_t                  frameIndex );

private:
    VkDevice device;

    // buffer for getting info for geometry in BLAS
    std::shared_ptr< AutoBuffer > buffer;

    std::shared_ptr< AutoBuffer >           matchPrev;
    // special CPU side buffer to reduce granular writes to staging
    std::unique_ptr< MatchPrevIndexType[] > matchPrevShadow;

    // geometry's uniqueID to geom frame info,
    // used for getting info from previous frame
    rgl::unordered_map< PrimitiveUniqueID, PrevInfo > idToPrevInfo[ MAX_FRAMES_IN_FLIGHT ];

    rgl::unordered_set< PrimitiveUniqueID > staticUniqueIds;
    rgl::unordered_set< PrimitiveUniqueID > dynamicUniqueIds[ MAX_FRAMES_IN_FLIGHT ];

    rgl::unordered_map< PrimitiveUniqueID, ShGeometryInstance > curFrame_IdToInfo;

    TlasIDToUniqueID tlas_prev;
};

inline RgColor4DPacked32 PackEmissiveFactorAndStrength( RgColor4DPacked32 factor, float strength )
{
    auto strengthClamped = static_cast< uint8_t >( std::clamp( strength, 0.0f, 255.0f ) );
    auto emisRGB         = Utils::UnpackColor4DPacked32Components( factor );

    return Utils::PackColor( emisRGB[ 0 ], emisRGB[ 1 ], emisRGB[ 2 ], strengthClamped );
}

}
