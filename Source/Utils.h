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

#include <algorithm>
#include <array>
#include <optional>
#include <filesystem>

#include "Common.h"
#include "RTGL1/RTGL1.h"

#define RG_SET_VEC3( dst, x, y, z ) \
    ( dst )[ 0 ] = ( x );           \
    ( dst )[ 1 ] = ( y );           \
    ( dst )[ 2 ] = ( z )

#define RG_SET_VEC3_A( dst, xyz ) \
    ( dst )[ 0 ] = ( xyz )[ 0 ];  \
    ( dst )[ 1 ] = ( xyz )[ 1 ];  \
    ( dst )[ 2 ] = ( xyz )[ 2 ]

#define RG_ACCESS_VEC2( src ) ( src )[ 0 ], ( src )[ 1 ]
#define RG_ACCESS_VEC3( src ) ( src )[ 0 ], ( src )[ 1 ], ( src )[ 2 ]
#define RG_ACCESS_VEC4( src ) ( src )[ 0 ], ( src )[ 1 ], ( src )[ 2 ], ( src )[ 3 ]

#define RG_MAX_VEC3( dst, m )                       \
    ( dst )[ 0 ] = std::max( ( dst )[ 0 ], ( m ) ); \
    ( dst )[ 1 ] = std::max( ( dst )[ 1 ], ( m ) ); \
    ( dst )[ 2 ] = std::max( ( dst )[ 2 ], ( m ) )

#define RG_SET_VEC4( dst, x, y, z, w ) \
    ( dst )[ 0 ] = ( x );              \
    ( dst )[ 1 ] = ( y );              \
    ( dst )[ 2 ] = ( z );              \
    ( dst )[ 3 ] = ( w )

// clang-format off

#define RG_MATRIX_TRANSPOSED( /* RgTransform */ m )                                   \
    {                                                                                 \
        ( m ).matrix[ 0 ][ 0 ], ( m ).matrix[ 1 ][ 0 ], ( m ).matrix[ 2 ][ 0 ], 0.0f, \
        ( m ).matrix[ 0 ][ 1 ], ( m ).matrix[ 1 ][ 1 ], ( m ).matrix[ 2 ][ 1 ], 0.0f, \
        ( m ).matrix[ 0 ][ 2 ], ( m ).matrix[ 1 ][ 2 ], ( m ).matrix[ 2 ][ 2 ], 0.0f, \
        ( m ).matrix[ 0 ][ 3 ], ( m ).matrix[ 1 ][ 3 ], ( m ).matrix[ 2 ][ 3 ], 1.0f, \
    }

#define RG_TRANSFORM_IDENTITY   \
    RgTransform{                \
        1.0f, 0.0f, 0.0f, 0.0f, \
        0.0f, 1.0f, 0.0f, 0.0f, \
        0.0f, 0.0f, 1.0f, 0.0f, }

#define VK_TRANSFORM_IDENTITY   \
    VkTransformMatrixKHR{       \
        1.0f, 0.0f, 0.0f, 0.0f, \
        0.0f, 1.0f, 0.0f, 0.0f, \
        0.0f, 0.0f, 1.0f, 0.0f, }

// clang-format on

namespace RTGL1
{
enum NullifyTokenType
{
};
inline constexpr NullifyTokenType NullifyToken = {};

template< size_t Size >
struct FloatStorage
{
    FloatStorage()  = default;
    ~FloatStorage() = default;

    explicit FloatStorage( NullifyTokenType ) { memset( data, 0, sizeof( data ) ); }
    explicit FloatStorage( const float* ptr ) { memcpy( data, ptr, sizeof( data ) ); }

    FloatStorage( const FloatStorage& other )                = default;
    FloatStorage( FloatStorage&& other ) noexcept            = default;
    FloatStorage& operator=( const FloatStorage& other )     = default;
    FloatStorage& operator=( FloatStorage&& other ) noexcept = default;

    [[nodiscard]] const float* Get() const { return data; }
    float*                     Get() { return data; }

    /*const float& operator[]( size_t i ) const
    {
        assert( i < std::size( data ) );
        return data[ i ];
    }*/

