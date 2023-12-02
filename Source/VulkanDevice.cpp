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

#include "HaltonSequence.h"
#include "Matrix.h"
#include "RenderResolutionHelper.h"
#include "RgException.h"
#include "DX12_CopyFramebuf.h"
#include "DX12_Interop.h"
#include "Utils.h"

#include "Generated/ShaderCommonC.h"

#include <algorithm>
#include <cstring>
#include <d3d12.h>
#include <d3dx12.h>

namespace RTGL1
{
namespace
{
    SwapchainType MakeSwapchainType( const RgStartFrameRenderResolutionParams& resolution )
    {
        if( resolution.frameGeneration != RG_FRAME_GENERATION_MODE_OFF )
        {
            switch( resolution.upscaleTechnique )
            {
                case RG_RENDER_UPSCALE_TECHNIQUE_AMD_FSR2:
                    return SWAPCHAIN_TYPE_FRAME_GENERATION_FSR3;

                case RG_RENDER_UPSCALE_TECHNIQUE_NVIDIA_DLSS:
                    return SWAPCHAIN_TYPE_FRAME_GENERATION_DLSS3;

                default: break;
            }
        }
        return resolution.preferDxgiPresent ? SWAPCHAIN_TYPE_DXGI //
                                            : SWAPCHAIN_TYPE_VULKAN_NATIVE;
    }
}
}

VkCommandBuffer RTGL1::VulkanDevice::BeginFrame( const RgStartFrameInfo& info )
{
    uint32_t frameIndex = currentFrameState.IncrementFrameIndexAndGet();
    timelineFrame++;

    assert( timelineFrame % dxgi::MAX_FRAMES_IN_FLIGHT_DX12 == frameIndex % MAX_FRAMES_IN_FLIGHT );

    if( !waitForOutOfFrameFence )
    {
        // wait for previous cmd with the same frame index
        Utils::WaitAndResetFence( device, frameFences[ frameIndex ] );
    }
    else
    {
        Utils::WaitAndResetFences(
            device, frameFences[ frameIndex ], outOfFrameFences[ frameIndex ] );
    }

    if( swapchain->WithDXGI() )
    {
        auto present = Semaphores_GetVkDx12Shared( dxgi::SHARED_SEM_PRESENT_COPY )
                           .value_or( dxgi::SharedSemaphore{} );
        dxgi::WaitAndPrepareForFrame( present.d3d12fence, present.d3d12fenceEvent, timelineFrame );
    }


    const auto& resolution = pnext::get< RgStartFrameRenderResolutionParams >( info );
    const auto& fluidInfo  = pnext::get< RgStartFrameFluidParams >( info );

    swapchain->AcquireImage( info.vsync,
                             info.hdr,
                             MakeSwapchainType( resolution ),
                             vkswapchainAvailableSemaphores[ frameIndex ] );
    m_skipGeneratedFrame =
        ( resolution.frameGeneration == RG_FRAME_GENERATION_MODE_WITHOUT_GENERATED );


    VkSemaphore semaphoreToWaitOnSubmit = VK_NULL_HANDLE;

    // if out-of-frame cmd exist, submit it
    {
        VkCommandBuffer preFrameCmd = currentFrameState.GetPreFrameCmdAndRemove();
        if( preFrameCmd != VK_NULL_HANDLE )
        {
            // Signal inFrameSemaphore after completion.
            // Signal outOfFrameFences, but for the next frame
            // because we can't reset cmd pool with cmds (in this case
            // it's preFrameCmd) that are in use.
            cmdManager->Submit_Binary( //
                preFrameCmd,
                {},
                inFrameSemaphores[ frameIndex ],
                outOfFrameFences[ ( frameIndex + 1 ) % MAX_FRAMES_IN_FLIGHT ] );

            // should wait other semaphore in this case
            semaphoreToWaitOnSubmit = inFrameSemaphores[ frameIndex ];

            waitForOutOfFrameFence = true;
        }
        else
        {
            waitForOutOfFrameFence = false;
        }
    }
    currentFrameState.SetSemaphore( semaphoreToWaitOnSubmit );


    if( devmode && devmode->reloadShaders )
    {
        shaderManager->ReloadShaders();
        devmode->reloadShaders = false;
    }
    sceneImportExport->PrepareForFrame( Utils::SafeCstr( info.pMapName ), info.allowMapAutoExport );

    {
        renderResolution.Setup( resolution,
                                swapchain->GetWidth(),
                                swapchain->GetHeight(),
                                amdFsr2.get(),
                                swapchain->WithFSR3FrameGeneration() ? amdFsr3dx12.get() : nullptr,
                                nvDlss2.get(),
                                swapchain->WithDLSS3FrameGeneration() ? nvDlss3dx12.get()
                                                                      : nullptr );

        framebuffers->PrepareForSize( renderResolution.GetResolutionState(),
                                      ( swapchain->WithDXGI() ) );

        m_pixelated = resolution.pixelizedRenderSizeEnable
                          ? std::optional{ resolution.pixelizedRenderSize }
                          : std::nullopt;
    }

    // reset cmds for current frame index
    cmdManager->PrepareForFrame( frameIndex );

    // clear the data that were created MAX_FRAMES_IN_FLIGHT ago
    worldSamplerManager->PrepareForFrame( frameIndex );
    genericSamplerManager->PrepareForFrame( frameIndex );
    textureManager->PrepareForFrame( frameIndex );
    cubemapManager->PrepareForFrame( frameIndex );
    rasterizer->PrepareForFrame( frameIndex );
    {
        if( m_supportsRayQueryAndPositionFetch && fluidInfo.enabled && !fluid )
        {
            fluid = std::make_shared< Fluid >( device, //
                                               cmdManager,
                                               memAllocator,
                                               framebuffers,
                                               *shaderManager,
                                               scene->GetASManager()->GetTLASDescSetLayout(),
                                               fluidInfo.particleBudget,
                                               fluidInfo.particleRadius );
            shaderManager->Subscribe( fluid );
            framebuffers->Subscribe( fluid );
            fluid->OnFramebuffersSizeChange( renderResolution.GetResolutionState() );
        }
        else if( !fluidInfo.enabled && fluid )
        {
            fluid.reset();
        }
        fluidGravity = fluidInfo.gravity;
        fluidColor   = fluidInfo.color;
    }
    if( debugWindows )
    {
        if( !debugWindows->PrepareForFrame( frameIndex, info.vsync ) )
        {
            debugWindows.reset();
            observer.reset();
        }
    }
    if( devmode )
    {
        devmode->primitivesTable.clear();
    }

    VkCommandBuffer cmd = cmdManager->StartGraphicsCmd();
    BeginCmdLabel( cmd, "Prepare for frame" );

    textureManager->TryHotReload( cmd, frameIndex );
    lightManager->PrepareForFrame( cmd, frameIndex );
    lightManager->SetLightstyles( info );
    scene->PrepareForFrame( cmd,
                            frameIndex,
                            info.ignoreExternalGeometry ||
                                ( devmode && devmode->ignoreExternalGeometry ),
                            info.staticSceneAnimationTime );

    {
        sceneImportExport->TryImportIfNew( cmd,
                                           frameIndex,
                                           *scene,
                                           *textureManager,
                                           *textureMetaManager,
                                           *lightManager,
                                           info.pResultStaticSceneStatus );

        scene->SubmitStaticLights(
            frameIndex,
            *lightManager,
            // SHIPPING_HACK
            uniform->GetData()->volumeAllowTintUnderwater &&
                uniform->GetData()->cameraMediaType == RG_MEDIA_TYPE_WATER,
            Utils::PackColorFromFloat( uniform->GetData()->volumeUnderwaterColor ) );
    }

    {
        lightmapScreenCoverage = info.lightmapScreenCoverage < 0.01f ? 0.0f
                                 : info.lightmapScreenCoverage > 0.99f
                                     ? 1.0f
                                     : info.lightmapScreenCoverage;
    }

    if( fluid )
    {
        fluid->PrepareForFrame( fluidInfo.reset );
    }

    return cmd;
}

