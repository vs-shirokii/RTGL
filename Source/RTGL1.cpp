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
bool                   g_breakOnError{ true };

}


namespace
{

using Device = RTGL1::VulkanDevice;
std::unique_ptr< Device > g_device{};

Device* TryGetDevice()
{
    if( g_device )
    {
        return g_device.get();
    }
    return nullptr;
}

Device& GetDevice()
{
    if( auto d = TryGetDevice() )
    {
        return *d;
    }
    throw RTGL1::RgException( RG_RESULT_NOT_INITIALIZED );
}

RgResult RGAPI_CALL rgDestroyInstance()
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

template< typename Func, typename Result = std::invoke_result_t< Func, Device& > >
    requires( std::is_default_constructible_v< Result > || std::is_same_v< Result, void > )
auto Call( Func&& f )
{
    using WrappedResult = std::conditional_t< std::is_same_v< Result, void >, RgResult, Result >;
    static_assert( RgResult{} == RG_RESULT_SUCCESS, "Default must be success" );

    try
    {
        Device& dev = GetDevice();

        {
            if constexpr( std::is_same_v< Result, void > )
            {
                f( dev );
                return WrappedResult{};
            }
            else
            {
                return WrappedResult{ f( dev ) };
            }
        }
    }
    catch( RTGL1::RgException& e )
    {
        RTGL1::debug::Error( e.what() );
    }
    return WrappedResult{};
}



RgResult RGAPI_CALL rgUploadMeshPrimitive( const RgMeshInfo*          pMesh,
                                           const RgMeshPrimitiveInfo* pPrimitive )
{
    return Call( [ & ]( Device& d ) { d.UploadMeshPrimitive( pMesh, pPrimitive ); } );
}

RgResult RGAPI_CALL rgUploadLensFlare( const RgLensFlareInfo* pInfo )
{
    return Call( [ & ]( Device& d ) { d.UploadLensFlare( pInfo ); } );
}

RgResult RGAPI_CALL rgSpawnFluid( const RgSpawnFluidInfo* pInfo )
{
    return Call( [ & ]( Device& d ) { d.SpawnFluid( pInfo ); } );
}

RgResult RGAPI_CALL rgUploadCamera( const RgCameraInfo* pInfo )
{
    return Call( [ & ]( Device& d ) { d.UploadCamera( pInfo ); } );
}

RgResult RGAPI_CALL rgUploadLight( const RgLightInfo* pInfo )
{
    return Call( [ & ]( Device& d ) { d.UploadLight( pInfo ); } );
}

RgResult RGAPI_CALL rgProvideOriginalTexture( const RgOriginalTextureInfo* pInfo )
{
    return Call( [ & ]( Device& d ) { d.ProvideOriginalTexture( pInfo ); } );
}

RgResult RGAPI_CALL rgMarkOriginalTextureAsDeleted( const char* pTextureName )
{
    return Call( [ & ]( Device& d ) { d.MarkOriginalTextureAsDeleted( pTextureName ); } );
}

RgResult RGAPI_CALL rgStartFrame( const RgStartFrameInfo* pInfo )
{
    return Call( [ & ]( Device& d ) { d.StartFrame( pInfo ); } );
}

RgResult RGAPI_CALL rgDrawFrame( const RgDrawFrameInfo* pInfo )
{
    return Call( [ & ]( Device& d ) { d.DrawFrame( pInfo ); } );
}

RgPrimitiveVertex* RGAPI_CALL rgUtilScratchAllocForVertices( uint32_t vertexCount )
{
    return Call( [ & ]( Device& d ) { return d.ScratchAllocForVertices( vertexCount ); } );
}

void RGAPI_CALL rgUtilScratchFree( const RgPrimitiveVertex* pPointer )
{
    Call( [ & ]( Device& d ) { d.ScratchFree( pPointer ); } );
}

void RGAPI_CALL rgUtilScratchGetIndices( RgUtilImScratchTopology topology,
                                         uint32_t                vertexCount,
                                         const uint32_t**        ppOutIndices,
                                         uint32_t*               pOutIndexCount )
{
    Call( [ & ]( Device& d ) {
        const auto indices = d.ScratchIm().GetIndices( topology, vertexCount );
        *ppOutIndices      = indices.data();
        *pOutIndexCount    = uint32_t( indices.size() );
    } );
}

void RGAPI_CALL rgUtilImScratchClear()
{
    Call( [ & ]( Device& d ) { d.ScratchIm().Clear(); } );
}

void RGAPI_CALL rgUtilImScratchStart( RgUtilImScratchTopology topology )
{
    Call( [ & ]( Device& d ) { d.ScratchIm().StartPrimitive( topology ); } );
}

void RGAPI_CALL rgUtilImScratchEnd()
{
    Call( [ & ]( Device& d ) { d.ScratchIm().EndPrimitive(); } );
}

void RGAPI_CALL rgUtilImScratchVertex( float x, float y, float z )
{
    Call( [ & ]( Device& d ) { d.ScratchIm().Vertex( x, y, z ); } );
}


void RGAPI_CALL rgUtilImScratchNormal( float x, float y, float z )
{
    Call( [ & ]( Device& d ) { d.ScratchIm().Normal( x, y, z ); } );
}

void RGAPI_CALL rgUtilImScratchTexCoord( float u, float v )
{
    Call( [ & ]( Device& d ) { d.ScratchIm().TexCoord( u, v ); } );
}

void RGAPI_CALL rgUtilImScratchTexCoord_Layer1( float u, float v )
{
    Call( [ & ]( Device& d ) { d.ScratchIm().TexCoord_Layer1( u, v ); } );
}

void RGAPI_CALL rgUtilImScratchTexCoord_Layer2( float u, float v )
{
    Call( [ & ]( Device& d ) { d.ScratchIm().TexCoord_Layer2( u, v ); } );
}

void RGAPI_CALL rgUtilImScratchTexCoord_Layer3( float u, float v )
{
    Call( [ & ]( Device& d ) { d.ScratchIm().TexCoord_Layer3( u, v ); } );
}

void RGAPI_CALL rgUtilImScratchColor( RgColor4DPacked32 color )
{
    Call( [ & ]( Device& d ) { d.ScratchIm().Color( color ); } );
}

void RGAPI_CALL rgUtilImScratchSetToPrimitive( RgMeshPrimitiveInfo* pTarget )
{
    Call( [ & ]( Device& d ) { d.ScratchIm().SetToPrimitive( pTarget ); } );
}

RgBool32 RGAPI_CALL rgUtilIsUpscaleTechniqueAvailable( RgRenderUpscaleTechnique technique,
                                                       RgFrameGenerationMode    frameGeneration,
                                                       const char**             ppFailureReason )
{
    return Call( [ & ]( Device& d ) {
        return d.IsUpscaleTechniqueAvailable( technique, frameGeneration, ppFailureReason );
    } );
}

RgBool32 RGAPI_CALL rgUtilDXGIAvailable( const char** ppFailureReason )
{
    return Call( [ & ]( Device& d ) { return d.IsDXGIAvailable( ppFailureReason ); } );
}

RgBool32 RGAPI_CALL rgUtilGetSupportedFeatures()
{
    return Call( [ & ]( Device& d ) { return d.GetSupportedFeatures(); } );
}

RgUtilMemoryUsage RGAPI_CALL rgUtilRequestMemoryUsage()
{
    return Call( [ & ]( Device& d ) { return d.RequestMemoryUsage(); } );
}

const char* RGAPI_CALL rgUtilGetResultDescription( RgResult result )
{
    return RTGL1::RgException::GetRgResultName( result );
}

RgColor4DPacked32 RGAPI_CALL rgUtilPackColorByte4D( uint8_t r, uint8_t g, uint8_t b, uint8_t a )
{
    return RTGL1::Utils::PackColor( r, g, b, a );
}

RgColor4DPacked32 RGAPI_CALL rgUtilPackColorFloat4D( float r, float g, float b, float a )
{
    return RTGL1::Utils::PackColorFromFloat( r, g, b, a );
}

RgColor4DPacked32 RGAPI_CALL rgUtilPackNormal( float x, float y, float z )
{
    return RTGL1::Utils::PackNormal( x, y, z );
}

void RGAPI_CALL rgUtilExportAsTGA( const void* pPixels,
                                   uint32_t    width,
                                   uint32_t    height,
                                   const char* pPath )
{
    RTGL1::TextureExporter::WriteTGA( pPath, pPixels, { width, height } );
}

}



