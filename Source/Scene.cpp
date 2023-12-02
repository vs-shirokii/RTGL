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

#define GLM_FORCE_QUAT_DATA_XYZW 1

#include "Scene.h"

#include "CmdLabel.h"
#include "GeomInfoManager.h"
#include "GltfImporter.h"
#include "Matrix.h"
#include "RgException.h"
#include "UniqueID.h"

#include "Generated/ShaderCommonC.h"

#include <glm/gtc/quaternion.hpp>

#include <future>
#include <ranges>

namespace
{

template< typename T, typename... Args >
struct has_constructor
{
    template< typename U, typename = decltype( U( std::declval< Args >()... ) ) >
    static std::true_type test( int );

    template< typename >
    static std::false_type test( ... );

    static constexpr bool value = decltype( test< T >( 0 ) )::value;
};

template< typename T, typename... Args >
inline constexpr bool has_constructor_v = has_constructor< T, Args... >::value;

template< bool WithSeparateFolder = true >
auto MakeGltfPath( const std::filesystem::path& base, std::string_view meshName )
    -> std::filesystem::path
{
    auto exportName = std::string( meshName );

    std::ranges::replace( exportName, '\\', '_' );
    std::ranges::replace( exportName, '/', '_' );

    if( WithSeparateFolder )
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

constexpr auto REPLACE_SET_MAX_INDEX = 999;

auto asNumber( const std::string_view& str ) -> std::optional< uint32_t >
{
    if( str.empty() )
    {
        return {};
    }

    uint32_t num = 0;
    for( char c : str )
    {
        if( c >= '0' && c <= '9' )
        {
            auto i = c - '0';
            num    = num * 10 + i;

            // overflow
            if( num > REPLACE_SET_MAX_INDEX )
            {
                return REPLACE_SET_MAX_INDEX + 1;
            }
        }
        else
        {
            return {};
        }
    }
    return num;
}

auto FindNextReplaceFileNameInFolder( const std::filesystem::path& folder ) -> std::string
{
    constexpr auto prefix = std::string_view{ "set_" };

    if( !exists( folder ) )
    {
        return std::string{ prefix } + '0';
    }

    if( !is_directory( folder ) )
    {
        RTGL1::debug::Warning( R"(Export fail: expected '{}' to be a folder)",
                               SanitizePathToShow( folder ) );
        return {};
    }

    uint32_t largest = 0;

    for( const auto& entry : std::filesystem::directory_iterator{ folder } )
    {
        if( entry.is_regular_file() )
        {
            const auto namestorage = entry.path().stem().string();
            const auto name        = std::string_view{ namestorage };

            if( name.starts_with( prefix ) )
            {
                if( auto i = asNumber( name.substr( prefix.length() ) ) )
                {
                    largest = std::max( largest, *i );
                }
            }
        }
    }

    if( largest + 1 > REPLACE_SET_MAX_INDEX )
    {
        RTGL1::debug::Warning( "Couldn't find next file name in folder: {}. Last index is {}{}",
                               SanitizePathToShow( folder ),
                               prefix,
                               REPLACE_SET_MAX_INDEX );
        return {};
    }

    return std::string{ prefix } + std::to_string( largest + 1 );
}

auto GetGltfFilesSortedAlphabetically( const std::filesystem::path& folder )
    -> std::set< std::filesystem::path >
{
    using namespace RTGL1;
    namespace fs = std::filesystem;

    if( folder.empty() || !exists( folder ) || !is_directory( folder ) )
    {
        return {};
    }

    auto gltfs = std::set< std::filesystem::path >{};

    try
    {
        for( const fs::directory_entry& entry : fs::directory_iterator{ folder } )
        {
            if( entry.is_regular_file() && MakeFileType( entry.path() ) == RTGL1::FileType::GLTF )
            {
                gltfs.insert( entry.path() );
            }
        }
    }
    catch( const std::filesystem::filesystem_error& e )
    {
        debug::Error( R"(directory_iterator failure: '{}'. path1: '{}'. path2: '{}')",
                      e.what(),
                      e.path1().string(),
                      e.path2().string() );
        return {};
    }

    return gltfs;
}
}

RTGL1::Scene::Scene( VkDevice                                _device,
                     const PhysicalDevice&                   _physDevice,
                     std::shared_ptr< MemoryAllocator >&     _allocator,
                     std::shared_ptr< CommandBufferManager > _cmdManager,
                     const GlobalUniform&                    _uniform,
                     const ShaderManager&                    _shaderManager,
                     uint64_t                                _maxReplacementsVerts,
                     uint64_t                                _maxDynamicVerts,
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
                                               _maxReplacementsVerts,
                                               _maxDynamicVerts,
                                               _enableTexCoordLayer1,
                                               _enableTexCoordLayer2,
                                               _enableTexCoordLayer3 );

