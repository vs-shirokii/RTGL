// Copyright (c) 2021 Sultim Tsyrendashiev
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

#include "LightManager.h"

#include <cmath>
#include <array>

#include "Generated/ShaderCommonC.h"
#include "CmdLabel.h"
#include "RgException.h"
#include "Utils.h"

namespace
{
constexpr double RG_PI = 3.1415926535897932384626433;

constexpr float MIN_COLOR_SUM     = 0.0001f;
constexpr float MIN_SPHERE_RADIUS = 0.005f;

constexpr uint32_t LIGHT_ARRAY_MAX_SIZE = 4096;

static_assert( LIGHT_GRID_ENABLED_ == LIGHT_GRID_ENABLED, "Change LIGHT_GRID_ENABLED_" );
#if LIGHT_GRID_ENABLED
constexpr VkDeviceSize GRID_LIGHTS_COUNT =
    LIGHT_GRID_CELL_SIZE * ( LIGHT_GRID_SIZE_X * LIGHT_GRID_SIZE_Y * LIGHT_GRID_SIZE_Z );
#endif

}

static uint32_t encodeE5( const RgFloat3D& c, float* normalization );
static uint32_t packHalf2x16( const float& x, const float& y );

RTGL1::LightManager::LightManager( VkDevice                            _device,
                                   std::shared_ptr< MemoryAllocator >& _allocator )
    : device( _device )
    , regLightCount( 0 )
    , regLightCount_Prev( 0 )
    , dirLightCount( 0 )
    , dirLightCount_Prev( 0 )
    , descSetLayout( VK_NULL_HANDLE )
    , descPool( VK_NULL_HANDLE )
    , descSets{}
    , needDescSetUpdate{}
{
    lightsBuffer = std::make_shared< AutoBuffer >( _allocator );
    lightsBuffer->Create( sizeof( ShLightEncoded ) * LIGHT_ARRAY_MAX_SIZE,
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          "Lights buffer" );

    lightsBuffer_Prev.Init( *_allocator,
                            sizeof( ShLightEncoded ) * LIGHT_ARRAY_MAX_SIZE,
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                            "Lights buffer - prev" );

#if LIGHT_GRID_ENABLED
    for( auto& buf : initialLightsGrid )
    {
        buf.Init( *_allocator,
                  sizeof( ShLightInCell ) * GRID_LIGHTS_COUNT,
                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                  "Lights grid" );
    }
#endif

    prevToCurIndex = std::make_shared< AutoBuffer >( _allocator );
    prevToCurIndex->Create( sizeof( uint32_t ) * LIGHT_ARRAY_MAX_SIZE,
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                            "Lights buffer - prev to cur" );

    curToPrevIndex = std::make_shared< AutoBuffer >( _allocator );
    curToPrevIndex->Create( sizeof( uint32_t ) * LIGHT_ARRAY_MAX_SIZE,
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                            "Lights buffer - cur to prev" );

    CreateDescriptors();
}

RTGL1::LightManager::~LightManager()
{
    vkDestroyDescriptorSetLayout( device, descSetLayout, nullptr );
    vkDestroyDescriptorPool( device, descPool, nullptr );
}

