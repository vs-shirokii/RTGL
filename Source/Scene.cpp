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

#include "Scene.h"

#include "CmdLabel.h"
#include "GeomInfoManager.h"
#include "GltfImporter.h"
#include "RgException.h"
#include "UniqueID.h"

#include "Generated/ShaderCommonC.h"

namespace
{
template< bool IsForScene = true >
auto MakeGltfPath( const std::filesystem::path& base, std::string_view meshName )
    -> std::filesystem::path
{
    auto exportName = std::string( meshName );

    std::ranges::replace( exportName, '\\', '_' );
    std::ranges::replace( exportName, '/', '_' );

    if( IsForScene )
    {
        return base / exportName / ( exportName + ".gltf" );
    }
    else
    {
        return base / ( exportName + ".gltf" );
    }
}

auto SanitizePathToShow( const std::filesystem::path& p )
{
    auto s = p.string();
    std::ranges::replace( s, '\\', '/' );
    return s;
}
}

RTGL1::Scene::Scene( VkDevice                                _device,
                     const PhysicalDevice&                   _physDevice,
                     std::shared_ptr< MemoryAllocator >&     _allocator,
                     std::shared_ptr< CommandBufferManager > _cmdManager,
                     const GlobalUniform&                    _uniform,
                     const ShaderManager&                    _shaderManager,
                     bool                                    _enableTexCoordLayer1,
                     bool                                    _enableTexCoordLayer2,
                     bool                                    _enableTexCoordLayer3 )
{
    geomInfoMgr = std::make_shared< GeomInfoManager >( _device, _allocator );

    asManager = std::make_shared< ASManager >( _device,
                                               _physDevice,
                                               _allocator,
                                               std::move( _cmdManager ),
                                               geomInfoMgr,
                                               _enableTexCoordLayer1,
                                               _enableTexCoordLayer2,
                                               _enableTexCoordLayer3 );

    vertPreproc =
        std::make_shared< VertexPreprocessing >( _device, _uniform, *asManager, _shaderManager );
}

void RTGL1::Scene::PrepareForFrame( VkCommandBuffer cmd,
                                    uint32_t        frameIndex,
                                    bool            _ignoreExternalGeometry )
{
    assert( !makingDynamic );
    assert( !makingStatic );
    this->ignoreExternalGeometry = _ignoreExternalGeometry;

    geomInfoMgr->PrepareForFrame( frameIndex );

    makingDynamic = asManager->BeginDynamicGeometry( cmd, frameIndex );
    dynamicUniqueIDs.clear();
    alreadyReplacedUniqueObjectIDs.clear();
}

void RTGL1::Scene::SubmitForFrame( VkCommandBuffer                         cmd,
                                   uint32_t                                frameIndex,
                                   const std::shared_ptr< GlobalUniform >& uniform,
                                   uint32_t uniformData_rayCullMaskWorld,
                                   bool     allowGeometryWithSkyFlag,
                                   bool     disableRTGeometry )
{
    // always submit dynamic geometry on the frame ending
    asManager->SubmitDynamicGeometry( makingDynamic, cmd, frameIndex );

    // geom infos must be ready before vertex preprocessing
    auto tlas     = asManager->MakeUniqueIDToTlasID( disableRTGeometry );
    auto tlasSize = static_cast< uint32_t >( tlas.size() );

    geomInfoMgr->CopyFromStaging( cmd, frameIndex, std::move( tlas ) );

    vertPreproc->Preprocess(
        cmd, frameIndex, VERT_PREPROC_MODE_ONLY_DYNAMIC, *uniform, *asManager, tlasSize );

    asManager->BuildTLAS( cmd,
                          frameIndex,
                          uniformData_rayCullMaskWorld,
                          allowGeometryWithSkyFlag,
                          disableRTGeometry );
}