    vertPreproc =
        std::make_shared< VertexPreprocessing >( _device, _uniform, *asManager, _shaderManager );
}

namespace RTGL1
{
namespace
{
    Camera MakeCamera( const RgCameraInfo& info )
    {
        auto cameraInfo = Camera{
            .aspect      = info.aspect,
            .fovYRadians = info.fovYRadians,
            .cameraNear  = info.cameraNear,
            .cameraFar   = info.cameraFar,
        };
        {
            static_assert( sizeof cameraInfo.projection == 16 * sizeof( float ) );
            static_assert( sizeof cameraInfo.view == 16 * sizeof( float ) );
            static_assert( sizeof cameraInfo.projectionInverse == 16 * sizeof( float ) );
            static_assert( sizeof cameraInfo.viewInverse == 16 * sizeof( float ) );
            static_assert( sizeof info.position == 3 * sizeof( float ) );
            static_assert( sizeof info.right == 3 * sizeof( float ) );
            static_assert( sizeof info.up == 3 * sizeof( float ) );

            Matrix::MakeViewMatrix( cameraInfo.view, info.position, info.right, info.up );
            Matrix::MakeProjectionMatrix( cameraInfo.projection,
                                          cameraInfo.aspect,
                                          cameraInfo.fovYRadians,
                                          cameraInfo.cameraNear,
                                          cameraInfo.cameraFar );
        }
        Matrix::Inverse( cameraInfo.viewInverse, cameraInfo.view );
        Matrix::Inverse( cameraInfo.projectionInverse, cameraInfo.projection );
        return cameraInfo;
    }

    template< typename T >
    T linear_interp( const T& a, const T& b, float t ) = delete;

    template <>
    float linear_interp( const float& a, const float& b, float t )
    {
        return std::lerp( a, b, t );
    }

    template<>
    RgFloat3D linear_interp( const RgFloat3D& a, const RgFloat3D& b, float t )
    {
        return RgFloat3D{
            std::lerp( a.data[ 0 ], b.data[ 0 ], t ),
            std::lerp( a.data[ 1 ], b.data[ 1 ], t ),
            std::lerp( a.data[ 2 ], b.data[ 2 ], t ),
        };
    }

    template<>
    RgQuaternion linear_interp( const RgQuaternion& a, const RgQuaternion& b, float t )
    {
        static_assert( GLM_FORCE_QUAT_DATA_XYZW );
        static_assert( sizeof( glm::quat ) == sizeof( RgQuaternion ) );

        const auto& qa = *reinterpret_cast< const glm::quat* >( a.data );
        const auto& qb = *reinterpret_cast< const glm::quat* >( b.data );

        glm::quat result = glm::slerp( qa, qb, t );
        return *reinterpret_cast< RgQuaternion* >( &result );
    }

    RgFloat3D ToRgFloat3D( const glm::vec3& a )
    {
        return { a.x, a.y, a.z };
    }

    auto QuatToUpRightVectors( const RgQuaternion& q ) -> std::pair< RgFloat3D, RgFloat3D >
    {
        static_assert( GLM_FORCE_QUAT_DATA_XYZW );
        static_assert( sizeof( glm::quat ) == sizeof( RgQuaternion ) );

        const auto& qa = *reinterpret_cast< const glm::quat* >( q.data );

        glm::mat3 tr = mat3_cast( qa );

        return {
            ToRgFloat3D( tr[ 1 ] ), // up
            ToRgFloat3D( tr[ 0 ] ), // right
        };
    }

    RgCameraInfo MakeCameraFromAnim( const RgCameraInfo&                  base,
                                     const std::optional< RgFloat3D >&    pos,
                                     const std::optional< RgQuaternion >& quat,
                                     const std::optional< float >&        fovYRadians )
    {
        auto cam = RgCameraInfo{ base };
        if( pos )
        {
            cam.position = *pos;
        }
        if( quat )
        {
            const auto [ vup, vright ] = QuatToUpRightVectors( *quat );

            cam.up    = vup;
            cam.right = vright;
        }
        if( fovYRadians && ( *fovYRadians ) > 0.01f )
        {
            cam.fovYRadians = *fovYRadians;
        }
        return cam;
    }