namespace
{

static_assert( sizeof( RTGL1::ShLightEncoded ) == 24, "Change encoding" );


RTGL1::ShLightEncoded EncodeAsDirectionalLight( const RgLightDirectionalEXT& info,
                                                float                        mult,
                                                const RgTransform*           transform )
{
    assert( !transform ); // not expected

    RgFloat3D direction = RTGL1::Utils::Normalize( info.direction );

    float angularRadius = 0.5f * RTGL1::Utils::DegToRad( info.angularDiameterDegrees );
    auto  fcolor        = RTGL1::Utils::UnpackColor4DPacked32< RgFloat3D >( info.color );
    {
        fcolor.data[ 0 ] *= info.intensity* mult;
        fcolor.data[ 1 ] *= info.intensity* mult;
        fcolor.data[ 2 ] *= info.intensity* mult;
    }


    RTGL1::ShLightEncoded lt;

    lt.lightType = LIGHT_TYPE_DIRECTIONAL;

    float norm;
    lt.colorE5 = encodeE5( fcolor, &norm );
    assert( norm <= 1.05f );

    lt.ldata0 = direction.data[ 0 ];
    lt.ldata1 = direction.data[ 1 ];
    lt.ldata2 = direction.data[ 2 ];

    lt.ldata3 = angularRadius;

    return lt;
}

RTGL1::ShLightEncoded EncodeAsSphereLight( const RgLightSphericalEXT& info,
                                           float                      mult,
                                           const RgTransform*         transform )
{
    RgFloat3D pos = RTGL1::ApplyTransformToPosition( transform, info.position );

    float radius = std::max( MIN_SPHERE_RADIUS, info.radius );
    // disk is visible from the point
    float area = float( RG_PI ) * radius * radius;

    auto fcolor = RTGL1::Utils::UnpackColor4DPacked32< RgFloat3D >( info.color );
    {
        fcolor.data[ 0 ] *= info.intensity / area * mult;
        fcolor.data[ 1 ] *= info.intensity / area * mult;
        fcolor.data[ 2 ] *= info.intensity / area * mult;
    }


    RTGL1::ShLightEncoded lt;

    lt.lightType = LIGHT_TYPE_SPHERE;

    float norm;
    lt.colorE5 = encodeE5( fcolor, &norm );

    lt.ldata0 = pos.data[ 0 ];
    lt.ldata1 = pos.data[ 1 ];
    lt.ldata2 = pos.data[ 2 ];

    {
        uint32_t* rn = reinterpret_cast< uint32_t* >( &lt.ldata3 );

        *rn = packHalf2x16(
            radius, //
            norm    // additional multiplier as e5 encoding might not preserve large values
        );
    }

    return lt;
}

#if TRIANGLE_LIGHTS
RTGL1::ShLightEncoded EncodeAsTriangleLight( const RgLightPolygonalEXT& info,
                                             const RgFloat3D&           unnormalizedNormal,
                                             float                      mult,
                                             const RgTransform*         transform )
{
    assert( !transform ); // not implemented

    RgFloat3D n   = unnormalizedNormal;
    float     len = RTGL1::Utils::Length( n.data );
    n.data[ 0 ] /= len;
    n.data[ 1 ] /= len;
    n.data[ 2 ] /= len;

    float area = len * 0.5f;
    assert( area > 0.0f );

    auto fcolor = RTGL1::Utils::UnpackColor4DPacked32< RgFloat3D >( info.color );


    RTGL1::ShLightEncoded lt = {};
    lt.lightType             = LIGHT_TYPE_TRIANGLE;

    lt.color[ 0 ] = fcolor.data[ 0 ] * info.intensity / area * mult;
    lt.color[ 1 ] = fcolor.data[ 1 ] * info.intensity / area * mult;
    lt.color[ 2 ] = fcolor.data[ 2 ] * info.intensity / area * mult;

    lt.data_0[ 0 ] = info.positions[ 0 ].data[ 0 ];
    lt.data_0[ 1 ] = info.positions[ 0 ].data[ 1 ];
    lt.data_0[ 2 ] = info.positions[ 0 ].data[ 2 ];

    lt.data_1[ 0 ] = info.positions[ 1 ].data[ 0 ];
    lt.data_1[ 1 ] = info.positions[ 1 ].data[ 1 ];
    lt.data_1[ 2 ] = info.positions[ 1 ].data[ 2 ];

    lt.data_2[ 0 ] = info.positions[ 2 ].data[ 0 ];
    lt.data_2[ 1 ] = info.positions[ 2 ].data[ 1 ];
    lt.data_2[ 2 ] = info.positions[ 2 ].data[ 2 ];

    lt.data_0[ 3 ] = unnormalizedNormal.data[ 0 ];
    lt.data_1[ 3 ] = unnormalizedNormal.data[ 1 ];
    lt.data_2[ 3 ] = unnormalizedNormal.data[ 2 ];

    return lt;
}
#endif

RTGL1::ShLightEncoded EncodeAsSpotLight( const RgLightSpotEXT& info,
                                         float                 mult,
                                         const RgTransform*    transform )
{
    RgFloat3D pos = RTGL1::ApplyTransformToPosition( transform, info.position );

    RgFloat3D direction =
        RTGL1::ApplyTransformToDirection( transform, RTGL1::Utils::Normalize( info.direction ) );
    assert( std::abs( RTGL1::Utils::Length( direction.data ) - 1.0f ) < 0.001f );

    float radius = std::max( MIN_SPHERE_RADIUS, info.radius );
    float area   = float( RG_PI ) * radius * radius;

    constexpr auto clampForCos = []( float a ) {
        return std::clamp( a, 0.0f, RTGL1::Utils::DegToRad( 89 ) );
    };

    constexpr auto float01to8bit = []( float a ){
        assert( a >= 0 && a <= 1 );
        return uint8_t( std::clamp( int( a * 255 ), 0, 255 ) );
    };

    float angleInner = std::min( info.angleInner, info.angleOuter - RTGL1::Utils::DegToRad( 1 ) );
    float angleOuter = info.angleOuter;

    uint8_t cosAngleInner = float01to8bit( std::cos( clampForCos( angleInner ) ) );
    uint8_t cosAngleOuter = float01to8bit( std::cos( clampForCos( angleOuter ) ) );

    auto fcolor = RTGL1::Utils::UnpackColor4DPacked32< RgFloat3D >( info.color );
    {
        fcolor.data[ 0 ] *= info.intensity / area * mult;
        fcolor.data[ 1 ] *= info.intensity / area * mult;
        fcolor.data[ 2 ] *= info.intensity / area * mult;
    }


    RTGL1::ShLightEncoded lt;

    lt.lightType = LIGHT_TYPE_SPOT;

    float norm;
    lt.colorE5 = encodeE5( fcolor, &norm );
    
    {
        uint32_t* data0_x = reinterpret_cast< uint32_t* >( &lt.ldata0 );
        uint32_t* data0_y = reinterpret_cast< uint32_t* >( &lt.ldata1 );

        *data0_x = packHalf2x16( pos.data[ 0 ], pos.data[ 1 ] );
        *data0_y = packHalf2x16( pos.data[ 2 ], norm );
    }
    {
        uint32_t* data0_z = reinterpret_cast< uint32_t* >( &lt.ldata2 );
        uint32_t* data0_w = reinterpret_cast< uint32_t* >( &lt.ldata3 );

        *data0_z = packHalf2x16( direction.data[ 0 ], direction.data[ 1 ] );
        *data0_w = packHalf2x16( 0, direction.data[ 2 ] );

        assert( ( ( *data0_w ) & 0x0000FFFF ) == 0 );
        *data0_w = ( *data0_w & 0xFFFF0000 ) | //
                   ( cosAngleInner << 8 ) |    //
                   ( cosAngleOuter );
    }

    return lt;
}

uint32_t GetLightArrayEnd( uint32_t regCount, uint32_t dirCount )
{
    // assuming that reg lights are always after directional ones
    return LIGHT_ARRAY_REGULAR_LIGHTS_OFFSET + regCount;
}

}

