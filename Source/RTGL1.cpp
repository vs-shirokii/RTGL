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

#include "VulkanDevice.h"
#include "RgException.h"

#include "TextureExporter.h"


namespace RTGL1::debug::detail
{

DebugPrintFn           g_print{};
RgMessageSeverityFlags g_printSeverity{ 0 };

}


namespace
{

std::unique_ptr< RTGL1::VulkanDevice > g_device{};

RTGL1::VulkanDevice* TryGetDevice()
{
    if( g_device )
    {
        return g_device.get();
    }
    return nullptr;
}

RTGL1::VulkanDevice& GetDevice()
{
    if( auto d = TryGetDevice() )
    {
        return *d;
    }
    throw RTGL1::RgException( RG_RESULT_NOT_INITIALIZED );
}

RgResult rgDestroyInstance()
{
    if( !TryGetDevice() )
    {
        return RG_RESULT_NOT_INITIALIZED;
    }

    try
    {
        g_device.reset();
    }
    catch( RTGL1::RgException& e )
    {
        RTGL1::debug::Error( e.what() );
        RTGL1::debug::detail::g_printSeverity = 0;
        RTGL1::debug::detail::g_print         = nullptr;
        return e.GetErrorCode();
    }

    RTGL1::debug::detail::g_printSeverity = 0;
    RTGL1::debug::detail::g_print         = nullptr;

    return RG_RESULT_SUCCESS;
}

template< typename Func, typename... Args >
    requires( std::is_same_v< std::invoke_result_t< Func, RTGL1::VulkanDevice, Args... >, void > )
auto Call( Func f, Args&&... args )
{
    try
    {
        RTGL1::VulkanDevice& dev = GetDevice();

        if( dev.IsSuspended() )
        {
            return RG_RESULT_SUCCESS;
        }

        ( dev.*f )( std::forward< Args >( args )... );
    }
    catch( RTGL1::RgException& e )
    {
        RTGL1::debug::Error( e.what() );
        return e.GetErrorCode();
    }
    return RG_RESULT_SUCCESS;
}

template< typename Func, typename... Args >
    requires( !std::is_same_v< std::invoke_result_t< Func, RTGL1::VulkanDevice, Args... >, void > &&
              std::is_default_constructible_v<
                  std::invoke_result_t< Func, RTGL1::VulkanDevice, Args... > > )
auto Call( Func f, Args&&... args )
{
    using ReturnType = std::invoke_result_t< Func, RTGL1::VulkanDevice, Args... >;

    try
    {
        RTGL1::VulkanDevice& dev = GetDevice();

        if( !dev.IsSuspended() )
        {
            return ( dev.*f )( std::forward< Args >( args )... );
        }
    }
    catch( RTGL1::RgException& e )
    {
        RTGL1::debug::Error( e.what() );
    }
    return ReturnType{};
}



RgResult rgUploadMeshPrimitive( const RgMeshInfo* pMesh, const RgMeshPrimitiveInfo* pPrimitive )
{
    return Call( &RTGL1::VulkanDevice::UploadMeshPrimitive, pMesh, pPrimitive );
}

RgResult rgUploadDecal( const RgDecalInfo* pInfo )
{
    return Call( &RTGL1::VulkanDevice::UploadDecal, pInfo );
}

RgResult rgUploadLensFlare( const RgLensFlareInfo* pInfo )
{
    return Call( &RTGL1::VulkanDevice::UploadLensFlare, pInfo );
}

RgResult rgUploadCamera( const RgCameraInfo* pInfo )
{
    return Call( &RTGL1::VulkanDevice::UploadCamera, pInfo );
}

RgResult rgUploadLight( const RgLightInfo* pInfo )
{
    return Call( &RTGL1::VulkanDevice::UploadLight, pInfo );
}

RgResult rgProvideOriginalTexture( const RgOriginalTextureInfo* pInfo )
{
    return Call( &RTGL1::VulkanDevice::ProvideOriginalTexture, pInfo );
}

RgResult rgMarkOriginalTextureAsDeleted( const char* pTextureName )
{
    return Call( &RTGL1::VulkanDevice::MarkOriginalTextureAsDeleted, pTextureName );
}

RgResult rgStartFrame( const RgStartFrameInfo* pInfo )
{
    return Call( &RTGL1::VulkanDevice::StartFrame, pInfo );
}

RgResult rgDrawFrame( const RgDrawFrameInfo* pInfo )
{
    return Call( &RTGL1::VulkanDevice::DrawFrame, pInfo );
}

RgPrimitiveVertex* rgUtilScratchAllocForVertices( uint32_t vertexCount )
{
    return Call( &RTGL1::VulkanDevice::ScratchAllocForVertices, vertexCount );
}

void rgUtilScratchFree( const RgPrimitiveVertex* pPointer )
{
    Call( &RTGL1::VulkanDevice::ScratchFree, pPointer );
}

void rgUtilScratchGetIndices( RgUtilImScratchTopology topology,
                              uint32_t                vertexCount,
                              const uint32_t**        ppOutIndices,
                              uint32_t*               pOutIndexCount )
{
    Call( &RTGL1::VulkanDevice::ScratchGetIndices,
          topology,
          vertexCount,
          ppOutIndices,
          pOutIndexCount );
}

void rgUtilImScratchClear()
{
    Call( &RTGL1::VulkanDevice::ImScratchClear );
}

void rgUtilImScratchStart( RgUtilImScratchTopology topology )
{
    Call( &RTGL1::VulkanDevice::ImScratchStart, topology );
}

void rgUtilImScratchEnd()
{
    Call( &RTGL1::VulkanDevice::ImScratchEnd );
}

void rgUtilImScratchVertex( float x, float y, float z )
{
    Call( &RTGL1::VulkanDevice::ImScratchVertex, x, y, z );
}


void rgUtilImScratchNormal( float x, float y, float z )
{
    Call( &RTGL1::VulkanDevice::ImScratchNormal, x, y, z );
}

void rgUtilImScratchTexCoord( float u, float v )
{
    Call( &RTGL1::VulkanDevice::ImScratchTexCoord, u, v );
}

void rgUtilImScratchTexCoord_Layer1( float u, float v )
{
    Call( &RTGL1::VulkanDevice::ImScratchTexCoord_Layer1, u, v );
}

void rgUtilImScratchTexCoord_Layer2( float u, float v )
{
    Call( &RTGL1::VulkanDevice::ImScratchTexCoord_Layer2, u, v );
}

void rgUtilImScratchTexCoord_Layer3( float u, float v )
{
    Call( &RTGL1::VulkanDevice::ImScratchTexCoord_Layer3, u, v );
}

void rgUtilImScratchColor( RgColor4DPacked32 color )
{
    Call( &RTGL1::VulkanDevice::ImScratchColor, color );
}

void rgUtilImScratchSetToPrimitive( RgMeshPrimitiveInfo* pTarget )
{
    Call( &RTGL1::VulkanDevice::ImScratchSetToPrimitive, pTarget );
}

RgBool32 rgUtilIsUpscaleTechniqueAvailable( RgRenderUpscaleTechnique technique )
{
    return Call( &RTGL1::VulkanDevice::IsUpscaleTechniqueAvailable, technique );
}

const char* rgUtilGetResultDescription( RgResult result )
{
    return RTGL1::RgException::GetRgResultName( result );
}

RgColor4DPacked32 rgUtilPackColorByte4D( uint8_t r, uint8_t g, uint8_t b, uint8_t a )
{
    return RTGL1::Utils::PackColor( r, g, b, a );
}

RgColor4DPacked32 rgUtilPackColorFloat4D( float r, float g, float b, float a )
{
    return RTGL1::Utils::PackColorFromFloat( r, g, b, a );
}

void rgUtilExportAsTGA( const void* pPixels, uint32_t width, uint32_t height, const char* pPath )
{
    RTGL1::TextureExporter::WriteTGA( pPath, pPixels, { width, height } );
}

}



