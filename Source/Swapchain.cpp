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

#include "Swapchain.h"

#include <algorithm>
#include <utility>

#include "DLSS3_DX12.h"
#include "DX12_CopyFramebuf.h"
#include "FSR3_DX12.h"
#include "HDR_Platform.h"
#include "LibraryConfig.h"
#include "RgException.h"
#include "DX12_Interop.h"
#include "Utils.h"

template<>
struct std::formatter< VkFormat >
{
    constexpr auto parse( std::format_parse_context& ctx ) { return ctx.begin(); }
    auto           format( const VkFormat& f, std::format_context& ctx ) const
    {
        return std::format_to( ctx.out(), "{}", std::underlying_type_t< VkFormat >{ f } );
    }
};

template<>
struct std::formatter< VkColorSpaceKHR >
{
    constexpr auto parse( std::format_parse_context& ctx ) { return ctx.begin(); }
    auto           format( const VkColorSpaceKHR& s, std::format_context& ctx ) const
    {
        return std::format_to( ctx.out(), "{}", std::underlying_type_t< VkColorSpaceKHR >{ s } );
    }
};

namespace
{
std::string JoinAsString( std::span< VkSurfaceFormatKHR > fs )
{
    auto s = std::string{};
    for( auto& f : fs )
    {
        s += std::format( "\n  format={}, colorSpace={}", f.format, f.colorSpace );
    }
    return s;
}

std::string JoinAsString( std::span< VkPresentModeKHR > fs )
{
    auto s = std::string{};
    for( auto& f : fs )
    {
        switch( f )
        {
            // clang-format off
            case VK_PRESENT_MODE_IMMEDIATE_KHR: s += "VK_PRESENT_MODE_IMMEDIATE_KHR";
            case VK_PRESENT_MODE_MAILBOX_KHR: s += "VK_PRESENT_MODE_MAILBOX_KHR";
            case VK_PRESENT_MODE_FIFO_KHR: s += "VK_PRESENT_MODE_FIFO_KHR";
            case VK_PRESENT_MODE_FIFO_RELAXED_KHR: s += "VK_PRESENT_MODE_FIFO_RELAXED_KHR";
            case VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR: s += "VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR";
            case VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR: s += "VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR";
            case VK_PRESENT_MODE_MAX_ENUM_KHR:
            // clang-format on
            default: s += std::to_string( f );
        }
        s += ' ';
    }
    return s;
}

bool IsNullExtent( const VkExtent2D& a )
{
    return a.width == 0 || a.height == 0;
}

auto ExtentAsOffset( const VkExtent2D& a ) -> VkOffset3D
{
    assert( a.width > 0 && a.height > 0 );
    return VkOffset3D{
        .x = static_cast< int >( a.width ),
        .y = static_cast< int >( a.height ),
        .z = 1,
    };
}

bool ExactlyOne( const auto&... bs )
{
    return ( ... ^ bool( bs ) );
}

bool operator==( const VkExtent2D& a, const VkExtent2D& b )
{
    return a.width == b.width && a.height == b.height;
}

bool operator!=( const VkExtent2D& a, const VkExtent2D& b )
{
    return !( a == b );
}

auto FindLDR( std::span< VkSurfaceFormatKHR > supported ) -> std::optional< VkSurfaceFormatKHR >
{
    VkFormat prioritized[] = {
        VK_FORMAT_R8G8B8A8_SRGB,
        VK_FORMAT_B8G8R8A8_SRGB,
    };
    for( VkFormat p : prioritized )
    {
        auto f =
            std::ranges::find_if( supported, //
                                  [ &p ]( const VkSurfaceFormatKHR& s ) { return p == s.format; } );
        if( f != supported.end() )
        {
            return std::optional{ *f };
        }
    }
    return std::nullopt;
}

auto FindHDR( std::span< VkSurfaceFormatKHR > supported ) -> std::optional< VkSurfaceFormatKHR >
{
    if( !RTGL1::HDR::IsSupported( 0 ) )
    {
        return std::nullopt;
    }

    VkSurfaceFormatKHR prioritized[] = {
        {
            .format     = VK_FORMAT_R16G16B16A16_SFLOAT,
            .colorSpace = VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT,
        },
        {
            .format     = VK_FORMAT_A2R10G10B10_UNORM_PACK32,
            .colorSpace = VK_COLOR_SPACE_HDR10_ST2084_EXT,
        },
        {
            .format     = VK_FORMAT_A2B10G10R10_UNORM_PACK32,
            .colorSpace = VK_COLOR_SPACE_HDR10_ST2084_EXT,
        },
    };
    for( const VkSurfaceFormatKHR& p : prioritized )
    {
        auto f =
            std::ranges::find_if( supported, //
                                  [ &p ]( const VkSurfaceFormatKHR& s ) {
                                      return p.format == s.format && p.colorSpace == s.colorSpace;
                                  } );
        if( f != supported.end() )
        {
            return std::optional{ *f };
        }
    }
    return std::nullopt;
}

auto g_hdrStateOnStartup = RTGL1::HDR::DisplayHDRState::Undefined;
void BakeStartupHDRState()
{
    g_hdrStateOnStartup = RTGL1::HDR::GetState( 0 );
}
void TryRevertHDRStateToStartup()
{
    using namespace RTGL1;

    if( g_hdrStateOnStartup == HDR::DisplayHDRState::Disabled &&
        HDR::GetState( 0 ) == HDR::DisplayHDRState::Enabled )
    {
        HDR::SetEnabled( 0, false );
    }
    else if( g_hdrStateOnStartup == HDR::DisplayHDRState::Enabled &&
             HDR::GetState( 0 ) == HDR::DisplayHDRState::Disabled )
    {
        HDR::SetEnabled( 0, true );
    }
}

}