void RTGL1::LightManager::PrepareForFrame( VkCommandBuffer cmd, uint32_t frameIndex )
{
    regLightCount_Prev = regLightCount;
    dirLightCount_Prev = dirLightCount;

    regLightCount = 0;
    dirLightCount = 0;

    // TODO: similar system to just swap desc sets, instead of actual copying
    if( GetLightArrayEnd( regLightCount_Prev, dirLightCount_Prev ) > 0 )
    {
        VkBufferCopy info = {
            .srcOffset = 0,
            .dstOffset = 0,
            .size      = GetLightArrayEnd( regLightCount_Prev, dirLightCount_Prev ) *
                    sizeof( ShLightEncoded ),
        };

        vkCmdCopyBuffer(
            cmd, lightsBuffer->GetDeviceLocal(), lightsBuffer_Prev.GetBuffer(), 1, &info );
    }

    memset( prevToCurIndex->GetMapped( frameIndex ),
            0xFF,
            sizeof( uint32_t ) * GetLightArrayEnd( regLightCount_Prev, dirLightCount_Prev ) );
    // no need to clear curToPrevIndex, as it'll be filled in the cur frame

    uniqueIDToArrayIndex[ frameIndex ].clear();
}

void RTGL1::LightManager::Reset()
{
    for( uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++ )
    {
        memset( prevToCurIndex->GetMapped( i ),
                0xFF,
                sizeof( uint32_t ) *
                    std::max( GetLightArrayEnd( regLightCount, dirLightCount ),
                              GetLightArrayEnd( regLightCount_Prev, dirLightCount_Prev ) ) );
        memset( curToPrevIndex->GetMapped( i ),
                0xFF,
                sizeof( uint32_t ) *
                    std::max( GetLightArrayEnd( regLightCount, dirLightCount ),
                              GetLightArrayEnd( regLightCount_Prev, dirLightCount_Prev ) ) );

        uniqueIDToArrayIndex[ i ].clear();
    }

    regLightCount_Prev = regLightCount = 0;
    dirLightCount_Prev = dirLightCount = 0;
}