    float data[ Size ];
};

using Float16D = FloatStorage< 16 >;
using Float4D  = FloatStorage< 4 >;

// Because std::optional requires explicit constructor
#define IfNotNull( ptr, ifnotnull ) \
    ( ( ptr ) != nullptr ? std::optional( ( ifnotnull ) ) : std::nullopt )

namespace Utils
{
    // Path to the folder containing .dll / .so
    auto FindBinFolder() -> std::filesystem::path;

    void BarrierImage( VkCommandBuffer                cmd,
                       VkImage                        image,
                       VkAccessFlags                  srcAccessMask,
                       VkAccessFlags                  dstAccessMask,
                       VkImageLayout                  oldLayout,
                       VkImageLayout                  newLayout,
                       VkPipelineStageFlags           srcStageMask,
                       VkPipelineStageFlags           dstStageMask,
                       const VkImageSubresourceRange& subresourceRange );

    void BarrierImage( VkCommandBuffer                cmd,
                       VkImage                        image,
                       VkAccessFlags                  srcAccessMask,
                       VkAccessFlags                  dstAccessMask,
                       VkImageLayout                  oldLayout,
                       VkImageLayout                  newLayout,
                       const VkImageSubresourceRange& subresourceRange );

    void BarrierImage( VkCommandBuffer      cmd,
                       VkImage              image,
                       VkAccessFlags        srcAccessMask,
                       VkAccessFlags        dstAccessMask,
                       VkImageLayout        oldLayout,
                       VkImageLayout        newLayout,
                       VkPipelineStageFlags srcStageMask,
                       VkPipelineStageFlags dstStageMask );

    void BarrierImage( VkCommandBuffer cmd,
                       VkImage         image,
                       VkAccessFlags   srcAccessMask,
                       VkAccessFlags   dstAccessMask,
                       VkImageLayout   oldLayout,
                       VkImageLayout   newLayout );

    void ASBuildMemoryBarrier( VkCommandBuffer cmd );

    void WaitForFence( VkDevice device, VkFence fence );
    void ResetFence( VkDevice device, VkFence fence );
    void WaitAndResetFence( VkDevice device, VkFence fence );
    void WaitAndResetFences( VkDevice device, VkFence fence_A, VkFence fence_B );

    VkFormat ToUnorm( VkFormat f );
    VkFormat ToSRGB( VkFormat f );
    bool     IsSRGB( VkFormat f );

    template< typename T >
    T Align( const T& v, const T& alignment );
    template< typename T >
    bool IsPow2( const T& v );

    bool AreViewportsSame( const VkViewport& a, const VkViewport& b );

    bool IsAlmostZero( const float v[ 3 ] );
    bool IsAlmostZero( const RgFloat3D& v );
    bool IsAlmostZero( const RgMatrix3D& m );

    inline bool AreAlmostSameF( float a, float b, float threshold = 0.001f )
    {
        return std::abs( a - b ) <= threshold;
    }

    inline bool AreAlmostSame( const RgFloat3D& a, const RgFloat3D& b, float threshold = 0.001f )
    {
        return AreAlmostSameF( a.data[ 0 ], b.data[ 0 ], threshold ) &&
               AreAlmostSameF( a.data[ 1 ], b.data[ 1 ], threshold ) &&
               AreAlmostSameF( a.data[ 2 ], b.data[ 2 ], threshold );
    }

    inline bool AreAlmostSameTr( const RgTransform& a,
                                 const RgTransform& b,
                                 float              threshold = 0.001f )
    {
        for( int i = 0; i < 3; i++ )
        {
            for( int j = 0; j < 4; j++ )
            {
                if( !AreAlmostSameF( a.matrix[ i ][ j ], b.matrix[ i ][ j ], threshold ) )
                {
                    return false;
                }
            }
        }
        return true;
    }