void RTGL1::VulkanDevice::FillUniform( RTGL1::ShGlobalUniform* gu,
                                       const RgDrawFrameInfo&  drawInfo ) const
{
    const float IdentityMat4x4[ 16 ] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

    const float aspect = renderResolution.Aspect();

    const auto& cameraInfo = scene->GetCamera( renderResolution.Aspect() );

    {
        memcpy( gu->viewPrev, gu->view, 16 * sizeof( float ) );
        memcpy( gu->projectionPrev, gu->projection, 16 * sizeof( float ) );

        memcpy( gu->view, cameraInfo.view, 16 * sizeof( float ) );
        memcpy( gu->projection, cameraInfo.projection, 16 * sizeof( float ) );

        memcpy( gu->invView, cameraInfo.viewInverse, 16 * sizeof( float ) );
        memcpy( gu->invProjection, cameraInfo.projectionInverse, 16 * sizeof( float ) );

        memcpy( gu->cameraPositionPrev, gu->cameraPosition, 3 * sizeof( float ) );

        auto p = MakeCameraPosition( cameraInfo );
        {
            gu->cameraPosition[ 0 ] = p.data[ 0 ];
            gu->cameraPosition[ 1 ] = p.data[ 1 ];
            gu->cameraPosition[ 2 ] = p.data[ 2 ];
        }
    }

    {
        gu->frameId   = frameId;
        gu->timeDelta = static_cast< float >(
            std::max< double >( currentFrameTime - previousFrameTime, 0.001 ) );
        gu->time = static_cast< float >( currentFrameTime );
    }

    {
        gu->renderWidth  = static_cast< float >( renderResolution.Width() );
        gu->renderHeight = static_cast< float >( renderResolution.Height() );
        // render width must be always even for checkerboarding!
        assert( ( int )gu->renderWidth % 2 == 0 );

        gu->upscaledRenderWidth  = static_cast< float >( renderResolution.UpscaledWidth() );
        gu->upscaledRenderHeight = static_cast< float >( renderResolution.UpscaledHeight() );

        RgFloat2D jitter = { 0, 0 };
        if( renderResolution.IsNvDlssEnabled() )
        {
            jitter = HaltonSequence::GetJitter_Halton23( frameId );
        }
        else if( renderResolution.IsAmdFsr2Enabled() )
        {
            if( amdFsr3dx12 )
            {
                jitter = amdFsr3dx12->GetJitter( renderResolution.GetResolutionState(), frameId );
            }
            else if( amdFsr2 )
            {
                jitter = amdFsr2->GetJitter( renderResolution.GetResolutionState(), frameId );
            }
            else
            {
                assert( 0 );
            }
        }

        gu->jitterX = jitter.data[ 0 ];
        gu->jitterY = jitter.data[ 1 ];
    }

    {
        const auto& params = pnext::get< RgDrawFrameTonemappingParams >( drawInfo );

        float luminanceMin = std::exp2( params.ev100Min ) * 12.5f / 100.0f;
        float luminanceMax = std::exp2( params.ev100Max ) * 12.5f / 100.0f;

        gu->stopEyeAdaptation   = params.disableEyeAdaptation;
        gu->minLogLuminance     = std::log2( luminanceMin );
        gu->maxLogLuminance     = std::log2( luminanceMax );
        gu->luminanceWhitePoint = params.luminanceWhitePoint;
    }

    {
        gu->lightCount     = lightManager->GetLightCount();
        gu->lightCountPrev = lightManager->GetLightCountPrev();

        gu->directionalLightExists = lightManager->DoesDirectionalLightExist();
    }

    {
        const auto& params = pnext::get< RgDrawFrameSkyParams >( drawInfo );

        static_assert( sizeof( gu->skyCubemapRotationTransform ) == sizeof( IdentityMat4x4 ) &&
                           sizeof( IdentityMat4x4 ) == 16 * sizeof( float ),
                       "Recheck skyCubemapRotationTransform sizes" );
        memcpy( gu->skyCubemapRotationTransform, IdentityMat4x4, 16 * sizeof( float ) );


        RG_SET_VEC3_A( gu->skyColorDefault, params.skyColorDefault.data );
        gu->skyColorMultiplier = std::max( 0.0f, params.skyColorMultiplier );
        gu->skyColorSaturation = std::max( 0.0f, params.skyColorSaturation );

        switch( params.skyType )
        {
            case RG_SKY_TYPE_COLOR: {
                gu->skyType = SKY_TYPE_COLOR;
                break;
            }
            case RG_SKY_TYPE_CUBEMAP: {
                gu->skyType = SKY_TYPE_CUBEMAP;
                break;
            }
            case RG_SKY_TYPE_RASTERIZED_GEOMETRY: {
                gu->skyType = SKY_TYPE_RASTERIZED_GEOMETRY;
                break;
            }
            default: gu->skyType = SKY_TYPE_COLOR;
        }

        gu->skyCubemapIndex =
            cubemapManager->TryGetDescriptorIndex( params.pSkyCubemapTextureName );

        if( !Utils::IsAlmostZero( params.skyCubemapRotationTransform ) )
        {
            Utils::SetMatrix3ToGLSLMat4( gu->skyCubemapRotationTransform,
                                         params.skyCubemapRotationTransform );
        }

        RgFloat3D skyViewerPosition = params.skyViewerPosition;

        for( uint32_t i = 0; i < 6; i++ )
        {
            float* viewProjDst = &gu->viewProjCubemap[ 16 * i ];

            Matrix::GetCubemapViewProjMat( viewProjDst,
                                           i,
                                           skyViewerPosition.data,
                                           cameraInfo.cameraNear,
                                           cameraInfo.cameraFar );
        }
    }

    gu->debugShowFlags = devmode ? devmode->debugShowFlags : 0;

    {
        const auto& params = pnext::get< RgDrawFrameTexturesParams >( drawInfo );

        gu->normalMapStrength      = params.normalMapStrength;
        gu->emissionMapBoost       = std::max( params.emissionMapBoost, 0.0f );
        gu->emissionMaxScreenColor = std::max( params.emissionMaxScreenColor, 0.0f );
        gu->minRoughness           = std::clamp( params.minRoughness, 0.0f, 1.0f );
        gu->parallaxMaxDepth       = std::max( params.heightMapDepth, 0.0f );
    }

    {
        const auto& params = pnext::get< RgDrawFrameIlluminationParams >( drawInfo );

        gu->maxBounceShadowsLights     = params.maxBounceShadows;
        gu->polyLightSpotlightFactor   = std::max( 0.0f, params.polygonalLightSpotlightFactor );
        gu->indirSecondBounce          = !!params.enableSecondBounceForIndirect;
        gu->lightIndexIgnoreFPVShadows = lightManager->GetLightIndexForShaders(
            currentFrameState.GetFrameIndex(), params.lightUniqueIdIgnoreFirstPersonViewerShadows );
        gu->cellWorldSize       = std::max( params.cellWorldSize, 0.001f );
        gu->gradientMultDiffuse = std::clamp( params.directDiffuseSensitivityToChange, 0.0f, 1.0f );
        gu->gradientMultIndirect =
            std::clamp( params.indirectDiffuseSensitivityToChange, 0.0f, 1.0f );
        gu->gradientMultSpecular = std::clamp( params.specularSensitivityToChange, 0.0f, 1.0f );
    }

    {
        const auto& params = pnext::get< RgDrawFrameBloomParams >( drawInfo );

        gu->bloomThreshold    = std::max( params.inputThreshold, 0.0f );
        gu->bloomIntensity    = 0.2f * std::max( params.bloomIntensity, 0.0f );
        gu->bloomEV           = std::max( params.inputEV, 0.0f );
        gu->lensDirtIntensity = std::max( params.lensDirtIntensity, 0.0f );
    }

    {
        const auto& params = pnext::get< RgDrawFrameReflectRefractParams >( drawInfo );

        switch( params.typeOfMediaAroundCamera )
        {
            case RG_MEDIA_TYPE_VACUUM: gu->cameraMediaType = MEDIA_TYPE_VACUUM; break;
            case RG_MEDIA_TYPE_WATER: gu->cameraMediaType = MEDIA_TYPE_WATER; break;
            case RG_MEDIA_TYPE_GLASS: gu->cameraMediaType = MEDIA_TYPE_GLASS; break;
            case RG_MEDIA_TYPE_ACID: gu->cameraMediaType = MEDIA_TYPE_ACID; break;
            default: gu->cameraMediaType = MEDIA_TYPE_VACUUM;
        }

        gu->reflectRefractMaxDepth = std::min( 16u, params.maxReflectRefractDepth );

        gu->indexOfRefractionGlass = std::max( 0.0f, params.indexOfRefractionGlass );
        gu->indexOfRefractionWater = std::max( 0.0f, params.indexOfRefractionWater );
        gu->thinMediaWidth         = std::max( 0.0f, params.thinMediaWidth );

        memcpy( gu->waterColorAndDensity, params.waterColor.data, 3 * sizeof( float ) );
        gu->waterColorAndDensity[ 3 ] = 0.0f;

        memcpy( gu->acidColorAndDensity, params.acidColor.data, 3 * sizeof( float ) );
        gu->acidColorAndDensity[ 3 ] = std::max( 0.0f, params.acidDensity );

        gu->waterWaveSpeed    = params.waterWaveSpeed;
        gu->waterWaveStrength = params.waterWaveNormalStrength;
        gu->waterTextureDerivativesMultiplier =
            std::max( 0.0f, params.waterWaveTextureDerivativesMultiplier );
        gu->waterTextureAreaScale =
            params.waterTextureAreaScale < 0.0001f ? 1.0f : params.waterTextureAreaScale;

        gu->twirlPortalNormal = !!params.portalNormalTwirl;
    }

    gu->rayCullBackFaces  = rayCullBackFacingTriangles ? 1 : 0;
    gu->rayLength         = clamp( drawInfo.rayLength, 0.1f, float( MAX_RAY_LENGTH ) );
    gu->primaryRayMinDist = clamp( cameraInfo.cameraNear, 0.001f, gu->rayLength );

    {
        gu->rayCullMaskWorld =
            INSTANCE_MASK_WORLD_0 | INSTANCE_MASK_WORLD_1 | INSTANCE_MASK_WORLD_2;

        // skip shadows for:
        // WORLD_1 - 'no shadows' geometry
        // WORLD_2 - 'sky' geometry
        gu->rayCullMaskWorld_Shadow = INSTANCE_MASK_WORLD_0;
    }

    gu->waterNormalTextureIndex = textureManager->GetWaterNormalTextureIndex();
    gu->dirtMaskTextureIndex    = textureManager->GetDirtMaskTextureIndex();

    gu->cameraRayConeSpreadAngle = atanf( ( 2.0f * tanf( cameraInfo.fovYRadians * 0.5f ) ) /
                                          float( renderResolution.Height() ) );

    RG_SET_VEC3_A( gu->worldUpVector, sceneImportExport->GetWorldUp().data );

    gu->lightmapScreenCoverage = lightmapScreenCoverage;

    {
        gu->fluidEnabled = fluid && fluid->Active();
        RG_SET_VEC3_A( gu->fluidColor, fluidColor.data );
    }

    {
        const auto& params = pnext::get< RgDrawFrameVolumetricParams >( drawInfo );

        gu->volumeCameraNear = std::max( cameraInfo.cameraNear, 0.001f );
        gu->volumeCameraFar  = std::min( cameraInfo.cameraFar, params.volumetricFar );

        {
            if( params.enable )
            {
                gu->volumeEnableType =
                    params.useSimpleDepthBased ? VOLUME_ENABLE_SIMPLE : VOLUME_ENABLE_VOLUMETRIC;
            }
            else
            {
                gu->volumeEnableType = VOLUME_ENABLE_NONE;
            }
            gu->volumeScattering = params.scaterring;
            gu->volumeAsymmetry  = std::clamp( params.assymetry, -1.0f, 1.0f );

            RG_SET_VEC3_A( gu->volumeAmbient, params.ambientColor.data );
            RG_MAX_VEC3( gu->volumeAmbient, 0.0f );

#if ILLUMINATION_VOLUME
            gu->illumVolumeEnable = params.useIlluminationVolume;
#else
            gu->illumVolumeEnable = 0;
#endif

            if( auto uniqueId = scene->TryGetVolumetricLight( *lightManager,
                                                              MakeCameraPosition( cameraInfo ) ) )
            {
                gu->volumeLightSourceIndex = lightManager->GetLightIndexForShaders(
                    currentFrameState.GetFrameIndex(), &uniqueId.value() );
            }
            else
            {
                gu->volumeLightSourceIndex = LIGHT_INDEX_NONE;
            }

            RG_SET_VEC3_A( gu->volumeFallbackSrcColor, params.fallbackSourceColor.data );
            RG_MAX_VEC3( gu->volumeFallbackSrcColor, 0.0f );

            RG_SET_VEC3_A( gu->volumeFallbackSrcDirection, params.fallbackSourceDirection.data );

            gu->volumeFallbackSrcExists = Utils::TryNormalize( gu->volumeFallbackSrcDirection ) &&
                                          ( gu->volumeFallbackSrcColor[ 0 ] > 0.01f &&
                                            gu->volumeFallbackSrcColor[ 1 ] > 0.01f &&
                                            gu->volumeFallbackSrcColor[ 2 ] > 0.01f );

            gu->volumeLightMult = std::max( 0.0f, params.lightMultiplier );

            gu->volumeAllowTintUnderwater = params.allowTintUnderwater;
            RG_SET_VEC3_A( gu->volumeUnderwaterColor, params.underwaterColor.data );
            RG_MAX_VEC3( gu->volumeUnderwaterColor, 0.0f );
        }

        if( gu->volumeEnableType != VOLUME_ENABLE_NONE )
        {
            memcpy( gu->volumeViewProj_Prev, gu->volumeViewProj, 16 * sizeof( float ) );
            memcpy( gu->volumeViewProjInv_Prev, gu->volumeViewProjInv, 16 * sizeof( float ) );

            float volumeproj[ 16 ];
            Matrix::MakeProjectionMatrix( volumeproj,
                                          aspect,
                                          cameraInfo.fovYRadians,
                                          gu->volumeCameraNear,
                                          gu->volumeCameraFar );

            Matrix::Multiply( gu->volumeViewProj, gu->view, volumeproj );
            Matrix::Inverse( gu->volumeViewProjInv, gu->volumeViewProj );
        }
    }

    gu->antiFireflyEnabled = devmode ? devmode->antiFirefly : true;

    if( swapchain->IsHDREnabled() )
    {
        gu->hdrDisplay = swapchain->IsST2084ColorSpace() ? HDR_DISPLAY_ST2084 : HDR_DISPLAY_LINEAR;
    }
    else
    {
        gu->hdrDisplay = HDR_DISPLAY_NONE;
    }
}

