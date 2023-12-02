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

#include "Camera.h"
#include "Framebuffers.h"

#include <expected>

struct ID3D12CommandList;
namespace sl
{
struct FrameToken;
}

namespace RTGL1
{


class RenderResolutionHelper;


class DLSS3_DX12
{
public:
    static void LoadSDK( const char* pAppGuid );
    static void UnloadSDK();

    static auto MakeInstance( uint64_t gpuLuid, bool justCheckCompatibility = false ) //
        ->                                                                            //
        std::expected< DLSS3_DX12*, const char* >;

private:
    DLSS3_DX12() = default;

public:
    ~DLSS3_DX12();

    DLSS3_DX12( const DLSS3_DX12& )                = delete;
    DLSS3_DX12( DLSS3_DX12&& ) noexcept            = delete;
    DLSS3_DX12& operator=( const DLSS3_DX12& )     = delete;
    DLSS3_DX12& operator=( DLSS3_DX12&& ) noexcept = delete;

    void CopyVkInputsToDX12( VkCommandBuffer        cmd,
                             uint32_t               frameIndex,
                             const Framebuffers&    framebuffers,
                             const ResolutionState& resolution );

    auto Apply( ID3D12CommandList*            dx12cmd,
                uint32_t                      frameIndex,
                Framebuffers&                 framebuffers,
                const RenderResolutionHelper& renderResolution,
                RgFloat2D                     jitterOffset,
                double                        timeDelta,
                bool                          resetAccumulation,
                const Camera&                 camera,
                uint32_t                      frameId,
                bool skipGeneratedFrame ) -> std::optional< FramebufferImageIndex >;

    void CopyDX12OutputToVk( VkCommandBuffer        cmd,
                             uint32_t               frameIndex,
                             const Framebuffers&    framebuffers,
                             const ResolutionState& resolution );

    auto GetOptimalSettings( uint32_t               userWidth,
                             uint32_t               userHeight,
                             RgRenderResolutionMode mode ) const -> std::pair< uint32_t, uint32_t >;

    void Reflex_SimStart( uint32_t frameId );
    void Reflex_SimEnd();
    void Reflex_RenderStart();
    void Reflex_RenderEnd();
    void Reflex_PresentStart();
    void Reflex_PresentEnd();

private:
    sl::FrameToken* m_frameToken{ nullptr };
};

}