RTGL1::UploadResult RTGL1::Scene::UploadPrimitive( uint32_t                   frameIndex,
                                                   const RgMeshInfo&          mesh,
                                                   const RgMeshPrimitiveInfo& primitive,
                                                   const TextureManager&      textureManager,
                                                   LightManager&              lightManager,
                                                   bool                       isStatic )
{
    const auto uniqueID = PrimitiveUniqueID{ mesh, primitive };

    auto replacement = static_cast< const WholeModelFile::RawModelData* >( nullptr );

    if( !ignoreExternalGeometry )
    {
        if( !isStatic && mesh.isExportable && !Utils::IsCstrEmpty( mesh.pMeshName ) )
        {
            // if dynamic-exportable was already uploaded
            // (i.e. found a matching mesh inside a static scene)
            // otherwise, continue as dynamic
            if( StaticMeshExists( mesh ) )
            {
                return UploadResult::ExportableStatic;
            }

            if( mesh.flags & RG_MESH_INFO_EXPORT_AS_SEPARATE_FILE )
            {
                replacement = find_p( replacements, mesh.pMeshName );
            }
        }
    }

    if( !InsertPrimitiveInfo( uniqueID, isStatic, mesh, primitive ) )
    {
        return UploadResult::Fail;
    }

    if( !replacement )
    {
        if( !asManager->AddMeshPrimitive( frameIndex,
                                          mesh,
                                          primitive,
                                          uniqueID,
                                          isStatic,
                                          false,
                                          textureManager,
                                          *geomInfoMgr ) )
        {
            return UploadResult::Fail;
        }
    }
    else
    {
        assert( !isStatic );

        // multiple primitives can correspond to one mesh instance,
        // if a replacement for a mesh is present, upload it once
        if( !alreadyReplacedUniqueObjectIDs.contains( mesh.uniqueObjectID ) )
        {
            constexpr bool isReplacement = true;

            for( uint32_t i = 0; i < replacement->primitives.size(); i++ )
            {
                auto r = MakeMeshPrimitiveInfoAndProcess(
                    ( replacement->primitives )[ i ],
                    i, //
                    [ & ]( const RgMeshPrimitiveInfo& replacementPrim ) {
                        if( !asManager->AddMeshPrimitive(
                                frameIndex,
                                mesh,
                                replacementPrim,
                                PrimitiveUniqueID{ mesh, replacementPrim },
                                false,
                                isReplacement,
                                textureManager,
                                *geomInfoMgr ) )
                        {
                            return UploadResult::Fail;
                        }
                        return UploadResult::ExportableDynamic;
                    } );
                assert( r != UploadResult::Fail );
            }

            for( const auto& localLight : replacement->localLights )
            {
                assert( localLight.base.uniqueID != 0 && localLight.base.isExportable );
                UploadLight( frameIndex, localLight, lightManager, false );
            }

            alreadyReplacedUniqueObjectIDs.insert( mesh.uniqueObjectID );
        }

        assert( mesh.isExportable );
        return UploadResult::ExportableDynamic;
    }

    return isStatic
               ? ( mesh.isExportable ? UploadResult::ExportableStatic : UploadResult::Static )
               : ( mesh.isExportable ? UploadResult::ExportableDynamic : UploadResult::Dynamic );
}

RTGL1::UploadResult RTGL1::Scene::UploadLight( uint32_t         frameIndex,
                                               const LightCopy& light,
                                               LightManager&    lightManager,
                                               bool             isStatic )
{
    bool isExportable = light.base.isExportable;

    if( !isStatic )
    {
        if( isExportable )
        {
            if( StaticLightExists( light ) )
            {
                return UploadResult::ExportableStatic;
            }
        }
    }

    if( !InsertLightInfo( isStatic, light ) )
    {
        return UploadResult::Fail;
    }

    // adding static to light manager is done separately in SubmitStaticLights
    if( !isStatic )
    {
        lightManager.Add( frameIndex, light );
    }

    return isStatic ? ( isExportable ? UploadResult::ExportableStatic : UploadResult::Static )
                    : ( isExportable ? UploadResult::ExportableDynamic : UploadResult::Dynamic );
}

void RTGL1::Scene::SubmitStaticLights( uint32_t          frameIndex,
                                       LightManager&     lightManager,
                                       bool              isUnderwater,
                                       RgColor4DPacked32 underwaterColor ) const
{
    for( const LightCopy& l : staticLights )
    {
        // SHIPPING HACK - BEGIN: tint sun if underwater
        if( isUnderwater )
        {
            if( auto sun = std::get_if< RgLightDirectionalEXT >( &l.extension ) )
            {
                auto tintedSun  = RgLightDirectionalEXT{ *sun };
                tintedSun.color = underwaterColor;

                lightManager.Add( frameIndex,
                                  LightCopy{
                                      .base       = l.base,
                                      .extension  = tintedSun,
                                      .additional = l.additional,
                                  } );
                continue;
            }
        }
        // SHIPPING HACK - END

        lightManager.Add( frameIndex, l );
    }
}