auto RTGL1::FindPresentModes( VkPhysicalDevice physDevice, VkSurfaceKHR surface ) -> PresentModes
{
    auto result = PresentModes{};

    auto supported = std::vector< VkPresentModeKHR >{};
    {
        VkResult r{};
        uint32_t presentModeCount{ 0 };
        r = vkGetPhysicalDeviceSurfacePresentModesKHR(
            physDevice, surface, &presentModeCount, nullptr );
        VK_CHECKERROR( r );
        supported.resize( presentModeCount );
        r = vkGetPhysicalDeviceSurfacePresentModesKHR(
            physDevice, surface, &presentModeCount, supported.data() );
        VK_CHECKERROR( r );
    }

    bool a = false;
    bool b = false;

    // try to find mailbox / fifo-relaxed
    for( auto p : supported )
    {
        if( p == VK_PRESENT_MODE_IMMEDIATE_KHR )
        {
            result.immediate = p;
            a                = true;
        }
        if( p == VK_PRESENT_MODE_FIFO_RELAXED_KHR )
        {
            result.vsync = p;
            b            = true;
        }
    }

    if( !a )
    {
        debug::Error( "Can't find VK_PRESENT_MODE_IMMEDIATE_KHR. Supported: {}",
                      JoinAsString( supported ) );
    }
    if( !b )
    {
        debug::Error( "Can't find VK_PRESENT_MODE_FIFO_RELAXED_KHR. Supported: {}",
                      JoinAsString( supported ) );
    }

    return result;
}

auto RTGL1::FindLdrAndHdrSurfaceFormats( VkPhysicalDevice physDevice,
                                         VkSurfaceKHR     surface,
                                         bool             printReport ) -> SurfaceFormats
{
    auto result = SurfaceFormats{};

    auto supported = std::vector< VkSurfaceFormatKHR >{};
    {
        VkResult r{};
        uint32_t formatCount{ 0 };
        r = vkGetPhysicalDeviceSurfaceFormatsKHR( physDevice, surface, &formatCount, nullptr );
        VK_CHECKERROR( r );
        supported.resize( formatCount );
        r = vkGetPhysicalDeviceSurfaceFormatsKHR(
            physDevice, surface, &formatCount, supported.data() );
        VK_CHECKERROR( r );
        if( printReport )
        {
            debug::Verbose( "Supported surface formats:{}", JoinAsString( supported ) );
        }
    }

    if( auto s = FindLDR( supported ) )
    {
        result.ldr = *s;
        if( printReport )
        {
            debug::Verbose( "Found LDR: format={}, colorSpace={}", s->format, s->colorSpace );
        }
    }
    else
    {
        throw RgException{ RG_RESULT_GRAPHICS_API_ERROR, "No supported LDR surface format" };
    }

    if( auto h = FindHDR( supported ) )
    {
        result.hdr = *h;
        if( printReport )
        {
            debug::Verbose( "Found HDR: format={}, colorSpace={}", h->format, h->colorSpace );
        }
    }

    return result;
}

bool RTGL1::IsExtentOptimal( VkPhysicalDevice physDevice, VkSurfaceKHR surface )
{
    auto surfCapabilities = VkSurfaceCapabilitiesKHR{};

    VkResult r =
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR( physDevice, surface, &surfCapabilities );
    if( r == VK_ERROR_SURFACE_LOST_KHR )
    {
        return false;
    }

    VK_CHECKERROR( r );

    return !IsNullExtent( surfCapabilities.maxImageExtent ) &&
           !IsNullExtent( surfCapabilities.currentExtent );
}

