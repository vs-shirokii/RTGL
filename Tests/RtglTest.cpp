#include <chrono>
#include <iostream>
#include <cstring>
#include <span>
#include <random>


#ifndef ASSET_DIRECTORY
#define ASSET_DIRECTORY ""
#endif

#ifdef _WIN32
    #define RG_USE_SURFACE_WIN32
#else
    #define RG_USE_SURFACE_XLIB
#endif

#include <RTGL1/RTGL1.h>
RG_D3D12CORE_HELPER( ASSET_DIRECTORY )

#define RG_CHECK( x )                                                              \
    assert( ( x ) == RG_RESULT_SUCCESS || ( x ) == RG_RESULT_SUCCESS_FOUND_MESH || \
            ( x ) == RG_RESULT_SUCCESS_FOUND_TEXTURE )


#pragma region BOILERPLATE

#include <GLFW/glfw3.h>
#ifdef _WIN32
    #define GLFW_EXPOSE_NATIVE_WIN32
#else
    #define GLFW_EXPOSE_NATIVE_X11
#endif
#include <GLFW/glfw3native.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/rotate_vector.hpp>

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "Libs/tinygltf/tiny_gltf.h"


namespace
{
GLFWwindow* g_GlfwHandle;

glm::vec3 ctl_CameraPosition  = glm::vec3{ 4, 1, 0 };
glm::vec3 ctl_CameraDirection = glm::vec3{ 0, 0, 1 };
glm::vec3 ctl_CameraUp        = glm::vec3{ 0, 1, 0 };
glm::vec3 ctl_CameraRight     = glm::vec3{ 1, 0, 0 };
glm::vec2 ctl_CameraPitchYaw  = { 0, glm::pi< float >() };
glm::vec3 ctl_LightPosition   = glm::vec3{ 0, 0, 1 };
float     ctl_LightIntensity  = 1.0f;
float     ctl_LightCount      = 0.0f;
float     ctl_SunIntensity    = 7000.0f;
float     ctl_SkyIntensity    = 1000.0f;
RgBool32  ctl_SkyboxEnable    = 1;
float     ctl_Roughness       = 0.05f;
float     ctl_Metallicity     = 1.0f;
RgBool32  ctl_MoveBoxes       = 0;

bool ProcessWindow()
{
    if( glfwWindowShouldClose( g_GlfwHandle ) )
    {
        return false;
    }

    glfwPollEvents();

    if( glfwGetKey( g_GlfwHandle, GLFW_KEY_P ) == GLFW_PRESS )
    {
        const GLFWvidmode* mode = glfwGetVideoMode( glfwGetPrimaryMonitor() );

        if( glfwGetWindowMonitor( g_GlfwHandle ) )
        {
            constexpr int width  = 1600;
            constexpr int height = 900;

            int xPos = ( mode->width - width ) / 2;
            int yPos = ( mode->height - height ) / 2;
            glfwSetWindowMonitor( g_GlfwHandle, NULL, xPos, yPos, width, height, GLFW_DONT_CARE );
        }
        else
        {
            glfwSetWindowMonitor( g_GlfwHandle,
                                  glfwGetPrimaryMonitor(),
                                  0,
                                  0,
                                  mode->width,
                                  mode->height,
                                  GLFW_DONT_CARE );
        }
    }
    return true;
}

float GetWindowAspect()
{
    if( g_GlfwHandle )
    {
        int width = 0, height = 0;
        glfwGetWindowSize( g_GlfwHandle, &width, &height );
        if( width > 0 && height > 0 )
        {
            return float( width ) / float( height );
        }
    }
    return 16.f / 9.f;
}

// clang-format off
void ProcessInput()
{
    static auto IsPressed    = []( int key ) { return glfwGetKey( g_GlfwHandle, ( key ) ) == GLFW_PRESS; };
    static auto ControlFloat = []( int key, float& value, float speed, float minval = 0.0f, float maxval = 1.0f ) {
            if( IsPressed( key ) ) {
                if( IsPressed( GLFW_KEY_KP_ADD ) ) value += speed;
                if( IsPressed( GLFW_KEY_KP_SUBTRACT ) ) value -= speed; }
            value = std::clamp( value, minval, maxval ); };
    static auto lastTimePressed = std::chrono::system_clock::now();
    static auto ControlSwitch   = []( int key, uint32_t& value, uint32_t stateCount = 2 ) {
        if( IsPressed( key ) ) {
            float secondsSinceLastTime = std::chrono::duration_cast< std::chrono::milliseconds >( std::chrono::system_clock::now() - lastTimePressed ).count() / 1000.0f;
            if( secondsSinceLastTime < 0.5f ) return;
            value           = ( value + 1 ) % stateCount;
            lastTimePressed = std::chrono::system_clock::now(); } };

    const auto cameraSpeed = 2.0f;
    const auto delta       = 1.0f / 60.0f;

    if( IsPressed( GLFW_KEY_UP ) )      ctl_CameraPitchYaw[ 0 ] += delta;
    if( IsPressed( GLFW_KEY_DOWN ) )    ctl_CameraPitchYaw[ 0 ] -= delta;
    if( IsPressed( GLFW_KEY_RIGHT ) )   ctl_CameraPitchYaw[ 1 ] -= delta;
    if( IsPressed( GLFW_KEY_LEFT ) )    ctl_CameraPitchYaw[ 1 ] += delta;
    const auto mat = glm::rotate( ctl_CameraPitchYaw[ 1 ], glm::vec3{ 0, 1, 0 } ) *
                     glm::rotate( ctl_CameraPitchYaw[ 0 ], glm::vec3{ 1, 0, 0 } );
    ctl_CameraDirection = glm::vec3{ mat * glm::vec4{ 0, 0, -1, 0 } };
    ctl_CameraUp        = glm::vec3{ mat * glm::vec4{ 0, 1, 0, 0 } };
    ctl_CameraRight     = glm::vec3{ mat * glm::vec4{ 1, 0, 0, 0 } };

    if( IsPressed( GLFW_KEY_W ) )       ctl_CameraPosition += delta * cameraSpeed * ctl_CameraDirection;
    if( IsPressed( GLFW_KEY_S ) )       ctl_CameraPosition -= delta * cameraSpeed * ctl_CameraDirection;
    if( IsPressed( GLFW_KEY_D ) )       ctl_CameraPosition += delta * cameraSpeed * ctl_CameraRight;
    if( IsPressed( GLFW_KEY_A ) )       ctl_CameraPosition -= delta * cameraSpeed * ctl_CameraRight;
    if( IsPressed( GLFW_KEY_E ) )       ctl_CameraPosition += delta * cameraSpeed * ctl_CameraUp;
    if( IsPressed( GLFW_KEY_Q ) )       ctl_CameraPosition -= delta * cameraSpeed * ctl_CameraUp;
    

    if( IsPressed( GLFW_KEY_KP_8 ) )    ctl_LightPosition[ 2 ] += delta * 5;
    if( IsPressed( GLFW_KEY_KP_5 ) )    ctl_LightPosition[ 2 ] -= delta * 5;
    if( IsPressed( GLFW_KEY_KP_6 ) )    ctl_LightPosition[ 0 ] += delta * 5;
    if( IsPressed( GLFW_KEY_KP_4 ) )    ctl_LightPosition[ 0 ] -= delta * 5;
    if( IsPressed( GLFW_KEY_KP_9 ) )    ctl_LightPosition[ 1 ] += delta * 5;
    if( IsPressed( GLFW_KEY_KP_7 ) )    ctl_LightPosition[ 1 ] -= delta * 5;

    ControlFloat( GLFW_KEY_R, ctl_Roughness,        delta,      0, 1 );
    ControlFloat( GLFW_KEY_M, ctl_Metallicity,      delta,      0, 1 );
    ControlFloat( GLFW_KEY_Y, ctl_LightIntensity,   delta,      0, 1000 );
    ControlFloat( GLFW_KEY_Y, ctl_LightCount,       delta * 5,  0, 1000 );
    ControlFloat( GLFW_KEY_I, ctl_SunIntensity,     delta,      0, 1000 );
    ControlFloat( GLFW_KEY_O, ctl_SkyIntensity,     delta,      0, 1000 );
    
    ControlSwitch( GLFW_KEY_TAB,        ctl_SkyboxEnable );
    ControlSwitch( GLFW_KEY_Z,          ctl_MoveBoxes );
}

double GetCurrentTimeInSeconds()
{
    static auto timeStart = std::chrono::system_clock::now();
    return double( std::chrono::duration_cast< std::chrono::milliseconds >( std::chrono::system_clock::now() - timeStart ).count() ) / 1000.0;
}

const RgFloat3D s_CubePositions[] = { 
    {-0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f}, { 0.5f,-0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f}, 
    { 0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f, 0.5f}, { 0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f}, { 0.5f,-0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f}, 
    { 0.5f,-0.5f, 0.5f}, {-0.5f,-0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f}, {-0.5f,-0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}, 
    {-0.5f,-0.5f, 0.5f}, {-0.5f,-0.5f,-0.5f}, {-0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}, {-0.5f,-0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f}, 
    {-0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f}, {-0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}, { 0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f, 0.5f}, 
    {-0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f, 0.5f}, {-0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f,-0.5f}, 
};
const RgFloat2D s_CubeTexCoords[] = {
    {0,0}, {1,0}, {0,1}, {0,1}, {0,0}, {1,0}, 
    {0,1}, {0,1}, {0,0}, {1,0}, {0,1}, {0,1}, 
    {0,0}, {1,0}, {0,1}, {0,1}, {0,0}, {1,0}, 
    {0,1}, {0,1}, {0,0}, {1,0}, {0,1}, {0,1}, 
    {0,0}, {1,0}, {0,1}, {0,1}, {0,0}, {1,0}, 
    {0,1}, {0,1}, {0,0}, {1,0}, {0,1}, {0,1}, 
};
const RgPrimitiveVertex* GetCubeVertices( RgColor4DPacked32 color )
{
    static RgPrimitiveVertex verts[ std::size( s_CubePositions ) ] = {};
    for( size_t i = 0; i < std::size( s_CubePositions ); i++ )
    {
        memcpy( verts[ i ].position, &s_CubePositions[ i ], 3 * sizeof( float ) );
        memcpy( verts[ i ].texCoord, &s_CubeTexCoords[ i ], 2 * sizeof( float ) );
        verts[ i ].color = color;
    }
    return verts;
}

const RgFloat3D s_QuadPositions[] = {
    {0,0,0}, {0,1,0}, {1, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0},
};
const RgFloat2D s_QuadTexCoords[] = {
    {0,0}, {0, 1}, {1, 0}, {1, 0}, {0, 1}, {1, 1},
};
const uint32_t s_QuadColorsABGR[] = {
    0xF0FF0000, 0xF0FFFFFF, 0xF0FFFFFF, 0xF0FFFFFF, 0xFFFFFFFF, 0xFF00FF00,
};
const RgPrimitiveVertex* GetQuadVertices()
{
    static RgPrimitiveVertex verts[ std::size( s_QuadPositions ) ] = {};
    for( size_t i = 0; i < std::size( s_QuadPositions ); i++ )
    {
        memcpy( verts[ i ].position, &s_QuadPositions[ i ], 3 * sizeof( float ) );
        memcpy( verts[ i ].texCoord, &s_QuadTexCoords[ i ], 2 * sizeof( float ) );
        verts[ i ].color = s_QuadColorsABGR[ i ];
    }
    return verts;
}
// clang-format on

uint32_t MurmurHash32( std::string_view str, uint32_t seed = 0 )
{
    const uint32_t m = 0x5bd1e995;
    const uint32_t r = 24;

    uint32_t len  = uint32_t( str.length() );
    uint32_t h    = seed ^ len;
    auto*    data = reinterpret_cast< const uint8_t* >( str.data() );

    while( len >= 4 )
    {
        unsigned int k = *reinterpret_cast< const uint32_t* >( data );

        k *= m;
        k ^= k >> r;
        k *= m;

        h *= m;
        h ^= k;

        data += 4;
        len -= 4;
    }

    if( len == 3 )
    {
        h ^= data[ 2 ] << 16;
    }

    if( len == 2 )
    {
        h ^= data[ 1 ] << 8;
    }

    if( len == 1 )
    {
        h ^= data[ 0 ];
        h *= m;
    }

    h ^= h >> 13;
    h *= m;
    h ^= h >> 15;

    return h;
}

using MeshName = std::string;
struct WorldMeshPrimitive
{
    std::vector< RgPrimitiveVertex > vertices;
    std::vector< uint32_t >          indices;
    std::string                      texture;
    uint32_t                         indexInMesh;
};
std::unordered_map< MeshName, std::pair< RgTransform, std::vector< WorldMeshPrimitive > > >
    g_allMeshes;

auto GetTexturePath( const std::filesystem::path& gltfFolder, std::string_view uri )
{
    return ( gltfFolder / uri ).string();
}

void ForEachGltfMesh( RgInterface&                 rt,
                      const std::filesystem::path& gltfFolder,
                      const tinygltf::Model&       model,
                      const tinygltf::Node&        node )
{
    if( node.mesh >= 0 && node.mesh < static_cast< int >( model.meshes.size() ) )
    {
        std::string_view meshName    = model.meshes[ node.mesh ].name;
        uint32_t         indexInMesh = 0;

        auto& [ dstTransform, dstPrimList ] = g_allMeshes[ meshName.data() ];

        {
            auto translation = node.translation.size() == 3
                                   ? glm::make_vec3( node.translation.data() )
                                   : glm::dvec3( 0.0 );
            auto rotation    = node.rotation.size() == 4 ? glm::make_quat( node.rotation.data() )
                                                         : glm::dmat4( 1.0 );
            auto scale =
                node.scale.size() == 3 ? glm::make_vec3( node.scale.data() ) : glm::dvec3( 1.0 );
            glm::dmat4 dtr;
            if( node.matrix.size() == 16 )
            {
                dtr = glm::make_mat4x4( node.matrix.data() );
            }
            else
            {
                dtr = glm::translate( glm::dmat4( 1.0 ), translation ) * glm::dmat4( rotation ) *
                      glm::scale( glm::dmat4( 1.0 ), scale );
            }
            auto tr = glm::mat4( dtr );

            dstTransform = RgTransform{ {
                { tr[ 0 ][ 0 ], tr[ 1 ][ 0 ], tr[ 2 ][ 0 ], tr[ 3 ][ 0 ] },
                { tr[ 0 ][ 1 ], tr[ 1 ][ 1 ], tr[ 2 ][ 1 ], tr[ 3 ][ 1 ] },
                { tr[ 0 ][ 2 ], tr[ 1 ][ 2 ], tr[ 2 ][ 2 ], tr[ 3 ][ 2 ] },
            } };
        }

        for( const auto& primitive : model.meshes[ node.mesh ].primitives )
        {
            std::vector< RgPrimitiveVertex > rgverts;
            std::vector< uint32_t >          rgindices;

            for( const auto& [ attribName, accessId ] : primitive.attributes )
            {
                auto& attribAccessor = model.accessors[ accessId ];
                auto& attribView     = model.bufferViews[ attribAccessor.bufferView ];
                auto& attribBuffer   = model.buffers[ attribView.buffer ];

                const uint8_t* data =
                    &attribBuffer.data[ attribAccessor.byteOffset + attribView.byteOffset ];
                int dataStride = attribAccessor.ByteStride( attribView );

                if( rgverts.empty() )
                {
                    rgverts.resize( attribAccessor.count );
                }
                assert( rgverts.size() == attribAccessor.count );

                if( attribName == "POSITION" )
                {
                    for( uint64_t i = 0; i < rgverts.size(); i++ )
                    {
                        const auto* src = reinterpret_cast< const float* >( data + i * dataStride );

                        rgverts[ i ].position[ 0 ] = src[ 0 ];
                        rgverts[ i ].position[ 1 ] = src[ 1 ];
                        rgverts[ i ].position[ 2 ] = src[ 2 ];
                    }
                }
                else if( attribName == "NORMAL" )
                {
                    for( uint64_t i = 0; i < rgverts.size(); i++ )
                    {
                        const auto* src = reinterpret_cast< const float* >( data + i * dataStride );

                        rgverts[ i ].normalPacked =
                            rt.rgUtilPackNormal( src[ 0 ], src[ 1 ], src[ 2 ] );
                    }
                }
                else if( attribName == "TEXCOORD_0" )
                {
                    for( uint64_t i = 0; i < rgverts.size(); i++ )
                    {
                        const auto* src = reinterpret_cast< const float* >( data + i * dataStride );

                        rgverts[ i ].texCoord[ 0 ] = src[ 0 ];
                        rgverts[ i ].texCoord[ 1 ] = src[ 1 ];
                    }
                }
            }

            for( auto& v : rgverts )
            {
                v.color = 0xFFFFFFFF;
            }

            {
                auto& indexAccessor = model.accessors[ primitive.indices ];
                auto& indexView     = model.bufferViews[ indexAccessor.bufferView ];
                auto& indexBuffer   = model.buffers[ indexView.buffer ];

                const uint8_t* data =
                    &indexBuffer.data[ indexAccessor.byteOffset + indexView.byteOffset ];
                int dataStride = indexAccessor.ByteStride( indexView );

                rgindices.resize( indexAccessor.count );

                for( uint64_t i = 0; i < rgindices.size(); i++ )
                {
                    uint32_t index = 0;

                    if( dataStride == sizeof( uint32_t ) )
                    {
                        index = *reinterpret_cast< const uint32_t* >( data + i * dataStride );
                    }
                    else if( dataStride == sizeof( uint16_t ) )
                    {
                        index = *reinterpret_cast< const uint16_t* >( data + i * dataStride );
                    }
                    else
                    {
                        assert( false );
                    }

                    rgindices[ i ] = index;
                }
            }

            std::string texName;
            {
                int tex = model.materials[ primitive.material ]
                              .pbrMetallicRoughness.baseColorTexture.index;
                if( tex >= 0 && model.textures[ tex ].source >= 0 )
                {
                    auto& image = model.images[ model.textures[ tex ].source ];
                    texName     = GetTexturePath( gltfFolder, image.uri );
                }
            }

            dstPrimList.push_back( WorldMeshPrimitive{
                .vertices    = std::move( rgverts ),
                .indices     = std::move( rgindices ),
                .texture     = std::move( texName ),
                .indexInMesh = indexInMesh++,
            } );
        }
    }

    for( int c : node.children )
    {
        assert( c >= 0 && c < static_cast< int >( model.nodes.size() ) );
        ForEachGltfMesh( rt, gltfFolder, model, model.nodes[ c ] );
    }
}

void FillGAllMeshes(
    RgInterface&                                                                   rt,
    std::string_view                                                               path,
    const std::function< void(
        const char* pTextureName, const void* pPixels, uint32_t w, uint32_t h ) >& materialFunc )
{
    const auto gltfFolder  = std::filesystem::path( path ).remove_filename();
    const auto absGltfPath = std::filesystem::path( ASSET_DIRECTORY ) / path;

    tinygltf::Model    model;
    tinygltf::TinyGLTF loader;
    std::string        err, warn;
    if( loader.LoadASCIIFromFile( &model, &err, &warn, absGltfPath.string() ) )
    {
        for( uint64_t m = 0; m < model.materials.size(); m++ )
        {
            const auto& gltfMat = model.materials[ m ];

            int itextures[] = {
                gltfMat.pbrMetallicRoughness.baseColorTexture.index,
                gltfMat.pbrMetallicRoughness.metallicRoughnessTexture.index,
                gltfMat.normalTexture.index,
            };

            for( int tex : itextures )
            {
                if( tex >= 0 && model.textures[ tex ].source >= 0 )
                {
                    auto& image = model.images[ model.textures[ tex ].source ];
                    assert( image.bits == 8 );

                    materialFunc( GetTexturePath( gltfFolder, image.uri ).c_str(),
                                  &image.image[ 0 ],
                                  uint32_t( image.width ),
                                  uint32_t( image.height ) );
                }
            }
        }

        const auto& scene = model.scenes[ model.defaultScene ];
        for( int sceneNode : scene.nodes )
        {
            ForEachGltfMesh( rt, gltfFolder, model, model.nodes[ sceneNode ] );
        }
    }
    else
    {
        std::cout << "Can't load GLTF. " << err << std::endl << warn << std::endl;
    }
}
}
#pragma endregion BOILERPLATE