bool RTGL1::Scene::InsertPrimitiveInfo( const PrimitiveUniqueID&   uniqueID,
                                        bool                       isStatic,
                                        const RgMeshInfo&          mesh,
                                        const RgMeshPrimitiveInfo& primitive )
{
    if( isStatic )
    {
        assert( !Utils::IsCstrEmpty( mesh.pMeshName ) );
        if( !Utils::IsCstrEmpty( mesh.pMeshName ) )
        {
            staticMeshNames.emplace( mesh.pMeshName );
        }

        if( !dynamicUniqueIDs.contains( uniqueID ) )
        {
            auto [ iter, isNew ] = staticUniqueIDs.emplace( uniqueID );
            if( isNew )
            {
                return true;
            }
        }
    }
    else
    {
        if( !staticUniqueIDs.contains( uniqueID ) )
        {
            auto [ iter, isNew ] = dynamicUniqueIDs.emplace( uniqueID );
            if( isNew )
            {
                return true;
            }
        }
    }

    debug::Warning( "Mesh primitive ({}) with ID ({}->{}): "
                    "Trying to upload but a primitive with the same ID already exists",
                    Utils::SafeCstr( mesh.pMeshName ),
                    mesh.uniqueObjectID,
                    primitive.primitiveIndexInMesh );
    return false;
}

bool RTGL1::Scene::InsertLightInfo( bool isStatic, const LightCopy& light )
{
    if( isStatic )
    {
        // just check that there's no id collision
        auto foundSameId =
            std::ranges::find_if( staticLights, [ &light ]( const LightCopy& other ) {
                return other.base.uniqueID == light.base.uniqueID;
            } );

        if( foundSameId != staticLights.end() )
        {
            debug::Warning(
                "Trying add a static light with a uniqueID {} that other light already has",
                light.base.uniqueID );
            return false;
        }

        // add to the list
        staticLights.push_back( light );
        return true;
    }
    else
    {
        return true;
    }
}

void RTGL1::Scene::NewScene( VkCommandBuffer           cmd,
                             uint32_t                  frameIndex,
                             const GltfImporter&       staticScene,
                             TextureManager&           textureManager,
                             const TextureMetaManager& textureMeta,
                             LightManager&             lightManager )
{
    staticUniqueIDs.clear();
    staticMeshNames.clear();
    staticLights.clear();

    textureManager.FreeAllImportedMaterials( frameIndex );

    assert( !makingStatic );
    makingStatic = asManager->BeginStaticGeometry();

    if( staticScene )
    {
        const auto sceneFile =
            staticScene.ParseFile( cmd, frameIndex, textureManager, textureMeta );

        for( const auto& [ name, m ] : sceneFile.models )
        {
            const auto mesh = MakeMeshInfoFrom( name.c_str(), m );

            for( uint32_t i = 0; i < m.primitives.size(); i++ )
            {
                MakeMeshPrimitiveInfoAndProcess(
                    m.primitives[ i ],
                    i, //
                    [ & ]( const RgMeshPrimitiveInfo& prim ) {
                        this->UploadPrimitive(
                            frameIndex, mesh, prim, textureManager, lightManager, true );
                    } );

                if( !m.localLights.empty() )
                {
                    debug::Warning( "Lights under the scene mesh ({}) are ignored, "
                                    "put them under the root node.",
                                    name,
                                    staticScene.FilePath() );
                }
            }
        }

        // global lights
        for( const auto& l : sceneFile.lights )
        {
            this->UploadLight( frameIndex, l, lightManager, true );
        }

        if( sceneFile.lights.empty() )
        {
            debug::Warning( "Haven't found any lights in {}: "
                            "Original exportable lights will be used",
                            staticScene.FilePath() );
        }
    }
    else
    {
        debug::Info( "New scene is empty" );
    }

    debug::Info( "Rebuilding static geometry. Waiting device idle..." );
    asManager->SubmitStaticGeometry( makingStatic );

    debug::Info( "Static geometry was rebuilt" );
}

void RTGL1::Scene::RereadReplacements( VkCommandBuffer              cmdForTextures,
                                       uint32_t                     frameIndex,
                                       const std::filesystem::path& replacementsFolder,
                                       TextureManager&              textureManager,
                                       const TextureMetaManager&    textureMeta )
{
    namespace fs = std::filesystem;

    replacements.clear();

    if( !exists( replacementsFolder ) )
    {
        return;
    }

    for( const fs::directory_entry& entry : fs::directory_iterator{ replacementsFolder } )
    {
        if( !entry.is_regular_file() || MakeFileType( entry.path() ) != FileType::GLTF )
        {
            continue;
        }

        if( auto i = GltfImporter{ entry.path(), RG_TRANSFORM_IDENTITY, 1.0f } )
        {
            auto wholeGltf = i.ParseFile( cmdForTextures, frameIndex, textureManager, textureMeta );

            if( !wholeGltf.lights.empty() )
            {
                debug::Warning( "Ignoring non-attached lights from \'{}\'", entry.path().string() );
            }

            for( const auto& [ meshName, mesh ] : wholeGltf.models )
            {
                auto [ iter, isNew ] = replacements.emplace( meshName, mesh );

                if( isNew )
                {
                    if( mesh.primitives.empty() && mesh.localLights.empty() )
                    {
                        debug::Warning( "Replacement is empty, it doesn't have "
                                        "any primitives or lights: \'{}\' - \'{}\'",
                                        meshName,
                                        entry.path().string() );
                    }
                }
                else
                {
                    debug::Warning( "Ignoring a replacement as it was already read "
                                    "from another .gltf file. \'{}\' - \'{}\'",
                                    meshName,
                                    entry.path().string() );
                }
            }
        }
    }
}