auto RTGL1::CalculateOptimalExtent( VkPhysicalDevice physDevice, VkSurfaceKHR surface )
    -> VkExtent2D
{
    auto surfCapabilities = VkSurfaceCapabilitiesKHR{};

    VkResult r = vkGetPhysicalDeviceSurfaceCapabilitiesKHR( physDevice, //
                                                            surface,
                                                            &surfCapabilities );
    VK_CHECKERROR( r );

    if( IsNullExtent( surfCapabilities.maxImageExtent ) ||
        IsNullExtent( surfCapabilities.currentExtent ) )
    {
        return { 0, 0 };
    }

    if( surfCapabilities.currentExtent.width == UINT32_MAX ||
        surfCapabilities.currentExtent.height == UINT32_MAX )
    {
        return surfCapabilities.maxImageExtent;
    }

    return surfCapabilities.currentExtent;
}

constexpr uint32_t DefaultSwapchainImageCount = 3U;

uint32_t RTGL1::CheckAndCalcImageCount( VkSurfaceKHR      surface,
                                        VkPhysicalDevice  physDevice,
                                        const VkExtent2D& suggested )
{
    auto surfCapabilities = VkSurfaceCapabilitiesKHR{};
    {
        VkResult r =
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR( physDevice, surface, &surfCapabilities );
        VK_CHECKERROR( r );
    }

    if( surfCapabilities.currentExtent.width != UINT32_MAX &&
        surfCapabilities.currentExtent.height != UINT32_MAX )
    {
        assert( suggested == surfCapabilities.currentExtent );
    }
    else
    {
        assert( surfCapabilities.minImageExtent.width <= suggested.width &&
                suggested.width <= surfCapabilities.maxImageExtent.width );
        assert( surfCapabilities.minImageExtent.height <= suggested.height &&
                suggested.height <= surfCapabilities.maxImageExtent.height );
    }

    if( surfCapabilities.maxImageCount > 0 )
    {
        return std::clamp( DefaultSwapchainImageCount, //
                           surfCapabilities.minImageCount,
                           surfCapabilities.maxImageCount );
    }
    return std::max( DefaultSwapchainImageCount, surfCapabilities.minImageCount );
}

RTGL1::Swapchain::Swapchain( VkDevice                                _device,
                             VkSurfaceKHR                            _surface,
                             VkPhysicalDevice                        _physDevice,
                             std::shared_ptr< CommandBufferManager > _cmdManager,
                             std::shared_ptr< MemoryAllocator >      _allocator,
                             std::shared_ptr< Framebuffers >         framebuffers,
                             std::shared_ptr< DLSS3_DX12 >&          dlss3,
                             std::shared_ptr< FSR3_DX12 >&           fsr3,
                             std::optional< uint64_t >               gpuLuid )
    : device{ _device }
    , surface{ _surface }
    , physDevice{ _physDevice }
    , cmdManager{ std::move( _cmdManager ) }
    , allocator{ std::move( _allocator ) }
    , m_surfaceFormat{ FindLdrAndHdrSurfaceFormats( _physDevice, _surface, true ) }
    , m_presentMode{ FindPresentModes( _physDevice, _surface ) }
    , m_dlss3{ dlss3 }
    , m_fsr3{ fsr3 }
    , m_framebuffers{ std::move( framebuffers ) }
    , m_gpuLuid{ gpuLuid }
{
    BakeStartupHDRState();

    // SHIPPING_HACK begin - precheck DLSS3, so it doesn't fail during the game
    if( gpuLuid )
    {
        auto inst = DLSS3_DX12::MakeInstance( *gpuLuid, true );
        if( !inst )
        {
            m_failed[ SWAPCHAIN_TYPE_FRAME_GENERATION_DLSS3 ] =
                inst.error() ? inst.error() : "Generic initialization failure ()";
        }
        assert( inst.value_or( nullptr ) == nullptr /* MakeInstance must return null */ );
    }
    // SHIPPING_HACK end
}

