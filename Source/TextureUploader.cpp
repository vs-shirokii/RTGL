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

#include "TextureUploader.h"

#include <algorithm>
#include <cmath>

#include "Const.h"
#include "Utils.h"

namespace
{

auto GetFormatsWithBlitSupport( VkPhysicalDevice physDevice ) -> rgl::unordered_set< VkFormat >
{
    constexpr VkFormat allFormats[] = {
        VK_FORMAT_R4G4_UNORM_PACK8,
        VK_FORMAT_R4G4B4A4_UNORM_PACK16,
        VK_FORMAT_B4G4R4A4_UNORM_PACK16,
        VK_FORMAT_R5G6B5_UNORM_PACK16,
        VK_FORMAT_B5G6R5_UNORM_PACK16,
        VK_FORMAT_R5G5B5A1_UNORM_PACK16,
        VK_FORMAT_B5G5R5A1_UNORM_PACK16,
        VK_FORMAT_A1R5G5B5_UNORM_PACK16,
        VK_FORMAT_R8_UNORM,
        VK_FORMAT_R8_SNORM,
        VK_FORMAT_R8_USCALED,
        VK_FORMAT_R8_SSCALED,
        VK_FORMAT_R8_UINT,
        VK_FORMAT_R8_SINT,
        VK_FORMAT_R8_SRGB,
        VK_FORMAT_R8G8_UNORM,
        VK_FORMAT_R8G8_SNORM,
        VK_FORMAT_R8G8_USCALED,
        VK_FORMAT_R8G8_SSCALED,
        VK_FORMAT_R8G8_UINT,
        VK_FORMAT_R8G8_SINT,
        VK_FORMAT_R8G8_SRGB,
        VK_FORMAT_R8G8B8_UNORM,
        VK_FORMAT_R8G8B8_SNORM,
        VK_FORMAT_R8G8B8_USCALED,
        VK_FORMAT_R8G8B8_SSCALED,
        VK_FORMAT_R8G8B8_UINT,
        VK_FORMAT_R8G8B8_SINT,
        VK_FORMAT_R8G8B8_SRGB,
        VK_FORMAT_B8G8R8_UNORM,
        VK_FORMAT_B8G8R8_SNORM,
        VK_FORMAT_B8G8R8_USCALED,
        VK_FORMAT_B8G8R8_SSCALED,
        VK_FORMAT_B8G8R8_UINT,
        VK_FORMAT_B8G8R8_SINT,
        VK_FORMAT_B8G8R8_SRGB,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_R8G8B8A8_SNORM,
        VK_FORMAT_R8G8B8A8_USCALED,
        VK_FORMAT_R8G8B8A8_SSCALED,
        VK_FORMAT_R8G8B8A8_UINT,
        VK_FORMAT_R8G8B8A8_SINT,
        VK_FORMAT_R8G8B8A8_SRGB,
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_B8G8R8A8_SNORM,
        VK_FORMAT_B8G8R8A8_USCALED,
        VK_FORMAT_B8G8R8A8_SSCALED,
        VK_FORMAT_B8G8R8A8_UINT,
        VK_FORMAT_B8G8R8A8_SINT,
        VK_FORMAT_B8G8R8A8_SRGB,
        VK_FORMAT_A8B8G8R8_UNORM_PACK32,
        VK_FORMAT_A8B8G8R8_SNORM_PACK32,
        VK_FORMAT_A8B8G8R8_USCALED_PACK32,
        VK_FORMAT_A8B8G8R8_SSCALED_PACK32,
        VK_FORMAT_A8B8G8R8_UINT_PACK32,
        VK_FORMAT_A8B8G8R8_SINT_PACK32,
        VK_FORMAT_A8B8G8R8_SRGB_PACK32,
        VK_FORMAT_A2R10G10B10_UNORM_PACK32,
        VK_FORMAT_A2R10G10B10_SNORM_PACK32,
        VK_FORMAT_A2R10G10B10_USCALED_PACK32,
        VK_FORMAT_A2R10G10B10_SSCALED_PACK32,
        VK_FORMAT_A2R10G10B10_UINT_PACK32,
        VK_FORMAT_A2R10G10B10_SINT_PACK32,
        VK_FORMAT_A2B10G10R10_UNORM_PACK32,
        VK_FORMAT_A2B10G10R10_SNORM_PACK32,
        VK_FORMAT_A2B10G10R10_USCALED_PACK32,
        VK_FORMAT_A2B10G10R10_SSCALED_PACK32,
        VK_FORMAT_A2B10G10R10_UINT_PACK32,
        VK_FORMAT_A2B10G10R10_SINT_PACK32,
        VK_FORMAT_R16_UNORM,
        VK_FORMAT_R16_SNORM,
        VK_FORMAT_R16_USCALED,
        VK_FORMAT_R16_SSCALED,
        VK_FORMAT_R16_UINT,
        VK_FORMAT_R16_SINT,
        VK_FORMAT_R16_SFLOAT,
        VK_FORMAT_R16G16_UNORM,
        VK_FORMAT_R16G16_SNORM,
        VK_FORMAT_R16G16_USCALED,
        VK_FORMAT_R16G16_SSCALED,
        VK_FORMAT_R16G16_UINT,
        VK_FORMAT_R16G16_SINT,
        VK_FORMAT_R16G16_SFLOAT,
        VK_FORMAT_R16G16B16_UNORM,
        VK_FORMAT_R16G16B16_SNORM,
        VK_FORMAT_R16G16B16_USCALED,
        VK_FORMAT_R16G16B16_SSCALED,
        VK_FORMAT_R16G16B16_UINT,
        VK_FORMAT_R16G16B16_SINT,
        VK_FORMAT_R16G16B16_SFLOAT,
        VK_FORMAT_R16G16B16A16_UNORM,
        VK_FORMAT_R16G16B16A16_SNORM,
        VK_FORMAT_R16G16B16A16_USCALED,
        VK_FORMAT_R16G16B16A16_SSCALED,
        VK_FORMAT_R16G16B16A16_UINT,
        VK_FORMAT_R16G16B16A16_SINT,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_FORMAT_R32_UINT,
        VK_FORMAT_R32_SINT,
        VK_FORMAT_R32_SFLOAT,
        VK_FORMAT_R32G32_UINT,
        VK_FORMAT_R32G32_SINT,
        VK_FORMAT_R32G32_SFLOAT,
        VK_FORMAT_R32G32B32_UINT,
        VK_FORMAT_R32G32B32_SINT,
        VK_FORMAT_R32G32B32_SFLOAT,
        VK_FORMAT_R32G32B32A32_UINT,
        VK_FORMAT_R32G32B32A32_SINT,
        VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_FORMAT_R64_UINT,
        VK_FORMAT_R64_SINT,
        VK_FORMAT_R64_SFLOAT,
        VK_FORMAT_R64G64_UINT,
        VK_FORMAT_R64G64_SINT,
        VK_FORMAT_R64G64_SFLOAT,
        VK_FORMAT_R64G64B64_UINT,
        VK_FORMAT_R64G64B64_SINT,
        VK_FORMAT_R64G64B64_SFLOAT,
        VK_FORMAT_R64G64B64A64_UINT,
        VK_FORMAT_R64G64B64A64_SINT,
        VK_FORMAT_R64G64B64A64_SFLOAT,
        VK_FORMAT_B10G11R11_UFLOAT_PACK32,
        VK_FORMAT_E5B9G9R9_UFLOAT_PACK32,
        VK_FORMAT_D16_UNORM,
        VK_FORMAT_X8_D24_UNORM_PACK32,
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_S8_UINT,
        VK_FORMAT_D16_UNORM_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_BC1_RGB_UNORM_BLOCK,
        VK_FORMAT_BC1_RGB_SRGB_BLOCK,
        VK_FORMAT_BC1_RGBA_UNORM_BLOCK,
        VK_FORMAT_BC1_RGBA_SRGB_BLOCK,
        VK_FORMAT_BC2_UNORM_BLOCK,
        VK_FORMAT_BC2_SRGB_BLOCK,
        VK_FORMAT_BC3_UNORM_BLOCK,
        VK_FORMAT_BC3_SRGB_BLOCK,
        VK_FORMAT_BC4_UNORM_BLOCK,
        VK_FORMAT_BC4_SNORM_BLOCK,
        VK_FORMAT_BC5_UNORM_BLOCK,
        VK_FORMAT_BC5_SNORM_BLOCK,
        VK_FORMAT_BC6H_UFLOAT_BLOCK,
        VK_FORMAT_BC6H_SFLOAT_BLOCK,
        VK_FORMAT_BC7_UNORM_BLOCK,
        VK_FORMAT_BC7_SRGB_BLOCK,
        VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK,
        VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK,
        VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK,
        VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK,
        VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK,
        VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK,
        VK_FORMAT_EAC_R11_UNORM_BLOCK,
        VK_FORMAT_EAC_R11_SNORM_BLOCK,
        VK_FORMAT_EAC_R11G11_UNORM_BLOCK,
        VK_FORMAT_EAC_R11G11_SNORM_BLOCK,
        VK_FORMAT_ASTC_4x4_UNORM_BLOCK,
        VK_FORMAT_ASTC_4x4_SRGB_BLOCK,
        VK_FORMAT_ASTC_5x4_UNORM_BLOCK,
        VK_FORMAT_ASTC_5x4_SRGB_BLOCK,
        VK_FORMAT_ASTC_5x5_UNORM_BLOCK,
        VK_FORMAT_ASTC_5x5_SRGB_BLOCK,
        VK_FORMAT_ASTC_6x5_UNORM_BLOCK,
        VK_FORMAT_ASTC_6x5_SRGB_BLOCK,
        VK_FORMAT_ASTC_6x6_UNORM_BLOCK,
        VK_FORMAT_ASTC_6x6_SRGB_BLOCK,
        VK_FORMAT_ASTC_8x5_UNORM_BLOCK,
        VK_FORMAT_ASTC_8x5_SRGB_BLOCK,
        VK_FORMAT_ASTC_8x6_UNORM_BLOCK,
        VK_FORMAT_ASTC_8x6_SRGB_BLOCK,
        VK_FORMAT_ASTC_8x8_UNORM_BLOCK,
        VK_FORMAT_ASTC_8x8_SRGB_BLOCK,
        VK_FORMAT_ASTC_10x5_UNORM_BLOCK,
        VK_FORMAT_ASTC_10x5_SRGB_BLOCK,
        VK_FORMAT_ASTC_10x6_UNORM_BLOCK,
        VK_FORMAT_ASTC_10x6_SRGB_BLOCK,
        VK_FORMAT_ASTC_10x8_UNORM_BLOCK,
        VK_FORMAT_ASTC_10x8_SRGB_BLOCK,
        VK_FORMAT_ASTC_10x10_UNORM_BLOCK,
        VK_FORMAT_ASTC_10x10_SRGB_BLOCK,
        VK_FORMAT_ASTC_12x10_UNORM_BLOCK,
        VK_FORMAT_ASTC_12x10_SRGB_BLOCK,
        VK_FORMAT_ASTC_12x12_UNORM_BLOCK,
        VK_FORMAT_ASTC_12x12_SRGB_BLOCK,
        VK_FORMAT_G8B8G8R8_422_UNORM,
        VK_FORMAT_B8G8R8G8_422_UNORM,
        VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM,
        VK_FORMAT_G8_B8R8_2PLANE_420_UNORM,
        VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM,
        VK_FORMAT_G8_B8R8_2PLANE_422_UNORM,
        VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM,
        VK_FORMAT_R10X6_UNORM_PACK16,
        VK_FORMAT_R10X6G10X6_UNORM_2PACK16,
        VK_FORMAT_R10X6G10X6B10X6A10X6_UNORM_4PACK16,
        VK_FORMAT_G10X6B10X6G10X6R10X6_422_UNORM_4PACK16,
        VK_FORMAT_B10X6G10X6R10X6G10X6_422_UNORM_4PACK16,
        VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16,
        VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16,
        VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16,
        VK_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16,
        VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16,
        VK_FORMAT_R12X4_UNORM_PACK16,
        VK_FORMAT_R12X4G12X4_UNORM_2PACK16,
        VK_FORMAT_R12X4G12X4B12X4A12X4_UNORM_4PACK16,
        VK_FORMAT_G12X4B12X4G12X4R12X4_422_UNORM_4PACK16,
        VK_FORMAT_B12X4G12X4R12X4G12X4_422_UNORM_4PACK16,
        VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16,
        VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16,
        VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16,
        VK_FORMAT_G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16,
        VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16,
        VK_FORMAT_G16B16G16R16_422_UNORM,
        VK_FORMAT_B16G16R16G16_422_UNORM,
        VK_FORMAT_G16_B16_R16_3PLANE_420_UNORM,
        VK_FORMAT_G16_B16R16_2PLANE_420_UNORM,
        VK_FORMAT_G16_B16_R16_3PLANE_422_UNORM,
        VK_FORMAT_G16_B16R16_2PLANE_422_UNORM,
        VK_FORMAT_G16_B16_R16_3PLANE_444_UNORM,
        //VK_FORMAT_G8_B8R8_2PLANE_444_UNORM,
        //VK_FORMAT_G10X6_B10X6R10X6_2PLANE_444_UNORM_3PACK16,
        //VK_FORMAT_G12X4_B12X4R12X4_2PLANE_444_UNORM_3PACK16,
        //VK_FORMAT_G16_B16R16_2PLANE_444_UNORM,
        //VK_FORMAT_A4R4G4B4_UNORM_PACK16,
        //VK_FORMAT_A4B4G4R4_UNORM_PACK16,
        //VK_FORMAT_ASTC_4x4_SFLOAT_BLOCK,
        //VK_FORMAT_ASTC_5x4_SFLOAT_BLOCK,
        //VK_FORMAT_ASTC_5x5_SFLOAT_BLOCK,
        //VK_FORMAT_ASTC_6x5_SFLOAT_BLOCK,
        //VK_FORMAT_ASTC_6x6_SFLOAT_BLOCK,
        //VK_FORMAT_ASTC_8x5_SFLOAT_BLOCK,
        //VK_FORMAT_ASTC_8x6_SFLOAT_BLOCK,
        //VK_FORMAT_ASTC_8x8_SFLOAT_BLOCK,
        //VK_FORMAT_ASTC_10x5_SFLOAT_BLOCK,
        //VK_FORMAT_ASTC_10x6_SFLOAT_BLOCK,
        //VK_FORMAT_ASTC_10x8_SFLOAT_BLOCK,
        //VK_FORMAT_ASTC_10x10_SFLOAT_BLOCK,
        //VK_FORMAT_ASTC_12x10_SFLOAT_BLOCK,
        //VK_FORMAT_ASTC_12x12_SFLOAT_BLOCK,
        //VK_FORMAT_PVRTC1_2BPP_UNORM_BLOCK_IMG,
        //VK_FORMAT_PVRTC1_4BPP_UNORM_BLOCK_IMG,
        //VK_FORMAT_PVRTC2_2BPP_UNORM_BLOCK_IMG,
        //VK_FORMAT_PVRTC2_4BPP_UNORM_BLOCK_IMG,
        //VK_FORMAT_PVRTC1_2BPP_SRGB_BLOCK_IMG,
        //VK_FORMAT_PVRTC1_4BPP_SRGB_BLOCK_IMG,
        //VK_FORMAT_PVRTC2_2BPP_SRGB_BLOCK_IMG,
        //VK_FORMAT_PVRTC2_4BPP_SRGB_BLOCK_IMG,
        //VK_FORMAT_R16G16_S10_5_NV,
    };

    auto result = rgl::unordered_set< VkFormat >{};
    for( auto f : allFormats )
    {
        auto props = VkFormatProperties{};
        vkGetPhysicalDeviceFormatProperties( physDevice, f, &props );

        if( props.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT )
        {
            if( props.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT )
            {
                result.insert( f );
            }
        }
    }
    return result;
}

}