    float       Dot( const float a[ 3 ], const float b[ 3 ] );
    float       Dot( const RgFloat3D& a, const RgFloat3D& b );
    float       Length( const float v[ 3 ] );
    float       Length( const RgFloat3D& v );
    float       SqrLength( const float v[ 3 ] );
    bool        TryNormalize( float inout[ 3 ] );
    void        Normalize( float inout[ 3 ] );
    RgFloat3D   Normalize( const RgFloat3D& v );
    RgFloat3D   SafeNormalize( const RgFloat3D& v, const RgFloat3D& fallback );
    void        SafeNormalize( float ( &v )[ 3 ], const RgFloat3D& fallback );
    void        Negate( float inout[ 3 ] );
    void        Nullify( float inout[ 3 ] );
    void        Cross( const float a[ 3 ], const float b[ 3 ], float r[ 3 ] );
    auto        Cross( const RgFloat3D& a, const RgFloat3D& b ) -> RgFloat3D;
    RgFloat3D   GetUnnormalizedNormal( const RgFloat3D positions[ 3 ] );
    bool        GetNormalAndArea( const RgFloat3D positions[ 3 ], RgFloat3D& normal, float& area );
    // In terms of GLSL: mat3(a), where a is mat4.
    // The remaining values are initialized with identity matrix.
    void        SetMatrix3ToGLSLMat4( float dst[ 16 ], const RgMatrix3D& src );
    RgTransform MakeTransform( const RgFloat3D& up, const RgFloat3D& forward, float scale );
    RgTransform MakeTransform( const RgFloat3D& position, const RgFloat3D& forward );

    constexpr double M_PI = 3.1415926535897932384626433;

    constexpr float DegToRad( float degrees )
    {
        return degrees * float( M_PI ) / 180.0f;
    }
    constexpr float RadToDeg( float radians )
    {
        return radians * 180.0f / float( M_PI );
    }

    constexpr uint32_t GetPreviousByModulo( uint32_t value, uint32_t count )
    {
        assert( count > 0 );
        return ( value + ( count - 1 ) ) % count;
    }
    constexpr uint32_t PrevFrame( uint32_t frameIndex )
    {
        return GetPreviousByModulo( frameIndex, MAX_FRAMES_IN_FLIGHT );
    }

    template< uint32_t GroupSize >
        requires( GroupSize > 0 )
    constexpr uint32_t WorkGroupCountStrict( uint32_t size )
    {
        return ( size + ( GroupSize - 1 ) ) / GroupSize;
    }

    constexpr uint32_t GetWorkGroupCount( uint32_t size, uint32_t groupSize )
    {
        if( groupSize == 0 )
        {
            assert( 0 );
            return 0;
        }

        return 1 + ( size + ( groupSize - 1 ) ) / groupSize;
    }
    constexpr uint32_t GetWorkGroupCount( float size, uint32_t groupSize )
    {
        return GetWorkGroupCount( static_cast< uint32_t >( std::ceil( size ) ), groupSize );
    }

    template< typename T1, typename T2 >
        requires( std::is_integral_v< T1 > && std::is_integral_v< T2 > )
    uint32_t GetWorkGroupCountT( T1 size, T2 groupSize );

    template< typename ReturnType = RgFloat4D >
        requires( std::is_same_v< ReturnType, RgFloat4D > ||
                  std::is_same_v< ReturnType, RgFloat3D > )
    constexpr ReturnType UnpackColor4DPacked32( RgColor4DPacked32 c )
    {
        if constexpr( std::is_same_v< ReturnType, RgFloat3D > )
        {
            return RgFloat3D{ {
                float( ( c >> 0 ) & 255u ) / 255.0f,
                float( ( c >> 8 ) & 255u ) / 255.0f,
                float( ( c >> 16 ) & 255u ) / 255.0f,
            } };
        }
        if constexpr( std::is_same_v< ReturnType, RgFloat4D > )
        {
            return RgFloat4D{ {
                float( ( c >> 0 ) & 255u ) / 255.0f,
                float( ( c >> 8 ) & 255u ) / 255.0f,
                float( ( c >> 16 ) & 255u ) / 255.0f,
                float( ( c >> 24 ) & 255u ) / 255.0f,
            } };
        }
        assert( 0 );
        return {};
    }

    template< bool WithAlpha >
    constexpr bool IsColor4DPacked32Zero( RgColor4DPacked32 c )
    {
        uint32_t mask = WithAlpha ? 0xFFFFFFFF : 0x00FFFFFF;
        return ( c & mask ) == 0;
    }

    constexpr std::array< uint8_t, 4 > UnpackColor4DPacked32Components( RgColor4DPacked32 c )
    {
        return { {
            uint8_t( ( c >> 0 ) & 255u ),
            uint8_t( ( c >> 8 ) & 255u ),
            uint8_t( ( c >> 16 ) & 255u ),
            uint8_t( ( c >> 24 ) & 255u ),
        } };
    }