auto RTGL1::VulkanDevice::Render( VkCommandBuffer& cmd, const RgDrawFrameInfo& drawInfo )
    -> FramebufferImageIndex
{
    // end of "Prepare for frame" label
    EndCmdLabel( cmd );


    const uint32_t frameIndex = currentFrameState.GetFrameIndex();
    const double   timeDelta  = std::max< double >( currentFrameTime - previousFrameTime, 0.0001 );
    const bool     resetHistory = drawInfo.resetHistory;


    const auto& cameraInfo = scene->GetCamera( renderResolution.Aspect() );

    bool mipLodBiasUpdated =
        worldSamplerManager->TryChangeMipLodBias( frameIndex, renderResolution.GetMipLodBias() );
    const RgFloat2D jitter = { uniform->GetData()->jitterX, uniform->GetData()->jitterY };

    textureManager->SubmitDescriptors(
        frameIndex, pnext::get< RgDrawFrameTexturesParams >( drawInfo ), mipLodBiasUpdated );
    cubemapManager->SubmitDescriptors( frameIndex );

    lightManager->SubmitForFrame( cmd, frameIndex );

    uniform->Upload( cmd, frameIndex );

    // submit geometry and upload uniform after getting data from a scene
    scene->SubmitForFrame( cmd,
                           frameIndex,
                           uniform,
                           uniform->GetData()->rayCullMaskWorld,
                           drawInfo.disableRayTracedGeometry );

    if( drawInfo.presentPrevFrame )
    {
        return m_prevAccum;
    }

    if( auto w = pnext::get< RgDrawFramePostEffectsParams >( drawInfo ).pWipe )
    {
        effectWipe->CopyToWipeEffectSourceIfNeeded( cmd, //
                                                    frameIndex,
                                                    *framebuffers,
                                                    m_prevAccum,
                                                    renderResolution.GetResolutionState(),
                                                    w );
    }

    if( !drawInfo.disableRasterization )
    {
        rasterizer->SubmitForFrame( cmd, frameIndex );

        // draw rasterized sky to albedo before tracing primary rays
        if( uniform->GetData()->skyType == RG_SKY_TYPE_RASTERIZED_GEOMETRY )
        {
            rasterizer->DrawSkyToCubemap( cmd, frameIndex, *textureManager, *uniform );
            rasterizer->DrawSkyToAlbedo(
                cmd,
                frameIndex,
                *textureManager,
                cameraInfo.view,
                pnext::get< RgDrawFrameSkyParams >( drawInfo ).skyViewerPosition,
                cameraInfo.projection,
                jitter,
                renderResolution );
        }

        if( fluid && !( devmode && devmode->fluidStopVisualize ) )
        {
            fluid->Visualize( cmd,
                              frameIndex,
                              cameraInfo.view,
                              cameraInfo.projection,
                              renderResolution,
                              cameraInfo.cameraNear,
                              cameraInfo.cameraFar );
        }
    }


    {
        lightGrid->Build( cmd, frameIndex, uniform, blueNoise, lightManager );

        portalList->SubmitForFrame( cmd, frameIndex );

        float volumetricMaxHistoryLen =
            resetHistory ? 0
                         : pnext::get< RgDrawFrameVolumetricParams >( drawInfo ).maxHistoryLength;

        const auto params = pathTracer->BindRayTracing( cmd,
                                                        frameIndex,
                                                        renderResolution.Width(),
                                                        renderResolution.Height(),
                                                        *scene,
                                                        *uniform,
                                                        *textureManager,
                                                        framebuffers,
                                                        restirBuffers,
                                                        *blueNoise,
                                                        *lightManager,
                                                        *cubemapManager,
                                                        *rasterizer->GetRenderCubemap(),
                                                        *portalList,
                                                        *volumetric );

        pathTracer->TracePrimaryRays( params );

        // draw decals on top of primary surface
        rasterizer->DrawDecals( cmd,
                                frameIndex,
                                *uniform,
                                *textureManager,
                                cameraInfo.view,
                                cameraInfo.projection,
                                jitter,
                                renderResolution );

        if( uniform->GetData()->reflectRefractMaxDepth > 0 )
        {
            pathTracer->TraceReflectionRefractionRays( params );
        }

        lightManager->BarrierLightGrid( cmd, frameIndex );
        pathTracer->CalculateInitialReservoirs( params );
        pathTracer->TraceDirectllumination( params );
        pathTracer->TraceIndirectllumination( params );
        pathTracer->TraceVolumetric( params );

        if( fluid )
        {
            fluid->Simulate( cmd,
                             frameIndex,
                             scene->GetASManager()->GetTLASDescSet( frameIndex ),
                             float( timeDelta ),
                             fluidGravity );
        }

        pathTracer->CalculateGradientsSamples( params );
        pathTracer->FinalizeIndirectIllumination_Compute( cmd,
                                                          frameIndex,
                                                          renderResolution.Width(),
                                                          renderResolution.Height(),
                                                          *scene,
                                                          *uniform,
                                                          *textureManager,
                                                          *framebuffers,
                                                          *restirBuffers,
                                                          *blueNoise,
                                                          *lightManager,
                                                          *cubemapManager,
                                                          *rasterizer->GetRenderCubemap(),
                                                          *portalList,
                                                          *volumetric );
        denoiser->Denoise( cmd, frameIndex, uniform );
        volumetric->ProcessScattering(
            cmd, frameIndex, *uniform, *blueNoise, *framebuffers, volumetricMaxHistoryLen );
        tonemapping->CalculateExposure( cmd, frameIndex, uniform );
    }

    imageComposition->PrepareForRaster( cmd, frameIndex, uniform.get() );
    volumetric->BarrierToReadIllumination( cmd );

    if( !drawInfo.disableRasterization )
    {
        // draw rasterized geometry into the final image
        rasterizer->DrawToFinalImage( cmd,
                                      frameIndex,
                                      *textureManager,
                                      *uniform,
                                      *tonemapping,
                                      *volumetric,
                                      cameraInfo.view,
                                      cameraInfo.projection,
                                      jitter,
                                      renderResolution,
                                      lightmapScreenCoverage );
    }

    imageComposition->Finalize( cmd,
                                frameIndex,
                                *uniform,
                                *tonemapping,
                                pnext::get< RgDrawFrameTonemappingParams >( drawInfo ) );

    FramebufferImageIndex accum       = FB_IMAGE_INDEX_FINAL;
    bool                  needHudOnly = false;
    {
        auto l_todx12 = [ this, frameIndex ]( VkCommandBuffer vkcmd,
                                              auto& technique ) -> ID3D12GraphicsCommandList* {
            if( !dxgi::HasDX12Instance() )
            {
                return nullptr;
            }
            technique.CopyVkInputsToDX12( vkcmd, //
                                          frameIndex,
                                          *framebuffers,
                                          renderResolution.GetResolutionState() );

            const VkSemaphore initFrameFinished = currentFrameState.GetSemaphoreForWaitAndRemove();

            auto vktodx12 = Semaphores_GetVkDx12Shared( dxgi::SHARED_SEM_FSR3_IN );
            if( !vktodx12 )
            {
                return nullptr;
            }

            cmdManager->Submit_Timeline( //
                vkcmd,
                nullptr,
                ToWait{ initFrameFinished, SEMAPHORE_IS_BINARY },
                ToSignal{ vktodx12->vksemaphore, timelineFrame } );

            ID3D12CommandQueue* dx12queue = dxgi::GetD3D12CommandQueue();
            if( !dx12queue )
            {
                return nullptr;
            }
            HRESULT hr = dx12queue->Wait( vktodx12->d3d12fence, timelineFrame );
            assert( SUCCEEDED( hr ) );

            return dxgi::CreateD3D12CommandList( frameIndex );
        };
        auto l_tovk = [ this, frameIndex ]( ID3D12GraphicsCommandList* dx12cmd,
                                            auto& technique ) -> VkCommandBuffer {
            if( !dx12cmd || !dxgi::HasDX12Instance() )
            {
                currentFrameState.SetSemaphore( nullptr );
                return cmdManager->StartGraphicsCmd();
            }
            HRESULT hr = dx12cmd->Close();
            assert( SUCCEEDED( hr ) );

            auto dx12tovk = Semaphores_GetVkDx12Shared( dxgi::SHARED_SEM_FSR3_OUT );
            if( !dx12tovk )
            {
                return nullptr;
            }

            ID3D12CommandQueue* dx12queue = dxgi::GetD3D12CommandQueue();
            if( !dx12queue )
            {
                return nullptr;
            }
            ID3D12CommandList* p = dx12cmd;
            dx12queue->ExecuteCommandLists( 1, &p );
            hr = dx12queue->Signal( dx12tovk->d3d12fence, timelineFrame );

            // next cmd should wait for DX12
            currentFrameState.SetSemaphore( SUCCEEDED( hr ) ? dx12tovk->vksemaphore : nullptr );
            auto vkcmd = cmdManager->StartGraphicsCmd();

            technique.CopyDX12OutputToVk( vkcmd, //
                                          frameIndex,
                                          *framebuffers,
                                          renderResolution.GetResolutionState() );
            return vkcmd;
        };

        // upscale finalized image
        if( renderResolution.IsNvDlssEnabled() )
        {
            if( nvDlss3dx12 && swapchain->WithDLSS3FrameGeneration() )
            {
                ID3D12GraphicsCommandList* dx12cmd = l_todx12( cmd, *nvDlss3dx12 );

                if( auto u = nvDlss3dx12->Apply( dx12cmd,
                                                 frameIndex,
                                                 *framebuffers,
                                                 renderResolution,
                                                 jitter,
                                                 timeDelta,
                                                 resetHistory,
                                                 cameraInfo,
                                                 frameId,
                                                 m_skipGeneratedFrame ) )
                {
                    accum = *u;
                    needHudOnly = false; // providing FB_IMAGE_INDEX_HUD_ONLY to DLSS3 doesn't work
                }
                else
                {
                    swapchain->MarkAsFailed( SWAPCHAIN_TYPE_FRAME_GENERATION_DLSS3 );
                }

                cmd = l_tovk( dx12cmd, *nvDlss3dx12 );
            }
            else if( nvDlss2 )
            {
                accum = nvDlss2->Apply( cmd,
                                        frameIndex,
                                        *framebuffers,
                                        renderResolution,
                                        jitter,
                                        timeDelta,
                                        resetHistory );
            }
            else
            {
                assert( 0 );
            }
        }
        else if( renderResolution.IsAmdFsr2Enabled() )
        {
            if( amdFsr3dx12 && swapchain->WithFSR3FrameGeneration() )
            {
                ID3D12GraphicsCommandList* dx12cmd = l_todx12( cmd, *amdFsr3dx12 );

                if( auto u = amdFsr3dx12->Apply( dx12cmd,
                                                 frameIndex,
                                                 *framebuffers,
                                                 renderResolution,
                                                 jitter,
                                                 timeDelta,
                                                 cameraInfo.cameraNear,
                                                 cameraInfo.cameraFar,
                                                 cameraInfo.fovYRadians,
                                                 resetHistory,
                                                 sceneImportExport->GetWorldScale(),
                                                 m_skipGeneratedFrame ) )
                {
                    accum = *u;
                    needHudOnly = true;
                }
                else
                {
                    swapchain->MarkAsFailed( SWAPCHAIN_TYPE_FRAME_GENERATION_FSR3 );
                }

                cmd = l_tovk( dx12cmd, *amdFsr3dx12 );
            }
            else if( amdFsr2 )
            {
                accum = amdFsr2->Apply( cmd,
                                        frameIndex,
                                        *framebuffers,
                                        renderResolution,
                                        jitter,
                                        timeDelta,
                                        cameraInfo.cameraNear,
                                        cameraInfo.cameraFar,
                                        cameraInfo.fovYRadians,
                                        resetHistory,
                                        sceneImportExport->GetWorldScale() );
            }
            else
            {
                assert( 0 );
            }
        }

        if( lightmapScreenCoverage > 0 && !drawInfo.disableRasterization )
        {
            rasterizer->DrawClassic(
                cmd,
                frameIndex,
                accum,
                *textureManager,
                *uniform,
                *tonemapping,
                *volumetric,
                cameraInfo.view,
                cameraInfo.projection,
                renderResolution,
                lightmapScreenCoverage,
                pnext::get< RgDrawFrameSkyParams >( drawInfo ).skyViewerPosition );
        }

        accum = framebuffers->BlitForEffects( cmd,
                                              frameIndex,
                                              accum,
                                              renderResolution.GetBlitFilter(),
                                              m_pixelated ? &m_pixelated.value() : nullptr );
    }


    const auto args = CommonnlyUsedEffectArguments{
        .cmd          = cmd,
        .frameIndex   = frameIndex,
        .framebuffers = framebuffers,
        .uniform      = uniform,
        .width        = renderResolution.UpscaledWidth(),
        .height       = renderResolution.UpscaledHeight(),
        .currentTime  = float( currentFrameTime ),
    };

    {
        if( renderResolution.IsDedicatedSharpeningEnabled() )
        {
            accum = sharpening->Apply( cmd,
                                       frameIndex,
                                       framebuffers,
                                       renderResolution.UpscaledWidth(),
                                       renderResolution.UpscaledHeight(),
                                       accum,
                                       renderResolution.GetSharpeningTechnique(),
                                       renderResolution.GetSharpeningIntensity() );
        }

        if( pnext::get< RgDrawFrameBloomParams >( drawInfo ).bloomIntensity > 0.0f )
        {
            accum = bloom->Apply( cmd,
                                  frameIndex,
                                  *uniform,
                                  *tonemapping,
                                  *textureManager,
                                  renderResolution.UpscaledWidth(),
                                  renderResolution.UpscaledHeight(),
                                  accum );
        }

        auto l_applyIf = [ &args ]( auto&                 effect,
                                    auto&                 setupArg,
                                    FramebufferImageIndex input ) -> FramebufferImageIndex {
            return effect->Setup( args, setupArg ) ? effect->Apply( args, input ) : input;
        };

        const auto& postef = pnext::get< RgDrawFramePostEffectsParams >( drawInfo );

        accum = l_applyIf( effectTeleport, postef.pTeleport, accum );
        accum = l_applyIf( effectColorTint, postef.pColorTint, accum );
        accum = l_applyIf( effectInverseBW, postef.pInverseBlackAndWhite, accum );
        accum = l_applyIf( effectHueShift, postef.pHueShift, accum );
        accum = l_applyIf( effectNightVision, postef.pNightVision, accum );
        accum = l_applyIf( effectChromaticAberration, postef.pChromaticAberration, accum );
        accum = l_applyIf( effectDistortedSides, postef.pDistortedSides, accum );
        accum = l_applyIf( effectWaves, postef.pWaves, accum );
        accum = l_applyIf( effectRadialBlur, postef.pRadialBlur, accum );
        accum = l_applyIf( effectVHS, postef.pVHS, accum );
    }

    // draw geometry such as HUD into an upscaled framebuf
    if( !drawInfo.disableRasterization )
    {
        if( !needHudOnly )
        {
            framebuffers->BarrierOne(
                cmd, frameIndex, accum, RTGL1::Framebuffers::BarrierType::Storage );

            rasterizer->DrawToSwapchain( cmd,
                                         frameIndex,
                                         accum,
                                         *textureManager,
                                         uniform->GetData()->view,
                                         uniform->GetData()->projection,
                                         renderResolution.UpscaledWidth(),
                                         renderResolution.UpscaledHeight(),
                                         swapchain->IsHDREnabled() );
        }
        else
        {
            rasterizer->DrawToSwapchain( cmd,
                                         frameIndex,
                                         FB_IMAGE_INDEX_HUD_ONLY,
                                         *textureManager,
                                         uniform->GetData()->view,
                                         uniform->GetData()->projection,
                                         renderResolution.UpscaledWidth(),
                                         renderResolution.UpscaledHeight(),
                                         swapchain->IsHDREnabled() );

            FramebufferImageIndex todx12[] = { FB_IMAGE_INDEX_HUD_ONLY };
            Framebuf_CopyVkToDX12( cmd,
                                   frameIndex,
                                   *framebuffers,
                                   renderResolution.UpscaledWidth(),
                                   renderResolution.UpscaledHeight(),
                                   todx12 );
        }
    }

    // post-effect that work on swapchain geometry too
    {
        const auto& postef = pnext::get< RgDrawFramePostEffectsParams >( drawInfo );

        if( effectWipe->Setup( args, postef.pWipe, frameId ) )
        {
            accum = effectWipe->Apply( args, *blueNoise, accum );
        }

        if( effectDither->Setup( args, postef.pDither ) )
        {
            accum = effectDither->Apply( args, accum );
        }

        if( postef.pCRT != nullptr && postef.pCRT->isActive )
        {
            effectCrtDemodulateEncode->Setup( args );
            accum = effectCrtDemodulateEncode->Apply( args, accum );

            effectCrtDecode->Setup( args );
            accum = effectCrtDecode->Apply( args, accum );
        }
    }

    // convert scene HDR to a present HDR compatible space,
    // or apply a tonemapping to fit into LDR
    {
        const auto& tnmp = pnext::get< RgDrawFrameTonemappingParams >( drawInfo );

        VkDescriptorSet lpmDescSet =
            imageComposition->SetupLpmParams( cmd, frameIndex, tnmp, swapchain->IsHDREnabled() );
        effectHDRPrepare->Setup( args, tnmp );

        VkDescriptorSet descSets[] = {
            args.framebuffers->GetDescSet( args.frameIndex ),
            args.uniform->GetDescSet( args.frameIndex ),
            lpmDescSet,
        };
        accum = effectHDRPrepare->Apply( descSets, args, accum );
    }

    m_prevAccum = accum;
    return accum;
}

