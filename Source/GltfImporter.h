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

#include "Const.h"
#include "Common.h"
#include "Containers.h"
#include "DrawFrameInfo.h"
#include "SamplerManager.h"

#include <filesystem>
#include <ranges>
#include <span>

struct cgltf_node;
struct cgltf_data;
struct cgltf_material;

namespace RTGL1
{

class Scene;
class TextureManager;
class TextureMetaManager;
class LightManager;

enum AnimationInterpolation
{
    ANIMATION_INTERPOLATION_LINEAR,
    ANIMATION_INTERPOLATION_STEP,
    ANIMATION_INTERPOLATION_CUBIC,
};

template< typename T >
struct AnimationFrame
{
    T                      value;
    float                  seconds;
    AnimationInterpolation interpolation;
};

template< typename T >
struct AnimationChannel
{
    std::vector< AnimationFrame< T > > frames{};
};

struct AnimationData
{
    AnimationChannel< RgFloat3D >    position{};
    AnimationChannel< RgQuaternion > quaternion{};
    AnimationChannel< float >        fovYRadians{};
};

inline bool IsAnimDataEmpty( const AnimationData & a )
{
    return a.position.frames.empty() && a.quaternion.frames.empty() && a.fovYRadians.frames.empty();
}

struct WholeModelFile
{
    const inline static auto DefaultSampler = SamplerManager::Handle{
        RG_SAMPLER_FILTER_AUTO,
        RG_SAMPLER_ADDRESS_MODE_REPEAT,
        RG_SAMPLER_ADDRESS_MODE_REPEAT,
    };

    struct RawMaterialData
    {
        bool               isReplacement{ false };
        RgTextureSwizzling pbrSwizzling{ RG_TEXTURE_SWIZZLING_OCCLUSION_ROUGHNESS_METALLIC };
        std::string        pTextureName{};
        std::array< std::filesystem::path, TEXTURES_PER_MATERIAL_COUNT >  fullPaths{};
        std::array< SamplerManager::Handle, TEXTURES_PER_MATERIAL_COUNT > samplers{
            DefaultSampler, DefaultSampler, DefaultSampler, DefaultSampler, DefaultSampler
        };
        bool trackOriginalTexture{ false };
    };

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

    struct RawModelData
    {
        uint64_t                        uniqueObjectID{ 0 };
        RgTransform                     meshTransform{};
        std::vector< RawPrimitiveData > primitives{};
        std::vector< LightCopy >        localLights{};
        AnimationData                   animobj{};
    };

    rgl::string_map< RawModelData > models{};
    std::vector< LightCopy >        lights{};
    std::optional< RgCameraInfo >   camera{};
    AnimationData                   animcamera{};
    std::vector< RawMaterialData >  materials{};
};


class GltfImporter
{
public:
    GltfImporter( const std::filesystem::path& gltfPath,
                  const ImportExportParams&    params,
                  const TextureMetaManager&    textureMeta,
                  bool                         isReplacement );
    ~GltfImporter() = default;

    GltfImporter( const GltfImporter& )                = delete;
    GltfImporter( GltfImporter&& ) noexcept            = delete;
    GltfImporter& operator=( const GltfImporter& )     = delete;
    GltfImporter& operator=( GltfImporter&& ) noexcept = delete;

    [[nodiscard]] auto FilePath() const { return std::string_view{ gltfPath }; }

    explicit operator bool() const { return isParsed; }

    WholeModelFile&& Move()
    {
        assert( isParsed );
        return std::move( parsedModel );
    }

private:
    void ParseFile( cgltf_data* data, bool isReplacement, const TextureMetaManager& textureMeta );

private:
    std::string           gltfPath;
    std::filesystem::path gltfFolder;
    ImportExportParams    params;
    WholeModelFile        parsedModel;
    bool                  isParsed;
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
inline auto MakeMeshPrimitiveInfoAndProcess( const WholeModelFile::RawPrimitiveData& primitive,
                                             uint32_t                                index,
                                             Func&& funcToProcessPrimitive )
    -> std::invoke_result_t< Func, const RgMeshPrimitiveInfo& >
{
    auto dstPrim = RgMeshPrimitiveInfo{
        .sType                = RG_STRUCTURE_TYPE_MESH_PRIMITIVE_INFO,
        .pNext                = nullptr,
        .flags                = primitive.flags,
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

    return funcToProcessPrimitive( dstPrim );
}

}
