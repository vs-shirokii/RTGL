/*

Copyright (c) 2024 V.Shirokii

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

#pragma once

#include "ResolutionState.h"

#include <cstdint>
#include <expected>
#include <optional>

#include "d3d12.h"

// forward declare
struct ID3D12Device;
struct ID3D12Resource;
struct ID3D12Fence;
struct ID3D12CommandQueue;
struct ID3D12GraphicsCommandList;
struct ID3D12PipelineState;
struct ID3D12RootSignature;
struct ID3D12DescriptorHeap;
struct IDXGIFactory4;
struct IDXGISwapChain4;
struct DXGI_SWAP_CHAIN_DESC1;
using VkDevice         = struct VkDevice_T*;
using VkPhysicalDevice = struct VkPhysicalDevice_T*;
using VkImage          = struct VkImage_T*;
using VkDeviceMemory   = struct VkDeviceMemory_T*;
using VkSemaphore      = struct VkSemaphore_T*;
using HANDLE           = void*;

namespace RTGL1
{
class CommandBufferManager;
class MemoryAllocator;
}

namespace RTGL1::dxgi
{
constexpr uint32_t MAX_FRAMES_IN_FLIGHT_DX12 = 2;

bool DX12Supported();
bool HasDX12Instance();
bool HasDX12SwapchainInstance();


void SetHwnd( void* hwnd );
void SetVk( VkDevice vkdevice, VkPhysicalDevice vkphysdevice );


using PFN_CreateSwapchain    = IDXGISwapChain4* ( * )( IDXGIFactory4*               factory,
                                                    ID3D12CommandQueue*          queue,
                                                    void*                        hwnd,
                                                    const DXGI_SWAP_CHAIN_DESC1* desc1 );
using PFN_SetD3D12           = void ( * )( void* d3dDevice );
using PFN_UpgradeInterface   = void ( * )( void** baseInterface );
using PFN_GetNativeInterface = void ( * )( void* proxyInterface, void** baseInterface );


bool InitAsFSR3( uint64_t gpuLuid, PFN_CreateSwapchain pfn_CreateFrameGenSwapchain );
bool InitAsDLFG( uint64_t               gpuLuid,
                 PFN_SetD3D12           pfn_SetD3D12,
                 PFN_UpgradeInterface   pfn_UpgradeInterface,
                 PFN_GetNativeInterface pfn_GetNativeInterface );
auto InitAsRawDXGI( uint64_t gpuLuid ) -> std::expected< bool, const char* >;
bool HasRawDXGI();
void Destroy();
auto GetD3D12Device() -> ID3D12Device*;
auto GetD3D12CommandQueue() -> ID3D12CommandQueue*;
auto GetAdapterLUID() -> uint64_t;
void WaitAndPrepareForFrame( ID3D12Fence* fence, HANDLE fenceEvent, uint64_t currentTimelineFrame );
auto CreateD3D12CommandList( uint32_t frameIndex ) -> ID3D12GraphicsCommandList*;

void DispatchBlit( ID3D12GraphicsCommandList* dx12cmd,
                   ID3D12Resource*            src,
                   ID3D12Resource*            dst,
                   uint32_t                   dst_width,
                   uint32_t                   dst_height,
                   bool                       dst_tosrgb );


auto CreateSwapchain( uint32_t               width,
                      uint32_t               height,
                      uint32_t               imageCount,
                      int /* VkFormat */     vkformat,
                      int /* VkColorSpace */ vkcolorspace,
                      bool                   vsync ) -> uint32_t;
void DestroySwapchain( bool waitForIdle = true );
auto GetSwapchainBack( uint32_t i ) -> ID3D12Resource*;
auto GetSwapchainCopySrc( uint32_t* width         = nullptr,
                          uint32_t* height        = nullptr,
                          bool*     convertToSrgb = nullptr ) -> ID3D12Resource*;
auto GetSwapchainDxgiFormat() -> int; /* DXGI_FORMAT */
auto GetSwapchainDxgiSwapchain() -> IDXGISwapChain4*;
void Present( ID3D12Fence* fence, uint64_t waitValue );
auto GetCurrentBackBufferIndex() -> uint32_t;
void WaitIdle();


enum SharedSemaphoreType
{
    SHARED_SEM_RENDER_FINISHED,
    SHARED_SEM_FSR3_IN,
    SHARED_SEM_FSR3_OUT,
    SHARED_SEM_PRESENT_COPY,
};
constexpr auto SHARED_SEMAPHORE_TYPE_COUNT = 4;
struct SharedSemaphore
{
    VkSemaphore  vksemaphore{ nullptr };
    HANDLE       sharedHandle{ nullptr };
    ID3D12Fence* d3d12fence{ nullptr };
    HANDLE       d3d12fenceEvent{ nullptr };
};
auto Semaphores_GetVkDx12Shared( SharedSemaphoreType i ) -> std::optional< SharedSemaphore >;


struct SharedImage
{
    ID3D12Resource* d3d12resource{ nullptr };
    HANDLE          sharedHandle{ nullptr };
    VkImage         vkimage{ nullptr };
    VkDeviceMemory  vkmemory{ nullptr };
    int             dxgiformat{ 0 };
    int             vkformat{ 0 };
    uint32_t        width{ 0 };
    uint32_t        height{ 0 };
};
void Framebuf_CreateDX12Resources( CommandBufferManager&  vkcmds,
                                   MemoryAllocator&       vkallocator,
                                   const ResolutionState& resolution );
void Framebuf_Destroy();
auto Framebuf_GetVkDx12Shared( int framebufImageIndex ) -> SharedImage;
bool Framebuf_HasSharedImages();
// Look Framebuf_CopyVkToDX12 / Framebuf_CopyDX12ToVk in DX12_CopyFramebuf.h

}