#if 0
namespace
{
void WaitTimelineAndSignalBinary( VkQueue     q,
                                  VkSemaphore towait_timeline,
                                  uint64_t    towait_value,
                                  VkSemaphore tosignal_binary )
{
    VkPipelineStageFlags s = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

    auto timeline = VkTimelineSemaphoreSubmitInfo{
        .sType                     = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
        .pNext                     = nullptr,
        .waitSemaphoreValueCount   = 1,
        .pWaitSemaphoreValues      = &towait_value,
        .signalSemaphoreValueCount = 0,
        .pSignalSemaphoreValues    = nullptr,
    };

    auto info = VkSubmitInfo{
        .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext                = &timeline,
        .waitSemaphoreCount   = 1,
        .pWaitSemaphores      = &towait_timeline,
        .pWaitDstStageMask    = &s,
        .commandBufferCount   = 0,
        .pCommandBuffers      = nullptr,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores    = &tosignal_binary,
    };

    VkResult r = vkQueueSubmit( q, 1, &info, VK_NULL_HANDLE );
    RTGL1::VK_CHECKERROR( r );
}
}
#endif

void RTGL1::VulkanDevice::EndFrame( VkCommandBuffer cmd, FramebufferImageIndex rendered )
{
    auto label = CmdLabel{ cmd, "Blit to swapchain" };

    const uint32_t    frameIndex        = currentFrameState.GetFrameIndex();
    const VkSemaphore initFrameFinished = currentFrameState.GetSemaphoreForWaitAndRemove();

    // present debug window
    if( debugWindows && !debugWindows->IsMinimized() )
    {
        VkCommandBuffer debugCmd = cmdManager->StartGraphicsCmd();
        debugWindows->SubmitForFrame( debugCmd, frameIndex );

        VkSemaphore towait[] = {
            debugWindows->GetSwapchainImageAvailableSemaphore_Binary( frameIndex ),
        };
        cmdManager->Submit_Binary( //
            debugCmd,
            towait,
            debugFinishedSemaphores[ frameIndex ], // signal
            VK_NULL_HANDLE );

        VkResult       r       = VK_SUCCESS;
        VkSwapchainKHR sw      = debugWindows->GetSwapchainHandle();
        uint32_t       swIndex = debugWindows->GetSwapchainCurrentImageIndex();

        auto presentInfo = VkPresentInfoKHR{
            .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores    = &debugFinishedSemaphores[ frameIndex ],
            .swapchainCount     = 1,
            .pSwapchains        = &sw,
            .pImageIndices      = &swIndex,
            .pResults           = &r,
        };
        vkQueuePresentKHR( queues->GetGraphics(), &presentInfo );
        debugWindows->OnQueuePresent( r );
    }

    if( nvDlss3dx12 )
    {
        nvDlss3dx12->Reflex_RenderEnd();
        nvDlss3dx12->Reflex_PresentStart();
    }


    const auto rendered_size =
        framebuffers->GetFramebufSize( renderResolution.GetResolutionState(), rendered );


    // present
    if( swapchain->WithDXGI() )
    {
        [ & ] {
            ID3D12CommandQueue* dx12queue = dxgi::GetD3D12CommandQueue();
            if( !dx12queue )
            {
                return;
            }

            // copy vk to dx12 buffer
            {
                FramebufferImageIndex fs[] = {
                    rendered,
                };
                Framebuf_CopyVkToDX12( cmd, //
                                       frameIndex,
                                       *framebuffers,
                                       rendered_size.width,
                                       rendered_size.height,
                                       fs );
            }
            // submit vk, and wait for vk in dx12
            {
                auto renderFin = Semaphores_GetVkDx12Shared( dxgi::SHARED_SEM_RENDER_FINISHED );
                if( !renderFin )
                {
                    debug::Warning( "Skipping DXGI present, as Semaphores_GetVkDx12Shared failed" );
                    return;
                }

                cmdManager->Submit_Timeline( //
                    cmd,
                    frameFences[ frameIndex ],
                    ToWait{ initFrameFinished, timelineFrame },
                    ToSignal{ renderFin->vksemaphore, timelineFrame } );

                HRESULT hr = dx12queue->Wait( renderFin->d3d12fence, timelineFrame );
                assert( SUCCEEDED( hr ) );
            }

            ID3D12GraphicsCommandList* dx12cmd = dxgi::CreateD3D12CommandList( frameIndex );
            // blit to the swapchain's shadow buffer (copysrc)
            {
                uint32_t dst_w      = 0;
                uint32_t dst_h      = 0;
                bool     dst_tosrgb = false;

                ID3D12Resource* src = dxgi::Framebuf_GetVkDx12Shared( rendered ).d3d12resource;
                ID3D12Resource* dst = dxgi::GetSwapchainCopySrc( &dst_w, &dst_h, &dst_tosrgb );

                dxgi::DispatchBlit( dx12cmd, src, dst, dst_w, dst_h, dst_tosrgb );
            }
            // copy from the shadow buffer to the actual swapchain image
            {
                ID3D12Resource* src = dxgi::GetSwapchainCopySrc();
                ID3D12Resource* dst = dxgi::GetSwapchainBack( swapchain->GetCurrentImageIndex() );

                dx12cmd->CopyResource( dst, src );
                {
                    D3D12_RESOURCE_BARRIER bs[] = {
                        CD3DX12_RESOURCE_BARRIER::Transition( dst, //
                                                              D3D12_RESOURCE_STATE_COPY_DEST,
                                                              D3D12_RESOURCE_STATE_PRESENT ),
                    };
                    dx12cmd->ResourceBarrier( std::size( bs ), bs );
                }
            }
            HRESULT hr = dx12cmd->Close();
            assert( SUCCEEDED( hr ) );

            // submit dx12, wait for execution, and present
            {
                auto present = Semaphores_GetVkDx12Shared( dxgi::SHARED_SEM_PRESENT_COPY );
                if( !present )
                {
                    debug::Warning( "Skipping DXGI present, as Semaphores_GetVkDx12Shared failed" );
                    return;
                }

                ID3D12CommandList* p = dx12cmd;
                dx12queue->ExecuteCommandLists( 1, &p );
                hr = dx12queue->Signal( present->d3d12fence, timelineFrame );
                assert( SUCCEEDED( hr ) );

                dxgi::Present( present->d3d12fence, timelineFrame );
            }
        }();
    }
    else
    {
        // copy to swapchain's back buffer
        {
            framebuffers->BarrierOne( cmd, frameIndex, rendered );

            swapchain->BlitForPresent( cmd,
                                       framebuffers->GetImage( rendered, frameIndex ),
                                       rendered_size,
                                       VK_FILTER_NEAREST,
                                       VK_IMAGE_LAYOUT_GENERAL );
        }

        uint32_t    towait_count = 0;
        VkSemaphore towait[ 2 ]  = {};
        if( swapchain->Valid() )
        {
            towait[ towait_count++ ] = vkswapchainAvailableSemaphores[ frameIndex ];
        }
        if( initFrameFinished )
        {
            towait[ towait_count++ ] = initFrameFinished;
        }

        cmdManager->Submit_Binary( //
            cmd,
            std::span{ towait, towait_count },
            emulatedSemaphores[ frameIndex ], // signal
            frameFences[ frameIndex ] );

        if( swapchain->Valid() )
        {
            VkResult       r       = VK_SUCCESS;
            VkSwapchainKHR sw      = swapchain->GetHandle();
            uint32_t       swIndex = swapchain->GetCurrentImageIndex();

            // present to surfaces after finishing the rendering
            auto presentInfo = VkPresentInfoKHR{
                .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                .waitSemaphoreCount = 1,
                .pWaitSemaphores    = &emulatedSemaphores[ frameIndex ],
                .swapchainCount     = 1,
                .pSwapchains        = &sw,
                .pImageIndices      = &swIndex,
                .pResults           = &r,
            };
            vkQueuePresentKHR( queues->GetGraphics(), &presentInfo );
            swapchain->OnQueuePresent( r );
        }
    }

    if( nvDlss3dx12 )
    {
        nvDlss3dx12->Reflex_PresentEnd();
    }

    frameId++;

    if( nvDlss3dx12 )
    {
        nvDlss3dx12->Reflex_SimStart( frameId );
    }
}