void MainLoop( RgInterface& rt, std::string_view gltfPath )
{
    RgResult r       = RG_RESULT_SUCCESS;
    uint64_t frameId = 0;


    // some resources can be initialized out of frame
    {
        constexpr uint32_t white = 0xFFFFFFFF;

#if 0
        auto skyboxInfo = RgOriginalCubemapInfo{
            .sType            = RG_STRUCTURE_TYPE_ORIGINAL_CUBEMAP_INFO,
            .pNext            = nullptr,
            .pTextureName     = "_external_/cubemap/0",
            .pPixelsPositiveX = &white,
            .pPixelsNegativeX = &white,
            .pPixelsPositiveY = &white,
            .pPixelsNegativeY = &white,
            .pPixelsPositiveZ = &white,
            .pPixelsNegativeZ = &white,
            .sideSize         = 1,
        };
        r = rgProvideOriginalCubemapTexture( &skyboxInfo );
        RG_CHECK( r );
#endif


        auto uploadMaterial =
            [ &rt ]( const char* pTextureName, const void* pPixels, uint32_t w, uint32_t h ) {
                auto info = RgOriginalTextureInfo{
                    .sType        = RG_STRUCTURE_TYPE_ORIGINAL_TEXTURE_INFO,
                    .pTextureName = pTextureName,
                    .pPixels      = pPixels,
                    .size         = { w, h },
                };
                RgResult t = rt.rgProvideOriginalTexture( &info );
                RG_CHECK( t );
            };

        /* g_allMeshes = */ FillGAllMeshes( rt, gltfPath, uploadMaterial );
    }


    std::random_device rndDevice;
    std::mt19937       rnd( rndDevice() );


    while( ProcessWindow() )
    {
        ProcessInput();


        //Sleep( 20 );

        {
            auto resolution = RgStartFrameRenderResolutionParams{
                .sType            = RG_STRUCTURE_TYPE_START_FRAME_RENDER_RESOLUTION_PARAMS,
                .pNext            = nullptr,
                .upscaleTechnique = RG_RENDER_UPSCALE_TECHNIQUE_AMD_FSR2,
                .resolutionMode   = RG_RENDER_RESOLUTION_MODE_BALANCED,
                .preferDxgiPresent = 1,
            };

            auto startInfo = RgStartFrameInfo{
                .sType    = RG_STRUCTURE_TYPE_START_FRAME_INFO,
                .pNext    = &resolution,
                .pMapName = "untitled",
                .vsync    = true,
            };

            r = rt.rgStartFrame( &startInfo );
            RG_CHECK( r );
        }


        {
            auto camera = RgCameraInfo{
                .sType       = RG_STRUCTURE_TYPE_CAMERA_INFO,
                .pNext       = nullptr,
                .position    = { ctl_CameraPosition.x, ctl_CameraPosition.y, ctl_CameraPosition.z },
                .up          = { ctl_CameraUp.x, ctl_CameraUp.y, ctl_CameraUp.z },
                .right       = { ctl_CameraRight.x, ctl_CameraRight.y, ctl_CameraRight.z },
                .fovYRadians = glm::radians( 75.0f ),
                .aspect      = GetWindowAspect(),
                .cameraNear  = 0.1f,
                .cameraFar   = 10000.0f,
            };

            r = rt.rgUploadCamera( &camera );
            RG_CHECK( r );
        }


        for( auto& [ meshName, src ] : g_allMeshes )
        {
            auto& [ transform, primitives ] = src;

            auto objectName = std::string{ "obj_" } + meshName;

            auto mesh = RgMeshInfo{
                .sType          = RG_STRUCTURE_TYPE_MESH_INFO,
                .pNext          = nullptr,
                .uniqueObjectID = MurmurHash32( objectName ),
                .pMeshName      = objectName.c_str(), // meshName.c_str(),
                .transform      = transform,
                .isExportable   = true,
            };

            // random permutation, as primitive upload order must not influence the final image
            std::ranges::shuffle( primitives, rnd );

            for( const auto& srcPrim : primitives )
            {
                RgMeshPrimitiveInfo prim = {
                    .sType                = RG_STRUCTURE_TYPE_MESH_PRIMITIVE_INFO,
                    .pNext                = nullptr,
                    .flags                = 0,
                    .primitiveIndexInMesh = srcPrim.indexInMesh,
                    .pVertices            = srcPrim.vertices.data(),
                    .vertexCount          = uint32_t( srcPrim.vertices.size() ),
                    .pIndices             = srcPrim.indices.data(),
                    .indexCount           = uint32_t( srcPrim.indices.size() ),
                    .pTextureName         = srcPrim.texture.c_str(),
                    .textureFrame         = 0,
                    .color                = 0xFFFFFFFF,
                    .classicLight         = 1.0f,
                };

                r = rt.rgUploadMeshPrimitive( &mesh, &prim );
                RG_CHECK( r );
            }
        }


        {
            auto mesh = RgMeshInfo{
                .sType          = RG_STRUCTURE_TYPE_MESH_INFO,
                .pNext          = nullptr,
                .uniqueObjectID = 10,
                .pMeshName      = "test",
                .transform      = { {
                    { 1,
                           0,
                           0,
                      ctl_MoveBoxes ? 5.0f - 0.05f * float( ( frameId + 30 ) % 200 ) : 1.0f },
                    { 0, 1, 0, 1.0f },
                    { 0, 0, 1, 0.0f },
                } },
                .isExportable   = false,
            };

            auto prim = RgMeshPrimitiveInfo{
                .sType                = RG_STRUCTURE_TYPE_MESH_PRIMITIVE_INFO,
                .pNext                = nullptr,
                .flags                = 0,
                .primitiveIndexInMesh = 0,
                .pVertices    = GetCubeVertices( rt.rgUtilPackColorByte4D( 255, 255, 255, 255 ) ),
                .vertexCount  = std::size( s_CubePositions ),
                .pTextureName = nullptr,
                .textureFrame = 0,
                .color        = rt.rgUtilPackColorByte4D( 128, 255, 128, 128 ),
                .classicLight = 1.0f,
            };

            r = rt.rgUploadMeshPrimitive( &mesh, &prim );
            RG_CHECK( r );
        }


        // upload world-space rasterized geometry for non-expensive transparency
        {
            auto mesh = RgMeshInfo{
                .sType          = RG_STRUCTURE_TYPE_MESH_INFO,
                .pNext          = nullptr,
                .uniqueObjectID = 12,
                .pMeshName      = "test_raster",
                .transform      = { {
                    { 1, 0, 0, -0.5f },
                    { 0, 1, 0, 1.0f },
                    { 0, 0, 1, 1.0f },
                } },
                .isExportable   = false,
            };

            auto sw = RgMeshPrimitiveSwapchainedEXT{
                .sType           = RG_STRUCTURE_TYPE_MESH_PRIMITIVE_SWAPCHAINED_EXT,
                .pNext           = nullptr,
                .flags           = 0,
                .pViewport       = nullptr,
                .pView           = nullptr,
                .pProjection     = nullptr,
                .pViewProjection = nullptr,
            };

            auto prim = RgMeshPrimitiveInfo{
                .sType                = RG_STRUCTURE_TYPE_MESH_PRIMITIVE_INFO,
                .pNext                = &sw,
                .flags                = 0,
                .primitiveIndexInMesh = 0,
                .pVertices            = GetQuadVertices(),
                .vertexCount          = std::size( s_QuadPositions ),
                .pTextureName         = nullptr,
                .textureFrame         = 0,
                // alpha is not 1.0
                .color = rt.rgUtilPackColorByte4D( 255, 128, 128, 128 ),
                .classicLight = 1.0f,
            };

            r = rt.rgUploadMeshPrimitive( &mesh, &prim );
            RG_CHECK( r );
        }


        // set bounding box of the decal to modify G-buffer
        /*{
            RgDecalInfo decalInfo = {
                .transform = { {
                    { 1, 0, 0, 0 },
                    { 0, 1, 0, 0 },
                    { 0, 0, 1, 0 },
                } },
                .material  = RG_NO_MATERIAL,
            };
            r = rgUploadDecal( instance, &decalInfo );
            RG_CHECK( r );
        }*/


        // upload the sun
        {
            auto dirLight = RgLightDirectionalEXT{
                .sType                  = RG_STRUCTURE_TYPE_LIGHT_DIRECTIONAL_EXT,
                .pNext                  = nullptr,
                .color                  = rt.rgUtilPackColorByte4D( 255, 255, 255, 255 ),
                .intensity              = ctl_SunIntensity,
                .direction              = { -1, -8, -1 },
                .angularDiameterDegrees = 0.5f,
            };

            auto l = RgLightInfo{
                .sType        = RG_STRUCTURE_TYPE_LIGHT_INFO,
                .pNext        = &dirLight,
                .uniqueID     = 0,
                .isExportable = true,
            };

            r = rt.rgUploadLight( &l );
            RG_CHECK( r );
        }


        // upload sphere lights
        /*{
            uint32_t count = ( frameId % 2 ) * 64 + 128;

            for( uint64_t i = 0; i < count; i++ )
            {
                RgSphericalLightUploadInfo spherical = {
                    .uniqueID = i + 1,
                    .color    = { ctl_LightIntensity, ctl_LightIntensity, ctl_LightIntensity },
                    .position = { ctl_LightPosition[ 0 ] + i * 3,
                                  ctl_LightPosition[ 1 ],
                                  ctl_LightPosition[ 2 ] },
                    .radius   = 0.2f,
                };
                r = rgUploadSphericalLight( instance, &spherical );
                RG_CHECK( r );
            }
        }*/


        // submit the frame
        {
            auto chromaticAberration = RgPostEffectChromaticAberration{
                .isActive  = true,
                .intensity = 0.3f,
            };

            auto postEffects = RgDrawFramePostEffectsParams{
                .sType                = RG_STRUCTURE_TYPE_DRAW_FRAME_POST_EFFECTS_PARAMS,
                .pNext                = nullptr,
                .pChromaticAberration = &chromaticAberration,
            };

            auto sky = RgDrawFrameSkyParams{
                .sType              = RG_STRUCTURE_TYPE_DRAW_FRAME_SKY_PARAMS,
                .pNext              = &postEffects,
                .skyType            = ctl_SkyboxEnable ? RG_SKY_TYPE_CUBEMAP : RG_SKY_TYPE_COLOR,
                .skyColorDefault    = { 0.71f, 0.88f, 1.0f },
                .skyColorMultiplier = ctl_SkyIntensity,
                .skyColorSaturation = 1.0f,
                .skyViewerPosition  = { 0, 0, 0 },
            };
#if 0
                .pSkyCubemapTextureName = "_external_/cubemap/0",
#endif

            auto frameInfo = RgDrawFrameInfo{
                .sType            = RG_STRUCTURE_TYPE_DRAW_FRAME_INFO,
                .pNext            = &sky,
                .rayLength        = 10000.0f,
                .currentTime      = GetCurrentTimeInSeconds(),
            };

            r = rt.rgDrawFrame( &frameInfo );
            RG_CHECK( r );
        }


        frameId++;
    }
}