    template< typename T >
    std::optional< T > SampleAnimationChannel( const AnimationChannel< T >& chan, float t )
    {
        if( chan.frames.empty() )
        {
            return std::nullopt;
        }

        for( size_t i = 0; i < chan.frames.size() - 1; i++ )
        {
            const AnimationFrame< T >& a = chan.frames[ i ];
            const AnimationFrame< T >& b = chan.frames[ i + 1 ];

            const float t0 = a.seconds;
            const float t1 = b.seconds;

            if( t0 <= t && t <= t1 )
            {
                if( a.interpolation == ANIMATION_INTERPOLATION_STEP )
                {
                    return a.value;
                }
                if( b.interpolation == ANIMATION_INTERPOLATION_STEP )
                {
                    return b.value;
                }

                // TODO: cubic
                assert( a.interpolation == ANIMATION_INTERPOLATION_LINEAR );

                const float factor = t0 < t1 ? ( t - t0 ) / ( t1 - t0 ) : 0;

                return linear_interp( a.value, b.value, factor );
            }
        }

        if( t < chan.frames.front().seconds )
        {
            return chan.frames.front().value;
        }
        return chan.frames.back().value;
    }

    RgCameraInfo SampleAnimation( const AnimationData& anim, const RgCameraInfo& base, float t )
    {
        auto pos  = SampleAnimationChannel( anim.position, t );
        auto quat = SampleAnimationChannel( anim.quaternion, t );
        auto fovy = SampleAnimationChannel( anim.fovYRadians, t );

        return MakeCameraFromAnim( base, pos, quat, fovy );
    }

    RgTransform SampleAnimationObj( const AnimationData& anim, const RgTransform& base, float t )
    {
        static auto l_column_length = []( const RgTransform& tr, int column ) {
            return Utils::Length( RgFloat3D{
                tr.matrix[ 0 ][ column ],
                tr.matrix[ 1 ][ column ],
                tr.matrix[ 2 ][ column ],
            } );
        };

        const auto pos  = SampleAnimationChannel( anim.position, t );
        const auto quat = SampleAnimationChannel( anim.quaternion, t );

        const float scale[] = {
            l_column_length( base, 0 ),
            l_column_length( base, 1 ),
            l_column_length( base, 2 ),
        };

        auto r = RgTransform{ base };

        if( pos )
        {
            r.matrix[ 0 ][ 3 ] = pos->data[ 0 ];
            r.matrix[ 1 ][ 3 ] = pos->data[ 1 ];
            r.matrix[ 2 ][ 3 ] = pos->data[ 2 ];
        }

        if( quat )
        {
            const auto [ vup, vright ] = QuatToUpRightVectors( *quat );
            const auto vforward        = Utils::Cross( vright, vup );

            // clang-format off
            r.matrix[ 0 ][ 0 ] = vright.data[ 0 ]; r.matrix[ 0 ][ 1 ] = vup.data[ 0 ]; r.matrix[ 0 ][ 2 ] = vforward.data[ 0 ];
            r.matrix[ 1 ][ 0 ] = vright.data[ 1 ]; r.matrix[ 1 ][ 1 ] = vup.data[ 1 ]; r.matrix[ 1 ][ 2 ] = vforward.data[ 1 ];
            r.matrix[ 2 ][ 0 ] = vright.data[ 2 ]; r.matrix[ 2 ][ 1 ] = vup.data[ 2 ]; r.matrix[ 2 ][ 2 ] = vforward.data[ 2 ];
            // clang-format on

            // do not lose the original scale
            // clang-format off
            r.matrix[ 0 ][ 0 ] *= scale[ 0 ]; r.matrix[ 0 ][ 1 ] *= scale[ 1 ]; r.matrix[ 0 ][ 2 ] *= scale[ 2 ];
            r.matrix[ 1 ][ 0 ] *= scale[ 0 ]; r.matrix[ 1 ][ 1 ] *= scale[ 1 ]; r.matrix[ 1 ][ 2 ] *= scale[ 2 ];
            r.matrix[ 2 ][ 0 ] *= scale[ 0 ]; r.matrix[ 2 ][ 1 ] *= scale[ 1 ]; r.matrix[ 2 ][ 2 ] *= scale[ 2 ];
            // clang-format on
        }

        return r;
    }
}
}

