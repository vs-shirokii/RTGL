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

#include "DX12_Interop.h"

#ifdef RG_USE_DX12

#include "CommandBufferManager.h"
#include "Generated/ShaderCommonCFramebuf.h"
#include "MemoryAllocator.h"
#include "Utils.h"

#include <d3d12.h>
#include <dxgi1_4.h>

#include <algorithm>

namespace RTGL1::dxgi
{

static_assert( MAX_FRAMES_IN_FLIGHT_DX12 == MAX_FRAMES_IN_FLIGHT );

namespace
{
    auto CreateSharedSemaphore( VkDevice         vkdevice,
                                VkPhysicalDevice vkphysdevice,
                                ID3D12Device*    dx12device,
                                const char*      debugname ) -> std::optional< SharedSemaphore >
    {
        SharedSemaphore dst{};

        auto l_fail = [ & ]() {
            if( dst.vksemaphore )
            {
                vkDestroySemaphore( vkdevice, dst.vksemaphore, nullptr );
            }
            if( dst.sharedHandle )
            {
                CloseHandle( dst.sharedHandle );
            }
            if( dst.d3d12fence )
            {
                dst.d3d12fence->Release();
            }
            if( dst.d3d12fenceEvent )
            {
                BOOL c = CloseHandle( dst.d3d12fenceEvent );
                assert( c );
            }
            return std::nullopt;
        };

        // should have been VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT,
        // but no support for export, so using opaque handle
        constexpr auto handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
        {
            auto win32sem = VkExportSemaphoreCreateInfo{
                .sType       = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,
                .pNext       = nullptr,
                .handleTypes = handleType,
            };

            auto timeline = VkSemaphoreTypeCreateInfo{
                .sType         = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
                .pNext         = &win32sem,
                .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
                .initialValue  = 0,
            };

            auto info = VkSemaphoreCreateInfo{
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                .pNext = &timeline,
            };
            VkResult r = vkCreateSemaphore( vkdevice, &info, nullptr, &dst.vksemaphore );
            if( r != VK_SUCCESS || !dst.vksemaphore )
            {
                debug::Error( "CreateSharedSemaphore: vkCreateSemaphore failed ({}): {}",
                              debugname,
                              int( r ) );
                return l_fail();
            }
            SET_DEBUG_NAME( vkdevice, dst.vksemaphore, VK_OBJECT_TYPE_SEMAPHORE, debugname );
        }
        // export to DX12 as HANDLE
        {
            auto handleInfo = VkPhysicalDeviceExternalSemaphoreInfo{
                .sType      = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO,
                .pNext      = nullptr,
                .handleType = handleType,
            };

            auto handleProps = VkExternalSemaphoreProperties{
                .sType = VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES,
                .pNext = nullptr,
            };
            vkGetPhysicalDeviceExternalSemaphoreProperties( vkphysdevice, //
                                                            &handleInfo,
                                                            &handleProps );

            // should be exportable to DX12
            if( !( handleProps.externalSemaphoreFeatures &
                   VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT ) )
            {
                debug::Error(
                    "CreateSharedSemaphore: VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT "
                    "doesn't support VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT ({})",
                    debugname );
                return l_fail();
            }

            auto win32info = VkSemaphoreGetWin32HandleInfoKHR{
                .sType      = VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR,
                .pNext      = nullptr,
                .semaphore  = dst.vksemaphore,
                .handleType = handleType,
            };

            VkResult r = svkGetSemaphoreWin32HandleKHR( vkdevice, &win32info, &dst.sharedHandle );
            if( r != VK_SUCCESS || !dst.sharedHandle )
            {
                debug::Error(
                    "CreateSharedSemaphore: svkGetSemaphoreWin32HandleKHR failed ({}): {}",
                    debugname,
                    int( r ) );
                return l_fail();
            }
        }
        // open Vulkan-created shared HANDLE in DX12
        {
            HRESULT hr =
                dx12device->OpenSharedHandle( dst.sharedHandle, IID_PPV_ARGS( &dst.d3d12fence ) );
            if( FAILED( hr ) || !dst.d3d12fence )
            {
                debug::Error(
                    "CreateSharedSemaphore: ID3D12Device::OpenSharedHandle failed ({}): {:08x}",
                    debugname,
                    uint32_t( hr ) );
                return l_fail();
            }
        }
        {
            dst.d3d12fenceEvent = CreateEventEx( nullptr, //
                                                 nullptr,
                                                 0,
                                                 EVENT_ALL_ACCESS );
            if( !dst.d3d12fenceEvent )
            {
                debug::Error( "CreateSharedSemaphore: CreateEventEx failed ({})", debugname );
                return l_fail();
            }
        }
        return dst;
    }