RTGL1::LightArrayIndex RTGL1::LightManager::GetIndex( const ShLightEncoded& encodedLight ) const
{
    switch( encodedLight.lightType )
    {
        case LIGHT_TYPE_DIRECTIONAL:
            return LightArrayIndex{ LIGHT_ARRAY_DIRECTIONAL_LIGHT_OFFSET + dirLightCount };
        case LIGHT_TYPE_SPHERE:
        case LIGHT_TYPE_TRIANGLE:
        case LIGHT_TYPE_SPOT:
            return LightArrayIndex{ LIGHT_ARRAY_REGULAR_LIGHTS_OFFSET + regLightCount };
        default: assert( 0 ); return LightArrayIndex{ 0 };
    }
}

void RTGL1::LightManager::IncrementCount( const ShLightEncoded& encodedLight )
{
    switch( encodedLight.lightType )
    {
        case LIGHT_TYPE_DIRECTIONAL: dirLightCount++; break;
        case LIGHT_TYPE_SPHERE:
        case LIGHT_TYPE_TRIANGLE:
        case LIGHT_TYPE_SPOT: regLightCount++; break;
        default: assert( 0 );
    }
}

void RTGL1::LightManager::AddInternal( uint32_t              frameIndex,
                                       uint64_t              uniqueId,
                                       const ShLightEncoded& encodedLight )
{
    if( GetLightArrayEnd( regLightCount, dirLightCount ) >= LIGHT_ARRAY_MAX_SIZE )
    {
        assert( 0 );
        return;
    }

    const LightArrayIndex index = GetIndex( encodedLight );
    IncrementCount( encodedLight );

    auto* dst = lightsBuffer->GetMappedAs< ShLightEncoded* >( frameIndex );
    memcpy( &dst[ index.GetArrayIndex() ], &encodedLight, sizeof( ShLightEncoded ) );


    FillMatchPrev( frameIndex, index, uniqueId );
    // must be unique
    assert( uniqueIDToArrayIndex[ frameIndex ].find( uniqueId ) ==
            uniqueIDToArrayIndex[ frameIndex ].end() );
    // save index for the next frame
    uniqueIDToArrayIndex[ frameIndex ][ uniqueId ] = index;
}