void RTGL1::Swapchain::AcquireImage( bool          vsync,
                                     bool          hdr,
                                     SwapchainType type,
                                     VkSemaphore   imageAvailableSemaphore )
{
    TryRecreate( CalculateOptimalExtent( physDevice, surface ), //
                 vsync,
                 hdr,
                 type );

    if( !Valid() )
    {
        return;
    }

    if( m_type == SWAPCHAIN_TYPE_DXGI || //
        m_type == SWAPCHAIN_TYPE_FRAME_GENERATION_DLSS3 ||
        m_type == SWAPCHAIN_TYPE_FRAME_GENERATION_FSR3 )
    {
        uint32_t c = dxgi::GetCurrentBackBufferIndex();
        assert( c < swapchainImages.size() );

        currentSwapchainIndex = ( c ) % static_cast< uint32_t >( swapchainImages.size() );
    }
    else if( m_type == SWAPCHAIN_TYPE_VULKAN_NATIVE )
    {
        assert( imageAvailableSemaphore );
        while( true )
        {
            VkResult r = vkAcquireNextImageKHR( device,
                                                swapchain,
                                                UINT64_MAX,
                                                imageAvailableSemaphore,
                                                VK_NULL_HANDLE,
                                                &currentSwapchainIndex );

            if( r == VK_SUCCESS )
            {
                break;
            }

            if( r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR )
            {
                TryRecreate( CalculateOptimalExtent( physDevice, surface ), //
                             m_vsync,
                             isHDR,
                             m_type );
            }
            else
            {
                assert( 0 );
            }
        }
    }
    else
    {
        assert( 0 );
    }
}

void RTGL1::Swapchain::BlitForPresent( VkCommandBuffer   cmd,
                                       VkImage           srcImage,
                                       const VkExtent2D& srcSize,
                                       VkFilter          filter,
                                       VkImageLayout     srcImageLayout )
{
    if( !Valid() )
    {
        return;
    }

    if( WithDXGI() )
    {
        debug::Error( "Swapchain::BlitForPresent must not be used with DXGI-based swapchains, "
                      "as there are no VkImage-s in such swapchain" );
        return;
    }

    // if source has almost the same size as the surface, then use nearest blit
    if( std::abs( int( srcSize.width ) - int( surfaceExtent.width ) ) < 8 &&
        std::abs( int( srcSize.height ) - int( surfaceExtent.height ) ) < 8 )
    {
        filter = VK_FILTER_NEAREST;
    }

    constexpr auto subres = VkImageSubresourceLayers{
        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
        .mipLevel       = 0,
        .baseArrayLayer = 0,
        .layerCount     = 1,
    };
    constexpr auto subresRange = VkImageSubresourceRange{
        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel   = 0,
        .levelCount     = 1,
        .baseArrayLayer = 0,
        .layerCount     = 1,
    };

    auto region = VkImageBlit{
        .srcSubresource = subres,
        .srcOffsets     = { {}, ExtentAsOffset( srcSize ) },
        .dstSubresource = subres,
        .dstOffsets     = { {}, ExtentAsOffset( surfaceExtent ) },
    };

    VkImage       swapchainImage       = swapchainImages[ currentSwapchainIndex ];
    VkImageLayout swapchainImageLayout = m_type == SWAPCHAIN_TYPE_VULKAN_NATIVE
                                             ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                                             : VK_IMAGE_LAYOUT_GENERAL;

    {
        VkImageMemoryBarrier2 bs[] = {
            {
                .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .pNext               = nullptr,
                .srcStageMask        = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT,
                .dstStageMask        = VK_PIPELINE_STAGE_2_BLIT_BIT,
                .dstAccessMask       = VK_ACCESS_2_TRANSFER_READ_BIT,
                .oldLayout           = srcImageLayout,
                .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image               = srcImage,
                .subresourceRange    = subresRange,
            },
            {
                .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .pNext               = nullptr,
                .srcStageMask        = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                .srcAccessMask       = VK_ACCESS_2_NONE,
                .dstStageMask        = VK_PIPELINE_STAGE_2_BLIT_BIT,
                .dstAccessMask       = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .oldLayout           = swapchainImageLayout,
                .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image               = swapchainImage,
                .subresourceRange    = subresRange,
            },
        };

        auto dep = VkDependencyInfo{
            .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext                    = nullptr,
            .dependencyFlags          = 0,
            .memoryBarrierCount       = 0,
            .pMemoryBarriers          = nullptr,
            .bufferMemoryBarrierCount = 0,
            .pBufferMemoryBarriers    = nullptr,
            .imageMemoryBarrierCount  = std::size( bs ),
            .pImageMemoryBarriers     = bs,
        };

        svkCmdPipelineBarrier2KHR( cmd, &dep );
    }

    vkCmdBlitImage( cmd,
                    srcImage,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    swapchainImage,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    1,
                    &region,
                    filter );

    {
        VkImageMemoryBarrier2 bs[] = {
            {
                .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .pNext               = nullptr,
                .srcStageMask        = VK_PIPELINE_STAGE_2_BLIT_BIT,
                .srcAccessMask       = VK_ACCESS_2_TRANSFER_READ_BIT,
                .dstStageMask        = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
                .dstAccessMask       = VK_ACCESS_2_NONE,
                .oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .newLayout           = srcImageLayout,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image               = srcImage,
                .subresourceRange    = subresRange,
            },
            {
                .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .pNext               = nullptr,
                .srcStageMask        = VK_PIPELINE_STAGE_2_BLIT_BIT,
                .srcAccessMask       = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .dstStageMask        = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
                .dstAccessMask       = VK_ACCESS_2_NONE,
                .oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .newLayout           = swapchainImageLayout,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image               = swapchainImage,
                .subresourceRange    = subresRange,
            },
        };

        auto dep = VkDependencyInfo{
            .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext                    = nullptr,
            .dependencyFlags          = 0,
            .memoryBarrierCount       = 0,
            .pMemoryBarriers          = nullptr,
            .bufferMemoryBarrierCount = 0,
            .pBufferMemoryBarriers    = nullptr,
            .imageMemoryBarrierCount  = std::size( bs ),
            .pImageMemoryBarriers     = bs,
        };

        svkCmdPipelineBarrier2KHR( cmd, &dep );
    }
}