    constexpr uint8_t UnpackAlphaFromPacked32AsUint8( RgColor4DPacked32 c )
    {
        return uint8_t( ( c >> 24 ) & 255u );
    }

    constexpr float UnpackAlphaFromPacked32( RgColor4DPacked32 c )
    {
        return float( UnpackAlphaFromPacked32AsUint8( c ) ) / 255.0f;
    }

    constexpr RgColor4DPacked32 ReplaceAlphaInPacked32( RgColor4DPacked32 c, uint8_t newalpha )
    {
        return ( c & 0x00FFFFFF ) | ( uint32_t{ newalpha } << 24 );
    }

    constexpr float Luminance( const float ( &c )[ 3 ] )
    {
        return 0.2125f * c[ 0 ] + 0.7154f * c[ 1 ] + 0.0721f * c[ 2 ];
    }

    inline float Dot( const float a[ 3 ], const float b[ 3 ] )
    {
        return a[ 0 ] * b[ 0 ] + a[ 1 ] * b[ 1 ] + a[ 2 ] * b[ 2 ];
    }

    inline float Dot( const RgFloat3D& a, const RgFloat3D& b )
    {
        return Dot( a.data, b.data );
    }

    inline float Length( const float v[ 3 ] )
    {
        return sqrtf( Dot( v, v ) );
    }

    inline float Length( const RgFloat3D& v )
    {
        return Length( v.data );
    }

    inline float SqrLength( const float v[ 3 ] )
    {
        return Dot( v, v );
    }

    template< size_t N >
        requires( N == 3 )
    float SqrDistance( const float ( &a )[ N ], const float ( &b )[ N ] )
    {
        float diff[ 3 ] = { b[ 0 ] - a[ 0 ], b[ 1 ] - a[ 1 ], b[ 2 ] - a[ 2 ] };
        return SqrLength( diff );
    }

    inline float SqrDistanceR( const RgFloat3D& a, const RgFloat3D& b )
    {
        return SqrDistance( a.data, b.data );
    }

    template< size_t N >
        requires( N == 3 )
    float Distance( const float ( &a )[ N ], const float ( &b )[ N ] )
    {
        return sqrtf( SqrDistance( a, b ) );
    }

    inline bool IsCstrEmpty( const char* cstr )
    {
        return cstr == nullptr || *cstr == '\0';
    }
    inline const char* SafeCstr( const char* cstr )
    {
        return cstr ? cstr : "";
    }

    template< size_t N >
    void SafeCstrCopy( char ( &dst )[ N ], std::string_view src )
    {
        memset( dst, 0, N );

        for( size_t i = 0; i < N - 1 && i < src.length(); i++ )
        {
            dst[ i ] = src[ i ];
        }
    }

    constexpr RgColor4DPacked32 PackColor( uint8_t r, uint8_t g, uint8_t b, uint8_t a )
    {
        return ( uint32_t( a ) << 24 ) | ( uint32_t( b ) << 16 ) | ( uint32_t( g ) << 8 ) |
               ( uint32_t( r ) );
    }

    constexpr uint8_t ToUint8Safe( float c )
    {
        return uint8_t( std::clamp( int32_t( c * 255.0f ), 0, 255 ) );
    };

    constexpr RgColor4DPacked32 PackColorFromFloat( float r, float g, float b, float a )
    {
        return PackColor( ToUint8Safe( r ), ToUint8Safe( g ), ToUint8Safe( b ), ToUint8Safe( a ) );
    }

    constexpr RgColor4DPacked32 MultiplyColorPacked32( RgColor4DPacked32 c, float mult )
    {
        auto rgba = UnpackColor4DPacked32Components( c );

        rgba[ 0 ] = ToUint8Safe( static_cast< float >( rgba[ 0 ] ) / 255.0f * mult );
        rgba[ 1 ] = ToUint8Safe( static_cast< float >( rgba[ 1 ] ) / 255.0f * mult );
        rgba[ 2 ] = ToUint8Safe( static_cast< float >( rgba[ 2 ] ) / 255.0f * mult );
        return PackColor( rgba[ 0 ], rgba[ 1 ], rgba[ 2 ], rgba[ 3 ] );
    }

    namespace detail
    {