// Interface implementation



void RTGL1::VulkanDevice::StartFrame( const RgStartFrameInfo* pOriginalInfo )
{
    if( currentFrameState.WasFrameStarted() )
    {
        throw RgException( RG_RESULT_FRAME_WASNT_ENDED );
    }

    if( pOriginalInfo == nullptr )
    {
        throw RgException( RG_RESULT_WRONG_FUNCTION_ARGUMENT, "Argument is null" );
    }

    if( pOriginalInfo->sType != RG_STRUCTURE_TYPE_START_FRAME_INFO )
    {
        throw RgException( RG_RESULT_WRONG_STRUCTURE_TYPE );
    }

    auto startFrame_Core = [ this ]( const RgStartFrameInfo& info ) {
        VkCommandBuffer newFrameCmd = BeginFrame( info );
        currentFrameState.OnBeginFrame( newFrameCmd );
    };

    auto startFrame_WithDevmode = [ this, startFrame_Core ]( const RgStartFrameInfo& original ) {
        auto modified            = RgStartFrameInfo{ original };
        auto modified_Resolution = pnext::get< RgStartFrameRenderResolutionParams >( original );
        auto modified_Fluid      = pnext::get< RgStartFrameFluidParams >( original );

        // clang-format off
        modified_Resolution .pNext = modified.pNext;
        modified_Fluid      .pNext = &modified_Resolution;
        modified            .pNext = &modified_Fluid;
        // clang-format on

        Dev_Override( modified, modified_Resolution, modified_Fluid );

        startFrame_Core( modified );
    };

    if( Dev_IsDevmodeInitialized() )
    {
        startFrame_WithDevmode( *pOriginalInfo );
    }
    else
    {
        startFrame_Core( *pOriginalInfo );
    }
}

