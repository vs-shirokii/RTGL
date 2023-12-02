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

#include "ASManager.h"
#include "Camera.h"
#include "GltfExporter.h"
#include "GltfImporter.h"
#include "LightManager.h"
#include "VertexPreprocessing.h"
#include "TextureMeta.h"
#include "UniqueID.h"

namespace RTGL1
{

enum class UploadResult
{
    Fail,
    Static,
    Dynamic,
    ExportableDynamic,
    ExportableStatic,
};

inline auto MakeCameraPosition( const Camera& c )
{
    return RgFloat3D{
        c.viewInverse[ 12 ],
        c.viewInverse[ 13 ],
        c.viewInverse[ 14 ],
    };
}

class Scene
{
public:
    explicit Scene( VkDevice                                device,
                    const PhysicalDevice&                   physDevice,
                    std::shared_ptr< MemoryAllocator >&     allocator,
                    std::shared_ptr< CommandBufferManager > cmdManager,
                    const GlobalUniform&                    uniform,
                    const ShaderManager&                    shaderManager,
                    uint64_t                                maxReplacementsVerts,
                    uint64_t                                maxDynamicVerts,
                    bool                                    enableTexCoordLayer1,
                    bool                                    enableTexCoordLayer2,
                    bool                                    enableTexCoordLayer3 );
    ~Scene() = default;

    Scene( const Scene& other )                = delete;
    Scene( Scene&& other ) noexcept            = delete;
    Scene& operator=( const Scene& other )     = delete;
    Scene& operator=( Scene&& other ) noexcept = delete;

    void PrepareForFrame( VkCommandBuffer cmd,
                          uint32_t        frameIndex,
                          bool            ignoreExternalGeometry,
                          float           staticSceneAnimationTime );
    void SubmitForFrame( VkCommandBuffer                         cmd,
                         uint32_t                                frameIndex,
                         const std::shared_ptr< GlobalUniform >& uniform,
                         uint32_t                                uniformData_rayCullMaskWorld,
                         bool                                    disableRTGeometry );

    UploadResult UploadPrimitive( uint32_t                   frameIndex,
                                  const RgMeshInfo&          mesh,
                                  const RgMeshPrimitiveInfo& primitive,
                                  const TextureManager&      textureManager,
                                  LightManager&              lightManager,
                                  bool                       isStatic );

    UploadResult UploadLight( uint32_t           frameIndex,
                              const LightCopy&   light,
                              LightManager&      lightManager,
                              bool               isStatic,
                              const RgTransform* transform = nullptr );

    void SubmitStaticLights( uint32_t          frameIndex,
                             LightManager&     lightManager,
                             bool              isUnderwater,
                             RgColor4DPacked32 underwaterColor ) const;

    void NewScene( VkCommandBuffer              cmd,
                   uint32_t                     frameIndex,
                   const ImportExportParams&    params,
                   const std::filesystem::path& staticSceneGltfPath,
                   const std::filesystem::path* replacementsFolder,
                   TextureManager&              textureManager,
                   const TextureMetaManager&    textureMeta,
                   LightManager&                lightManager );

    const std::shared_ptr< ASManager >&           GetASManager();
    const std::shared_ptr< VertexPreprocessing >& GetVertexPreprocessing();

    auto TryGetVolumetricLight( const LightManager& lightManager, const RgFloat3D& cameraPos ) const
        -> std::optional< uint64_t >;

    bool ReplacementExists( const RgMeshInfo& mesh ) const;

    void          AddDefaultCamera( const RgCameraInfo& info );
    const Camera& GetCamera( float fallbackAspect );

    [[nodiscard]] bool StaticSceneExists() const { return !staticMeshNames.empty(); }

private:
    [[nodiscard]] bool StaticMeshExists( const RgMeshInfo& mesh ) const;
    [[nodiscard]] bool StaticLightExists( const LightCopy& light ) const;

    bool InsertPrimitiveInfo( const PrimitiveUniqueID&   uniqueID,
                              bool                       isStatic,
                              const RgMeshInfo&          mesh,
                              const RgMeshPrimitiveInfo& primitive );

    bool InsertLightInfo( bool isStatic, const LightCopy& light );

private:
    std::shared_ptr< ASManager >           asManager;
    std::shared_ptr< GeomInfoManager >     geomInfoMgr;
    std::shared_ptr< VertexPreprocessing > vertPreproc;

    // Dynamic indices are cleared every frame
    rgl::unordered_set< PrimitiveUniqueID > dynamicUniqueIDs;
    rgl::unordered_set< uint64_t >          alreadyReplacedUniqueObjectIDs;