        /*
        vec2 uint_to_vec2(uint base) {
            uvec2 decode = (uvec2(base) >> uvec2(0, 16)) & uvec2(0xFFFF, 0xFFFF);
            return vec2(decode) / vec2(65535.0, 65535.0) * 2.0 - 1.0;
        }

        vec3 oct_to_vec3(vec2 oct) {
            vec3 v = vec3(oct.xy, 1.0 - abs(oct.x) - abs(oct.y));
            float t = max(-v.z, 0.0);
            v.xy += t * -sign(v.xy);
            return normalize(v);
        }

        vec3 decode_uint_oct_to_norm(uint base) {
            return oct_to_vec3(uint_to_vec2(base));
        }
         */

        constexpr float sign( float v )
        {
            return v < 0.f ? -1.f : v > 0.f ? 1.f : 0.f;
        }

        constexpr float signNotZero( float v )
        {
            return v < 0.f ? -1.f : 1.f;
        }

        constexpr RgFloat2D uint_to_vec2( uint32_t base )
        {
            uint32_t decode[] = {
                base & uint32_t{ 0xFFFF },
                ( base >> 16u ) & uint32_t{ 0xFFFF },
            };
            return {
                ( static_cast< float >( decode[ 0 ] ) / 65535.f ) * 2.f - 1.f,
                ( static_cast< float >( decode[ 1 ] ) / 65535.f ) * 2.f - 1.f,
            };
        }

        inline RgFloat3D oct_to_vec3( const RgFloat2D& oct )
        {
            float x = oct.data[ 0 ];
            float y = oct.data[ 1 ];
            float z = 1.f - std::abs( x ) - std::abs( y );

            float t = std::max( -z, 0.f );

            x += t * ( -sign( x ) );
            y += t * ( -sign( y ) );

            RgFloat3D v = { x, y, z };
            TryNormalize( v.data );
            return v;
        }

        static_assert( std::is_same_v< RgNormalPacked32, uint32_t > );
        inline RgFloat3D decode_uint_oct_to_norm( uint32_t base )
        {
            return oct_to_vec3( uint_to_vec2( base ) );
        }

        constexpr uint32_t vec2_to_uint( const RgFloat2D& base )
        {
            uint32_t enc[] = {
                std::clamp( static_cast< uint32_t >( base.data[ 0 ] * 65535.f ), 0u, 65535u ),
                std::clamp( static_cast< uint32_t >( base.data[ 1 ] * 65535.f ), 0u, 65535u ),
            };
            return enc[ 0 ] | ( enc[ 1 ] << 16u );
        }

        inline RgFloat2D vec3_to_oct( float x, float y, float z )
        {
            float ab = std::abs( x ) + std::abs( y ) + std::abs( z );
            if( ab > 0.000001f ) // safety for close-to-zero case
            {
                x /= ab;
                y /= ab;
                z /= ab;
            }

            float oct[] = {
                z >= 0.f ? x : ( 1.f - std::abs( y ) ) * signNotZero( x ),
                z >= 0.f ? y : ( 1.f - std::abs( x ) ) * signNotZero( y ),
            };

            return {
                oct[ 0 ] * 0.5f + 0.5f,
                oct[ 1 ] * 0.5f + 0.5f,
            };
        }

        static_assert( std::is_same_v< RgNormalPacked32, uint32_t > );
        inline uint32_t encode_norm_to_uint_oct( float x, float y, float z )
        {
            return vec2_to_uint( vec3_to_oct( x, y, z ) );
        }
    }

    // must match Shaders/Utils.h
    [[nodiscard]] inline RgNormalPacked32 PackNormal( float x, float y, float z )
    {
        float v[] = { x, y, z };
        TryNormalize( v );
        return detail::encode_norm_to_uint_oct( v[ 0 ], v[ 1 ], v[ 2 ] );
    }

    [[nodiscard]] inline RgNormalPacked32 PackNormal( const RgFloat3D& v )
    {
        return PackNormal( v.data[ 0 ], v.data[ 1 ], v.data[ 2 ] );
    }

    // must match Shaders/Utils.h
    [[nodiscard]] inline RgFloat3D UnpackNormal( RgNormalPacked32 x )
    {
        return detail::decode_uint_oct_to_norm( x );
    }

    constexpr RgColor4DPacked32 PackColorFromFloat( const float ( &rgba )[ 4 ] )
    {
        return PackColorFromFloat( rgba[ 0 ], rgba[ 1 ], rgba[ 2 ], rgba[ 3 ] );
    }