void RTGL1::VulkanDevice::DrawFrame( const RgDrawFrameInfo* pOriginalInfo )
{
    if( !currentFrameState.WasFrameStarted() )
    {
        throw RgException( RG_RESULT_FRAME_WASNT_STARTED );
    }
    if( pOriginalInfo == nullptr )
    {
        throw RgException( RG_RESULT_WRONG_FUNCTION_ARGUMENT, "Argument is null" );
    }
    if( pOriginalInfo->sType != RG_STRUCTURE_TYPE_DRAW_FRAME_INFO )
    {
        throw RgException( RG_RESULT_WRONG_STRUCTURE_TYPE );
    }

    DrawEndUserWarnings();

    auto drawFrame_Core = [ this ]( const RgDrawFrameInfo& info ) {
        VkCommandBuffer cmd = currentFrameState.GetCmdBuffer();

        previousFrameTime = currentFrameTime;
        currentFrameTime  = info.currentTime;

        if( observer )
        {
            observer->RecheckFiles();
        }

        if( nvDlss3dx12 )
        {
            nvDlss3dx12->Reflex_SimEnd();
            nvDlss3dx12->Reflex_RenderStart();
        }

        FramebufferImageIndex rendered;

        if( renderResolution.Width() > 0 && renderResolution.Height() > 0 )
        {
            FillUniform( uniform->GetData(), info );
            Dev_Draw();
            rendered = Render( cmd, info );
        }
        else
        {
            rendered = m_prevAccum;
        }

        EndFrame( cmd, rendered );
        currentFrameState.OnEndFrame();


        sceneImportExport->TryExport( *textureManager, ovrdFolder );
    };

    auto drawFrame_WithScene = [ this, &drawFrame_Core ]( const RgDrawFrameInfo& original ) {
        auto modified            = RgDrawFrameInfo{ original };
        auto modified_Volumetric = pnext::get< RgDrawFrameVolumetricParams >( original );
        auto modified_Sky        = pnext::get< RgDrawFrameSkyParams >( original );

        sceneMetaManager->Modify(
            sceneImportExport->GetImportMapName(), modified_Volumetric, modified_Sky );

        // clang-format off
        modified_Volumetric     .pNext = modified.pNext;
        modified_Sky            .pNext = &modified_Volumetric;
        modified                .pNext = &modified_Sky;
        // clang-format on

        drawFrame_Core( modified );
    };

    auto drawFrame_WithDevmode = [ this, &drawFrame_WithScene ]( const RgDrawFrameInfo& original ) {
        auto modified              = RgDrawFrameInfo{ original };
        auto modified_Illumination = pnext::get< RgDrawFrameIlluminationParams >( original );
        auto modified_Tonemapping  = pnext::get< RgDrawFrameTonemappingParams >( original );
        auto modified_Textures     = pnext::get< RgDrawFrameTexturesParams >( original );

        // clang-format off
        modified_Illumination   .pNext = modified.pNext;
        modified_Tonemapping    .pNext = &modified_Illumination;
        modified_Textures       .pNext = &modified_Tonemapping;
        modified                .pNext = &modified_Textures;
        // clang-format on

        Dev_Override( modified_Illumination, modified_Tonemapping, modified_Textures );

        drawFrame_WithScene( modified );
    };

    if( Dev_IsDevmodeInitialized() )
    {
        drawFrame_WithDevmode( *pOriginalInfo );
    }
    else
    {
        drawFrame_WithScene( *pOriginalInfo );
    }
}

namespace RTGL1
{
namespace
{
    bool IsRasterized( const RgMeshInfo& mesh, const RgMeshPrimitiveInfo& primitive )
    {
        if( primitive.flags & RG_MESH_PRIMITIVE_DECAL )
        {
            return true;
        }

        if( primitive.flags & RG_MESH_PRIMITIVE_SKY )
        {
            return true;
        }

        if( !( primitive.flags & RG_MESH_PRIMITIVE_GLASS ) &&
            !( mesh.flags & RG_MESH_FORCE_GLASS ) &&
            !( primitive.flags & RG_MESH_PRIMITIVE_WATER ) &&
            !( mesh.flags & RG_MESH_FORCE_WATER ) && !( primitive.flags & RG_MESH_PRIMITIVE_ACID ) )
        {
            if( primitive.flags & RG_MESH_PRIMITIVE_TRANSLUCENT )
            {
                return true;
            }

            if( Utils::UnpackAlphaFromPacked32( primitive.color ) <
                MESH_TRANSLUCENT_ALPHA_THRESHOLD )
            {
                return true;
            }
        }

        return false;
    }
}
}