    rgl::unordered_set< PrimitiveUniqueID > staticUniqueIDs;
    rgl::string_set                         staticMeshNames;
    std::vector< LightCopy >                staticLights;
    std::optional< uint64_t >               lastDynamicSun_uniqueId{};

    std::optional< Camera >       curFrameCamera{};
    std::optional< RgCameraInfo > cameraInfo_Default{};
    std::optional< RgCameraInfo > cameraInfo_Imported{};

    rgl::string_map< WholeModelFile::RawModelData > replacements;

    StaticGeometryToken  makingStatic{};
    DynamicGeometryToken makingDynamic{};

    bool ignoreExternalGeometry{};

    std::vector< std::tuple< PrimitiveUniqueID, RgTransform, AnimationData > > m_obj_ImportedAnim{};
    AnimationData m_cameraInfo_ImportedAnim{};
    float         m_staticSceneAnimationTime{ 0 };

public:
    // SHIPPING_HACK begin
    rgl::string_map< std::vector< PrimitiveUniqueID > > m_primitivesToUpdateTextures{};
    // SHIPPING_HACK end
};


class SceneImportExport : public IFileDependency
{
public:
    SceneImportExport( std::filesystem::path       _scenesFolder,
                       std::filesystem::path       _replacementsFolder,
                       const RgInstanceCreateInfo& _info );
    ~SceneImportExport() override = default;

    SceneImportExport( const SceneImportExport& other )                = delete;
    SceneImportExport( SceneImportExport&& other ) noexcept            = delete;
    SceneImportExport& operator=( const SceneImportExport& other )     = delete;
    SceneImportExport& operator=( SceneImportExport&& other ) noexcept = delete;

    void PrepareForFrame( std::string_view mapName, bool allowSceneAutoExport );
    void TryImportIfNew( VkCommandBuffer           cmd,
                         uint32_t                  frameIndex,
                         Scene&                    scene,
                         TextureManager&           textureManager,
                         TextureMetaManager&       textureMetaManager,
                         LightManager&             lightManager,
                         RgStaticSceneStatusFlags* out_staticSceneStatus );
    void TryExport( const TextureManager& textureManager, const std::filesystem::path& ovrdFolder );

    void RequestReimport();
    void RequestReplacementsReimport();
    void OnFileChanged( FileType type, const std::filesystem::path& filepath ) override;

    void          RequestExport();
    void          RequestReplacementsExport_OneFrame();
    void          RequestReplacementsExport_RecordBegin();
    void          RequestReplacementsExport_RecordEnd();
    GltfExporter* TryGetExporter( bool isReplacement );

    std::string_view GetImportMapName() const;
    std::string_view GetExportMapName() const;

    const RgFloat3D& GetWorldUp() const;
    const RgFloat3D& GetWorldForward() const;
    RgFloat3D        GetWorldRight() const;
    float            GetWorldScale() const;

    auto MakeWorldTransform() const -> RgTransform;
    auto MakeImportExportParams() const -> ImportExportParams;

private:
    std::filesystem::path scenesFolder;
    std::filesystem::path replacementsFolder;

    bool reimportStatic;
    bool reimportReplacements;

    bool reimportStaticInNextFrame;

    enum class ExportState
    {
        None,
        OneFrame,
        Recording,
        FinilizeIntoFile,
    };

    bool                            exportRequested{ false };
    ExportState                     exportReplacementsRequest{ ExportState::None };
    std::unique_ptr< GltfExporter > sceneExporter{};
    std::unique_ptr< GltfExporter > replacementsExporter{};

    std::string currentMap{};
    RgFloat3D   worldUp;
    RgFloat3D   worldForward;
    float       worldScale;
    float       importedLightIntensityScaleDirectional;
    float       importedLightIntensityScaleSphere;
    float       importedLightIntensityScaleSpot;

public:
    mutable struct
    {
        struct DevField
        {
            bool enable{ false };
            char value[ 128 ]{ "" };

            void SetDefaults( const SceneImportExport& s )
            {
                std::snprintf( value, std::size( value ), "%s", s.currentMap.c_str() );
                value[ std::size( value ) - 1 ] = '\0';
            }
        };

        DevField importName;
        DevField exportName;

        struct
        {
            bool      enable{ false };
            RgFloat3D up{};
            RgFloat3D forward{};
            float     scale{};

            void SetDefaults( const SceneImportExport& s )
            {
                up      = s.worldUp;
                forward = s.worldForward;
                scale   = s.worldScale;
            }
        } worldTransform;

        bool buttonRecording{ false };
    } dev;

    auto dev_GetSceneImportGltfPath() -> std::string;
    auto dev_GetSceneExportGltfPath() -> std::string;
};

}