extern "C"
{
RGAPI RgResult RGCONV RGAPI_CALL rgCreateInstance( const RgInstanceCreateInfo* pInfo,
                                                   RgInterface*                pInterface )
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
        auto interf = RgInterface{
            .rgCreateInstance                  = rgCreateInstance,
            .rgDestroyInstance                 = rgDestroyInstance,
            .rgStartFrame                      = rgStartFrame,
            .rgUploadCamera                    = rgUploadCamera,
            .rgUploadMeshPrimitive             = rgUploadMeshPrimitive,
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
            .rgUtilDXGIAvailable               = rgUtilDXGIAvailable,
            .rgUtilRequestMemoryUsage          = rgUtilRequestMemoryUsage,
            .rgUtilGetResultDescription        = rgUtilGetResultDescription,
            .rgUtilPackColorByte4D             = rgUtilPackColorByte4D,
            .rgUtilPackColorFloat4D            = rgUtilPackColorFloat4D,
            .rgUtilPackNormal                  = rgUtilPackNormal,
            .rgUtilExportAsTGA                 = rgUtilExportAsTGA,
            .rgUtilGetSupportedFeatures        = rgUtilGetSupportedFeatures,
            .rgSpawnFluid                      = rgSpawnFluid,
        };

        // error if DLL has less functionality, otherwise, warning
        if( pInfo->sizeOfRgInterface > sizeof( RgInterface ) )
        {
            RTGL1::debug::Error( "RTGL1.dll was compiled with sizeof(RgInterface)={}, "
                                 "but the application requires sizeof(RgInterface)={}. "
                                 "Some of the features might not work correctly",
                                 sizeof( RgInterface ),
                                 pInfo->sizeOfRgInterface );
        }
        else if( pInfo->sizeOfRgInterface < sizeof( RgInterface ) )
        {
            RTGL1::debug::Warning( "RTGL1.dll was compiled with sizeof(RgInterface)={}, "
                                   "but the application requires sizeof(RgInterface)={}",
                                   sizeof( RgInterface ),
                                   pInfo->sizeOfRgInterface );
        }

        memcpy( pInterface, &interf, std::min( sizeof( RgInterface ), pInfo->sizeOfRgInterface ) );

        // initialize everything
        g_device = std::make_unique< Device >( pInfo );
    }
    // TODO: Device must clean all the resources if initialization failed!
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
