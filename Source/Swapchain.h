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

#include "Common.h"
#include "CommandBufferManager.h"
#include "ISwapchainDependency.h"
#include "MemoryAllocator.h"

#include <expected>

namespace RTGL1
{


struct PresentModes
{
    VkPresentModeKHR vsync{ VK_PRESENT_MODE_FIFO_KHR };
    VkPresentModeKHR immediate{ VK_PRESENT_MODE_FIFO_KHR };
};
auto FindPresentModes( VkPhysicalDevice physDevice, VkSurfaceKHR surface ) -> PresentModes;


struct SurfaceFormats
{
    VkSurfaceFormatKHR                  ldr{};
    std::optional< VkSurfaceFormatKHR > hdr{};
};
auto FindLdrAndHdrSurfaceFormats( VkPhysicalDevice physDevice,
                                  VkSurfaceKHR     surface,
                                  bool             printReport = false ) -> SurfaceFormats;

bool IsExtentOptimal( VkPhysicalDevice physDevice, VkSurfaceKHR surface );
auto CalculateOptimalExtent( VkPhysicalDevice physDevice, VkSurfaceKHR surface ) -> VkExtent2D;
auto CheckAndCalcImageCount( VkSurfaceKHR      surface,
                             VkPhysicalDevice  physDevice,
                             const VkExtent2D& suggested ) -> uint32_t;


class FSR3_DX12;
class DLSS3_DX12;
class Framebuffers;


enum SwapchainType
{
    SWAPCHAIN_TYPE_NONE,
    SWAPCHAIN_TYPE_VULKAN_NATIVE,
    SWAPCHAIN_TYPE_DXGI,
    SWAPCHAIN_TYPE_FRAME_GENERATION_DLSS3,
    SWAPCHAIN_TYPE_FRAME_GENERATION_FSR3,
};
// clang-format off
template< typename T > constexpr size_t enum_size_t() = delete;
template<>             constexpr size_t enum_size_t< SwapchainType >() { return 5; }
template< typename T > constexpr size_t enum_size = enum_size_t< T >();
// clang-format on


class Swapchain
{
public:
    Swapchain( VkDevice                                device,
               VkSurfaceKHR                            surface,
               VkPhysicalDevice                        physDevice,
               std::shared_ptr< CommandBufferManager > cmdManager,
               std::shared_ptr< MemoryAllocator >      allocator,
               std::shared_ptr< Framebuffers >         framebuffers,
               std::shared_ptr< DLSS3_DX12 >&          dlss3,
               std::shared_ptr< FSR3_DX12 >&           fsr3,
               std::optional< uint64_t >               gpuLuid );
    ~Swapchain();

    Swapchain( const Swapchain& )                = delete;
    Swapchain( Swapchain&& ) noexcept            = delete;
    Swapchain& operator=( const Swapchain& )     = delete;
    Swapchain& operator=( Swapchain&& ) noexcept = delete;

    void AcquireImage( bool          vsync,
                       bool          hdr,
                       SwapchainType type,
                       VkSemaphore   imageAvailableSemaphore );
    void BlitForPresent( VkCommandBuffer   cmd,
                         VkImage           srcImage,
                         const VkExtent2D& srcSize,
                         VkFilter          filter,
                         VkImageLayout     srcImageLayout = VK_IMAGE_LAYOUT_GENERAL );
    void OnQueuePresent( VkResult queuePresentResult );

    bool Valid() const;

    bool SupportsHDR() const;
    bool IsHDREnabled() const;
    bool IsST2084ColorSpace() const;
    bool WithDXGI() const;
    bool WithDLSS3FrameGeneration() const;
    bool WithFSR3FrameGeneration() const;

    auto FailReason( SwapchainType t ) const -> const char*;

    auto GetWidth() const -> uint32_t;
    auto GetHeight() const -> uint32_t;
    auto GetCurrentImageIndex() const -> uint32_t;
    auto GetHandle() const -> VkSwapchainKHR;

    void MarkAsFailed( SwapchainType t );

private:
    // Safe to call even if swapchain wasn't created
    bool TryRecreate( const VkExtent2D& newExtent,
                      bool              vsync,
                      bool              hdr,
                      SwapchainType     type );

    void Create( const VkExtent2D& size,
                 bool              vsync,
                 bool              hdr,
                 SwapchainType     type,
                 VkSwapchainKHR    oldSwapchain = VK_NULL_HANDLE );

    auto DestroyWithoutSwapchain() -> VkSwapchainKHR;

private:
    VkDevice                                device{};
    VkSurfaceKHR                            surface{};
    VkPhysicalDevice                        physDevice{};
    std::shared_ptr< CommandBufferManager > cmdManager{};
    std::shared_ptr< MemoryAllocator >      allocator{};

    SurfaceFormats m_surfaceFormat{};
    PresentModes   m_presentMode{};

    VkExtent2D    surfaceExtent{ UINT32_MAX, UINT32_MAX };
    bool          m_vsync{ false };
    bool          isHDR{ false };
    SwapchainType m_type{ SWAPCHAIN_TYPE_NONE };

    VkSwapchainKHR                swapchain{};
    std::vector< VkImage >        swapchainImages{};
    std::vector< VkDeviceMemory > swapchainMemory{};

    uint32_t currentSwapchainIndex{ UINT32_MAX };

    std::shared_ptr< DLSS3_DX12 >& m_dlss3;
    std::shared_ptr< FSR3_DX12 >&  m_fsr3;

    std::shared_ptr< Framebuffers > m_framebuffers;
    
    std::optional< std::string > m_failed[ enum_size< SwapchainType > ]{};

    std::optional< uint64_t > m_gpuLuid{};
};

}