namespace
{

template< typename T >
bool IsLightColorTooDim( const T& l )
{
    if( l.intensity <= 0.00001f )
    {
        return true;
    }

    if( RTGL1::Utils::IsColor4DPacked32Zero< false >( l.color ) )
    {
        return true;
    }

    return false;
}

float CalculateLightStyle( const std::optional< RgLightAdditionalEXT >& extra,
                           std::span< const uint8_t >                   lightstyles )
{
    if( extra && ( extra->flags & RG_LIGHT_ADDITIONAL_LIGHTSTYLE ) )
    {
        if( extra->lightstyle >= 0 && size_t( extra->lightstyle ) < lightstyles.size() )
        {
            return float( lightstyles[ extra->lightstyle ] ) / 255.0f;
        }
        else
        {
            assert( 0 );
        }
    }
    return 1.0f;
}

}

void RTGL1::LightManager::Add( uint32_t           frameIndex,
                               const LightCopy&   light,
                               const RgTransform* transform )
{
    std::visit(
        ext::overloaded{
            [ & ]( const RgLightDirectionalEXT& lext ) {
                if( IsLightColorTooDim( lext ) )
                {
                    return;
                }

                if( dirLightCount > 0 )
                {
                    debug::Error( "Only one directional light is allowed" );
                    return;
                }

                AddInternal(
                    frameIndex,
                    light.base.uniqueID,
                    EncodeAsDirectionalLight(
                        lext, CalculateLightStyle( light.additional, lightstyles ), transform ) );
            },
            [ & ]( const RgLightSphericalEXT& lext ) {
                if( IsLightColorTooDim( lext ) )
                {
                    return;
                }

                AddInternal(
                    frameIndex,
                    light.base.uniqueID,
                    EncodeAsSphereLight(
                        lext, CalculateLightStyle( light.additional, lightstyles ), transform ) );
            },
            [ & ]( const RgLightSpotEXT& lext ) {
                if( IsLightColorTooDim( lext ) )
                {
                    return;
                }

                AddInternal(
                    frameIndex,
                    light.base.uniqueID,
                    EncodeAsSpotLight(
                        lext, CalculateLightStyle( light.additional, lightstyles ), transform ) );
            },
            [ & ]( const RgLightPolygonalEXT& lext ) {
#if TRIANGLE_LIGHTS
                if( IsLightColorTooDim( lext ) )
                {
                    return;
                }

                RgFloat3D unnormalizedNormal = Utils::GetUnnormalizedNormal( lext.positions );
                if( Utils::Dot( unnormalizedNormal.data, unnormalizedNormal.data ) <= 0.0f )
                {
                    return;
                }

                AddInternal(
                    frameIndex,
                    light.base.uniqueID,
                    EncodeAsTriangleLight( lext,
                                           unnormalizedNormal,
                                           CalculateLightStyle( light.additional, lightstyles ),
                                           transform ) );
#else
                debug::Error( "Polygonal / triangle lights are not supported" );
#endif
            },
        },
        light.extension );
}

void RTGL1::LightManager::SubmitForFrame( VkCommandBuffer cmd, uint32_t frameIndex )
{
    CmdLabel label( cmd, "Copying lights" );

    lightsBuffer->CopyFromStaging( cmd,
                                   frameIndex,
                                   sizeof( ShLightEncoded ) *
                                       GetLightArrayEnd( regLightCount, dirLightCount ) );

    prevToCurIndex->CopyFromStaging(
        cmd,
        frameIndex,
        sizeof( uint32_t ) * GetLightArrayEnd( regLightCount_Prev, dirLightCount_Prev ) );
    curToPrevIndex->CopyFromStaging(
        cmd, frameIndex, sizeof( uint32_t ) * GetLightArrayEnd( regLightCount, dirLightCount ) );

    // should be used when buffers changed
    if( needDescSetUpdate[ frameIndex ] )
    {
        UpdateDescriptors( frameIndex );
        needDescSetUpdate[ frameIndex ] = false;
    }
}