const std::shared_ptr< RTGL1::ASManager >& RTGL1::Scene::GetASManager()
{
    return asManager;
}

const std::shared_ptr< RTGL1::VertexPreprocessing >& RTGL1::Scene::GetVertexPreprocessing()
{
    return vertPreproc;
}

std::optional< uint64_t > RTGL1::Scene::TryGetVolumetricLight(
    const RgDrawFrameIlluminationParams& params ) const
{
    auto lightstyles = std::span( params.pLightstyleValues, params.lightstyleValuesCount );

    if( auto best = LightManager::TryGetVolumetricLight( staticLights, lightstyles ) )
    {
        return best;
    }

    // if nothing, just try find sun
    for( const auto& l : staticLights )
    {
        if( auto sun = std::get_if< RgLightDirectionalEXT >( &l.extension ) )
        {
            return l.base.uniqueID;
        }
    }

    return std::nullopt;
}

bool RTGL1::Scene::StaticMeshExists( const RgMeshInfo& mesh ) const
{
    if( !Utils::IsCstrEmpty( mesh.pMeshName ) )
    {
        // TODO: actually, need to consider RgMeshInfo::uniqueObjectID,
        // as there might be different instances of the same mesh
        if( staticMeshNames.contains( mesh.pMeshName ) )
        {
            return true;
        }
    }
    return false;
}

bool RTGL1::Scene::StaticLightExists( const LightCopy& light ) const
{
    assert( light.base.isExportable );

    // TODO: compare ID-s?
    return !staticLights.empty();
}

RTGL1::SceneImportExport::SceneImportExport( std::filesystem::path _scenesFolder,
                                             std::filesystem::path _replacementsFolder,
                                             const RgFloat3D&      _worldUp,
                                             const RgFloat3D&      _worldForward,
                                             const float&          _worldScale )
    : scenesFolder{ std::move( _scenesFolder ) }
    , replacementsFolder{ std::move( _replacementsFolder ) }
    , reimportRequested{ false }            // reread when new scene appears
    , reimportReplacementsRequested{ true } // should reread initially
    , worldUp{ Utils::SafeNormalize( _worldUp, { 0, 1, 0 } ) }
    , worldForward{ Utils::SafeNormalize( _worldForward, { 0, 0, 1 } ) }
    , worldScale{ std::max( 0.0f, _worldScale ) }
{
}

void RTGL1::SceneImportExport::RequestReimport()
{
    reimportRequested = true;
}

void RTGL1::SceneImportExport::RequestReplacementsReimport()
{
    reimportReplacementsRequested = true;
}

void RTGL1::SceneImportExport::PrepareForFrame()
{
    if( exportRequested )
    {
        sceneExporter = std::make_unique< GltfExporter >( MakeWorldTransform(), GetWorldScale() );
    }
    exportRequested = false;
}

void RTGL1::SceneImportExport::CheckForNewScene( std::string_view    mapName,
                                                 VkCommandBuffer     cmd,
                                                 uint32_t            frameIndex,
                                                 Scene&              scene,
                                                 TextureManager&     textureManager,
                                                 TextureMetaManager& textureMeta,
                                                 LightManager&       lightManager )
{
    if( currentMap != mapName || reimportRequested || reimportReplacementsRequested )
    {

        currentMap                    = mapName;
        reimportRequested             = false;
        reimportReplacementsRequested = false;

        // before importer, as it relies on texture properties
        textureMeta.RereadFromFiles( GetImportMapName() );

        debug::Verbose( "Starting new scene..." );
        {
            auto staticScene = GltfImporter{ MakeGltfPath( scenesFolder, GetImportMapName() ),
                                             MakeWorldTransform(),
                                             GetWorldScale() };

            scene.NewScene(
                cmd, frameIndex, staticScene, textureManager, textureMeta, lightManager );
        }
        debug::Verbose( "New scene is ready" );

        // NOTE: RereadReplacements must happen with NewScene,
        //       as replacement AS depend on static AS infrastructure

        debug::Verbose( "Reading replacements..." );
        {
            scene.RereadReplacements(
                cmd, frameIndex, replacementsFolder, textureManager, textureMeta );
        }
        debug::Verbose( "Replacements are ready" );
    }
}