    constexpr float Saturate( float v )
    {
        return std::clamp( v, 0.0f, 1.0f );
    }

    constexpr RgFloat3D ApplyTransform( const RgTransform& tr, const RgFloat3D& local )
    {
        RgFloat3D global = {};
        for( uint32_t i = 0; i < 3; i++ )
        {
            global.data[ i ] = tr.matrix[ i ][ 0 ] * local.data[ 0 ] +
                               tr.matrix[ i ][ 1 ] * local.data[ 1 ] +
                               tr.matrix[ i ][ 2 ] * local.data[ 2 ] + tr.matrix[ i ][ 3 ];
        }
        return global;
    }

// clang-format off
    // Column memory order!
    #define RG_TRANSFORM_TO_GLTF_MATRIX( t ) {                                      \
        ( t ).matrix[ 0 ][ 0 ], ( t ).matrix[ 1 ][ 0 ], ( t ).matrix[ 2 ][ 0 ], 0,  \
        ( t ).matrix[ 0 ][ 1 ], ( t ).matrix[ 1 ][ 1 ], ( t ).matrix[ 2 ][ 1 ], 0,  \
        ( t ).matrix[ 0 ][ 2 ], ( t ).matrix[ 1 ][ 2 ], ( t ).matrix[ 2 ][ 2 ], 0,  \
        ( t ).matrix[ 0 ][ 3 ], ( t ).matrix[ 1 ][ 3 ], ( t ).matrix[ 2 ][ 3 ], 1   }
    // clang-format on
};


template< size_t N >
    requires( N >= 3 )
void ApplyTransformToPosition( const RgTransform* transform, float ( &pos )[ N ] )
{
    if( transform )
    {
        const auto& m     = transform->matrix;
        const float out[] = {
            m[ 0 ][ 0 ] * pos[ 0 ] + m[ 0 ][ 1 ] * pos[ 1 ] + m[ 0 ][ 2 ] * pos[ 2 ] + m[ 0 ][ 3 ],
            m[ 1 ][ 0 ] * pos[ 0 ] + m[ 1 ][ 1 ] * pos[ 1 ] + m[ 1 ][ 2 ] * pos[ 2 ] + m[ 1 ][ 3 ],
            m[ 2 ][ 0 ] * pos[ 0 ] + m[ 2 ][ 1 ] * pos[ 1 ] + m[ 2 ][ 2 ] * pos[ 2 ] + m[ 2 ][ 3 ],
        };
        pos[ 0 ] = out[ 0 ];
        pos[ 1 ] = out[ 1 ];
        pos[ 2 ] = out[ 2 ];
    }
}

template< size_t N >
    requires( N >= 3 )
void ApplyTransformToDirection( const RgTransform* transform, float ( &dir )[ N ] )
{
    if( transform )
    {
        const auto& m     = transform->matrix;
        const float out[] = {
            m[ 0 ][ 0 ] * dir[ 0 ] + m[ 0 ][ 1 ] * dir[ 1 ] + m[ 0 ][ 2 ] * dir[ 2 ],
            m[ 1 ][ 0 ] * dir[ 0 ] + m[ 1 ][ 1 ] * dir[ 1 ] + m[ 1 ][ 2 ] * dir[ 2 ],
            m[ 2 ][ 0 ] * dir[ 0 ] + m[ 2 ][ 1 ] * dir[ 1 ] + m[ 2 ][ 2 ] * dir[ 2 ],
        };
        dir[ 0 ] = out[ 0 ];
        dir[ 1 ] = out[ 1 ];
        dir[ 2 ] = out[ 2 ];
    }
}

inline RgFloat3D ApplyTransformToPosition( const RgTransform* transform, const RgFloat3D& pos )
{
    RgFloat3D r = pos;
    ApplyTransformToPosition( transform, r.data );
    return r;
}

inline RgFloat3D ApplyTransformToDirection( const RgTransform* transform, const RgFloat3D& dir )
{
    RgFloat3D r = dir;
    ApplyTransformToDirection( transform, r.data );
    return r;
}