    DXGI_FORMAT VkFormatToDxgiFormat( VkFormat vkformat )
    {
        switch( vkformat )
        {
            // case VK_FORMAT_R32G32B32A32_TYPELESS: return DXGI_FORMAT_R32G32B32A32_TYPELESS;
            case VK_FORMAT_R32G32B32A32_SFLOAT: return DXGI_FORMAT_R32G32B32A32_FLOAT;
            case VK_FORMAT_R32G32B32A32_UINT: return DXGI_FORMAT_R32G32B32A32_UINT;
            case VK_FORMAT_R32G32B32A32_SINT: return DXGI_FORMAT_R32G32B32A32_SINT;
            // case VK_FORMAT_R32G32B32_TYPELESS: return DXGI_FORMAT_R32G32B32_TYPELESS;
            case VK_FORMAT_R32G32B32_SFLOAT: return DXGI_FORMAT_R32G32B32_FLOAT;
            case VK_FORMAT_R32G32B32_UINT: return DXGI_FORMAT_R32G32B32_UINT;
            case VK_FORMAT_R32G32B32_SINT: return DXGI_FORMAT_R32G32B32_SINT;
            // case VK_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_TYPELESS;
            case VK_FORMAT_R16G16B16A16_SFLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
            case VK_FORMAT_R16G16B16A16_UNORM: return DXGI_FORMAT_R16G16B16A16_UNORM;
            case VK_FORMAT_R16G16B16A16_UINT: return DXGI_FORMAT_R16G16B16A16_UINT;
            case VK_FORMAT_R16G16B16A16_SNORM: return DXGI_FORMAT_R16G16B16A16_SNORM;
            case VK_FORMAT_R16G16B16A16_SINT: return DXGI_FORMAT_R16G16B16A16_SINT;
            // case VK_FORMAT_R32G32_TYPELESS: return DXGI_FORMAT_R32G32_TYPELESS;
            case VK_FORMAT_R32G32_SFLOAT: return DXGI_FORMAT_R32G32_FLOAT;
            case VK_FORMAT_R32G32_UINT: return DXGI_FORMAT_R32G32_UINT;
            case VK_FORMAT_R32G32_SINT: return DXGI_FORMAT_R32G32_SINT;
            // case VK_FORMAT_R32G8X24_TYPELESS: return DXGI_FORMAT_R32G8X24_TYPELESS;
            case VK_FORMAT_D32_SFLOAT_S8_UINT: return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
            // case VK_FORMAT_R32_FLOAT_X8X24_TYPELESS: return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
            // case VK_FORMAT_X32_TYPELESS_G8X24_UINT: return DXGI_FORMAT_X32_TYPELESS_G8X24_UINT;
            // case VK_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_TYPELESS;
            case VK_FORMAT_A2B10G10R10_UNORM_PACK32: 
            case VK_FORMAT_A2R10G10B10_UNORM_PACK32: return DXGI_FORMAT_R10G10B10A2_UNORM;
            case VK_FORMAT_A2B10G10R10_UINT_PACK32:
            case VK_FORMAT_A2R10G10B10_UINT_PACK32: return DXGI_FORMAT_R10G10B10A2_UINT;
            // case VK_FORMAT_R11G11B10_FLOAT: return DXGI_FORMAT_R11G11B10_FLOAT;
            // case VK_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_TYPELESS;
            case VK_FORMAT_R8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
            case VK_FORMAT_R8G8B8A8_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            case VK_FORMAT_R8G8B8A8_UINT: return DXGI_FORMAT_R8G8B8A8_UINT;
            case VK_FORMAT_R8G8B8A8_SNORM: return DXGI_FORMAT_R8G8B8A8_SNORM;
            case VK_FORMAT_R8G8B8A8_SINT: return DXGI_FORMAT_R8G8B8A8_SINT;
            // case VK_FORMAT_R16G16_TYPELESS: return DXGI_FORMAT_R16G16_TYPELESS;
            case VK_FORMAT_R16G16_SFLOAT: return DXGI_FORMAT_R16G16_FLOAT;
            case VK_FORMAT_R16G16_UNORM: return DXGI_FORMAT_R16G16_UNORM;
            case VK_FORMAT_R16G16_UINT: return DXGI_FORMAT_R16G16_UINT;
            case VK_FORMAT_R16G16_SNORM: return DXGI_FORMAT_R16G16_SNORM;
            case VK_FORMAT_R16G16_SINT: return DXGI_FORMAT_R16G16_SINT;
            // case VK_FORMAT_R32_TYPELESS: return DXGI_FORMAT_R32_TYPELESS;
            case VK_FORMAT_D32_SFLOAT: return DXGI_FORMAT_D32_FLOAT;
            case VK_FORMAT_R32_SFLOAT: return DXGI_FORMAT_R32_FLOAT;
            case VK_FORMAT_R32_UINT: return DXGI_FORMAT_R32_UINT;
            case VK_FORMAT_R32_SINT: return DXGI_FORMAT_R32_SINT;
            // case VK_FORMAT_R24G8_TYPELESS: return DXGI_FORMAT_R24G8_TYPELESS;
            case VK_FORMAT_D24_UNORM_S8_UINT: return DXGI_FORMAT_D24_UNORM_S8_UINT;
            // case VK_FORMAT_R24_UNORM_X8_TYPELESS: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
            // case VK_FORMAT_X24_TYPELESS_G8_UINT: return DXGI_FORMAT_X24_TYPELESS_G8_UINT;
            // case VK_FORMAT_R8G8_TYPELESS: return DXGI_FORMAT_R8G8_TYPELESS;
            case VK_FORMAT_R8G8_UNORM: return DXGI_FORMAT_R8G8_UNORM;
            case VK_FORMAT_R8G8_UINT: return DXGI_FORMAT_R8G8_UINT;
            case VK_FORMAT_R8G8_SNORM: return DXGI_FORMAT_R8G8_SNORM;
            case VK_FORMAT_R8G8_SINT: return DXGI_FORMAT_R8G8_SINT;
            // case VK_FORMAT_R16_TYPELESS: return DXGI_FORMAT_R16_TYPELESS;
            case VK_FORMAT_R16_SFLOAT: return DXGI_FORMAT_R16_FLOAT;
            case VK_FORMAT_D16_UNORM: return DXGI_FORMAT_D16_UNORM;
            case VK_FORMAT_R16_UNORM: return DXGI_FORMAT_R16_UNORM;
            case VK_FORMAT_R16_UINT: return DXGI_FORMAT_R16_UINT;
            case VK_FORMAT_R16_SNORM: return DXGI_FORMAT_R16_SNORM;
            case VK_FORMAT_R16_SINT: return DXGI_FORMAT_R16_SINT;
            // case VK_FORMAT_R8_TYPELESS: return DXGI_FORMAT_R8_TYPELESS;
            case VK_FORMAT_R8_UNORM: return DXGI_FORMAT_R8_UNORM;
            case VK_FORMAT_R8_UINT: return DXGI_FORMAT_R8_UINT;
            case VK_FORMAT_R8_SNORM: return DXGI_FORMAT_R8_SNORM;
            case VK_FORMAT_R8_SINT: return DXGI_FORMAT_R8_SINT;
            // case VK_FORMAT_A8_UNORM: return DXGI_FORMAT_A8_UNORM;
            // case VK_FORMAT_R1_UNORM: return DXGI_FORMAT_R1_UNORM;
            case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32: return DXGI_FORMAT_R9G9B9E5_SHAREDEXP;
            // case VK_FORMAT_R8G8_B8G8_UNORM: return DXGI_FORMAT_R8G8_B8G8_UNORM;
            // case VK_FORMAT_G8R8_G8B8_UNORM: return DXGI_FORMAT_G8R8_G8B8_UNORM;
            // case VK_FORMAT_BC1_TYPELESS: return DXGI_FORMAT_BC1_TYPELESS;
            case VK_FORMAT_BC1_RGB_UNORM_BLOCK: return DXGI_FORMAT_BC1_UNORM;
            case VK_FORMAT_BC1_RGB_SRGB_BLOCK: return DXGI_FORMAT_BC1_UNORM_SRGB;
            // case VK_FORMAT_BC2_TYPELESS: return DXGI_FORMAT_BC2_TYPELESS;
            case VK_FORMAT_BC2_UNORM_BLOCK: return DXGI_FORMAT_BC2_UNORM;
            case VK_FORMAT_BC2_SRGB_BLOCK: return DXGI_FORMAT_BC2_UNORM_SRGB;
            // case VK_FORMAT_BC3_TYPELESS: return DXGI_FORMAT_BC3_TYPELESS;
            case VK_FORMAT_BC3_UNORM_BLOCK: return DXGI_FORMAT_BC3_UNORM;
            case VK_FORMAT_BC3_SRGB_BLOCK: return DXGI_FORMAT_BC3_UNORM_SRGB;
            // case VK_FORMAT_BC4_TYPELESS: return DXGI_FORMAT_BC4_TYPELESS;
            case VK_FORMAT_BC4_UNORM_BLOCK: return DXGI_FORMAT_BC4_UNORM;
            case VK_FORMAT_BC4_SNORM_BLOCK: return DXGI_FORMAT_BC4_SNORM;
            // case VK_FORMAT_BC5_TYPELESS: return DXGI_FORMAT_BC5_TYPELESS;
            case VK_FORMAT_BC5_UNORM_BLOCK: return DXGI_FORMAT_BC5_UNORM;
            case VK_FORMAT_BC5_SNORM_BLOCK: return DXGI_FORMAT_BC5_SNORM;
            case VK_FORMAT_B5G6R5_UNORM_PACK16: return DXGI_FORMAT_B5G6R5_UNORM;
            case VK_FORMAT_B5G5R5A1_UNORM_PACK16: return DXGI_FORMAT_B5G5R5A1_UNORM;
            case VK_FORMAT_B8G8R8A8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM;
            // case VK_FORMAT_B8G8R8X8_UNORM: return DXGI_FORMAT_B8G8R8X8_UNORM;
            // case VK_FORMAT_R10G10B10_XR_BIAS_A2_UNORM: return
            // DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM; case VK_FORMAT_B8G8R8A8_TYPELESS: return
            // DXGI_FORMAT_B8G8R8A8_TYPELESS;
            case VK_FORMAT_B8G8R8A8_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
            // case VK_FORMAT_B8G8R8X8_TYPELESS: return DXGI_FORMAT_B8G8R8X8_TYPELESS;
            // case VK_FORMAT_B8G8R8X8_SRGB: return DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
            // case VK_FORMAT_BC6H_TYPELESS: return DXGI_FORMAT_BC6H_TYPELESS;
            case VK_FORMAT_BC6H_UFLOAT_BLOCK: return DXGI_FORMAT_BC6H_UF16;
            case VK_FORMAT_BC6H_SFLOAT_BLOCK: return DXGI_FORMAT_BC6H_SF16;
            // case VK_FORMAT_BC7_TYPELESS: return DXGI_FORMAT_BC7_TYPELESS;
            case VK_FORMAT_BC7_UNORM_BLOCK: return DXGI_FORMAT_BC7_UNORM;
            case VK_FORMAT_BC7_SRGB_BLOCK: return DXGI_FORMAT_BC7_UNORM_SRGB;
            // case VK_FORMAT_AYUV: return DXGI_FORMAT_AYUV;
            // case VK_FORMAT_Y410: return DXGI_FORMAT_Y410;
            // case VK_FORMAT_Y416: return DXGI_FORMAT_Y416;
            // case VK_FORMAT_NV12: return DXGI_FORMAT_NV12;
            // case VK_FORMAT_P010: return DXGI_FORMAT_P010;
            // case VK_FORMAT_P016: return DXGI_FORMAT_P016;
            // case VK_FORMAT_420_OPAQUE: return DXGI_FORMAT_420_OPAQUE;
            // case VK_FORMAT_YUY2: return DXGI_FORMAT_YUY2;
            // case VK_FORMAT_Y210: return DXGI_FORMAT_Y210;
            // case VK_FORMAT_Y216: return DXGI_FORMAT_Y216;
            // case VK_FORMAT_NV11: return DXGI_FORMAT_NV11;
            // case VK_FORMAT_AI44: return DXGI_FORMAT_AI44;
            // case VK_FORMAT_IA44: return DXGI_FORMAT_IA44;
            // case VK_FORMAT_P8: return DXGI_FORMAT_P8;
            // case VK_FORMAT_A8P8: return DXGI_FORMAT_A8P8;
            case VK_FORMAT_B4G4R4A4_UNORM_PACK16: return DXGI_FORMAT_B4G4R4A4_UNORM;
            default: assert( 0 ); return DXGI_FORMAT_UNKNOWN;
        }
    }