void RTGL1::VulkanDevice::UploadMeshPrimitive( const RgMeshInfo*          pMesh,
                                               const RgMeshPrimitiveInfo* pPrimitive )
{
    if( pPrimitive == nullptr )
    {
        throw RgException( RG_RESULT_WRONG_FUNCTION_ARGUMENT, "Argument is null" );
    }
    if( pPrimitive->sType != RG_STRUCTURE_TYPE_MESH_PRIMITIVE_INFO )
    {
        throw RgException( RG_RESULT_WRONG_STRUCTURE_TYPE );
    }
    if( pPrimitive->vertexCount == 0 || pPrimitive->pVertices == nullptr )
    {
        return;
    }
    Dev_TryBreak( pPrimitive->pTextureName, false );


    auto logDebugStat = [ this ]( Devmode::DebugPrimMode     mode,
                                  const RgMeshInfo*          mesh,
                                  const RgMeshPrimitiveInfo& prim,
                                  UploadResult               rtResult = UploadResult::Fail ) {
        if( !devmode || devmode->primitivesTableMode != mode )
        {
            return;
        }

        switch( mode )
        {
            case Devmode::DebugPrimMode::RayTraced:
                devmode->primitivesTable.push_back( Devmode::DebugPrim{
                    .result         = rtResult,
                    .callIndex      = uint32_t( devmode->primitivesTable.size() ),
                    .objectId       = mesh->uniqueObjectID,
                    .meshName       = Utils::SafeCstr( mesh->pMeshName ),
                    .primitiveIndex = prim.primitiveIndexInMesh,
                    .textureName    = Utils::SafeCstr( prim.pTextureName ),
                } );
                break;
            case Devmode::DebugPrimMode::Rasterized:
                devmode->primitivesTable.push_back( Devmode::DebugPrim{
                    .result         = UploadResult::Dynamic,
                    .callIndex      = uint32_t( devmode->primitivesTable.size() ),
                    .objectId       = mesh->uniqueObjectID,
                    .meshName       = Utils::SafeCstr( mesh->pMeshName ),
                    .primitiveIndex = prim.primitiveIndexInMesh,
                    .textureName    = Utils::SafeCstr( prim.pTextureName ),
                } );
                break;
            case Devmode::DebugPrimMode::NonWorld:
                devmode->primitivesTable.push_back( Devmode::DebugPrim{
                    .result         = UploadResult::Dynamic,
                    .callIndex      = uint32_t( devmode->primitivesTable.size() ),
                    .objectId       = 0,
                    .meshName       = {},
                    .primitiveIndex = prim.primitiveIndexInMesh,
                    .textureName    = Utils::SafeCstr( prim.pTextureName ),
                } );
                break;
            case Devmode::DebugPrimMode::Decal:
                devmode->primitivesTable.push_back( Devmode::DebugPrim{
                    .result         = UploadResult::Dynamic,
                    .callIndex      = uint32_t( devmode->primitivesTable.size() ),
                    .objectId       = 0,
                    .meshName       = {},
                    .primitiveIndex = 0,
                    .primitiveName  = {},
                    .textureName    = Utils::SafeCstr( prim.pTextureName ),
                } );
                break;
            case Devmode::DebugPrimMode::None:
            default: break;
        }
    };

    // --- //

    auto uploadPrimitive_Core = [ this, &logDebugStat ]( const RgMeshInfo&          mesh,
                                                         const RgMeshPrimitiveInfo& prim ) {
        assert( !pnext::find< RgMeshPrimitiveSwapchainedEXT >( &prim ) );

        if( IsRasterized( mesh, prim ) )
        {
            rasterizer->Upload( currentFrameState.GetFrameIndex(),
                                prim.flags & RG_MESH_PRIMITIVE_SKY     ? GeometryRasterType::SKY
                                : prim.flags & RG_MESH_PRIMITIVE_DECAL ? GeometryRasterType::DECAL
                                                                       : GeometryRasterType::WORLD,
                                mesh.transform,
                                prim,
                                nullptr,
                                nullptr );

            logDebugStat( prim.flags & RG_MESH_PRIMITIVE_DECAL ? Devmode::DebugPrimMode::Decal
                                                               : Devmode::DebugPrimMode::Rasterized,
                          &mesh,
                          prim );
        }
        else
        {
            // upload a primitive, potentially loading replacements
            UploadResult r = scene->UploadPrimitive( currentFrameState.GetFrameIndex(),
                                                     mesh,
                                                     prim,
                                                     *textureManager,
                                                     *lightManager,
                                                     false );

            if( lightmapScreenCoverage > 0 )
            {
                if( !( mesh.flags & RG_MESH_FIRST_PERSON_VIEWER ) )
                {
                    rasterizer->Upload( currentFrameState.GetFrameIndex(),
                                        GeometryRasterType::WORLD_CLASSIC,
                                        mesh.transform,
                                        prim,
                                        nullptr,
                                        nullptr );
                }
            }

            logDebugStat( Devmode::DebugPrimMode::RayTraced, &mesh, prim, r );


            if( auto e = sceneImportExport->TryGetExporter( mesh.flags &
                                                            RG_MESH_EXPORT_AS_SEPARATE_FILE ) )
            {
                auto allowMeshExport = [ & ]( const RgMeshInfo& m ) {
                    if( r != UploadResult::ExportableDynamic &&
                        r != UploadResult::ExportableStatic )
                    {
                        return false;
                    }
                    if( scene->ReplacementExists( m ) )
                    {
                        if( devmode && devmode->allowExportOfExistingReplacements )
                        {
                            return true;
                        }
                        return false;
                    }
                    return true;
                };

                if( allowMeshExport( mesh ) )
                {
                    e->AddPrimitive( mesh, prim );
                }

                // SHIPPING_HACK: add lights to the scene gltf even for non-exportable geometry
                if( !( mesh.flags & RG_MESH_EXPORT_AS_SEPARATE_FILE ) )
                {
                    e->AddPrimitiveLights( mesh, prim );
                }
            }


            // TODO: remove legacy way to attach lights
            if( auto attachedLight = pnext::find< RgMeshPrimitiveAttachedLightEXT >( &prim ) )
            {
                bool quad = ( prim.indexCount == 6 && prim.vertexCount == 4 ) ||
                            ( prim.indexCount == 0 && prim.vertexCount == 6 );

                if( attachedLight->evenOnDynamic || quad )
                {
                    assert( tempStorageLights.empty() );

                    if( quad )
                    {
                        auto center = RgFloat3D{ 0, 0, 0 };
                        {
                            for( uint32_t v = 0; v < prim.vertexCount; v++ )
                            {
                                center.data[ 0 ] += prim.pVertices[ v ].position[ 0 ];
                                center.data[ 1 ] += prim.pVertices[ v ].position[ 1 ];
                                center.data[ 2 ] += prim.pVertices[ v ].position[ 2 ];
                            }
                            center.data[ 0 ] /= float( prim.vertexCount );
                            center.data[ 1 ] /= float( prim.vertexCount );
                            center.data[ 2 ] /= float( prim.vertexCount );
                        }

                        center.data[ 0 ] += mesh.transform.matrix[ 0 ][ 3 ];
                        center.data[ 1 ] += mesh.transform.matrix[ 1 ][ 3 ];
                        center.data[ 2 ] += mesh.transform.matrix[ 2 ][ 3 ];

                        tempStorageLights.emplace_back( RgLightSphericalEXT{
                            .sType     = RG_STRUCTURE_TYPE_LIGHT_SPHERICAL_EXT,
                            .pNext     = nullptr,
                            .color     = attachedLight->color,
                            .intensity = attachedLight->intensity,
                            .position  = center,
                            .radius    = 0.1f,
                        } );
                    }
                    else
                    {
                        GltfExporter::MakeLightsForPrimitiveDynamic(
                            mesh,
                            prim,
                            sceneImportExport->GetWorldScale(),
                            tempStorageInit,
                            tempStorageLights );
                    }

                    auto hashCombine = []< typename T >( uint64_t seed, const T& v ) {
                        seed ^= std::hash< T >{}( v ) + 0x9e3779b9 + ( seed << 6 ) + ( seed >> 2 );
                        return seed;
                    };

                    static const uint64_t attchSalt =
                        hashCombine( 0, std::string_view{ "attachedlight" } );

                    // NOTE: can't use texture / mesh name, as texture can be
                    // just 1 frame of animation sequence.. so this is more stable
                    uint64_t hashBase{ attchSalt };
                    hashBase = hashCombine( hashBase, mesh.uniqueObjectID );
                    hashBase = hashCombine( hashBase, prim.primitiveIndexInMesh );

                    uint64_t counter = 0;

                    for( AnyLightEXT& lext : tempStorageLights )
                    {
                        std::visit(
                            [ & ]< typename T >( T& specific ) {
                                static_assert( detail::AreLinkable< T, RgLightInfo > );
                                
                                RgLightInfo linfo = {
                                    .sType        = RG_STRUCTURE_TYPE_LIGHT_INFO,
                                    .pNext        = &specific,
                                    .uniqueID     = hashCombine( hashBase, counter ),
                                    .isExportable = false,
                                };

                                UploadLight( &linfo );
                            },
                            lext );

                        counter++;
                    }

                    tempStorageInit.clear();
                    tempStorageLights.clear();
                }
            }
        }
    };

    // --- //

    auto uploadPrimitive_WithMeta = [ this, &uploadPrimitive_Core ](
                                        const RgMeshInfo& mesh, const RgMeshPrimitiveInfo& prim ) {
        // ignore replacement, if the scene requires
        if( mesh.isExportable && ( mesh.flags & RG_MESH_EXPORT_AS_SEPARATE_FILE ) &&
            !Utils::IsCstrEmpty( mesh.pMeshName ) )
        {
            if( sceneMetaManager->IsReplacementIgnored( sceneImportExport->GetImportMapName(),
                                                        mesh.pMeshName ) )
            {
                return;
            }
        }

        auto modified = RgMeshPrimitiveInfo{ prim };

        auto modified_attachedLight = std::optional< RgMeshPrimitiveAttachedLightEXT >{};
        auto modified_pbr           = std::optional< RgMeshPrimitivePBREXT >{};

        if( auto original = pnext::find< RgMeshPrimitiveAttachedLightEXT >( &prim ) )
        {
            modified_attachedLight = *original;
        }

        if( auto original = pnext::find< RgMeshPrimitivePBREXT >( &prim ) )
        {
            modified_pbr = *original;
        }

        if( mesh.flags & RG_MESH_FORCE_MIRROR )
        {
            modified.flags |= RG_MESH_PRIMITIVE_MIRROR;
        }
        if( mesh.flags & RG_MESH_FORCE_GLASS )
        {
            modified.flags |= RG_MESH_PRIMITIVE_GLASS;
        }
        if( mesh.flags & RG_MESH_FORCE_WATER )
        {
            modified.flags |= RG_MESH_PRIMITIVE_WATER;
        }

        if( !textureMetaManager->Modify( modified, modified_attachedLight, modified_pbr, false ) )
        {
            return;
        }

        if( modified_attachedLight )
        {
            // insert
            modified_attachedLight.value().pNext = modified.pNext;
            modified.pNext                       = &modified_attachedLight.value();
        }

        if( modified_pbr )
        {
            // insert
            modified_pbr.value().pNext = modified.pNext;
            modified.pNext             = &modified_pbr.value();
        }

        uploadPrimitive_Core( mesh, modified );
    };

    // --- //

    auto uploadPrimitive_FilterSwapchained = [ this, &uploadPrimitive_WithMeta, &logDebugStat ](
                                                 const RgMeshInfo*          mesh,
                                                 const RgMeshPrimitiveInfo& prim ) {
        if( mesh )
        {
            if( mesh->sType != RG_STRUCTURE_TYPE_MESH_INFO )
            {
                throw RgException( RG_RESULT_WRONG_STRUCTURE_TYPE );
            }
        }

        if( auto raster = pnext::find< RgMeshPrimitiveSwapchainedEXT >( &prim ) )
        {
            float vp[ 16 ];
            if( raster->pViewProjection )
            {
                memcpy( vp, raster->pViewProjection, sizeof( vp ) );
            }
            else
            {
                const auto& cameraInfo = scene->GetCamera( renderResolution.Aspect() );

                const float* v = raster->pView ? raster->pView : cameraInfo.view;
                const float* p = raster->pProjection ? raster->pProjection : cameraInfo.projection;
                Matrix::Multiply( vp, p, v );
            }

            rasterizer->Upload( currentFrameState.GetFrameIndex(),
                                GeometryRasterType::SWAPCHAIN,
                                mesh ? mesh->transform : RG_TRANSFORM_IDENTITY,
                                prim,
                                vp,
                                raster->pViewport );

            logDebugStat( Devmode::DebugPrimMode::NonWorld, nullptr, prim );
        }
        else
        {
            if( mesh == nullptr )
            {
                throw RgException( RG_RESULT_WRONG_FUNCTION_ARGUMENT, "Argument is null" );
            }
            if( mesh->flags & RG_MESH_EXPORT_AS_SEPARATE_FILE )
            {
                if( !mesh->isExportable )
                {
                    throw RgException( RG_RESULT_WRONG_FUNCTION_ARGUMENT,
                                       "RG_MESH_INFO_EXPORT_AS_SEPARATE_FILE is set, "
                                       "expected isExportable to be true" );
                }
            }

            uploadPrimitive_WithMeta( *mesh, prim );
        }
    };

    // --- //

    uploadPrimitive_FilterSwapchained( pMesh, *pPrimitive );
}

void RTGL1::VulkanDevice::UploadLensFlare( const RgLensFlareInfo* pInfo )
{
    if( pInfo == nullptr )
    {
        throw RgException( RG_RESULT_WRONG_FUNCTION_ARGUMENT, "Argument is null" );
    }
    if( pInfo->sType != RG_STRUCTURE_TYPE_LENS_FLARE_INFO )
    {
        throw RgException( RG_RESULT_WRONG_STRUCTURE_TYPE );
    }

    float emisMult = 0.0f;

    if( auto meta = textureMetaManager->Access( pInfo->pTextureName ) )
    {
        emisMult = meta->emissiveMult;

        if( meta->forceIgnore || meta->forceIgnoreIfRasterized )
        {
            return;
        }
    }

    rasterizer->UploadLensFlare(
        currentFrameState.GetFrameIndex(), *pInfo, emisMult, *textureManager );

    if( devmode && devmode->primitivesTableMode == Devmode::DebugPrimMode::Rasterized )
    {
        devmode->primitivesTable.push_back( Devmode::DebugPrim{
            .result         = UploadResult::Dynamic,
            .callIndex      = uint32_t( devmode->primitivesTable.size() ),
            .objectId       = 0,
            .meshName       = {},
            .primitiveIndex = 0,
            .primitiveName  = {},
            .textureName    = Utils::SafeCstr( pInfo->pTextureName ),
        } );
    }
}

void RTGL1::VulkanDevice::SpawnFluid( const RgSpawnFluidInfo* pInfo )
{
    if( pInfo == nullptr )
    {
        throw RgException( RG_RESULT_WRONG_FUNCTION_ARGUMENT, "Argument is null" );
    }
    if( pInfo->sType != RG_STRUCTURE_TYPE_SPAWN_FLUID_INFO )
    {
        throw RgException( RG_RESULT_WRONG_STRUCTURE_TYPE );
    }
    if( !fluid )
    {
        return;
    }
    fluid->AddSource( *pInfo );
}

