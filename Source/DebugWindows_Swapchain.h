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

#include "Swapchain.h"
#include "Utils.h"

namespace RTGL1
{

class DebugWindows_Swapchain
{
public:
    DebugWindows_Swapchain( VkDevice                                device,
                            VkSurfaceKHR                            surface,
                            VkPhysicalDevice                        physDevice,
                            std::shared_ptr< CommandBufferManager > cmdManager )
        : m_device{ device }
        , m_surface{ surface }
        , m_physDevice{ physDevice }
        , m_cmdManager{ std::move( cmdManager ) }
        , m_surfaceFormat{ FindLdrAndHdrSurfaceFormats( physDevice, surface ) }
        , m_presentMode{ FindPresentModes( physDevice, surface ) }
    {
    }
    ~DebugWindows_Swapchain()
    {
        VkSwapchainKHR s = DestroyWithoutSwapchain();
        vkDestroySwapchainKHR( m_device, s, nullptr );
    }

    DebugWindows_Swapchain( const DebugWindows_Swapchain& )                = delete;
    DebugWindows_Swapchain( DebugWindows_Swapchain&& ) noexcept            = delete;
    DebugWindows_Swapchain& operator=( const DebugWindows_Swapchain& )     = delete;
    DebugWindows_Swapchain& operator=( DebugWindows_Swapchain&& ) noexcept = delete;

    void AcquireImage( VkSemaphore imageAvailableSemaphore );
    void OnQueuePresent( VkResult queuePresentResult );

    auto GetSurfaceFormatLDR() const -> VkFormat;
    auto GetVkSwapchain() const -> VkSwapchainKHR;
    auto GetImageView( uint32_t index ) const -> VkImageView;
    auto GetImageCount() const -> uint32_t;
    auto GetExtent() const -> VkExtent2D;
    auto GetCurrentImageIndex() const -> uint32_t;

    void CallCreateSubscribers();
    void CallDestroySubscribers();
    void Subscribe( std::shared_ptr< ISwapchainDependency > subscriber );

private:
    bool TryRecreate( const VkExtent2D& newExtent );
    auto DestroyWithoutSwapchain() -> VkSwapchainKHR;
    void Create( const VkExtent2D& newExtent, VkSwapchainKHR oldSwapchain );

private:
    VkDevice                                m_device{};
    VkSurfaceKHR                            m_surface{};
    VkPhysicalDevice                        m_physDevice{};
    std::shared_ptr< CommandBufferManager > m_cmdManager{};
    SurfaceFormats                          m_surfaceFormat{};
    PresentModes                            m_presentMode{};
    VkSwapchainKHR                          m_swapchain{};
    std::vector< VkImage >                  m_swapchainImages{};
    std::vector< VkImageView >              m_swapchainViews{};
    VkExtent2D                              m_extent{ UINT32_MAX, UINT32_MAX };
    uint32_t                                m_currentSwapchainIndex{ UINT32_MAX };

    std::list< std::weak_ptr< ISwapchainDependency > > m_subscribers;