using namespace RTGL1;

TextureUploader::TextureUploader( VkDevice                           _device,
                                  std::shared_ptr< MemoryAllocator > _memAllocator )
    : device( _device )
    , memAllocator{ std::move( _memAllocator ) }
    , supportBlit{ GetFormatsWithBlitSupport( memAllocator->GetPhysicalDevice() ) }
{
}

TextureUploader::~TextureUploader()
{
    for( uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++ )
    {
        ClearStaging( i );
    }

    for( auto& p : updateableImageInfos )
    {
        memAllocator->DestroyStagingSrcTextureBuffer( p.second.stagingBuffer );
    }
}

void TextureUploader::ClearStaging( uint32_t frameIndex )
{
    // clear unused staging
    for( VkBuffer stagingBuffer : stagingToFree[ frameIndex ] )
    {
        memAllocator->DestroyStagingSrcTextureBuffer( stagingBuffer );
    }

    stagingToFree[ frameIndex ].clear();
}

bool TextureUploader::DoesFormatSupportBlit( VkFormat format ) const
{
    return supportBlit.contains( format );
}

bool TextureUploader::AreMipmapsPregenerated( const UploadInfo& info ) const
{
    return info.pregeneratedLevelCount > 0;
}

uint32_t TextureUploader::GetMipmapCount( const RgExtent2D& size, const UploadInfo& info ) const
{
    if( !info.useMipmaps )
    {
        return 1;
    }

    if( AreMipmapsPregenerated( info ) )
    {
        return std::min( info.pregeneratedLevelCount, MAX_PREGENERATED_MIPMAP_LEVELS );
    }

    auto widthCount  = static_cast< uint32_t >( log2( size.width ) );
    auto heightCount = static_cast< uint32_t >( log2( size.height ) );

    return std::min( widthCount, heightCount ) + 1;
}