    auto CreateSharedImage( ID3D12Device*    dx12device,
                            VkDevice         vkdevice,
                            MemoryAllocator& vkallocator,
                            VkFormat         vkformat,
                            DXGI_FORMAT      dxgiformat,
                            uint32_t         width,
                            uint32_t         height,
                            const char*      debugname ) -> std::optional< SharedImage >
    {
        SharedImage dst{};

        auto l_fail = [ & ]() {
            if( dst.sharedHandle )
            {
                CloseHandle( dst.sharedHandle );
            }
            if( dst.d3d12resource )
            {
                dst.d3d12resource->Release();
            }
            if( dst.vkimage )
            {
                vkDestroyImage( vkdevice, dst.vkimage, nullptr );
            }
            if( dst.vkmemory )
            {
                vkFreeMemory( vkdevice, dst.vkmemory, nullptr );
            }
            return std::nullopt;
        };

        // DX12
        {
            auto desc = D3D12_RESOURCE_DESC{
                .Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
                .Width            = width,
                .Height           = height,
                .DepthOrArraySize = 1,
                .MipLevels        = 1,
                .Format           = dxgiformat,
                .SampleDesc       = { .Count = 1, .Quality = 0 },
                .Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN,
                .Flags            = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            };

            auto heapProps = D3D12_HEAP_PROPERTIES{
                .Type = D3D12_HEAP_TYPE_DEFAULT,
            };

            HRESULT hr = dx12device->CreateCommittedResource( &heapProps,
                                                              D3D12_HEAP_FLAG_SHARED,
                                                              &desc,
                                                              D3D12_RESOURCE_STATE_COMMON,
                                                              nullptr,
                                                              IID_PPV_ARGS( &dst.d3d12resource ) );
            if( FAILED( hr ) || !dst.d3d12resource )
            {
                debug::Error(
                    "CreateSharedImage: ID3D12Device::CreateCommittedResource failed ({}): {}",
                    debugname,
                    hr );
                return l_fail();
            }

            hr = dx12device->CreateSharedHandle( dst.d3d12resource, //
                                                 nullptr,
                                                 GENERIC_ALL,
                                                 nullptr,
                                                 &dst.sharedHandle );
            if( FAILED( hr ) || !dst.sharedHandle )
            {
                debug::Error( "CreateSharedImage: ID3D12Device::CreateSharedHandle failed ({}): {}",
                              debugname,
                              hr );
                return l_fail();
            }
        }

        // Vulkan
        auto           r            = VkResult{};
        constexpr auto dx12type     = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;
        HANDLE         dx12resource = dst.sharedHandle;
        {
            auto external = VkExternalMemoryImageCreateInfo{
                .sType       = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
                .pNext       = nullptr,
                .handleTypes = dx12type,
            };

            auto imageInfo = VkImageCreateInfo{
                .sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                .pNext       = &external,
                .flags       = 0,
                .imageType   = VK_IMAGE_TYPE_2D,
                .format      = vkformat,
                .extent      = { width, height, 1 },
                .mipLevels   = 1,
                .arrayLayers = 1,
                .samples     = VK_SAMPLE_COUNT_1_BIT,
                .tiling      = VK_IMAGE_TILING_OPTIMAL,
                .usage       = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                .queueFamilyIndexCount = 0,
                .pQueueFamilyIndices   = nullptr,
                .initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED,
            };

            r = vkCreateImage( vkdevice, &imageInfo, nullptr, &dst.vkimage );
            if( r != VK_SUCCESS )
            {
                debug::Error(
                    "CreateSharedImage: vkCreateImage failed ({}): {}", debugname, int( r ) );
                return l_fail();
            }
            SET_DEBUG_NAME( vkdevice, dst.vkimage, VK_OBJECT_TYPE_IMAGE, debugname );
        }
        {
            auto props = VkMemoryWin32HandlePropertiesKHR{
                .sType = VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR,
            };
            r = svkGetMemoryWin32HandlePropertiesKHR( vkdevice, dx12type, dx12resource, &props );
            if( r != VK_SUCCESS || props.memoryTypeBits == 0 )
            {
                debug::Error(
                    "CreateSharedImage: vkGetMemoryWin32HandlePropertiesKHR failed ({}): {}",
                    debugname,
                    int( r ) );
                return l_fail();
            }

            auto memoryTypeIndex = vkallocator.GetMemoryTypeIndex(
                props.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );

            if( !memoryTypeIndex )
            {
                debug::Error(
                    "CreateSharedImage: GetMemoryTypeIndex failed ({}): {}", debugname, int( r ) );
                return l_fail();
            }

            auto memreqs = VkMemoryRequirements{};
            vkGetImageMemoryRequirements( vkdevice, dst.vkimage, &memreqs );

            VkDeviceSize sz = Utils::Align( memreqs.size, memreqs.alignment );

            auto dedicated = VkMemoryDedicatedAllocateInfo{
                .sType  = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
                .pNext  = nullptr,
                .image  = dst.vkimage,
                .buffer = VK_NULL_HANDLE,
            };
            auto importInfo = VkImportMemoryWin32HandleInfoKHR{
                .sType      = VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR,
                .pNext      = &dedicated,
                .handleType = dx12type,
                .handle     = dx12resource,
                .name       = nullptr,
            };
            auto allocInfo = VkMemoryAllocateInfo{
                .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                .pNext           = &importInfo,
                .allocationSize  = sz,
                .memoryTypeIndex = *memoryTypeIndex,
            };
            r = vkAllocateMemory( vkdevice, &allocInfo, nullptr, &dst.vkmemory );
            if( r != VK_SUCCESS )
            {
                debug::Error(
                    "CreateSharedImage: vkAllocateMemory failed ({}): {}", debugname, int( r ) );
                return l_fail();
            }
            SET_DEBUG_NAME( vkdevice, dst.vkmemory, VK_OBJECT_TYPE_DEVICE_MEMORY, debugname );

            r = vkBindImageMemory( vkdevice, dst.vkimage, dst.vkmemory, 0 );
            if( r != VK_SUCCESS )
            {
                debug::Error(
                    "CreateSharedImage: vkBindImageMemory failed ({}): {}", debugname, int( r ) );
                return l_fail();
            }
        }

        static_assert( std::is_same_v< std::underlying_type_t< DXGI_FORMAT >, //
                                       decltype( dst.dxgiformat ) > );
        static_assert( std::is_same_v< std::underlying_type_t< VkFormat >, //
                                       decltype( dst.vkformat ) > );
        static_assert( sizeof( dst ) == 48, "Don't forget to add members here" );
        dst.width      = width;
        dst.height     = height;
        dst.vkformat   = int( vkformat );
        dst.dxgiformat = int( dxgiformat );

        return dst;
    }