void RTGL1::VulkanDevice::UploadCamera( const RgCameraInfo* pInfo )
{
    if( pInfo == nullptr )
    {
        throw RgException( RG_RESULT_WRONG_FUNCTION_ARGUMENT, "Argument is null" );
    }
    if( pInfo->sType != RG_STRUCTURE_TYPE_CAMERA_INFO )
    {
        throw RgException( RG_RESULT_WRONG_STRUCTURE_TYPE );
    }
    if( Utils::SqrLength( pInfo->right.data ) < 0.01f )
    {
        throw RgException( RG_RESULT_WRONG_FUNCTION_ARGUMENT, "Null RgCameraInfo::right" );
    }
    if( Utils::SqrLength( pInfo->up.data ) < 0.01f )
    {
        throw RgException( RG_RESULT_WRONG_FUNCTION_ARGUMENT, "Null RgCameraInfo::up" );
    }

    auto base = [ this ]( const RgCameraInfo& info ) {
        scene->AddDefaultCamera( info );

        if( auto readback =
                pnext::find< RgCameraInfoReadbackEXT >( const_cast< RgCameraInfo* >( &info ) ) )
        {
            const Camera& cameraInfo = scene->GetCamera( renderResolution.Aspect() );

            static_assert( sizeof readback->view == sizeof cameraInfo.view );
            static_assert( sizeof readback->projection == sizeof cameraInfo.projection );
            static_assert( sizeof readback->viewInverse == sizeof cameraInfo.viewInverse );
            static_assert( sizeof readback->projectionInverse ==
                           sizeof cameraInfo.projectionInverse );

            memcpy( readback->view, cameraInfo.view, sizeof cameraInfo.view );
            memcpy( readback->projection, cameraInfo.projection, sizeof cameraInfo.projection );
            memcpy( readback->viewInverse, cameraInfo.viewInverse, sizeof cameraInfo.viewInverse );
            memcpy( readback->projectionInverse,
                    cameraInfo.projectionInverse,
                    sizeof cameraInfo.projectionInverse );
        }
    };

    if( Dev_IsDevmodeInitialized() )
    {
        auto modified = RgCameraInfo{ *pInfo };
        Dev_Override( modified );

        base( modified );
    }
    else
    {
        base( *pInfo );
    }
}

void RTGL1::VulkanDevice::UploadLight( const RgLightInfo* pInfo )
{
    if( pInfo == nullptr )
    {
        throw RgException( RG_RESULT_WRONG_FUNCTION_ARGUMENT, "Argument is null" );
    }
    if( pInfo->sType != RG_STRUCTURE_TYPE_LIGHT_INFO )
    {
        throw RgException( RG_RESULT_WRONG_STRUCTURE_TYPE );
    }

    auto findExt =
        []( const RgLightInfo& info ) -> std::optional< std::variant< RgLightDirectionalEXT,
                                                                      RgLightSphericalEXT,
                                                                      RgLightSpotEXT,
                                                                      RgLightPolygonalEXT > > {
        if( auto l = pnext::find< RgLightDirectionalEXT >( &info ) )
        {
            return *l;
        }
        if( auto l = pnext::find< RgLightSphericalEXT >( &info ) )
        {
            return *l;
        }
        if( auto l = pnext::find< RgLightSpotEXT >( &info ) )
        {
            return *l;
        }
        if( auto l = pnext::find< RgLightPolygonalEXT >( &info ) )
        {
            return *l;
        }
        return {};
    };

    auto findAdditional = []( const RgLightInfo& info ) -> std::optional< RgLightAdditionalEXT > {
        if( auto l = pnext::find< RgLightAdditionalEXT >( &info ) )
        {
            return *l;
        }
        return {};
    };

    auto ext = findExt( *pInfo );
    if( !ext )
    {
        debug::Warning( "Couldn't find RgLightDirectionalEXT, RgLightSphericalEXT, RgLightSpotEXT "
                        "or RgLightPolygonalEXT on RgLightInfo (uniqueID={})",
                        pInfo->uniqueID );
        return;
    }

    auto light = LightCopy{
        .base       = *pInfo,
        .extension  = *ext,
        .additional = findAdditional( *pInfo ),
    };

    // reset pNext, as using in-place members
    {
        light.base.pNext = nullptr;
        std::visit( []( auto& e ) { e.pNext = nullptr; }, light.extension );
        if( light.additional )
        {
            light.additional->pNext = nullptr;
        }
    }

    UploadResult r =
        scene->UploadLight( currentFrameState.GetFrameIndex(), light, *lightManager, false );

    if( auto e = sceneImportExport->TryGetExporter( false ) )
    {
        if( r == UploadResult::ExportableDynamic || r == UploadResult::ExportableStatic )
        {
            e->AddLight( light );
        }
    }
}

void RTGL1::VulkanDevice::ProvideOriginalTexture( const RgOriginalTextureInfo* pInfo )
{
    if( pInfo == nullptr )
    {
        throw RgException( RG_RESULT_WRONG_FUNCTION_ARGUMENT, "Argument is null" );
    }
    if( pInfo->sType != RG_STRUCTURE_TYPE_ORIGINAL_TEXTURE_INFO )
    {
        throw RgException( RG_RESULT_WRONG_STRUCTURE_TYPE );
    }
    Dev_TryBreak( pInfo->pTextureName, true );

    textureManager->TryCreateMaterial( currentFrameState.GetCmdBufferForMaterials( cmdManager ),
                                       currentFrameState.GetFrameIndex(),
                                       *pInfo,
                                       ovrdFolder );

    // SHIPPING_HACK begin
    if( !Utils::IsCstrEmpty( pInfo->pTextureName ) )
    {
        auto texturesToUpdateOnStaticGeom = scene->m_primitivesToUpdateTextures.find( pInfo->pTextureName );
        if( texturesToUpdateOnStaticGeom != scene->m_primitivesToUpdateTextures.end() )
        {
            for( const PrimitiveUniqueID& geomUniqueId : texturesToUpdateOnStaticGeom->second )
            {
                scene->GetASManager()->Hack_PatchTexturesForStaticPrimitive(
                    geomUniqueId, pInfo->pTextureName, *textureManager );
            }
        }
    }
    // SHIPPING_HACK end
}

void RTGL1::VulkanDevice::MarkOriginalTextureAsDeleted( const char* pTextureName )
{
    textureManager->TryDestroyMaterial( currentFrameState.GetFrameIndex(), pTextureName );
    cubemapManager->TryDestroyCubemap( currentFrameState.GetFrameIndex(), pTextureName );
}

bool RTGL1::VulkanDevice::IsUpscaleTechniqueAvailable( RgRenderUpscaleTechnique technique,
                                                       RgFrameGenerationMode    frameGeneration,
                                                       const char** ppFailureReason ) const
{
    if( ppFailureReason )
    {
        *ppFailureReason = nullptr;
    }

    switch( technique )
    {
        case RG_RENDER_UPSCALE_TECHNIQUE_NEAREST:
        case RG_RENDER_UPSCALE_TECHNIQUE_LINEAR:
            if( frameGeneration != RG_FRAME_GENERATION_MODE_OFF )
            {
                return false;
            }
            return true;


        case RG_RENDER_UPSCALE_TECHNIQUE_AMD_FSR2:
            if( frameGeneration != RG_FRAME_GENERATION_MODE_OFF )
            {
                const char* error = swapchain->FailReason( SWAPCHAIN_TYPE_FRAME_GENERATION_FSR3 );
                assert( error == nullptr || error[ 0 ] != '\0' );

                if( ppFailureReason )
                {
                    *ppFailureReason = error;
                }
                return !error;
            }
            return bool( amdFsr2 );


        case RG_RENDER_UPSCALE_TECHNIQUE_NVIDIA_DLSS: {
            if( frameGeneration != RG_FRAME_GENERATION_MODE_OFF )
            {
                const char* error = swapchain->FailReason( SWAPCHAIN_TYPE_FRAME_GENERATION_DLSS3 );
                assert( error == nullptr || error[ 0 ] != '\0' );

                if( ppFailureReason )
                {
                    *ppFailureReason = error;
                }
                return !error;
            }
            return bool( nvDlss2 );
        }

        default: {
            throw RgException(
                RG_RESULT_WRONG_FUNCTION_ARGUMENT,
                "Incorrect technique was passed to rgIsRenderUpscaleTechniqueAvailable" );
        }
    }
}

bool RTGL1::VulkanDevice::IsDXGIAvailable( const char** ppFailureReason ) const
{
    const char* dxgiError = swapchain->FailReason( SWAPCHAIN_TYPE_DXGI );
    assert( dxgiError == nullptr || dxgiError[ 0 ] != '\0' );

    if( ppFailureReason )
    {
        *ppFailureReason = dxgiError;
    }
    return swapchain && !dxgiError;
}

RgFeatureFlags RTGL1::VulkanDevice::GetSupportedFeatures() const
{
    RgFeatureFlags f = 0;

    if( swapchain && swapchain->SupportsHDR() )
    {
        f |= RG_FEATURE_HDR;
    }

    if( m_supportsRayQueryAndPositionFetch )
    {
        f |= RG_FEATURE_FLUID;
    }

    return f;
}

RgUtilMemoryUsage RTGL1::VulkanDevice::RequestMemoryUsage() const
{
    auto& [ r_lastTime, r_usage ] = cachedMemoryUsage;

    constexpr auto CheckEachSeconds = 0.5;
    if( std::abs( currentFrameTime - r_lastTime ) > CheckEachSeconds )
    {
        r_lastTime = currentFrameTime;
        r_usage    = RTGL1::RequestMemoryUsage( physDevice->Get() );
    }

    return r_usage;
}

RgPrimitiveVertex* RTGL1::VulkanDevice::ScratchAllocForVertices( uint32_t vertexCount )
{
    // TODO: scratch allocator
    return new RgPrimitiveVertex[ vertexCount ];
}

void RTGL1::VulkanDevice::ScratchFree( const RgPrimitiveVertex* pPointer )
{
    // TODO: scratch allocator
    delete[] pPointer;
}

void RTGL1::VulkanDevice::Print( std::string_view msg, RgMessageSeverityFlags severity ) const
{
    static auto printMutex = std::mutex{};

    auto l = std::lock_guard{ printMutex };

    if( devmode )
    {
        auto getCountIfSameAsLast = [ & ]() -> uint32_t* {
            if( !devmode->logs.empty() )
            {
                auto& [ severityLast, count, msgLast ] = devmode->logs.back();
                if( severityLast == severity && msgLast == msg )
                {
                    return &count;
                }
            }
            return nullptr;
        };

        if( uint32_t* same = getCountIfSameAsLast() )
        {
            *same = *same + 1;
        }
        else
        {
            if( devmode->logs.size() > 2048 )
            {
                devmode->logs.pop_front();
            }

            devmode->logs.emplace_back( severity, 1, msg );
        }
    }

    if( userPrint )
    {
        userPrint->Print( msg.data(), severity );
    }
}