void TextureUploader::PrepareMipmaps( VkCommandBuffer cmd,
                                      VkImage         image,
                                      uint32_t        baseWidth,
                                      uint32_t        baseHeight,
                                      uint32_t        mipmapCount,
                                      uint32_t        layerCount )
{
    if( mipmapCount <= 1 )
    {
        return;
    }

    uint32_t mipWidth  = baseWidth;
    uint32_t mipHeight = baseHeight;

    for( uint32_t mipLevel = 1; mipLevel < mipmapCount; mipLevel++ )
    {
        uint32_t prevMipWidth  = mipWidth;
        uint32_t prevMipHeight = mipHeight;

        mipWidth >>= 1;
        mipHeight >>= 1;

        assert( mipWidth > 0 && mipHeight > 0 );
        assert( mipLevel != mipmapCount - 1 || ( mipWidth == 1 || mipHeight == 1 ) );

        VkImageSubresourceRange curMipmap = {};
        curMipmap.aspectMask              = VK_IMAGE_ASPECT_COLOR_BIT;
        curMipmap.baseMipLevel            = mipLevel;
        curMipmap.levelCount              = 1;
        curMipmap.baseArrayLayer          = 0;
        curMipmap.layerCount              = layerCount;

        // current mip to TRANSFER_DST
        Utils::BarrierImage( cmd,
                             image,
                             0,
                             VK_ACCESS_TRANSFER_WRITE_BIT,
                             VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             curMipmap );

        // blit from previous mip level
        VkImageBlit curBlit = {};

        curBlit.srcSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        curBlit.srcSubresource.mipLevel       = mipLevel - 1;
        curBlit.srcSubresource.baseArrayLayer = 0;
        curBlit.srcSubresource.layerCount     = layerCount;
        curBlit.srcOffsets[ 0 ]               = { 0, 0, 0 };
        curBlit.srcOffsets[ 1 ] = { ( int32_t )prevMipWidth, ( int32_t )prevMipHeight, 1 };

        curBlit.dstSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        curBlit.dstSubresource.mipLevel       = mipLevel;
        curBlit.dstSubresource.baseArrayLayer = 0;
        curBlit.dstSubresource.layerCount     = layerCount;
        curBlit.dstOffsets[ 0 ]               = { 0, 0, 0 };
        curBlit.dstOffsets[ 1 ]               = { ( int32_t )mipWidth, ( int32_t )mipHeight, 1 };

        vkCmdBlitImage( cmd,
                        image,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        image,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        1,
                        &curBlit,
                        VK_FILTER_LINEAR );

        // current mip to TRANSFER_SRC for the next one
        Utils::BarrierImage( cmd,
                             image,
                             VK_ACCESS_TRANSFER_WRITE_BIT,
                             VK_ACCESS_TRANSFER_READ_BIT,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             curMipmap );
    }
}