extern "C"
{
RGAPI RgResult RGCONV rgCreateInstance( const RgInstanceCreateInfo* pInfo, RgInterface* pInterface )
{
    if( pInfo == nullptr || pInterface == nullptr )
    {
        return RG_RESULT_WRONG_FUNCTION_ARGUMENT;
    }

    if( TryGetDevice() )
    {
        return RG_RESULT_ALREADY_INITIALIZED;
    }

    *pInterface = {};

    // during init, use raw logger
    {
        RTGL1::debug::detail::g_printSeverity = pInfo->allowedMessages;
        RTGL1::debug::detail::g_print         = [ pInfo ]( std::string_view       msg,
                                                   RgMessageSeverityFlags severity ) {
            if( pInfo->pfnPrint )
            {
                assert( RTGL1::debug::detail::g_printSeverity & severity );
                pInfo->pfnPrint( msg.data(), severity, pInfo->pUserPrintData );
            }
        };
    }

    try
    {
        g_device = std::make_unique< RTGL1::VulkanDevice >( pInfo );

        *pInterface = RgInterface{
            .rgCreateInstance                  = rgCreateInstance,
            .rgDestroyInstance                 = rgDestroyInstance,
            .rgStartFrame                      = rgStartFrame,
            .rgUploadCamera                    = rgUploadCamera,
            .rgUploadMeshPrimitive             = rgUploadMeshPrimitive,
            .rgUploadDecal                     = rgUploadDecal,
            .rgUploadLensFlare                 = rgUploadLensFlare,
            .rgUploadLight                     = rgUploadLight,
            .rgProvideOriginalTexture          = rgProvideOriginalTexture,
            .rgMarkOriginalTextureAsDeleted    = rgMarkOriginalTextureAsDeleted,
            .rgDrawFrame                       = rgDrawFrame,
            .rgUtilScratchAllocForVertices     = rgUtilScratchAllocForVertices,
            .rgUtilScratchFree                 = rgUtilScratchFree,
            .rgUtilScratchGetIndices           = rgUtilScratchGetIndices,
            .rgUtilImScratchClear              = rgUtilImScratchClear,
            .rgUtilImScratchStart              = rgUtilImScratchStart,
            .rgUtilImScratchVertex             = rgUtilImScratchVertex,
            .rgUtilImScratchNormal             = rgUtilImScratchNormal,
            .rgUtilImScratchTexCoord           = rgUtilImScratchTexCoord,
            .rgUtilImScratchTexCoord_Layer1    = rgUtilImScratchTexCoord_Layer1,
            .rgUtilImScratchTexCoord_Layer2    = rgUtilImScratchTexCoord_Layer2,
            .rgUtilImScratchTexCoord_Layer3    = rgUtilImScratchTexCoord_Layer3,
            .rgUtilImScratchColor              = rgUtilImScratchColor,
            .rgUtilImScratchEnd                = rgUtilImScratchEnd,
            .rgUtilImScratchSetToPrimitive     = rgUtilImScratchSetToPrimitive,
            .rgUtilIsUpscaleTechniqueAvailable = rgUtilIsUpscaleTechniqueAvailable,
            .rgUtilGetResultDescription        = rgUtilGetResultDescription,
            .rgUtilPackColorByte4D             = rgUtilPackColorByte4D,
            .rgUtilPackColorFloat4D            = rgUtilPackColorFloat4D,
            .rgUtilExportAsTGA                 = rgUtilExportAsTGA,
        };
    }
    // TODO: VulkanDevice must clean all the resources if initialization failed!
    // So for now exceptions must not happen. But if they did, target application must be closed.
    catch( RTGL1::RgException& e )
    {
        RTGL1::debug::Error( e.what() );
        RTGL1::debug::detail::g_printSeverity = 0;
        RTGL1::debug::detail::g_print         = nullptr;
        return e.GetErrorCode();
    }

    // now use a fancy logger
    {
        RgMessageSeverityFlags allmsg = RG_MESSAGE_SEVERITY_VERBOSE | RG_MESSAGE_SEVERITY_INFO |
                                        RG_MESSAGE_SEVERITY_WARNING | RG_MESSAGE_SEVERITY_ERROR;
        RgMessageSeverityFlags usermsg = pInfo->allowedMessages;

        RTGL1::debug::detail::g_printSeverity =
            g_device && g_device->IsDevMode() ? allmsg : usermsg;

        RTGL1::debug::detail::g_print = []( std::string_view       msg,
                                            RgMessageSeverityFlags severity ) {
            if( g_device )
            {
                assert( RTGL1::debug::detail::g_printSeverity & severity );
                g_device->Print( msg, severity );
            }
        };
    }

    return RG_RESULT_SUCCESS;
}
}