void RTGL1::Scene::AddDefaultCamera( const RgCameraInfo& info )
{
    // NOTE: if there are pointers, need to make deep copies
    assert( !info.pView );
    assert( !info.pNext || pnext::cast< RgCameraInfoReadbackEXT >( info.pNext ) );

    cameraInfo_Default = info;
}

const RTGL1::Camera& RTGL1::Scene::GetCamera( float fallbackAspect )
{
    if( !curFrameCamera )
    {
        auto l_make = [ & ]() {
            if( cameraInfo_Imported && cameraInfo_Default )
            {
                auto modified = RgCameraInfo{ *cameraInfo_Default };
                {
                    const auto imp = SampleAnimation( m_cameraInfo_ImportedAnim,
                                                      *cameraInfo_Imported,
                                                      m_staticSceneAnimationTime );

                    modified.fovYRadians = imp.fovYRadians;
                    modified.position    = imp.position;
                    modified.up          = imp.up;
                    modified.right       = imp.right;
                }
                return MakeCamera( modified );
            }
            if( cameraInfo_Default )
            {
                return MakeCamera( *cameraInfo_Default );
            }
            if( cameraInfo_Imported )
            {
                auto modified = SampleAnimation(
                    m_cameraInfo_ImportedAnim, *cameraInfo_Imported, m_staticSceneAnimationTime );
                {
                    modified.aspect = fallbackAspect;
                }
                return MakeCamera( modified );
            }

            debug::Warning( "No camera provided via API, nor through .gltf" );
            return MakeCamera( RgCameraInfo{
                .sType       = RG_STRUCTURE_TYPE_CAMERA_INFO,
                .position    = { 0, 0, 0 },
                .up          = { 0, 1, 0 },
                .right       = { 1, 0, 0 },
                .fovYRadians = Utils::DegToRad( 75 ),
                .aspect      = 16.0f / 9.0f,
                .cameraNear  = 0.1f,
                .cameraFar   = 1000,
            } );
        };

        curFrameCamera = l_make();
    }
    return *curFrameCamera;
}

void RTGL1::Scene::PrepareForFrame( VkCommandBuffer cmd,
                                    uint32_t        frameIndex,
                                    bool            _ignoreExternalGeometry,
                                    float           staticSceneAnimationTime )
{
    assert( !makingDynamic );
    assert( !makingStatic );
    this->ignoreExternalGeometry = _ignoreExternalGeometry;

    geomInfoMgr->PrepareForFrame( frameIndex );

    makingDynamic = asManager->BeginDynamicGeometry( cmd, frameIndex );
    dynamicUniqueIDs.clear();
    alreadyReplacedUniqueObjectIDs.clear();
    lastDynamicSun_uniqueId = std::nullopt;

    curFrameCamera     = {};
    cameraInfo_Default = {};

    m_staticSceneAnimationTime = staticSceneAnimationTime;

    // SHIPPING_HACK
    for( const auto& [ obj, basetransf, anim ] : m_obj_ImportedAnim )
    {
        asManager->Hack_PatchGeomInfoTransformForStatic(
            obj, SampleAnimationObj( anim, basetransf, m_staticSceneAnimationTime ) );
    }
}

void RTGL1::Scene::SubmitForFrame( VkCommandBuffer                         cmd,
                                   uint32_t                                frameIndex,
                                   const std::shared_ptr< GlobalUniform >& uniform,
                                   uint32_t uniformData_rayCullMaskWorld,
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
                          disableRTGeometry );
}