void TextureUploader::CopyStagingToImage( VkCommandBuffer   cmd,
                                          VkBuffer          staging,
                                          VkImage           image,
                                          const RgExtent2D& size,
                                          uint32_t          baseLayer,
                                          uint32_t          layerCount )
{
    VkBufferImageCopy copyRegion = {};
    copyRegion.bufferOffset      = 0;
    // tigthly packed
    copyRegion.bufferRowLength                 = 0;
    copyRegion.bufferImageHeight               = 0;
    copyRegion.imageExtent                     = { size.width, size.height, 1 };
    copyRegion.imageOffset                     = { 0, 0, 0 };
    copyRegion.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.imageSubresource.mipLevel       = 0;
    copyRegion.imageSubresource.baseArrayLayer = baseLayer;
    copyRegion.imageSubresource.layerCount     = layerCount;

    vkCmdCopyBufferToImage(
        cmd, staging, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion );
}

void TextureUploader::CopyStagingToImageMipmaps( VkCommandBuffer   cmd,
                                                 VkBuffer          staging,
                                                 VkImage           image,
                                                 uint32_t          layerIndex,
                                                 const UploadInfo& info )
{
    uint32_t mipWidth  = info.baseSize.width;
    uint32_t mipHeight = info.baseSize.height;

    uint32_t levelCount = GetMipmapCount( info.baseSize, info );

    VkBufferImageCopy copyRegions[ MAX_PREGENERATED_MIPMAP_LEVELS ];

    for( uint32_t mipLevel = 0; mipLevel < levelCount; mipLevel++ )
    {
        auto& cr = copyRegions[ mipLevel ];

        cr                                 = {};
        cr.bufferOffset                    = info.pLevelDataOffsets[ mipLevel ];
        cr.bufferRowLength                 = 0;
        cr.bufferImageHeight               = 0;
        cr.imageExtent                     = { mipWidth, mipHeight, 1 };
        cr.imageOffset                     = { 0, 0, 0 };
        cr.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        cr.imageSubresource.mipLevel       = mipLevel;
        cr.imageSubresource.baseArrayLayer = layerIndex;
        cr.imageSubresource.layerCount     = 1;

        mipWidth >>= 1;
        mipHeight >>= 1;
    }

    vkCmdCopyBufferToImage(
        cmd, staging, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, levelCount, copyRegions );
}