    template< typename T >
        requires( std::is_default_constructible_v< T > )
    bool IsDefault( const T& v )
    {
        static auto defaultinstance = T{};
        return std::memcmp( &v, &defaultinstance, sizeof( T ) ) == 0;
    }

    const char* SemaphoreDebugName( std::underlying_type_t< SharedSemaphoreType > s )
    {
        switch( SharedSemaphoreType( s ) )
        {
            case SHARED_SEM_RENDER_FINISHED: return "SHARED_SEM_RENDER_FINISHED";
            case SHARED_SEM_FSR3_IN: return "SHARED_SEM_FSR3_IN";
            case SHARED_SEM_FSR3_OUT: return "SHARED_SEM_FSR3_OUT";
            case SHARED_SEM_PRESENT_COPY: return "SHARED_SEM_PRESENT_COPY";
            default: assert( 0 ); return "<get name failed>";
        }
    }
} // anonymous namespace


SharedImage     g_images[ ShFramebuffers_Count ]{};
SharedSemaphore g_semaphores[ SHARED_SEMAPHORE_TYPE_COUNT ]{};

VkDevice         g_vkdevice{ nullptr };
VkPhysicalDevice g_vkphysdevice{ nullptr };


void SetVk( VkDevice vkdevice, VkPhysicalDevice vkphysdevice )
{
    g_vkdevice     = vkdevice;
    g_vkphysdevice = vkphysdevice;
}

void Semaphores_Destroy( ID3D12Device* dx12device )
{
    if( g_vkdevice )
    {
        vkDeviceWaitIdle( g_vkdevice );
    }

    if( !dx12device )
    {
        assert( 0 );
        return;
    }

    for( SharedSemaphore& s : g_semaphores )
    {
        if( s.d3d12fenceEvent )
        {
            BOOL c = CloseHandle( s.d3d12fenceEvent );
            assert( c );
        }
        if( s.d3d12fence )
        {
            if( dx12device )
            {
                auto left = s.d3d12fence->Release();
                assert( left == 0 );
            }
            else
            {
                assert( 0 );
            }
        }
        if( s.sharedHandle )
        {
            if( dx12device )
            {
                auto b = CloseHandle( s.sharedHandle );
                assert( b );
            }
            else
            {
                assert( 0 );
            }
        }
        if( s.vksemaphore )
        {
            if( g_vkdevice )
            {
                vkDestroySemaphore( g_vkdevice, s.vksemaphore, nullptr );
            }
            else
            {
                assert( 0 );
            }
        }
        s = {};
    }
}

bool Semaphores_Create( ID3D12Device* dx12device )
{
    if( !dx12device )
    {
        assert( 0 );
        return false;
    }
    assert( g_vkdevice && g_vkphysdevice );

    assert( std::ranges::all_of( g_semaphores, IsDefault< SharedSemaphore > ) );

    bool failed = false;
    for( int i = 0; i < SHARED_SEMAPHORE_TYPE_COUNT; i++ )
    {
        auto s = CreateSharedSemaphore( g_vkdevice, //
                                        g_vkphysdevice,
                                        dx12device,
                                        SemaphoreDebugName( i ) );
        if( !s )
        {
            failed = true;
            break;
        }
        g_semaphores[ i ] = s.value();
    }

    if( failed )
    {
        Semaphores_Destroy( dx12device );
        return false;
    }
    return true;
}


auto Semaphores_GetVkDx12Shared( SharedSemaphoreType i ) -> std::optional< SharedSemaphore >
{
    assert( i >= 0 && i < int( std::size( g_semaphores ) ) );

    const SharedSemaphore& s = g_semaphores[ i ];

    if( !s.sharedHandle || !s.d3d12fence || !s.d3d12fenceEvent || !s.vksemaphore )
    {
        debug::Error( "Semaphores_GetVkDx12Shared failed, shared semaphore was destroyed" );
        return {};
    }
    return s;
}


int internal_VkFormatToDxgiFormat( int vkformat )
{
    return VkFormatToDxgiFormat( VkFormat( vkformat ) );
}

int internal_VkColorSpaceToDxgiColorSpace( int vkcolorspace )
{
    switch( VkColorSpaceKHR( vkcolorspace ) )
    {
        // clang-format off
        case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR:       return DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
        case VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT: return DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
        case VK_COLOR_SPACE_HDR10_ST2084_EXT:         return DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
            // clang-format on

        default: assert( 0 ); return DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    }
}


void Framebuf_CreateDX12Resources( CommandBufferManager&  vkcmds,
                                   MemoryAllocator&       vkallocator,
                                   const ResolutionState& resolution )
{
    if( !HasDX12Instance() )
    {
        return;
    }

    ID3D12Device* dx12device = GetD3D12Device();
    if( !dx12device )
    {
        return;
    }

    assert( std::ranges::all_of( g_images, IsDefault< SharedImage > ) );

    std::tuple< FramebufferImageIndex, uint32_t, uint32_t > fs[] = {
        { FB_IMAGE_INDEX_FINAL, resolution.renderWidth, resolution.renderHeight },
        { FB_IMAGE_INDEX_DEPTH_NDC, resolution.renderWidth, resolution.renderHeight },
        { FB_IMAGE_INDEX_MOTION_DLSS, resolution.renderWidth, resolution.renderHeight },
        { FB_IMAGE_INDEX_REACTIVITY, resolution.renderWidth, resolution.renderHeight },
        { FB_IMAGE_INDEX_UPSCALED_PING, resolution.upscaledWidth, resolution.upscaledHeight },
        { FB_IMAGE_INDEX_UPSCALED_PONG, resolution.upscaledWidth, resolution.upscaledHeight },
        { FB_IMAGE_INDEX_HUD_ONLY, resolution.upscaledWidth, resolution.upscaledHeight },
    };

    bool failed = false;
    for( const auto& [ index, width, height ] : fs )
    {
        // if fails, need to add support for .._Prev framebufs
        assert( RTGL1::ShFramebuffers_Bindings[ index ] ==
                RTGL1::ShFramebuffers_BindingsSwapped[ index ] );

        VkFormat vkformat = ShFramebuffers_Formats[ index ];

        DXGI_FORMAT dxgiformat = VkFormatToDxgiFormat( vkformat );
        if( dxgiformat == DXGI_FORMAT_UNKNOWN )
        {
            debug::Error( "Failed to convert VkFormat={} to DXGI_FORMAT", int( vkformat ) );
            failed = true;
            break;
        }

        auto name = std::string{ ShFramebuffers_DebugNames[ index ] } + " - Imported";

        auto s = CreateSharedImage( dx12device,
                                    vkallocator.GetDevice(),
                                    vkallocator,
                                    vkformat,
                                    dxgiformat,
                                    width,
                                    height,
                                    name.c_str() );
        if( !s )
        {
            failed = true;
            break;
        }

        g_images[ index ] = s.value();
    }

    if( failed )
    {
        assert( 0 );
        Framebuf_Destroy();
        return;
    }

    // to general layout
    VkCommandBuffer cmd = vkcmds.StartGraphicsCmd();
    for( const auto& [ index, _a, _i ] : fs )
    {
        Utils::BarrierImage( cmd,
                             g_images[ index ].vkimage,
                             0,
                             0,
                             VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_GENERAL );
    }
    vkcmds.Submit( cmd );
    vkcmds.WaitGraphicsIdle();
}

void Framebuf_Destroy()
{
    if( g_vkdevice )
    {
        vkDeviceWaitIdle( g_vkdevice );
    }
    dxgi::WaitIdle();

    if( !HasDX12Instance() )
    {
        return;
    }

    ID3D12Device* dx12device = GetD3D12Device();
    if( !dx12device )
    {
        return;
    }

    for( SharedImage& dst : g_images )
    {
        if( dst.vkimage )
        {
            if( g_vkdevice )
            {
                vkDestroyImage( g_vkdevice, dst.vkimage, nullptr );
            }
            else
            {
                assert( 0 );
            }
        }
        if( dst.vkmemory )
        {
            if( g_vkdevice )
            {
                vkFreeMemory( g_vkdevice, dst.vkmemory, nullptr );
            }
            else
            {
                assert( 0 );
            }
        }
        if( dst.sharedHandle )
        {
            if( dx12device )
            {
                auto b = CloseHandle( dst.sharedHandle );
                assert( b );
            }
            else
            {
                assert( 0 );
            }
        }
        if( dst.d3d12resource )
        {
            if( dx12device )
            {
                auto left = dst.d3d12resource->Release();
                // assert( left == 0 );
            }
            else
            {
                assert( 0 );
            }
        }
        dst = {};
    }
}

auto Framebuf_GetVkDx12Shared( int framebufImageIndex ) -> SharedImage
{
    if( framebufImageIndex < 0 || framebufImageIndex >= int( std::size( g_images ) ) )
    {
        assert( 0 );
        return {};
    }

    SharedImage& s = g_images[ framebufImageIndex ];

    assert( s.width > 0 && s.height > 0 && s.d3d12resource && s.sharedHandle && s.vkimage &&
            s.vkmemory && s.dxgiformat != DXGI_FORMAT_UNKNOWN &&
            s.vkformat != VK_FORMAT_UNDEFINED );
    return s;
}

bool Framebuf_HasSharedImages()
{
    return HasDX12Instance() && !std::ranges::all_of( g_images, IsDefault< SharedImage > );
}
}

#else

// clang-format off
namespace RTGL1::dxgi
{
auto VkFormatToDxgiFormat( int vkformat ) -> int{ assert( 0 ); return 0; }
}
// clang-format on

#endif // RG_USE_DX12