void RTGL1::LightManager::BarrierLightGrid( VkCommandBuffer cmd, uint32_t frameIndex )
{
#if LIGHT_GRID_ENABLED
    VkBufferMemoryBarrier2 barrier = {
        .sType         = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .pNext         = nullptr,
        .srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        .dstStageMask =
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
        .dstAccessMask       = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
        .srcQueueFamilyIndex = 0,
        .dstQueueFamilyIndex = 0,
        .buffer              = initialLightsGrid[ frameIndex ].GetBuffer(),
        .offset              = 0,
        .size                = VK_WHOLE_SIZE,
    };

    VkDependencyInfo dependency = {
        .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .bufferMemoryBarrierCount = 1,
        .pBufferMemoryBarriers    = &barrier,
    };

    svkCmdPipelineBarrier2KHR( cmd, &dependency );
#endif
}

VkDescriptorSetLayout RTGL1::LightManager::GetDescSetLayout() const
{
    return descSetLayout;
}

VkDescriptorSet RTGL1::LightManager::GetDescSet( uint32_t frameIndex ) const
{
    return descSets[ frameIndex ];
}

void RTGL1::LightManager::FillMatchPrev( uint32_t        curFrameIndex,
                                         LightArrayIndex lightIndexInCurFrame,
                                         UniqueLightID   uniqueID )
{
    uint32_t prevFrame = ( curFrameIndex + 1 ) % MAX_FRAMES_IN_FLIGHT;
    const rgl::unordered_map< UniqueLightID, LightArrayIndex >& uniqueToPrevIndex =
        uniqueIDToArrayIndex[ prevFrame ];

    auto found = uniqueToPrevIndex.find( uniqueID );
    if( found == uniqueToPrevIndex.end() )
    {
        return;
    }

    LightArrayIndex lightIndexInPrevFrame = found->second;

    auto* prev2cur = prevToCurIndex->GetMappedAs< uint32_t* >( curFrameIndex );
    prev2cur[ lightIndexInPrevFrame.GetArrayIndex() ] = lightIndexInCurFrame.GetArrayIndex();

    auto* cur2prev = curToPrevIndex->GetMappedAs< uint32_t* >( curFrameIndex );
    cur2prev[ lightIndexInCurFrame.GetArrayIndex() ] = lightIndexInPrevFrame.GetArrayIndex();
}

constexpr uint32_t BINDINGS[] = {
    BINDING_LIGHT_SOURCES,
    BINDING_LIGHT_SOURCES_PREV,
    BINDING_LIGHT_SOURCES_INDEX_PREV_TO_CUR,
    BINDING_LIGHT_SOURCES_INDEX_CUR_TO_PREV,
#if LIGHT_GRID_ENABLED
    BINDING_INITIAL_LIGHTS_GRID,
    BINDING_INITIAL_LIGHTS_GRID_PREV,
#endif
};

void RTGL1::LightManager::CreateDescriptors()
{
    {
        std::array< VkDescriptorSetLayoutBinding, std::size( BINDINGS ) > bindings = {};

        for( uint32_t i = 0; i < std::size( BINDINGS ); i++ )
        {
            uint32_t bnd = BINDINGS[ i ];
            assert( i == bnd );

            VkDescriptorSetLayoutBinding& b = bindings[ bnd ];
            b.binding                       = bnd;
            b.descriptorType                = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            b.descriptorCount               = 1;
            b.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_COMPUTE_BIT;
        }

        VkDescriptorSetLayoutCreateInfo layoutInfo = {
            .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = bindings.size(),
            .pBindings    = bindings.data(),
        };

        VkResult r = vkCreateDescriptorSetLayout( device, &layoutInfo, nullptr, &descSetLayout );

        VK_CHECKERROR( r );
        SET_DEBUG_NAME( device,
                        descSetLayout,
                        VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                        "Light buffers Desc set layout" );
    }
    {
        VkDescriptorPoolSize poolSize = {
            .type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = std::size( BINDINGS ) * MAX_FRAMES_IN_FLIGHT,
        };

        VkDescriptorPoolCreateInfo poolInfo = {
            .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets       = MAX_FRAMES_IN_FLIGHT,
            .poolSizeCount = 1,
            .pPoolSizes    = &poolSize,
        };

        VkResult r = vkCreateDescriptorPool( device, &poolInfo, nullptr, &descPool );

        VK_CHECKERROR( r );
        SET_DEBUG_NAME(
            device, descPool, VK_OBJECT_TYPE_DESCRIPTOR_POOL, "Light buffers Desc set pool" );
    }
    {
        VkDescriptorSetAllocateInfo allocInfo = {
            .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool     = descPool,
            .descriptorSetCount = 1,
            .pSetLayouts        = &descSetLayout,
        };

        for( uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++ )
        {
            VkResult r = vkAllocateDescriptorSets( device, &allocInfo, &descSets[ i ] );

            VK_CHECKERROR( r );
            SET_DEBUG_NAME(
                device, descSets[ i ], VK_OBJECT_TYPE_DESCRIPTOR_SET, "Light buffers Desc set" );
        }

        for( uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++ )
        {
            UpdateDescriptors( i );
        }
    }
}