bool TextureUploader::CreateImage( const UploadInfo& info, VkImage* result )
{
    const RgExtent2D& size = info.baseSize;

    // 1. Create image and allocate its memory

    VkImageCreateInfo imageInfo = {};
    imageInfo.sType             = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType         = VK_IMAGE_TYPE_2D;
    imageInfo.flags             = info.isCubemap ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    imageInfo.format            = info.format;
    imageInfo.extent            = { size.width, size.height, 1 };
    imageInfo.mipLevels         = GetMipmapCount( size, info );
    imageInfo.arrayLayers       = info.isCubemap ? 6 : 1;
    imageInfo.samples           = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling            = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage             = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    VkImage image = memAllocator->CreateDstTextureImage( &imageInfo, info.pDebugName );
    if( image == VK_NULL_HANDLE )
    {
        return false;
    }

    SET_DEBUG_NAME( device, image, VK_OBJECT_TYPE_IMAGE, info.pDebugName );

    *result = image;
    return true;
}

void TextureUploader::PrepareImage( VkImage           image,
                                    VkBuffer          staging[],
                                    const UploadInfo& info,
                                    ImagePrepareType  prepareType )
{
    VkCommandBuffer   cmd         = info.cmd;
    const RgExtent2D& size        = info.baseSize;
    uint32_t          layerCount  = info.isCubemap ? 6 : 1;
    uint32_t          mipmapCount = GetMipmapCount( size, info );

    VkImageSubresourceRange firstMipmap = {};
    firstMipmap.aspectMask              = VK_IMAGE_ASPECT_COLOR_BIT;
    firstMipmap.baseMipLevel            = 0;
    firstMipmap.levelCount              = 1;
    firstMipmap.baseArrayLayer          = 0;
    firstMipmap.layerCount              = layerCount;

    VkImageSubresourceRange allMipmaps = {};
    allMipmaps.aspectMask              = VK_IMAGE_ASPECT_COLOR_BIT;
    allMipmaps.baseMipLevel            = 0;
    allMipmaps.levelCount              = mipmapCount;
    allMipmaps.baseArrayLayer          = 0;
    allMipmaps.layerCount              = layerCount;


    // 2. Copy buffer data to the first mipmap

    VkAccessFlags        curAccessMask;
    VkImageLayout        curLayout;
    VkPipelineStageFlags curStageMask;

    // if image was already prepared
    if( prepareType == ImagePrepareType::UPDATE )
    {
        curAccessMask = VK_ACCESS_SHADER_READ_BIT;
        curLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        curStageMask =
            VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else
    {
        curAccessMask = 0;
        curLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
        curStageMask  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    }

    // if need to copy from staging
    if( prepareType != ImagePrepareType::INIT_WITHOUT_COPYING )
    {
        if( AreMipmapsPregenerated( info ) )
        {
            // copy all mip levels from memory

            assert( layerCount == 1 );

            const uint32_t layerIndex = 0;

            // set layout for copying
            Utils::BarrierImage( cmd,
                                 image,
                                 curAccessMask,
                                 VK_ACCESS_TRANSFER_WRITE_BIT,
                                 curLayout,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 curStageMask,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 allMipmaps );

            // update params
            curAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            curLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            curStageMask  = VK_PIPELINE_STAGE_TRANSFER_BIT;

            CopyStagingToImageMipmaps( cmd, staging[ layerIndex ], image, layerIndex, info );
        }
        else
        {
            // copy only first mip level, others will be generated, if needed

            for( uint32_t layer = 0; layer < layerCount; layer++ )
            {
                // set layout for copying
                Utils::BarrierImage( cmd,
                                     image,
                                     curAccessMask,
                                     VK_ACCESS_TRANSFER_WRITE_BIT,
                                     curLayout,
                                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                     curStageMask,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     firstMipmap );

                // update params
                curAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                curLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                curStageMask  = VK_PIPELINE_STAGE_TRANSFER_BIT;

                // copy only first mipmap
                CopyStagingToImage( cmd, staging[ layer ], image, size, layer, 1 );
            }
        }
    }

    if( mipmapCount > 1 )
    {
        if( !AreMipmapsPregenerated( info ) )
        {
            // 3A. 1. Generate mipmaps

            if( DoesFormatSupportBlit( info.format ) )
            {
                // first mipmap to TRANSFER_SRC to create mipmaps using blit
                Utils::BarrierImage( cmd,
                                     image,
                                     curAccessMask,
                                     VK_ACCESS_TRANSFER_READ_BIT,
                                     curLayout,
                                     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                     curStageMask,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     firstMipmap );


                PrepareMipmaps( cmd, image, size.width, size.height, mipmapCount, layerCount );

                curLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            }
            else
            {
                debug::Warning(
                    "Texture will have black mipmaps, as VkFormat {} doesn't support blit: {}",
                    uint32_t( info.format ),
                    Utils::IsCstrEmpty( info.pDebugName ) ? "<unnamed>" : info.pDebugName );
                assert( 0 );
            }
        }
        else
        {
            // 3B. 1. Mipmaps are already copied
        }


        // 3A, 3B. 2. Prepare all mipmaps for reading in ray tracing and fragment shaders

        Utils::BarrierImage( cmd,
                             image,
                             VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT,
                             VK_ACCESS_SHADER_READ_BIT,
                             curLayout,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR |
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             allMipmaps );
    }
    else
    {
        // 3C. Prepare only the first mipmap for reading in ray tracing and fragment shaders

        Utils::BarrierImage( cmd,
                             image,
                             curAccessMask,
                             VK_ACCESS_SHADER_READ_BIT,
                             curLayout,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                             curStageMask,
                             VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR |
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             firstMipmap );
    }
}

VkImageView TextureUploader::CreateImageView( VkImage                             image,
                                              VkFormat                            format,
                                              bool                                isCubemap,
                                              uint32_t                            mipmapCount,
                                              std::optional< RgTextureSwizzling > swizzling )
{
    VkComponentMapping mapping = {
        .r = VK_COMPONENT_SWIZZLE_IDENTITY,
        .g = VK_COMPONENT_SWIZZLE_IDENTITY,
        .b = VK_COMPONENT_SWIZZLE_IDENTITY,
        .a = VK_COMPONENT_SWIZZLE_IDENTITY,
    };

    if( swizzling )
    {
        switch( swizzling.value() )
        {
            case RG_TEXTURE_SWIZZLING_NULL_ROUGHNESS_METALLIC:
                mapping = {
                    .r = VK_COMPONENT_SWIZZLE_ONE,
                    .g = VK_COMPONENT_SWIZZLE_G,
                    .b = VK_COMPONENT_SWIZZLE_B,
                    .a = VK_COMPONENT_SWIZZLE_A,
                };
                break;

            case RG_TEXTURE_SWIZZLING_NULL_METALLIC_ROUGHNESS:
                mapping = {
                    .r = VK_COMPONENT_SWIZZLE_ONE,
                    .g = VK_COMPONENT_SWIZZLE_B,
                    .b = VK_COMPONENT_SWIZZLE_G,
                    .a = VK_COMPONENT_SWIZZLE_A,
                };
                break;

            case RG_TEXTURE_SWIZZLING_OCCLUSION_ROUGHNESS_METALLIC:
                mapping = {
                    .r = VK_COMPONENT_SWIZZLE_R,
                    .g = VK_COMPONENT_SWIZZLE_G,
                    .b = VK_COMPONENT_SWIZZLE_B,
                    .a = VK_COMPONENT_SWIZZLE_A,
                };
                break;

            case RG_TEXTURE_SWIZZLING_OCCLUSION_METALLIC_ROUGHNESS:
                mapping = {
                    .r = VK_COMPONENT_SWIZZLE_R,
                    .g = VK_COMPONENT_SWIZZLE_B,
                    .b = VK_COMPONENT_SWIZZLE_G,
                    .a = VK_COMPONENT_SWIZZLE_A,
                };
                break;

            case RG_TEXTURE_SWIZZLING_ROUGHNESS_METALLIC:
                mapping = {
                    .r = VK_COMPONENT_SWIZZLE_ONE,
                    .g = VK_COMPONENT_SWIZZLE_R,
                    .b = VK_COMPONENT_SWIZZLE_G,
                    .a = VK_COMPONENT_SWIZZLE_A,
                };
                break;

            case RG_TEXTURE_SWIZZLING_METALLIC_ROUGHNESS:
                mapping = {
                    .r = VK_COMPONENT_SWIZZLE_ONE,
                    .g = VK_COMPONENT_SWIZZLE_G,
                    .b = VK_COMPONENT_SWIZZLE_R,
                    .a = VK_COMPONENT_SWIZZLE_A,
                };
                break;

            default: assert( 0 ); break;
        }
    }


    VkImageViewCreateInfo viewInfo = {
        .sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image      = image,
        .viewType   = isCubemap ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D,
        .format     = format,
        .components = mapping,
        .subresourceRange = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = mipmapCount,
            .baseArrayLayer = 0,
            .layerCount     = isCubemap ? 6u : 1u,
        },
    };

    VkImageView view;
    VkResult    r = vkCreateImageView( device, &viewInfo, nullptr, &view );
    VK_CHECKERROR( r );

    return view;
}

