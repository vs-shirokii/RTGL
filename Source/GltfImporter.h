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

#include "Common.h"
#include "Containers.h"
#include "DrawFrameInfo.h"

#include <filesystem>

struct cgltf_node;
struct cgltf_data;
struct cgltf_material;

namespace RTGL1
{

class Scene;
class TextureManager;
class TextureMetaManager;
class LightManager;

struct WholeModelFile
{
    struct RawModelData
    {
        struct RawPrimitiveData
        {
            std::vector< RgPrimitiveVertex >                 vertices;
            std::vector< uint32_t >                          indices;
            RgMeshPrimitiveFlags                             flags;
            std::string                                      textureName;
            RgColor4DPacked32                                color;
            float                                            emissive;
            std::optional< RgMeshPrimitiveAttachedLightEXT > attachedLight;
            std::optional< RgMeshPrimitivePBREXT >           pbr;
            std::optional< RgMeshPrimitivePortalEXT >        portal;
        };

        uint64_t                        uniqueObjectID{ 0 };
        RgTransform                     meshTransform{};
        std::vector< RawPrimitiveData > primitives{};
        std::vector< LightCopy >        localLights{};
    };

    rgl::string_map< RawModelData > models{};
    std::vector< LightCopy >        lights{};
};


class GltfImporter
{
public:
    GltfImporter( const std::filesystem::path& gltfPath,
                  const RgTransform&           worldTransform,
                  float                        oneGameUnitInMeters );
    ~GltfImporter();

    GltfImporter( const GltfImporter& other )                = delete;
    GltfImporter( GltfImporter&& other ) noexcept            = delete;
    GltfImporter& operator=( const GltfImporter& other )     = delete;
    GltfImporter& operator=( GltfImporter&& other ) noexcept = delete;

    [[nodiscard]] auto ParseFile( VkCommandBuffer           cmd,
                                  uint32_t                  frameIndex,
                                  TextureManager&           textureManager,
                                  const TextureMetaManager& textureMeta ) const -> WholeModelFile;

    explicit operator bool() const;

    [[nodiscard]] auto FilePath() const { return std::string_view{ gltfPath }; }

private:
    cgltf_data*           data;
    std::string           gltfPath;
    std::filesystem::path gltfFolder;
    float                 oneGameUnitInMeters;
};

}


namespace RTGL1
{

inline auto MakeMeshInfoFrom( const char* name, const WholeModelFile::RawModelData& model )
    -> RgMeshInfo
{
    return RgMeshInfo{
        .sType          = RG_STRUCTURE_TYPE_MESH_INFO,
        .pNext          = nullptr,
        .flags          = 0,
        .uniqueObjectID = model.uniqueObjectID,
        .pMeshName      = name,
        .transform      = model.meshTransform,
        .isExportable   = false,
        .animationTime  = 0,
    };
}

template< typename Func >
inline auto MakeMeshPrimitiveInfoAndProcess(
    const WholeModelFile::RawModelData::RawPrimitiveData& primitive,
    uint32_t                                              index,
    Func&&                                                funcToProcessPrimitive )

{
    auto dstPrim = RgMeshPrimitiveInfo{
        .sType                = RG_STRUCTURE_TYPE_MESH_PRIMITIVE_INFO,
        .pNext                = nullptr,
        .flags                = primitive.flags,
        .pPrimitiveNameInMesh = nullptr,
        .primitiveIndexInMesh = index,
        .pVertices            = primitive.vertices.data(),
        .vertexCount          = static_cast< uint32_t >( primitive.vertices.size() ),
        .pIndices             = primitive.indices.data(),
        .indexCount           = static_cast< uint32_t >( primitive.indices.size() ),
        .pTextureName         = primitive.textureName.c_str(),
        .textureFrame         = 0,
        .color                = primitive.color,
        .emissive             = primitive.emissive,
    };

    auto tryLink =
        []< typename T >( RgMeshPrimitiveInfo& base, const std::optional< T >& src, T& dst ) {
            if( src )
            {
                dst = *src;
                assert( dst.sType == detail::TypeToStructureType< T > );

                dst.pNext  = base.pNext;
                base.pNext = &dst;
            }
        };

    auto dstAttachedLight = RgMeshPrimitiveAttachedLightEXT{};
    auto dstPbr           = RgMeshPrimitivePBREXT{};
    auto dstPortal        = RgMeshPrimitivePortalEXT{};

    tryLink( dstPrim, primitive.attachedLight, dstAttachedLight );
    tryLink( dstPrim, primitive.pbr, dstPbr );
    tryLink( dstPrim, primitive.portal, dstPortal );

    funcToProcessPrimitive( dstPrim );
}

}
