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

#include "Utils.h"

#include <cmath>

#ifdef __linux__
    #include <dlfcn.h>
    #include <unistd.h>
    #include <linux/limits.h>
#endif

using namespace RTGL1;

auto Utils::FindBinFolder() -> std::filesystem::path
{
#if defined( _WIN32 )
    wchar_t rtglDllPath[ MAX_PATH ]{};
    GetModuleFileNameW( GetModuleHandle( RG_LIBRARY_NAME ), rtglDllPath, MAX_PATH );
#elif defined( __linux__ )
    wchar_t rtglDllPath[ PATH_MAX ]{};
    Dl_info dl_info{};
    if( dladdr( ( void* )GetFolderPath, &dl_info ) )
    {
        if( dl_info.dli_fname )
        {
            std::mbstowcs( rtglDllPath, dl_info.dli_fname, PATH_MAX );
        }
    }
#endif
    auto binFolder = std::filesystem::path{ rtglDllPath }.parent_path();
    if( binFolder.filename() == "debug" )
    {
        binFolder = binFolder.parent_path();
    }
    assert( binFolder.filename() == "bin" );
    return binFolder;
}

#if !RG_USE_REMIX

void Utils::BarrierImage( VkCommandBuffer                cmd,
                          VkImage                        image,
                          VkAccessFlags                  srcAccessMask,
                          VkAccessFlags                  dstAccessMask,
                          VkImageLayout                  oldLayout,
                          VkImageLayout                  newLayout,
                          VkPipelineStageFlags           srcStageMask,
                          VkPipelineStageFlags           dstStageMask,
                          const VkImageSubresourceRange& subresourceRange )
{
    VkImageMemoryBarrier imageBarrier = {};
    imageBarrier.sType                = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    imageBarrier.image                = image;
    imageBarrier.srcQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
    imageBarrier.dstQueueFamilyIndex  = VK_QUEUE_FAMILY_IGNORED;
    imageBarrier.srcAccessMask        = srcAccessMask;
    imageBarrier.dstAccessMask        = dstAccessMask;
    imageBarrier.oldLayout            = oldLayout;
    imageBarrier.newLayout            = newLayout;
    imageBarrier.subresourceRange     = subresourceRange;

    vkCmdPipelineBarrier(
        cmd, srcStageMask, dstStageMask, 0, 0, nullptr, 0, nullptr, 1, &imageBarrier );
}

void Utils::BarrierImage( VkCommandBuffer                cmd,
                          VkImage                        image,
                          VkAccessFlags                  srcAccessMask,
                          VkAccessFlags                  dstAccessMask,
                          VkImageLayout                  oldLayout,
                          VkImageLayout                  newLayout,
                          const VkImageSubresourceRange& subresourceRange )
{
    BarrierImage( cmd,
                  image,
                  srcAccessMask,
                  dstAccessMask,
                  oldLayout,
                  newLayout,
                  VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                  VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                  subresourceRange );
}

void Utils::BarrierImage( VkCommandBuffer      cmd,
                          VkImage              image,
                          VkAccessFlags        srcAccessMask,
                          VkAccessFlags        dstAccessMask,
                          VkImageLayout        oldLayout,
                          VkImageLayout        newLayout,
                          VkPipelineStageFlags srcStageMask,
                          VkPipelineStageFlags dstStageMask )
{
    VkImageSubresourceRange subresourceRange = {};
    subresourceRange.aspectMask              = VK_IMAGE_ASPECT_COLOR_BIT;
    subresourceRange.baseMipLevel            = 0;
    subresourceRange.levelCount              = 1;
    subresourceRange.baseArrayLayer          = 0;
    subresourceRange.layerCount              = 1;

    BarrierImage( cmd,
                  image,
                  srcAccessMask,
                  dstAccessMask,
                  oldLayout,
                  newLayout,
                  srcStageMask,
                  dstStageMask,
                  subresourceRange );
}

void Utils::BarrierImage( VkCommandBuffer cmd,
                          VkImage         image,
                          VkAccessFlags   srcAccessMask,
                          VkAccessFlags   dstAccessMask,
                          VkImageLayout   oldLayout,
                          VkImageLayout   newLayout )
{
    VkImageSubresourceRange subresourceRange = {};
    subresourceRange.aspectMask              = VK_IMAGE_ASPECT_COLOR_BIT;
    subresourceRange.baseMipLevel            = 0;
    subresourceRange.levelCount              = 1;
    subresourceRange.baseArrayLayer          = 0;
    subresourceRange.layerCount              = 1;

    BarrierImage( cmd,
                  image,
                  srcAccessMask,
                  dstAccessMask,
                  oldLayout,
                  newLayout,
                  VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                  VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                  subresourceRange );
}

void Utils::ASBuildMemoryBarrier( VkCommandBuffer cmd )
{
    VkMemoryBarrier barrier = {};
    barrier.sType           = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask   = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR |
                            VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;

    // wait for all building
    vkCmdPipelineBarrier( cmd,
                          VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                          VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                          0,
                          1,
                          &barrier,
                          0,
                          nullptr,
                          0,
                          nullptr );
}