void RTGL1::Swapchain::OnQueuePresent( VkResult queuePresentResult )
{
    assert( m_type == SWAPCHAIN_TYPE_VULKAN_NATIVE );

    if( queuePresentResult == VK_ERROR_OUT_OF_DATE_KHR || queuePresentResult == VK_SUBOPTIMAL_KHR )
    {
        TryRecreate( CalculateOptimalExtent( physDevice, surface ), //
                     m_vsync,
                     isHDR,
                     m_type );
    }
}

bool RTGL1::Swapchain::Valid() const
{
    return m_type != SWAPCHAIN_TYPE_NONE && !IsNullExtent( surfaceExtent ) &&
           !swapchainImages.empty();
}

bool RTGL1::Swapchain::TryRecreate( const VkExtent2D& newExtent,
                                    bool              vsync,
                                    bool              hdr,
                                    SwapchainType     type )
{
    // sanitize
    {
        assert( type != SWAPCHAIN_TYPE_NONE );
        if( IsNullExtent( newExtent ) )
        {
            type = SWAPCHAIN_TYPE_NONE;
        }

        if( type == SWAPCHAIN_TYPE_DXGI ||                   //
            type == SWAPCHAIN_TYPE_FRAME_GENERATION_DLSS3 || //
            type == SWAPCHAIN_TYPE_FRAME_GENERATION_FSR3 )
        {
            if( m_failed[ type ] || !dxgi::DX12Supported() || !m_gpuLuid )
            {
                type = SWAPCHAIN_TYPE_VULKAN_NATIVE;
            }
        }

        if( type == SWAPCHAIN_TYPE_FRAME_GENERATION_DLSS3 )
        {
            vsync = false;
        }

        if( hdr && !SupportsHDR() )
        {
            hdr = false;
        }
    }

    if( surfaceExtent == newExtent && m_vsync == vsync && isHDR == hdr && m_type == type )
    {
        return false;
    }

    vkDeviceWaitIdle( device );
    dxgi::WaitIdle();

    VkSwapchainKHR old = DestroyWithoutSwapchain();
    Create( newExtent, vsync, hdr, type, old );

    vkDeviceWaitIdle( device );
    dxgi::WaitIdle();

    return true;
}