TextureUploader::UploadResult TextureUploader::UploadImage( const UploadInfo& info )
{
    // cubemaps are processed in other class
    assert( !info.isCubemap );


    const void*       data     = info.pData;
    VkDeviceSize      dataSize = info.dataSize;
    const RgExtent2D& size     = info.baseSize;


    // updateable can have null data, so it can be provided later
    if( !info.isUpdateable )
    {
        assert( data != nullptr );
    }


    VkResult r;
    void*    mappedData;
    VkImage  image;


    // 1. Allocate and fill buffer

    VkBufferCreateInfo stagingInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size  = dataSize,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    };

    VkBuffer stagingBuffer =
        memAllocator->CreateStagingSrcTextureBuffer( &stagingInfo, info.pDebugName, &mappedData );
    if( stagingBuffer == VK_NULL_HANDLE )
    {
        return {};
    }
    SET_DEBUG_NAME( device, stagingBuffer, VK_OBJECT_TYPE_BUFFER, info.pDebugName );


    bool wasCreated = CreateImage( info, &image );
    if( !wasCreated )
    {
        // clean created resources
        memAllocator->DestroyStagingSrcTextureBuffer( stagingBuffer );
        return {};
    }


    // if it's updateable but the data is not provided yet
    if( info.isUpdateable && data == nullptr )
    {
        // create image without copying
        PrepareImage( image, VK_NULL_HANDLE, info, ImagePrepareType::INIT_WITHOUT_COPYING );
    }
    else
    {
        // copy image data to buffer
        memcpy( mappedData, data, dataSize );

        // and copy it to image
        PrepareImage( image, &stagingBuffer, info, ImagePrepareType::INIT );
    }


    // create image view
    VkImageView imageView = CreateImageView(
        image, info.format, info.isCubemap, GetMipmapCount( size, info ), info.swizzling );
    SET_DEBUG_NAME( device, imageView, VK_OBJECT_TYPE_IMAGE_VIEW, info.pDebugName );


    // save info about created image
    if( info.isUpdateable )
    {
        // for updateable images: save pointer for updating the image data in the future

        updateableImageInfos[ image ] = UpdateableImageInfo{
            .stagingBuffer   = stagingBuffer,
            .mappedData      = mappedData,
            .dataSize        = static_cast< uint32_t >( dataSize ),
            .imageSize       = size,
            .generateMipmaps = info.useMipmaps,
            .format          = info.format,
        };
    }
    else
    {
        // for static images that won't be updated:
        // push staging buffer to be deleted when it won't be in use
        stagingToFree[ info.frameIndex ].push_back( stagingBuffer );
    }


    return UploadResult{
        .wasUploaded = true,
        .image       = image,
        .view        = imageView,
    };
}