void Utils::WaitForFence( VkDevice device, VkFence fence )
{
    VkResult r = vkWaitForFences( device, 1, &fence, VK_TRUE, UINT64_MAX );
    VK_CHECKERROR( r );
}

void Utils::ResetFence( VkDevice device, VkFence fence )
{
    VkResult r = vkResetFences( device, 1, &fence );
    VK_CHECKERROR( r );
}

void Utils::WaitAndResetFence( VkDevice device, VkFence fence )
{
    VkResult r;

    r = vkWaitForFences( device, 1, &fence, VK_TRUE, UINT64_MAX );
    VK_CHECKERROR( r );

    r = vkResetFences( device, 1, &fence );
    VK_CHECKERROR( r );
}

void Utils::WaitAndResetFences( VkDevice device, VkFence fence_A, VkFence fence_B )
{
    VkResult r;
    VkFence  fences[ 2 ];
    uint32_t count = 0;

    if( fence_A != VK_NULL_HANDLE )
    {
        fences[ count++ ] = fence_A;
    }

    if( fence_B != VK_NULL_HANDLE )
    {
        fences[ count++ ] = fence_B;
    }

    r = vkWaitForFences( device, count, fences, VK_TRUE, UINT64_MAX );
    VK_CHECKERROR( r );

    r = vkResetFences( device, count, fences );
    VK_CHECKERROR( r );
}

#endif // !RG_USE_REMIX

VkFormat Utils::ToUnorm( VkFormat f )
{
    switch( f )
    {
        case VK_FORMAT_R8_SRGB: return VK_FORMAT_R8_UNORM;
        case VK_FORMAT_R8G8_SRGB: return VK_FORMAT_R8G8_UNORM;
        case VK_FORMAT_R8G8B8_SRGB: return VK_FORMAT_R8G8B8_UNORM;
        case VK_FORMAT_B8G8R8_SRGB: return VK_FORMAT_B8G8R8_UNORM;
        case VK_FORMAT_R8G8B8A8_SRGB: return VK_FORMAT_R8G8B8A8_UNORM;
        case VK_FORMAT_B8G8R8A8_SRGB: return VK_FORMAT_B8G8R8A8_UNORM;
        case VK_FORMAT_A8B8G8R8_SRGB_PACK32: return VK_FORMAT_A8B8G8R8_UNORM_PACK32;
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK: return VK_FORMAT_BC1_RGB_UNORM_BLOCK;
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        case VK_FORMAT_BC2_SRGB_BLOCK: return VK_FORMAT_BC2_UNORM_BLOCK;
        case VK_FORMAT_BC3_SRGB_BLOCK: return VK_FORMAT_BC3_UNORM_BLOCK;
        case VK_FORMAT_BC7_SRGB_BLOCK: return VK_FORMAT_BC7_UNORM_BLOCK;
        default: return f;
    }
}

VkFormat Utils::ToSRGB( VkFormat f )
{
    switch( f )
    {
        case VK_FORMAT_R8_UNORM: return VK_FORMAT_R8_SRGB;
        case VK_FORMAT_R8G8_UNORM: return VK_FORMAT_R8G8_SRGB;
        case VK_FORMAT_R8G8B8_UNORM: return VK_FORMAT_R8G8B8_SRGB;
        case VK_FORMAT_B8G8R8_UNORM: return VK_FORMAT_B8G8R8_SRGB;
        case VK_FORMAT_R8G8B8A8_UNORM: return VK_FORMAT_R8G8B8A8_SRGB;
        case VK_FORMAT_B8G8R8A8_UNORM: return VK_FORMAT_B8G8R8A8_SRGB;
        case VK_FORMAT_A8B8G8R8_UNORM_PACK32: return VK_FORMAT_A8B8G8R8_SRGB_PACK32;
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK: return VK_FORMAT_BC1_RGB_SRGB_BLOCK;
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK: return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
        case VK_FORMAT_BC2_UNORM_BLOCK: return VK_FORMAT_BC2_SRGB_BLOCK;
        case VK_FORMAT_BC3_UNORM_BLOCK: return VK_FORMAT_BC3_SRGB_BLOCK;
        case VK_FORMAT_BC7_UNORM_BLOCK: return VK_FORMAT_BC7_SRGB_BLOCK;
        default: return f;
    }
}

bool Utils::IsSRGB( VkFormat f )
{
    return f != ToUnorm( f );
}

bool Utils::AreViewportsSame( const VkViewport& a, const VkViewport& b )
{
    // special epsilons for viewports
    const float eps      = 0.1f;
    const float depthEps = 0.001f;

    return std::abs( a.x - b.x ) < eps && std::abs( a.y - b.y ) < eps &&
           std::abs( a.width - b.width ) < eps && std::abs( a.height - b.height ) < eps &&
           std::abs( a.minDepth - b.minDepth ) < depthEps &&
           std::abs( a.maxDepth - b.maxDepth ) < depthEps;
}