void RTGL1::LightManager::UpdateDescriptors( uint32_t frameIndex )
{
    const VkBuffer buffers[] = {
        lightsBuffer->GetDeviceLocal(),
        lightsBuffer_Prev.GetBuffer(),
        prevToCurIndex->GetDeviceLocal(),
        curToPrevIndex->GetDeviceLocal(),
#if LIGHT_GRID_ENABLED
        initialLightsGrid[ frameIndex ].GetBuffer(),
        initialLightsGrid[ Utils::GetPreviousByModulo( frameIndex, MAX_FRAMES_IN_FLIGHT ) ]
            .GetBuffer(),
#endif
    };
    static_assert( std::size( BINDINGS ) == std::size( buffers ) );

    std::array< VkDescriptorBufferInfo, std::size( BINDINGS ) > bufs = {};
    std::array< VkWriteDescriptorSet, std::size( BINDINGS ) >   wrts = {};

    for( uint32_t i = 0; i < std::size( BINDINGS ); i++ )
    {
        uint32_t bnd = BINDINGS[ i ];
        // 'buffers' should be actually a map (binding->buffer), but a plain array works too, if
        // this is true
        assert( i == bnd );

        bufs[ bnd ] = VkDescriptorBufferInfo{
            .buffer = buffers[ bnd ],
            .offset = 0,
            .range  = VK_WHOLE_SIZE,
        };

        wrts[ bnd ] = VkWriteDescriptorSet{
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = descSets[ frameIndex ],
            .dstBinding      = bnd,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo     = &bufs[ bnd ],
        };
    }

    vkUpdateDescriptorSets( device, wrts.size(), wrts.data(), 0, nullptr );
}

uint32_t RTGL1::LightManager::GetLightCount() const
{
    return regLightCount;
}

uint32_t RTGL1::LightManager::GetLightCountPrev() const
{
    return regLightCount_Prev;
}


uint32_t RTGL1::LightManager::DoesDirectionalLightExist() const
{
    return dirLightCount > 0 ? 1 : 0;
}

uint32_t RTGL1::LightManager::GetLightIndexForShaders( uint32_t        frameIndex,
                                                       const uint64_t* pLightUniqueId ) const
{
    if( pLightUniqueId == nullptr )
    {
        return LIGHT_INDEX_NONE;
    }
    UniqueLightID uniqueId = { *pLightUniqueId };

    const auto f = uniqueIDToArrayIndex[ frameIndex ].find( uniqueId );
    if( f == uniqueIDToArrayIndex[ frameIndex ].end() )
    {
        return LIGHT_INDEX_NONE;
    }

    return f->second.GetArrayIndex();
}