void RTGL1::Swapchain::Create( const VkExtent2D& size, //
                               bool              vsync,
                               bool              hdr,
                               SwapchainType     type,
                               VkSwapchainKHR    oldSwapchain )
{
    assert( swapchain == VK_NULL_HANDLE );
    assert( swapchainImages.empty() );
    assert( swapchainMemory.empty() );

    auto l_safeNewType = [ this, &oldSwapchain ]( SwapchainType type, //
                                                  bool          isSwitchingHdr ) -> SwapchainType {
        if( type != SWAPCHAIN_TYPE_DXGI &&                   //
            type != SWAPCHAIN_TYPE_FRAME_GENERATION_DLSS3 && //
            type != SWAPCHAIN_TYPE_FRAME_GENERATION_FSR3 )
        {
            return type;
        }

        if( isSwitchingHdr )
        {
            debug::Info( "HDR is being switched, suppressing frame generation" );
            type = SWAPCHAIN_TYPE_DXGI;
        }

        if( m_failed[ type ] || !dxgi::DX12Supported() || !m_gpuLuid )
        {
            assert( 0 ); // should be sanitized before Create()
            return SWAPCHAIN_TYPE_VULKAN_NATIVE;
        }

        // vkCreateSwapchainKHR won't be called, destroy manually
        if( oldSwapchain )
        {
            vkDestroySwapchainKHR( device, oldSwapchain, nullptr );
            oldSwapchain = nullptr;
        }

        // if corresponding handler already exists,
        // recreate only swapchain, but not the handler itself
        if( this->m_type == type )
        {
            if( ( m_type == SWAPCHAIN_TYPE_FRAME_GENERATION_DLSS3 && m_dlss3 ) ||
                ( m_type == SWAPCHAIN_TYPE_FRAME_GENERATION_FSR3 && m_fsr3 ) ||
                ( m_type == SWAPCHAIN_TYPE_DXGI && dxgi::HasRawDXGI() ) )
            {
                return type;
            }
        }

        // clean up previous
        {
            if( this->m_fsr3 )
            {
                m_framebuffers->Unsubscribe( this->m_fsr3.get() );
            }
            this->m_fsr3  = {};
            this->m_dlss3 = {};
            dxgi::Destroy();
        }

        auto reason = "Generic initialization failure";

        if( type == SWAPCHAIN_TYPE_DXGI )
        {
            auto inst = dxgi::InitAsRawDXGI( *m_gpuLuid );
            if( inst )
            {
                return SWAPCHAIN_TYPE_DXGI;
            }
            reason = inst.error();
        }
        else if( type == SWAPCHAIN_TYPE_FRAME_GENERATION_DLSS3 )
        {
            auto inst = DLSS3_DX12::MakeInstance( *m_gpuLuid );
            if( inst )
            {
                this->m_dlss3 = std::shared_ptr< DLSS3_DX12 >{ inst.value() };
                return SWAPCHAIN_TYPE_FRAME_GENERATION_DLSS3;
            }
            reason = inst.error();
        }
        else if( type == SWAPCHAIN_TYPE_FRAME_GENERATION_FSR3 )
        {
            auto inst = FSR3_DX12::MakeInstance( *m_gpuLuid );
            if( inst )
            {
                this->m_fsr3 = std::shared_ptr< FSR3_DX12 >{ inst.value() };
                m_framebuffers->Subscribe( this->m_fsr3 );

                return SWAPCHAIN_TYPE_FRAME_GENERATION_FSR3;
            }
            reason = inst.error();
        }
        else
        {
            assert( 0 );
        }
        assert( reason );

        // there was a failure, never try again this type
        m_failed[ type ] = std::string{ reason ? reason : "<empty>" };
        {
            if( this->m_fsr3 )
            {
                m_framebuffers->Unsubscribe( this->m_fsr3.get() );
            }
            this->m_fsr3  = {};
            this->m_dlss3 = {};
            dxgi::Destroy();
        }
        return SWAPCHAIN_TYPE_VULKAN_NATIVE;
    };

    auto l_safeHdr = [ this ]( bool hdr ) -> bool {
        if( hdr && !SupportsHDR() )
        {
            assert( 0 ); // should be sanitized before Create()
            return false;
        }

        // enforce platform-specific HDR to be enabled;
        // on Windows, if 'Use HDR' is not enabled in the settings,
        // and then if to specify HDR format for a swapchain, Windows
        // will try automatically enable 'Use HDR', however the colors will be skewed
        if( hdr )
        {
            // if disabled => enable
            if( HDR::GetState( 0 ) == HDR::DisplayHDRState::Disabled )
            {
                HDR::SetEnabled( 0, true );
                cmdManager->WaitDeviceIdle();
            }
        }
        else
        {
            // if enabled => disable
            if( HDR::GetState( 0 ) == HDR::DisplayHDRState::Enabled )
            {
                HDR::SetEnabled( 0, false );
                cmdManager->WaitDeviceIdle();
            }
        }
        return hdr;
    };

    {
        const SwapchainType prev = m_type;

        this->surfaceExtent = size;
        this->m_type        = l_safeNewType( type, this->isHDR != hdr );
        this->m_vsync       = vsync;
        this->isHDR         = l_safeHdr( hdr );

        if( LibConfig().dxgiToVkSwapchainSwitchHack )
        {
            // SHIPPING_HACK begin -- when switching HWND from DXGI to Vulkan swapchain,
            // there's a chance that Vulkan one would not update the window contents (if windowed),
            // but everything (vkQueuePresentKHR etc) would succeed.
            // Somehow, forcing VSync (after DXGI-Vk switch) for one frame makes
            // the window contents to be updated correctly
            if( prev == SWAPCHAIN_TYPE_DXGI ||                   //
                prev == SWAPCHAIN_TYPE_FRAME_GENERATION_DLSS3 || //
                prev == SWAPCHAIN_TYPE_FRAME_GENERATION_FSR3 )
            {
                if( m_type == SWAPCHAIN_TYPE_VULKAN_NATIVE )
                {
                    m_vsync = true;
                }
            }
            // SHIPPING_HACK end
        }
    }

    if( m_type == SWAPCHAIN_TYPE_NONE || m_type == SWAPCHAIN_TYPE_VULKAN_NATIVE )
    {
        if( this->m_fsr3 )
        {
            m_framebuffers->Unsubscribe( this->m_fsr3.get() );
        }
        this->m_fsr3  = {};
        this->m_dlss3 = {};
    }
    if( m_type == SWAPCHAIN_TYPE_NONE )
    {
        assert( IsNullExtent( surfaceExtent ) );
        if( oldSwapchain )
        {
            vkDestroySwapchainKHR( device, oldSwapchain, nullptr );
            oldSwapchain = nullptr;
        }
        return;
    }

    const VkImageLayout targetLayout =
        ( m_type == SWAPCHAIN_TYPE_VULKAN_NATIVE ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                                                 : VK_IMAGE_LAYOUT_GENERAL );

    const VkSurfaceFormatKHR surfaceFormat = isHDR ? *m_surfaceFormat.hdr //
                                                   : m_surfaceFormat.ldr;

    VkResult r{};

    if( m_type == SWAPCHAIN_TYPE_DXGI ||                   //
        m_type == SWAPCHAIN_TYPE_FRAME_GENERATION_DLSS3 || //
        m_type == SWAPCHAIN_TYPE_FRAME_GENERATION_FSR3 )
    {
        assert( ExactlyOne( m_fsr3, m_dlss3, dxgi::HasRawDXGI() ) );

        const uint32_t imageCount = dxgi::CreateSwapchain( surfaceExtent.width, //
                                                           surfaceExtent.height,
                                                           DefaultSwapchainImageCount,
                                                           surfaceFormat.format,
                                                           surfaceFormat.colorSpace,
                                                           m_vsync );
        swapchainMemory.resize( imageCount );
        swapchainImages.resize( imageCount );
    }
    else
    {
        if( this->m_fsr3 )
        {
            m_framebuffers->Unsubscribe( this->m_fsr3.get() );
        }
        this->m_fsr3  = {};
        this->m_dlss3 = {};

        auto swapchainInfo = VkSwapchainCreateInfoKHR{
            .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .surface          = surface,
            .minImageCount    = CheckAndCalcImageCount( surface, physDevice, surfaceExtent ),
            .imageFormat      = surfaceFormat.format,
            .imageColorSpace  = surfaceFormat.colorSpace,
            .imageExtent      = surfaceExtent,
            .imageArrayLayers = 1,
            .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                          VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                          ( isHDR ? VK_IMAGE_USAGE_STORAGE_BIT : 0u ),
            .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .preTransform     = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
            .compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            .presentMode      = m_vsync ? m_presentMode.vsync : m_presentMode.immediate,
            .clipped          = VK_FALSE,
            .oldSwapchain     = oldSwapchain,
        };

        r = vkCreateSwapchainKHR( device, &swapchainInfo, nullptr, &swapchain );
        VK_CHECKERROR( r );
        if( oldSwapchain != VK_NULL_HANDLE )
        {
            vkDestroySwapchainKHR( device, oldSwapchain, nullptr );
        }

        uint32_t imageCount{ 0 };
        r = vkGetSwapchainImagesKHR( device, swapchain, &imageCount, nullptr );
        VK_CHECKERROR( r );

        swapchainImages.resize( imageCount );
        r = vkGetSwapchainImagesKHR( device, swapchain, &imageCount, swapchainImages.data() );
        VK_CHECKERROR( r );
    }

    VkCommandBuffer cmd = cmdManager->StartGraphicsCmd();
    for( VkImage img : swapchainImages )
    {
        if( img )
        {
            Utils::BarrierImage( cmd, //
                                 img,
                                 0,
                                 0,
                                 VK_IMAGE_LAYOUT_UNDEFINED,
                                 targetLayout );
        }
    }
    cmdManager->Submit( cmd );
    cmdManager->WaitGraphicsIdle();
}