    constexpr static bool Vsync = false;
};



inline bool DebugWindows_Swapchain::TryRecreate( const VkExtent2D& newExtent )
{
    if( m_extent.width == newExtent.width && m_extent.height == newExtent.height )
    {
        return false;
    }

    m_cmdManager->WaitDeviceIdle();

    VkSwapchainKHR old = DestroyWithoutSwapchain();
    Create( newExtent, old );

    return true;
}

inline void DebugWindows_Swapchain::AcquireImage( VkSemaphore imageAvailableSemaphore )
{
    VkExtent2D optimal = CalculateOptimalExtent( m_physDevice, m_surface );

    // if requested params are different
    if( m_extent.width != optimal.width || m_extent.height != optimal.height )
    {
        TryRecreate( optimal );
    }

    assert( imageAvailableSemaphore );
    while( true )
    {
        VkResult r = vkAcquireNextImageKHR( m_device,
                                            m_swapchain,
                                            UINT64_MAX,
                                            imageAvailableSemaphore,
                                            VK_NULL_HANDLE,
                                            &m_currentSwapchainIndex );

        if( r == VK_SUCCESS )
        {
            break;
        }
        else if( r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR )
        {
            TryRecreate( optimal );
        }
        else
        {
            assert( 0 );
        }
    }
}

inline void DebugWindows_Swapchain::OnQueuePresent( VkResult queuePresentResult )
{
    if( queuePresentResult == VK_ERROR_OUT_OF_DATE_KHR || queuePresentResult == VK_SUBOPTIMAL_KHR )
    {
        TryRecreate( CalculateOptimalExtent( m_physDevice, m_surface ) );
    }
}

inline auto DebugWindows_Swapchain::DestroyWithoutSwapchain() -> VkSwapchainKHR
{
    vkDeviceWaitIdle( m_device );

    if( m_swapchain )
    {
        CallDestroySubscribers();
    }

    for( VkImageView v : m_swapchainViews )
    {
        vkDestroyImageView( m_device, v, nullptr );
    }
    m_swapchainViews.clear();
    m_swapchainImages.clear();

    VkSwapchainKHR old = m_swapchain;
    m_swapchain        = VK_NULL_HANDLE;
    return old;
}


inline void DebugWindows_Swapchain::Create( const VkExtent2D& newExtent,
                                            VkSwapchainKHR    oldSwapchain )
{
    VkResult r{};
    assert( m_swapchain == VK_NULL_HANDLE );
    assert( m_swapchainImages.empty() );
    assert( m_swapchainViews.empty() );

    this->m_extent = newExtent;
    
    uint32_t imageCount = CheckAndCalcImageCount( m_surface, m_physDevice, m_extent );

    auto swapchainInfo = VkSwapchainCreateInfoKHR{
        .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface          = m_surface,
        .minImageCount    = imageCount,
        .imageFormat      = m_surfaceFormat.ldr.format,
        .imageColorSpace  = m_surfaceFormat.ldr.colorSpace,
        .imageExtent      = m_extent,
        .imageArrayLayers = 1,
        .imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform     = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode      = Vsync ? m_presentMode.vsync : m_presentMode.immediate,
        .clipped          = VK_FALSE,
        .oldSwapchain     = oldSwapchain,
    };

    r = vkCreateSwapchainKHR( m_device, &swapchainInfo, nullptr, &m_swapchain );
    VK_CHECKERROR( r );
    if( oldSwapchain != VK_NULL_HANDLE )
    {
        vkDestroySwapchainKHR( m_device, oldSwapchain, nullptr );
    }
    r = vkGetSwapchainImagesKHR( m_device, m_swapchain, &imageCount, nullptr );
    VK_CHECKERROR( r );

    // VkImage
    m_swapchainImages.resize( imageCount );
    r = vkGetSwapchainImagesKHR( m_device, m_swapchain, &imageCount, m_swapchainImages.data() );
    VK_CHECKERROR( r );
    for( uint32_t i = 0; i < imageCount; i++ )
    {
        SET_DEBUG_NAME( m_device, //
                        m_swapchainImages[ i ],
                        VK_OBJECT_TYPE_IMAGE,
                        "Dev Swapchain image" );
    }

    // VkImageView
    m_swapchainViews.resize( imageCount );
    for( uint32_t i = 0; i < imageCount; i++ )
    {
        auto viewInfo = VkImageViewCreateInfo{
            .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image            = m_swapchainImages[ i ],
            .viewType         = VK_IMAGE_VIEW_TYPE_2D,
            .format           = m_surfaceFormat.ldr.format,
            .components       = {},
            .subresourceRange = { .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                                  .baseMipLevel   = 0,
                                  .levelCount     = 1,
                                  .baseArrayLayer = 0,
                                  .layerCount     = 1 },
        };

        r = vkCreateImageView( m_device, &viewInfo, nullptr, &m_swapchainViews[ i ] );
        VK_CHECKERROR( r );
        SET_DEBUG_NAME( m_device,
                        m_swapchainViews[ i ],
                        VK_OBJECT_TYPE_IMAGE_VIEW,
                        "Dev Swapchain image view" );
    }

    VkCommandBuffer cmd = m_cmdManager->StartGraphicsCmd();
    for( VkImage img : m_swapchainImages )
    {
        if( img )
        {
            Utils::BarrierImage( cmd, //
                                 img,
                                 0,
                                 0,
                                 VK_IMAGE_LAYOUT_UNDEFINED,
                                 VK_IMAGE_LAYOUT_PRESENT_SRC_KHR );
        }
    }
    m_cmdManager->Submit( cmd );
    m_cmdManager->WaitGraphicsIdle();

    CallCreateSubscribers();
}

inline auto DebugWindows_Swapchain::GetSurfaceFormatLDR() const -> VkFormat
{
    return m_surfaceFormat.ldr.format;
}

inline auto DebugWindows_Swapchain::GetVkSwapchain() const -> VkSwapchainKHR
{
    assert( m_swapchain );
    return m_swapchain;
}

inline auto DebugWindows_Swapchain::GetImageView( uint32_t index ) const -> VkImageView
{
    if( index >= m_swapchainViews.size() )
    {
        assert( 0 );
        return nullptr;
    }
    return m_swapchainViews[ index ];
}

inline auto DebugWindows_Swapchain::GetImageCount() const -> uint32_t
{
    return static_cast< uint32_t >( m_swapchainViews.size() );
}

inline auto DebugWindows_Swapchain::GetExtent() const -> VkExtent2D
{
    return m_extent;
}

inline auto DebugWindows_Swapchain::GetCurrentImageIndex() const -> uint32_t
{
    assert( m_currentSwapchainIndex < GetImageCount() );
    return m_currentSwapchainIndex;
}

inline void DebugWindows_Swapchain::CallCreateSubscribers()
{
    for( auto& ws : m_subscribers )
    {
        if( auto s = ws.lock() )
        {
            s->OnSwapchainCreate( this );
        }
    }
}

inline void DebugWindows_Swapchain::CallDestroySubscribers()
{
    for( auto& ws : m_subscribers )
    {
        if( auto s = ws.lock() )
        {
            s->OnSwapchainDestroy();
        }
    }
}

inline void DebugWindows_Swapchain::Subscribe( std::shared_ptr< ISwapchainDependency > subscriber )
{
    if( subscriber )
    {
        m_subscribers.emplace_back( subscriber );
    }
}
}
