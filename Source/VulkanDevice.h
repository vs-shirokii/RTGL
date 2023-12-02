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

#include <RTGL1/RTGL1.h>

#include <memory>

// clang-format off
#include "Common.h"

#include "CommandBufferManager.h"
#include "PhysicalDevice.h"
#include "Scene.h"
#include "Swapchain.h"
#include "Queues.h"
#include "GlobalUniform.h"
#include "PathTracer.h"
#include "Rasterizer.h"
#include "Framebuffers.h"
#include "MemoryAllocator.h"
#include "TextureManager.h"
#include "BlueNoise.h"
#include "ImageComposition.h"
#include "Tonemapping.h"
#include "CubemapManager.h"
#include "Denoiser.h"
#include "UserFunction.h"
#include "Bloom.h"
#include "Sharpening.h"
#include "DLSS2.h"
#include "DLSS3_DX12.h"
#include "RenderResolutionHelper.h"
#include "EffectWipe.h"
#include "EffectSimple_Instances.h"
#include "LightGrid.h"
#include "FSR2.h"
#include "FSR3_DX12.h"
#include "FrameState.h"
#include "PortalList.h"
#include "RestirBuffers.h"
#include "Volumetric.h"
#include "DebugWindows.h"
#include "ScratchImmediate.h"
#include "FolderObserver.h"
#include "TextureMeta.h"
#include "SceneMeta.h"
#include "DrawFrameInfo.h"
#include "Fluid.h"
#include "VulkanDevice_Dev.h"
// clang-format on

namespace RTGL1
{

struct Devmode;


class VulkanDevice
{
public:
    explicit VulkanDevice( const RgInstanceCreateInfo* pInfo );
    ~VulkanDevice();

    VulkanDevice( const VulkanDevice& other )                = delete;
    VulkanDevice( VulkanDevice&& other ) noexcept            = delete;
    VulkanDevice& operator=( const VulkanDevice& other )     = delete;
    VulkanDevice& operator=( VulkanDevice&& other ) noexcept = delete;

    void UploadMeshPrimitive( const RgMeshInfo* pMesh, const RgMeshPrimitiveInfo* pPrimitive );
    void UploadLensFlare( const RgLensFlareInfo* pInfo );
    void SpawnFluid( const RgSpawnFluidInfo* pInfo );

    void UploadCamera( const RgCameraInfo* pInfo );

    void UploadLight( const RgLightInfo* pInfo );

    void ProvideOriginalTexture( const RgOriginalTextureInfo* pInfo );
    void ProvideOriginalCubemapTexture( const RgOriginalCubemapInfo* pInfo );
    void MarkOriginalTextureAsDeleted( const char* pTextureName );

    void StartFrame( const RgStartFrameInfo* pInfo );
    void DrawFrame( const RgDrawFrameInfo* pInfo );


    bool IsUpscaleTechniqueAvailable( RgRenderUpscaleTechnique technique,
                                      RgFrameGenerationMode    frameGeneration,
                                      const char**             ppFailureReason ) const;

    bool              IsDXGIAvailable( const char** ppFailureReason ) const;
    RgFeatureFlags    GetSupportedFeatures() const;
    RgUtilMemoryUsage RequestMemoryUsage() const;

    RgPrimitiveVertex* ScratchAllocForVertices( uint32_t count );
    void               ScratchFree( const RgPrimitiveVertex* pPointer );
    ScratchImmediate&  ScratchIm() { return scratchImmediate; }


    void Print( std::string_view msg, RgMessageSeverityFlags severity ) const;
    bool IsDevMode() const { return devmode != nullptr; }

private:
    void CreateInstance( const RgInstanceCreateInfo& info );
    void CreateDevice();
    void CreateSyncPrimitives();
    void ValidateAndOverrideCreateInfo( const RgInstanceCreateInfo* pInfo ) const;

    void DestroyInstance();
    void DestroyDevice();
    void DestroySyncPrimitives();

    void FillUniform( ShGlobalUniform* gu, const RgDrawFrameInfo& drawInfo ) const;

    VkCommandBuffer BeginFrame( const RgStartFrameInfo& info );
    auto            Render( VkCommandBuffer &cmd, const RgDrawFrameInfo& drawInfo ) -> FramebufferImageIndex;
    void            EndFrame( VkCommandBuffer cmd, FramebufferImageIndex rendered );

    void DrawEndUserWarnings();

private:
    bool Dev_IsDevmodeInitialized() const;
    void Dev_Draw() const;
    void Dev_Override( RgStartFrameInfo&                   info,
                       RgStartFrameRenderResolutionParams& resolution,
                       RgStartFrameFluidParams&            fluid ) const;
    void Dev_Override( RgCameraInfo& info ) const;
    void Dev_Override( RgDrawFrameIlluminationParams&     illumination,
                       RgDrawFrameTonemappingParams&      tonemappingp,
                       RgDrawFrameTexturesParams&         textures ) const;
    void Dev_TryBreak( const char* pTextureName, bool isImageUpload );

private:
    VkInstance   instance;
    VkDevice     device;
    VkSurfaceKHR surface;

    FrameState currentFrameState;

    // incremented every frame
    uint32_t frameId;
    uint64_t timelineFrame{ 1 };