VkSwapchainKHR RTGL1::Swapchain::DestroyWithoutSwapchain()
{
    vkDeviceWaitIdle( device );

    if( m_type == SWAPCHAIN_TYPE_DXGI || //
        m_type == SWAPCHAIN_TYPE_FRAME_GENERATION_DLSS3 ||
        m_type == SWAPCHAIN_TYPE_FRAME_GENERATION_FSR3 )
    {
        dxgi::DestroySwapchain();

        // free as they were allocated manually
        for( VkDeviceMemory m : swapchainMemory )
        {
            vkFreeMemory( device, m, nullptr );
        }
        for( VkImage i : swapchainImages )
        {
            vkDestroyImage( device, i, nullptr );
        }
    }
    else
    {
        assert( swapchainMemory.empty() );
    }

    swapchainImages.clear();
    swapchainMemory.clear();

    VkSwapchainKHR old = this->swapchain;

    this->swapchain = VK_NULL_HANDLE;

    return old;
}

RTGL1::Swapchain::~Swapchain()
{
    VkSwapchainKHR old = DestroyWithoutSwapchain();
    vkDestroySwapchainKHR( device, old, nullptr );

    // restore the state at the app start
    TryRevertHDRStateToStartup();
}

bool RTGL1::Swapchain::SupportsHDR() const
{
    return bool{ m_surfaceFormat.hdr };
}