auto RTGL1::LightManager::TryGetVolumetricLight( const RgFloat3D&                 cameraPos,
                                                 const std::vector< LightCopy >&  from,
                                                 const std::optional< uint64_t >& fallback ) const
    -> std::optional< uint64_t >
{
    static auto l_isVolumetric = []( const LightCopy& l ) {
        return l.additional && ( l.additional->flags & RG_LIGHT_ADDITIONAL_VOLUMETRIC );
    };

    auto l_approxVolumetricIntensity = [ this ]( const LightCopy& l ) {
        assert( l_isVolumetric( l ) );

        float intensity =
            std::visit( []( const auto& lext ) { return lext.intensity; }, l.extension );

        return intensity * CalculateLightStyle( l.additional, lightstyles );
    };
    auto l_approxDistanceSq = []( const LightCopy& l, const RgFloat3D& from ) -> float {
        // clang-format off
        return std::visit( ext::overloaded{
            [ & ]( const RgLightDirectionalEXT& lext ) { return 0.f; },
            [ & ]( const RgLightSphericalEXT&   lext ) { return Utils::SqrDistanceR( lext.position, from ); },
            [ & ]( const RgLightSpotEXT&        lext ) { return Utils::SqrDistanceR( lext.position, from ); },
            [ & ]( const RgLightPolygonalEXT&   lext ) { assert( 0 ); return -1.f; },
        }, l.extension );
        // clang-format on
    };

    struct Candidate
    {
        uint64_t id;
        float    distanceSq;
    };
    auto best = std::optional< Candidate >{};
    auto any  = std::optional< Candidate >{};

    for( const auto& l : from )
    {
        if( !l_isVolumetric( l ) )
        {
            continue;
        }
        
        if( l_approxVolumetricIntensity( l ) > 0.0f )
        {
            float distSq = l_approxDistanceSq( l, cameraPos );
            if( distSq >= 0 )
            {
                // choose closest
                if( !best || distSq < best->distanceSq )
                {
                    best = Candidate{
                        .id         = l.base.uniqueID,
                        .distanceSq = distSq,
                    };
                }
            }
        }

        if( !any )
        {
            any = Candidate{
                .id         = l.base.uniqueID,
                .distanceSq = 0,
            };
        }
    }

    if( best )
    {
        return best->id;
    }

    // SHIPPING_HACK: don't fallback to sun, if at least
    // one light is marked as isVolumetric, but has 0 intensity
    if( any )
    {
        return any->id;
    }

    // if nothing, just try find the sun in the provided list
    for( const auto& l : from )
    {
        if( auto sun = std::get_if< RgLightDirectionalEXT >( &l.extension ) )
        {
            return l.base.uniqueID;
        }
    }

    return fallback;
}

void RTGL1::LightManager::SetLightstyles( const RgStartFrameInfo& params )
{
    if( !params.pLightstyleValues8 || params.lightstyleValuesCount == 0 )
    {
        return;
    }

    auto values = std::span{ params.pLightstyleValues8, params.lightstyleValuesCount };
    lightstyles.assign( values.begin(), values.end() );
}

static_assert( RTGL1::MAX_FRAMES_IN_FLIGHT == 2 );


#include "glm/glm.hpp"

namespace
{

using namespace glm;
#include "Shaders/Utils.h"

}

static uint32_t encodeE5( const RgFloat3D& c, float* normalization )
{
    assert( normalization );

    static_assert( sizeof( vec3 ) == sizeof( RgFloat3D ) );
    const vec3& l = *reinterpret_cast< const vec3* >( &c );

    const float norm = std::max( { l.x, l.y, l.z } ) / float{ ENCODE_E5B9G9R9_SHAREDEXP_MAX };

    uint32_t r;
    if( norm <= 1.0f )
    {
        r = encodeE5B9G9R9( l );
    }
    else
    {
        // fallback: normalize to preserve colors, to not clamp to white
        r = encodeE5B9G9R9( l / norm );
    }

#if 0
    {
        vec3 debugC = decodeE5B9G9R9( r );
        assert( length( debugC - vec3( c.data[ 0 ], c.data[ 1 ], c.data[ 2 ] ) ) < 10 );
    }
#endif

    *normalization = norm;
    return r;
}

static uint32_t packHalf2x16( const float& x, const float& y )
{
    return glm::packHalf2x16( { x, y } );
}