bool RTGL1::Scene::ReplacementExists( const RgMeshInfo& mesh ) const
{
    if( mesh.isExportable && !Utils::IsCstrEmpty( mesh.pMeshName ) )
    {
        if( mesh.flags & RG_MESH_EXPORT_AS_SEPARATE_FILE )
        {
            return find_p( replacements, mesh.pMeshName );
        }
    }
    return false;
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

            if( mesh.flags & RG_MESH_EXPORT_AS_SEPARATE_FILE )
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

            for( auto localLight : replacement->localLights )
            {
                assert( localLight.base.uniqueID != 0 && localLight.base.isExportable );

                auto hashCombine = []< typename T >( size_t seed, const T& v ) {
                    return seed ^
                           ( std::hash< T >{}( v ) + 0x9e3779b9 + ( seed << 6 ) + ( seed >> 2 ) );
                };

                localLight.base.uniqueID =
                    hashCombine( localLight.base.uniqueID, mesh.uniqueObjectID );
                localLight.base.isExportable = false;

                if( localLight.additional && ( localLight.additional->flags &
                                               RG_LIGHT_ADDITIONAL_APPLY_PARENT_MESH_INTENSITY ) )
                {
                    std::visit( [ mult = mesh.localLightsIntensity ]( auto& ext ) //
                                { ext.intensity *= mult; },
                                localLight.extension );
                }

                UploadLight( frameIndex, localLight, lightManager, false, &mesh.transform );
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

RTGL1::UploadResult RTGL1::Scene::UploadLight( uint32_t           frameIndex,
                                               const LightCopy&   light,
                                               LightManager&      lightManager,
                                               bool               isStatic,
                                               const RgTransform* transform )
{
    assert( !isStatic || ( isStatic && !transform ) );

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
        lightManager.Add( frameIndex, light, transform );

        if( std::holds_alternative< RgLightDirectionalEXT >( light.extension ) )
        {
            lastDynamicSun_uniqueId = light.base.uniqueID;
        }
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
        // SHIPPING_HACK begin - tint sun if underwater
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
        // SHIPPING_HACK end

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

void RTGL1::Scene::NewScene( VkCommandBuffer              cmd,
                             uint32_t                     frameIndex,
                             const ImportExportParams&    params,
                             const std::filesystem::path& staticSceneGltfPath,
                             const std::filesystem::path* replacementsFolder,
                             TextureManager&              textureManager,
                             const TextureMetaManager&    textureMeta,
                             LightManager&                lightManager )
{
    const bool reimportReplacements = !!replacementsFolder;

    staticUniqueIDs.clear();
    staticMeshNames.clear();
    staticLights.clear();
    cameraInfo_Imported = {};
    m_cameraInfo_ImportedAnim = {};
    m_obj_ImportedAnim        = {};

    {
        textureManager.FreeAllImportedMaterials( frameIndex, reimportReplacements );
    }

    assert( !makingStatic );
    makingStatic = asManager->BeginStaticGeometry( reimportReplacements );

    if( reimportReplacements )
    {
        replacements.clear();

        debug::Verbose( "Reading replacements..." );
        const auto gltfs = GetGltfFilesSortedAlphabetically( *replacementsFolder );

        auto allImported = std::vector< std::future< std::unique_ptr< WholeModelFile > > >{};
        {
            // reverse alphabetical -- last ones have more priority
            for( const auto& p : std::ranges::reverse_view{ gltfs } )
            {
                allImported.push_back( std::async(
                    std::launch::async,
                    [ & ](
                        const std::filesystem::path& path ) -> std::unique_ptr< WholeModelFile > //
                    {
                        if( auto i = GltfImporter{ path, params, textureMeta, true } )
                        {
                            return std::make_unique< WholeModelFile >( i.Move() );
                        }
                        return {};
                    },
                    p ) );
            }
        }

        for( auto &ff : allImported )
        {
            auto wholeGltf = ff.valid() //
                                 ? ff.get()
                                 : std::unique_ptr< WholeModelFile >{};

            if( !wholeGltf )
            {
                continue;
            }
            auto path = std::filesystem::path{ "<TODO: GET NAME>" };

            if( !wholeGltf->lights.empty() )
            {
                debug::Warning( "Ignoring non-attached lights from \'{}\'", path.string() );
            }

            for( const auto& mat : wholeGltf->materials )
            {
                textureManager.TryCreateImportedMaterial( cmd,
                                                          frameIndex,
                                                          mat.pTextureName,
                                                          mat.fullPaths,
                                                          mat.samplers,
                                                          mat.pbrSwizzling,
                                                          mat.isReplacement );
            }

            for( auto& [ meshName, meshSrc ] : wholeGltf->models )
            {
                auto [ iter, isNew ] = replacements.emplace( meshName, std::move( meshSrc ) );

                if( isNew )
                {
                    WholeModelFile::RawModelData& m = iter->second;

                    for( uint32_t index = 0; index < m.primitives.size(); index++ )
                    {
                        MakeMeshPrimitiveInfoAndProcess(
                            m.primitives[ index ], index, [ & ]( const RgMeshPrimitiveInfo& prim ) {
                                asManager->CacheReplacement(
                                    std::string_view{ meshName }, prim, index );
                            } );

                        // save up some memory by not storing - as we uploaded already
                        m.primitives[ index ].vertices = {};
                        m.primitives[ index ].indices  = {};
                    }

                    if( m.primitives.empty() && m.localLights.empty() )
                    {
                        debug::Warning( "Replacement is empty, it doesn't have "
                                        "any primitives or lights: \'{}\' - \'{}\'",
                                        meshName,
                                        path.string() );
                    }
                }
                else
                {
                    debug::Warning( "Ignoring a replacement as it was already read "
                                    "from another .gltf file. \'{}\' - \'{}\'",
                                    meshName,
                                    path.string() );
                }
            }
        }
        debug::Verbose( "Replacements are ready" );
    }

    asManager->MarkReplacementsRegionEnd( makingStatic );

    // SHIPPING_HACK begin
    m_primitivesToUpdateTextures.clear();
    auto trackTextureToReplace = rgl::string_set{};
    // SHIPPING_HACK end

    if( auto staticScene = GltfImporter{ staticSceneGltfPath, params, textureMeta, false } )
    {
        WholeModelFile sceneFile = staticScene.Move();

        debug::Verbose( "Starting new static scene..." );

        if( auto patchScene = GltfImporter{
                AddSuffix( staticSceneGltfPath, SCENE_PATCH_SUFFIX ), params, textureMeta, false } )
        {
            WholeModelFile patch = patchScene.Move();

            sceneFile.materials.insert( sceneFile.materials.end(),
                                        std::make_move_iterator( patch.materials.begin() ),
                                        std::make_move_iterator( patch.materials.end() ) );

            for( auto& [ name, mdl ] : patch.models )
            {
                auto f = sceneFile.models.find( name );
                if( f != sceneFile.models.end() )
                {
                    if( !Utils::AreAlmostSameTr( f->second.meshTransform, mdl.meshTransform ) )
                    {
                        debug::Warning(
                            "Patch file contains node \'{}\' with one transform, but the base gltf "
                            "file contains a node with same name which has ANOTHER transform. "
                            "Expect incorrect patch file meshes. Base gltf file: {}",
                            name,
                            staticSceneGltfPath.string() );
                    }

                    f->second.primitives.insert( f->second.primitives.end(),
                                                 std::make_move_iterator( mdl.primitives.begin() ),
                                                 std::make_move_iterator( mdl.primitives.end() ) );
                }
                else
                {
                    sceneFile.models.emplace( std::move( name ), std::move( mdl ) );
                }
            }
        }

        for( const auto& mat : sceneFile.materials )
        {
            textureManager.TryCreateImportedMaterial( cmd,
                                                      frameIndex,
                                                      mat.pTextureName,
                                                      mat.fullPaths,
                                                      mat.samplers,
                                                      mat.pbrSwizzling,
                                                      mat.isReplacement );

            // SHIPPING_HACK begin
            if( mat.trackOriginalTexture && !mat.pTextureName.empty() )
            {
                trackTextureToReplace.insert( mat.pTextureName );
            }
            // SHIPPING_HACK end
        }

        for( const auto& [ name, m ] : sceneFile.models )
        {
            const auto mesh = MakeMeshInfoFrom( name.c_str(), m );

            for( uint32_t i = 0; i < m.primitives.size(); i++ )
            {
                MakeMeshPrimitiveInfoAndProcess(
                    m.primitives[ i ],
                    i, //
                    [ & ]( const RgMeshPrimitiveInfo& prim ) {
                        UploadResult ur = this->UploadPrimitive(
                            frameIndex, mesh, prim, textureManager, lightManager, true );

                        // SHIPPING_HACK begin
                        if( ( ur == UploadResult::ExportableStatic ||
                              ur == UploadResult::Static ) &&
                            !Utils::IsCstrEmpty( prim.pTextureName ) &&
                            trackTextureToReplace.contains( prim.pTextureName ) )
                        {
                            const auto uniqueId = PrimitiveUniqueID{ mesh, prim };
                            static_assert( has_constructor_v< PrimitiveUniqueID,
                                                              const RgMeshInfo&,
                                                              const RgMeshPrimitiveInfo& >,
                                           "Change PrimitiveUniqueID constructor here" );

                            m_primitivesToUpdateTextures[ prim.pTextureName ].push_back( uniqueId );
                        }
                        // SHIPPING_HACK end
                        // SHIPPING_HACK begin
                        if( ur == UploadResult::Static && !IsAnimDataEmpty( m.animobj ) )
                        {
                            const auto uniqueId = PrimitiveUniqueID{ mesh, prim };
                            static_assert( has_constructor_v< PrimitiveUniqueID,
                                                              const RgMeshInfo&,
                                                              const RgMeshPrimitiveInfo& >,
                                           "Change PrimitiveUniqueID constructor here" );

                            m_obj_ImportedAnim.emplace_back( uniqueId, mesh.transform, m.animobj );
                        }
                        // SHIPPING_HACK end
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

        // camera
        if( sceneFile.camera )
        {
            cameraInfo_Imported = *( sceneFile.camera );
        }
        if( !IsAnimDataEmpty( sceneFile.animcamera ) )
        {
            m_cameraInfo_ImportedAnim = std::move( sceneFile.animcamera );
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
        debug::Verbose( "Static scene is ready" );
    }
    else
    {
        debug::Info( "New scene is empty" );
    }

    debug::Verbose( "Rebuilding static geometry. Waiting device idle..." );
    asManager->SubmitStaticGeometry( makingStatic, reimportReplacements );

    debug::Info( "Static geometry was rebuilt" );
}

const std::shared_ptr< RTGL1::ASManager >& RTGL1::Scene::GetASManager()
{
    return asManager;
}

const std::shared_ptr< RTGL1::VertexPreprocessing >& RTGL1::Scene::GetVertexPreprocessing()
{
    return vertPreproc;
}

auto RTGL1::Scene::TryGetVolumetricLight( const LightManager& lightManager,
                                          const RgFloat3D&    cameraPos ) const
    -> std::optional< uint64_t >
{
    return lightManager.TryGetVolumetricLight( cameraPos, staticLights, lastDynamicSun_uniqueId );
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

namespace
{
float OneIfNonZero( float v )
{
    return v < std::numeric_limits< float >::epsilon() ? 1.0f : v;
}
}

RTGL1::SceneImportExport::SceneImportExport( std::filesystem::path       _scenesFolder,
                                             std::filesystem::path       _replacementsFolder,
                                             const RgInstanceCreateInfo& _info )
    : scenesFolder{ std::move( _scenesFolder ) }
    , replacementsFolder{ std::move( _replacementsFolder ) }
    , reimportStatic{ false }      // reread when new scene appears
    , reimportReplacements{ true } // should reread initially
    , reimportStaticInNextFrame{ false }
    , worldUp{ Utils::SafeNormalize( _info.worldUp, { 0, 1, 0 } ) }
    , worldForward{ Utils::SafeNormalize( _info.worldForward, { 0, 0, 1 } ) }
    , worldScale{ std::max( 0.0f, _info.worldScale ) }
    , importedLightIntensityScaleDirectional{ OneIfNonZero(
          _info.importedLightIntensityScaleDirectional ) }
    , importedLightIntensityScaleSphere{ OneIfNonZero( _info.importedLightIntensityScaleSphere ) }
    , importedLightIntensityScaleSpot{ OneIfNonZero( _info.importedLightIntensityScaleSpot ) }
{
}

void RTGL1::SceneImportExport::RequestReimport()
{
    reimportStatic = true;
}

void RTGL1::SceneImportExport::RequestReplacementsReimport()
{
    reimportReplacements = true;
}

namespace RTGL1
{
bool g_showAutoExportPlaque = false;
}

void RTGL1::SceneImportExport::PrepareForFrame( std::string_view mapName,
                                                bool             allowSceneAutoExport )
{
    // import

    if( reimportStaticInNextFrame )
    {
        reimportStatic            = true;
        reimportStaticInNextFrame = false;
    }

    if( currentMap != mapName )
    {
        currentMap     = mapName;
        reimportStatic = true;

        if( allowSceneAutoExport && !currentMap.empty() )
        {
            if( !exists( MakeGltfPath( scenesFolder, GetImportMapName() ) ) )
            {
                exportRequested           = true;
                reimportStatic            = false;
                reimportStaticInNextFrame = true;

                g_showAutoExportPlaque = true;
            }
        }
    }

    // export scene

    if( exportRequested )
    {
        assert( !sceneExporter );
        sceneExporter   = std::make_unique< GltfExporter >( MakeImportExportParams(), true );
        exportRequested = false;
    }

    // export replacements

    if( exportReplacementsRequest == ExportState::OneFrame )
    {
        assert( !replacementsExporter );
        replacementsExporter = std::make_unique< GltfExporter >( MakeImportExportParams(), false );
    }
    else if( exportReplacementsRequest == ExportState::Recording )
    {
        if( !replacementsExporter )
        {
            replacementsExporter =
                std::make_unique< GltfExporter >( MakeImportExportParams(), false );
        }
    }
}

void RTGL1::SceneImportExport::TryImportIfNew( VkCommandBuffer           cmd,
                                               uint32_t                  frameIndex,
                                               Scene&                    scene,
                                               TextureManager&           textureManager,
                                               TextureMetaManager&       textureMeta,
                                               LightManager&             lightManager,
                                               RgStaticSceneStatusFlags* out_staticSceneStatus )
{
    const bool newSceneRequested = reimportStatic || reimportStaticInNextFrame;

    if( reimportReplacements || reimportStatic )
    {
        // before importer, as it relies on texture properties
        textureMeta.RereadFromFiles( GetImportMapName() );

        scene.NewScene( cmd,
                        frameIndex,
                        MakeImportExportParams(),
                        MakeGltfPath( scenesFolder, GetImportMapName() ),
                        reimportReplacements ? &replacementsFolder : nullptr,
                        textureManager,
                        textureMeta,
                        lightManager );

        reimportReplacements = false;
        reimportStatic       = false;
    }

    if( out_staticSceneStatus )
    {
        *out_staticSceneStatus = 0;

        if( scene.StaticSceneExists() )
        {
            ( *out_staticSceneStatus ) |= RG_STATIC_SCENE_STATUS_LOADED;
        }
        if( newSceneRequested )
        {
            ( *out_staticSceneStatus ) |= RG_STATIC_SCENE_STATUS_NEW_SCENE_STARTED;
        }
        if( sceneExporter )
        {
            ( *out_staticSceneStatus ) |= RG_STATIC_SCENE_STATUS_EXPORT_STARTED;
        }
    }
}

void RTGL1::SceneImportExport::TryExport( const TextureManager&        textureManager,
                                          const std::filesystem::path& ovrdFolder )
{
    if( sceneExporter )
    {
        sceneExporter->ExportToFiles( MakeGltfPath< true >( scenesFolder, GetExportMapName() ),
                                      textureManager,
                                      ovrdFolder,
                                      true );
        sceneExporter.reset();
    }

    if( replacementsExporter && ( exportReplacementsRequest == ExportState::FinilizeIntoFile ||
                                  exportReplacementsRequest == ExportState::OneFrame ) )
    {
        auto setname = FindNextReplaceFileNameInFolder( replacementsFolder );
        if( !setname.empty() )
        {
            replacementsExporter->ExportToFiles(
                MakeGltfPath< false >( replacementsFolder, setname ),
                textureManager,
                ovrdFolder,
                false );
        }
        replacementsExporter.reset();
        exportReplacementsRequest = ExportState::None;
    }
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

RTGL1::GltfExporter* RTGL1::SceneImportExport::TryGetExporter( bool isReplacement )
{
    if( isReplacement )
    {
        return replacementsExporter.get();
    }
    else
    {
        return sceneExporter.get();
    }
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

auto RTGL1::SceneImportExport::MakeImportExportParams() const -> RTGL1::ImportExportParams
{
    return ImportExportParams{
        .worldTransform                         = MakeWorldTransform(),
        .oneGameUnitInMeters                    = GetWorldScale(),
        .importedLightIntensityScaleDirectional = importedLightIntensityScaleDirectional,
        .importedLightIntensityScaleSphere      = importedLightIntensityScaleSphere,
        .importedLightIntensityScaleSpot        = importedLightIntensityScaleSpot,
    };
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

void RTGL1::SceneImportExport::RequestReplacementsExport_OneFrame()
{
    if( exportReplacementsRequest == ExportState::None )
    {
        exportReplacementsRequest = ExportState::OneFrame;
    }
}

void RTGL1::SceneImportExport::RequestReplacementsExport_RecordBegin()
{
    if( exportReplacementsRequest == ExportState::None )
    {
        exportReplacementsRequest = ExportState::Recording;
    }
}

void RTGL1::SceneImportExport::RequestReplacementsExport_RecordEnd()
{
    if( exportReplacementsRequest == ExportState::Recording )
    {
        exportReplacementsRequest = ExportState::FinilizeIntoFile;
    }
}