    VkFence     frameFences[ MAX_FRAMES_IN_FLIGHT ]              = {};
    VkSemaphore debugFinishedSemaphores[ MAX_FRAMES_IN_FLIGHT ]  = {};
    VkSemaphore inFrameSemaphores[ MAX_FRAMES_IN_FLIGHT ]        = {};
    VkSemaphore vkswapchainAvailableSemaphores[ MAX_FRAMES_IN_FLIGHT ] = {};
    VkSemaphore emulatedSemaphores[ MAX_FRAMES_IN_FLIGHT ]       = {};

    bool    waitForOutOfFrameFence;
    VkFence outOfFrameFences[ MAX_FRAMES_IN_FLIGHT ] = {};

    bool m_supportsRayQueryAndPositionFetch{ false };

    std::shared_ptr< PhysicalDevice > physDevice;
    std::shared_ptr< Queues >         queues;
    std::shared_ptr< Swapchain >      swapchain;

    std::shared_ptr< MemoryAllocator > memAllocator;

    std::shared_ptr< CommandBufferManager > cmdManager;

    std::shared_ptr< Framebuffers >  framebuffers;
    std::shared_ptr< RestirBuffers > restirBuffers;
    std::shared_ptr< Volumetric >    volumetric;
    std::shared_ptr< Fluid >         fluid;

    std::shared_ptr< GlobalUniform >     uniform;
    std::shared_ptr< Scene >             scene;
    std::shared_ptr< SceneImportExport > sceneImportExport;

    std::shared_ptr< ShaderManager >             shaderManager;
    std::shared_ptr< RayTracingPipeline >        rtPipeline;
    std::shared_ptr< PathTracer >                pathTracer;
    std::shared_ptr< Rasterizer >                rasterizer;
    std::shared_ptr< PortalList >                portalList;
    std::shared_ptr< LightManager >              lightManager;
    std::shared_ptr< LightGrid >                 lightGrid;
    std::shared_ptr< Denoiser >                  denoiser;
    std::shared_ptr< Tonemapping >               tonemapping;
    std::shared_ptr< ImageComposition >          imageComposition;
    std::shared_ptr< Bloom >                     bloom;
    std::shared_ptr< FSR2 >                      amdFsr2;
    std::shared_ptr< FSR3_DX12 >                 amdFsr3dx12;
    std::shared_ptr< DLSS2 >                     nvDlss2;
    std::shared_ptr< DLSS3_DX12 >                nvDlss3dx12;
    std::shared_ptr< Sharpening >                sharpening;
    std::shared_ptr< EffectWipe >                effectWipe;
    std::shared_ptr< EffectRadialBlur >          effectRadialBlur;
    std::shared_ptr< EffectChromaticAberration > effectChromaticAberration;
    std::shared_ptr< EffectInverseBW >           effectInverseBW;
    std::shared_ptr< EffectHueShift >            effectHueShift;
    std::shared_ptr< EffectNightVision >         effectNightVision;
    std::shared_ptr< EffectDistortedSides >      effectDistortedSides;
    std::shared_ptr< EffectWaves >               effectWaves;
    std::shared_ptr< EffectColorTint >           effectColorTint;
    std::shared_ptr< EffectTeleport >            effectTeleport;
    std::shared_ptr< EffectCrtDemodulateEncode > effectCrtDemodulateEncode;
    std::shared_ptr< EffectCrtDecode >           effectCrtDecode;
    std::shared_ptr< EffectVHS >                 effectVHS;
    std::shared_ptr< EffectDither >              effectDither;
    std::shared_ptr< EffectHDRPrepare >          effectHDRPrepare;

    std::shared_ptr< SamplerManager >     worldSamplerManager;
    std::shared_ptr< SamplerManager >     genericSamplerManager;
    std::shared_ptr< BlueNoise >          blueNoise;
    std::shared_ptr< TextureManager >     textureManager;
    std::shared_ptr< TextureMetaManager > textureMetaManager;
    std::shared_ptr< SceneMetaManager >   sceneMetaManager;
    std::shared_ptr< CubemapManager >     cubemapManager;

    std::filesystem::path ovrdFolder;

    VkDebugUtilsMessengerEXT          debugMessenger;
    std::unique_ptr< UserPrint >      userPrint;
    std::shared_ptr< DebugWindows >   debugWindows;
    ScratchImmediate                  scratchImmediate;
    std::unique_ptr< FolderObserver > observer;
    
    float lightmapScreenCoverage{ 0 };

    // TODO: remove; used to not allocate on each call
    std::vector< PositionNormal > tempStorageInit;
    std::vector< AnyLightEXT >    tempStorageLights;

    std::unique_ptr< Devmode > devmode;

    bool rayCullBackFacingTriangles;

    RenderResolutionHelper renderResolution;

    double previousFrameTime;
    double currentFrameTime;

    mutable std::pair< double, RgUtilMemoryUsage > cachedMemoryUsage{};

    const std::string appGuid;

    std::optional< RgExtent2D > m_pixelated{};
    FramebufferImageIndex       m_prevAccum{ FB_IMAGE_INDEX_UPSCALED_PONG };
    bool                        m_skipGeneratedFrame{ false };

    RgFloat3D fluidGravity{ 0, -9.8f, 0 };
    RgFloat3D fluidColor{ 1, 1, 1 };
};

}
