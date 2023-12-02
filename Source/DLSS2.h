/*

Copyright (c) 2024 V.Shirokii
Copyright (c) 2021 Sultim Tsyrendashiev

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

#include <vector>

#include "Camera.h"
#include "Framebuffers.h"
#include "ResolutionState.h"

struct NVSDK_NGX_Parameter;
struct NVSDK_NGX_Handle;

namespace RTGL1
{


class RenderResolutionHelper;


class DLSS2
{
public:
    static auto MakeInstance( VkInstance       instance,
                              VkDevice         device,
                              VkPhysicalDevice physDevice,
                              const char*      pAppGuid ) -> std::shared_ptr< DLSS2 >;

    DLSS2( VkInstance       instance,
           VkDevice         device,
           VkPhysicalDevice physDevice,
           const char*      pAppGuid );
    ~DLSS2();

    DLSS2( const DLSS2& )                = delete;
    DLSS2( DLSS2&& ) noexcept            = delete;
    DLSS2& operator=( const DLSS2& )     = delete;
    DLSS2& operator=( DLSS2&& ) noexcept = delete;

    auto Apply( VkCommandBuffer               cmd,
                uint32_t                      frameIndex,
                Framebuffers&                 framebuffers,
                const RenderResolutionHelper& renderResolution,
                RgFloat2D                     jitterOffset,
                double                        timeDelta,
                bool                          resetAccumulation ) -> FramebufferImageIndex;

    auto GetOptimalSettings( uint32_t               userWidth,
                             uint32_t               userHeight,
                             RgRenderResolutionMode mode ) const -> std::pair< uint32_t, uint32_t >;

    static auto RequiredVulkanExtensions_Instance() -> std::optional< std::vector< const char* > >;
    static auto RequiredVulkanExtensions_Device( VkPhysicalDevice physDevice )
        -> std::optional< std::vector< const char* > >;

private:
    bool Valid() const;
    void Destroy();

private:
    VkDevice m_device{};
    
    bool                 m_initialized{ false };
    NVSDK_NGX_Parameter* m_params{ nullptr };

    NVSDK_NGX_Handle* m_feature{ nullptr };
    ResolutionState   m_prevResolution{};
};

}

inline auto RTGL1::DLSS2::MakeInstance( VkInstance       instance,
                                        VkDevice         device,
                                        VkPhysicalDevice physDevice,
                                        const char*      pAppGuid ) -> std::shared_ptr< DLSS2 >
{
    auto inst = std::make_shared< DLSS2 >( instance, device, physDevice, pAppGuid );
    if( !inst || !inst->Valid() )
    {
        return {};
    }
    return inst;
}