bool RTGL1::Swapchain::IsHDREnabled() const
{
    if( isHDR )
    {
        assert( SupportsHDR() );
    }
    return isHDR;
}

bool RTGL1::Swapchain::IsST2084ColorSpace() const
{
    if( !IsHDREnabled() || !m_surfaceFormat.hdr )
    {
        assert( 0 );
        return false;
    }
    return m_surfaceFormat.hdr->colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT;
}

bool RTGL1::Swapchain::WithDXGI() const
{
    return m_type == SWAPCHAIN_TYPE_DXGI || //
           m_type == SWAPCHAIN_TYPE_FRAME_GENERATION_DLSS3 ||
           m_type == SWAPCHAIN_TYPE_FRAME_GENERATION_FSR3;
}

bool RTGL1::Swapchain::WithDLSS3FrameGeneration() const
{
    if( m_type == SWAPCHAIN_TYPE_FRAME_GENERATION_DLSS3 )
    {
        assert( m_dlss3 );
        assert( !m_fsr3 );
        return true;
    }
    return false;
}

bool RTGL1::Swapchain::WithFSR3FrameGeneration() const
{
    if( m_type == SWAPCHAIN_TYPE_FRAME_GENERATION_FSR3 )
    {
        assert( !m_dlss3 );
        assert( m_fsr3 );
        return true;
    }
    return false;
}

auto RTGL1::Swapchain::FailReason( SwapchainType t ) const -> const char*
{
    if( t == SWAPCHAIN_TYPE_DXGI ||                   //
        t == SWAPCHAIN_TYPE_FRAME_GENERATION_DLSS3 || //
        t == SWAPCHAIN_TYPE_FRAME_GENERATION_FSR3 )
    {
        if( !dxgi::DX12Supported() )
        {
            return "No DirectX 12 support";
        }
        if( !m_gpuLuid )
        {
            return "GPU failed to provide LUID";
        }
        if( m_failed[ t ] )
        {
            assert( !m_failed[ t ]->empty() );
            return m_failed[ t ]->c_str();
        }
        return nullptr;
    }
    assert( 0 );
    return nullptr;
}

uint32_t RTGL1::Swapchain::GetWidth() const
{
    return surfaceExtent.width;
}

uint32_t RTGL1::Swapchain::GetHeight() const
{
    return surfaceExtent.height;
}

uint32_t RTGL1::Swapchain::GetCurrentImageIndex() const
{
    return currentSwapchainIndex;
}

VkSwapchainKHR RTGL1::Swapchain::GetHandle() const
{
    assert( swapchain );
    return swapchain;
}

void RTGL1::Swapchain::MarkAsFailed( SwapchainType t )
{
    if( t == SWAPCHAIN_TYPE_DXGI ||                   //
        t == SWAPCHAIN_TYPE_FRAME_GENERATION_DLSS3 || //
        t == SWAPCHAIN_TYPE_FRAME_GENERATION_FSR3 )
    {
        m_failed[ t ] = std::string{ "Failed while trying to apply" };
    }
    else
    {
        assert( 0 );
    }
}