int main( int argc, char* argv[] )
{
    glfwInit();
    glfwWindowHint( GLFW_CLIENT_API, GLFW_NO_API );
    glfwWindowHint( GLFW_RESIZABLE, GLFW_TRUE );
    g_GlfwHandle = glfwCreateWindow( 1600, 900, "RTGL1 Test", nullptr, nullptr );


    auto r  = RgResult{};
    auto rt = RgInterface{};

#ifdef _WIN32
    auto win32Info = RgWin32SurfaceCreateInfo{
        .hinstance = GetModuleHandle( NULL ),
        .hwnd      = glfwGetWin32Window( g_GlfwHandle ),
    };
#else
    RgXlibSurfaceCreateInfo xlibInfo = {
        .dpy    = glfwGetX11Display(),
        .window = glfwGetX11Window( g_GlfwHandle ),
    };
#endif

    auto info = RgInstanceCreateInfo{
        .sType = RG_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,

        .version           = RG_RTGL_VERSION_API,
        .sizeOfRgInterface = sizeof( RgInterface ),

        .pAppName = "RTGL1 Test",
        .pAppGUID = "459d6734-62a6-4d47-927a-bedcdb0445c5",

#ifdef _WIN32
        .pWin32SurfaceInfo = &win32Info,
#else
        .pXlibSurfaceCreateInfo = &xlibInfo,
#endif

        .pOverrideFolderPath = ASSET_DIRECTORY,

        .pfnPrint = []( const char*            pMessage,
                        RgMessageSeverityFlags severity,
                        void*                  pUserData ) { std::cout << pMessage << std::endl; },
        .allowedMessages = RG_MESSAGE_SEVERITY_VERBOSE | RG_MESSAGE_SEVERITY_INFO |
                           RG_MESSAGE_SEVERITY_WARNING | RG_MESSAGE_SEVERITY_ERROR,

        .primaryRaysMaxAlbedoLayers          = 1,
        .indirectIlluminationMaxAlbedoLayers = 1,

        .rayCullBackFacingTriangles = false,

        .allowTexCoordLayer1        = false,
        .allowTexCoordLayer2        = false,
        .allowTexCoordLayer3        = false,
        .lightmapTexCoordLayerIndex = 1,

        .rasterizedMaxVertexCount = 1 << 24,
        .rasterizedMaxIndexCount  = 1 << 25,

        .rasterizedSkyCubemapSize = 256,

        // to match the GLTF standard
        .pbrTextureSwizzling = RG_TEXTURE_SWIZZLING_NULL_ROUGHNESS_METALLIC,

        .worldUp      = { 0, 1, 0 },
        .worldForward = { 0, 0, 1 },
        .worldScale   = 1.0f,
    };

#ifndef NDEBUG
    constexpr bool isdebug = true;
#else
    constexpr bool isdebug = false;
#endif

#ifdef _WIN32
    HMODULE rtDll = nullptr;
#else
    void* rtDll = nullptr;
#endif

    r = rgLoadLibraryAndCreate( &info, isdebug, nullptr, & rt, &rtDll );
    RG_CHECK( r );

    {
        auto gltfPath = argc > 1 ? argv[ 1 ] : "_external_/Sponza/glTF/Sponza.gltf";
        MainLoop( rt, gltfPath );
    }

    r = rgDestroyAndUnloadLibrary( &rt, rtDll );
    RG_CHECK( r );


    glfwDestroyWindow( g_GlfwHandle );
    glfwTerminate();

    return 0;
}