inline auto ApplyJitter( const float*     originalProj,
                         const RgFloat2D& jitter,
                         uint32_t         width,
                         uint32_t         height ) -> std::array< float, 16 >
{
    auto jitterredProj = std::array< float, 16 >{};

    memcpy( jitterredProj.data(), originalProj, 16 * sizeof( float ) );
    jitterredProj[ 2 * 4 + 0 ] += jitter.data[ 0 ] / float( width );
    jitterredProj[ 2 * 4 + 1 ] += jitter.data[ 1 ] / float( height );

    return jitterredProj;
}


template< typename T >
constexpr T clamp( const T& v, const T& v_min, const T& v_max )
{
    assert( v_min <= v_max );
    return std::min( v_max, std::max( v_min, v ) );
}

template< typename T >
bool Utils::IsPow2( const T& v )
{
    static_assert( std::is_integral_v< T > );
    return ( v != 0 ) && ( ( v & ( v - 1 ) ) == 0 );
}

template< typename T >
T Utils::Align( const T& v, const T& alignment )
{
    static_assert( std::is_integral_v< T > );
    assert( IsPow2( alignment ) );

    return ( v + alignment - 1 ) & ~( alignment - 1 );
}

template< typename T1, typename T2 >
    requires( std::is_integral_v< T1 > && std::is_integral_v< T2 > )
uint32_t Utils::GetWorkGroupCountT( T1 size, T2 groupSize )
{
    assert( size <= std::numeric_limits< uint32_t >::max() );
    assert( groupSize <= std::numeric_limits< uint32_t >::max() );

    return GetWorkGroupCount( static_cast< uint32_t >( size ),
                              static_cast< uint32_t >( groupSize ) );
}

template< typename M >
auto find_p( const M& m, const std::string_view key )
{
    using ResultType = const typename M::value_type::second_type*;

    auto found = m.find( key );
    if( found != m.end() )
    {
        static_assert( std::is_same_v< ResultType, decltype( &found->second ) > );
        return &found->second;
    }
    return static_cast< ResultType >( nullptr );
}

struct CopyRange
{
    void add( uint32_t x )
    {
        vbegin = std::min( x, vbegin );
        vend   = std::max( x + 1, vend );
    }

    static auto merge( const CopyRange& a, const CopyRange& b )
    {
        return CopyRange{
            .vbegin = std::min( a.vbegin, b.vbegin ),
            .vend   = std::max( a.vend, b.vend ),
        };
    }

    static auto mergeSafe( const CopyRange& a, const CopyRange& b )
    {
        if( a.valid() )
        {
            if( b.valid() )
            {
                return merge( a, b );
            }
            return a;
        }
        return b;
    }

    static auto remove_at_start( const CopyRange& full, const CopyRange& toremove )
    {
        if( toremove.count() == 0 )
        {
            return full;
        }
        assert( toremove.vbegin <= toremove.vbegin && toremove.vend <= toremove.vend );
        assert( full.vbegin == toremove.vbegin && toremove.vend <= full.vend );
        if( full.count() < toremove.count() )
        {
            assert( 0 );
            return CopyRange{};
        }
        return CopyRange{
            .vbegin = full.vbegin + toremove.count(),
            .vend   = full.vend,
        };
    }

    uint32_t first() const { return vbegin; }
    uint32_t count() const { return vend - vbegin; }
    bool     valid() const { return count() > 0; }

    uint32_t vbegin{ 0 };
    uint32_t vend{ 0 };
};

inline auto MakeRangeFromCount( uint32_t first, uint32_t count )
{
    return CopyRange{
        .vbegin = first,
        .vend   = first + count,
    };
}

inline auto MakeRangeFromOverallCount( uint32_t first, uint32_t overallCount )
{
    return CopyRange{
        .vbegin = first,
        .vend   = overallCount,
    };
}

inline auto AddSuffix( const std::filesystem::path& base, const std::wstring_view filesuffix )
    -> std::filesystem::path
{
    const auto ext = base.extension();

    auto result = base;
    result.replace_extension();

    result += filesuffix;

    result.replace_extension( ext );
    return result;
}

namespace ext
{
    // https://en.cppreference.com/w/cpp/utility/variant/visit
    // helper type for the visitor #4
    template< class... Ts >
    struct overloaded : Ts...
    {
        using Ts::operator()...;
    };

    template< typename T >
    struct type_identity
    {
        using type = T;
    };
}

}