void RTGL1::SceneImportExport::TryExport( const TextureManager&        textureManager,
                                          const std::filesystem::path& ovrdFolder )
{
    if( sceneExporter )
    {
        sceneExporter->ExportToFiles(
            MakeGltfPath( scenesFolder, GetExportMapName() ), textureManager, ovrdFolder, true );
        sceneExporter.reset();
    }

    for( auto& [ path, e ] : replacementExporters )
    {
        e->ExportToFiles( path, textureManager, ovrdFolder, false );
    }
    replacementExporters.clear();
}

void RTGL1::SceneImportExport::OnFileChanged( FileType type, const std::filesystem::path& filepath )
{
    if( type == FileType::GLTF )
    {
        if( filepath == MakeGltfPath( scenesFolder, GetImportMapName() ) )
        {
            debug::Info( "Hot-reloading GLTF..." );
            RequestReimport();
        }
        else if( filepath.string().contains( REPLACEMENTS_FOLDER ) )
        {
            debug::Info( "Hot-reloading GLTF replacements..." );
            debug::Info( "Triggered by: {}", SanitizePathToShow( filepath ) );
            RequestReplacementsReimport();
        }
    }
}

RTGL1::GltfExporter* RTGL1::SceneImportExport::TryGetExporter( const char* customExportFileName )
{
    if( !sceneExporter )
    {
        return nullptr;
    }

    if( !Utils::IsCstrEmpty( customExportFileName ) )
    {
        const auto replacementsPath =
            MakeGltfPath< false >( replacementsFolder, customExportFileName );

        auto found = replacementExporters.find( replacementsPath );
        if( found != replacementExporters.end() )
        {
            return found->second.get();
        }
        else
        {
            auto [ iter, isNew ] = replacementExporters.emplace(
                replacementsPath,
                std::make_unique< GltfExporter >( MakeWorldTransform(), GetWorldScale() ) );

            return iter->second.get();
        }
    }

    return sceneExporter.get();
}

const RgFloat3D& RTGL1::SceneImportExport::GetWorldUp() const
{
    if( dev.worldTransform.enable )
    {
        return dev.worldTransform.up;
    }

    assert( !Utils::IsAlmostZero( worldUp ) );
    return worldUp;
}

const RgFloat3D& RTGL1::SceneImportExport::GetWorldForward() const
{
    if( dev.worldTransform.enable )
    {
        return dev.worldTransform.forward;
    }

    assert( !Utils::IsAlmostZero( worldForward ) );
    return worldForward;
}

RgFloat3D RTGL1::SceneImportExport::GetWorldRight() const
{
    const auto& up      = GetWorldUp();
    const auto& forward = GetWorldForward();

    RgFloat3D worldRight = Utils::Cross( up, forward );
    assert( std::abs( Utils::SqrLength( worldRight.data ) - 1.0 ) < 0.001f );
    return worldRight;
}

float RTGL1::SceneImportExport::GetWorldScale() const
{
    if( dev.worldTransform.enable )
    {
        return dev.worldTransform.scale;
    }

    assert( worldScale >= 0.0f );
    return worldScale;
}

auto RTGL1::SceneImportExport::MakeWorldTransform() const -> RgTransform
{
    return Utils::MakeTransform(
        Utils::Normalize( GetWorldUp() ), Utils::Normalize( GetWorldForward() ), GetWorldScale() );
}

auto RTGL1::SceneImportExport::dev_GetSceneImportGltfPath() -> std::string
{
    return SanitizePathToShow( MakeGltfPath( scenesFolder, GetImportMapName() ) );
}

auto RTGL1::SceneImportExport::dev_GetSceneExportGltfPath() -> std::string
{
    return SanitizePathToShow( MakeGltfPath( scenesFolder, GetExportMapName() ) );
}

std::string_view RTGL1::SceneImportExport::GetImportMapName() const
{
    if( dev.importName.enable )
    {
        dev.importName.value[ std::size( dev.importName.value ) - 1 ] = '\0';
        return dev.importName.value;
    }

    return currentMap;
}

std::string_view RTGL1::SceneImportExport::GetExportMapName() const
{
    if( dev.exportName.enable )
    {
        dev.exportName.value[ std::size( dev.exportName.value ) - 1 ] = '\0';
        return dev.exportName.value;
    }

    return currentMap;
}

void RTGL1::SceneImportExport::RequestExport()
{
    exportRequested = true;
}