void TextureUploader::UpdateImage( VkCommandBuffer cmd, VkImage targetImage, const void* data )
{
    assert( targetImage != VK_NULL_HANDLE );
    assert( data != nullptr );

    auto it = updateableImageInfos.find( targetImage );

    if( it != updateableImageInfos.end() )
    {
        auto& updateInfo = it->second;

        assert( updateInfo.mappedData != nullptr );
        memcpy( updateInfo.mappedData, data, updateInfo.dataSize );

        UploadInfo info = {};
        info.cmd        = cmd;
        info.baseSize   = updateInfo.imageSize;
        info.useMipmaps = updateInfo.generateMipmaps;
        info.format     = updateInfo.format;

        // copy from staging
        PrepareImage( targetImage, &updateInfo.stagingBuffer, info, ImagePrepareType::UPDATE );
    }
}

void TextureUploader::DestroyImage( VkImage image, VkImageView view )
{
    auto it = updateableImageInfos.find( image );

    // if it's updateable
    if( it != updateableImageInfos.end() )
    {
        // destroy its staging buffer, as it exists during
        // the overall lifetime of an updateable image
        memAllocator->DestroyStagingSrcTextureBuffer( it->second.stagingBuffer );

        updateableImageInfos.erase( it );
    }

    memAllocator->DestroyTextureImage( image );
    vkDestroyImageView( device, view, nullptr );
}
