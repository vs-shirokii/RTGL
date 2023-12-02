// Copyright (c) 2024 V.Shirokii
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

#include "Containers.h"
#include "Framebuffers.h"

#include <expected>

struct ID3D12CommandList;
struct FfxFsr3Context;
struct FfxFrameGenerationConfig;

struct NoOpDeleter
{
    void operator()( void* ) const noexcept {}
};

namespace RTGL1
{
class RenderResolutionHelper;

class FSR3_DX12 final : public IFramebuffersDependency
{
public:
    static void LoadSDK();
    static void UnloadSDK();

    static auto MakeInstance( uint64_t gpuLuid ) -> std::expected< FSR3_DX12*, const char* >;

private:
    explicit FSR3_DX12() = default;

public:
    ~FSR3_DX12() override;

    FSR3_DX12( const FSR3_DX12& )                = delete;
    FSR3_DX12( FSR3_DX12&& ) noexcept            = delete;
    FSR3_DX12& operator=( const FSR3_DX12& )     = delete;
    FSR3_DX12& operator=( FSR3_DX12&& ) noexcept = delete;

    void CopyVkInputsToDX12( VkCommandBuffer        cmd,
                             uint32_t               frameIndex,
                             const Framebuffers&    framebuffers,
                             const ResolutionState& resolution );

    auto Apply( ID3D12CommandList*            dx12cmd,
                uint32_t                      frameIndex,
                const Framebuffers&           framebuffers,
                const RenderResolutionHelper& renderResolution,
                RgFloat2D                     jitterOffset,
                double                        timeDelta,
                float                         nearPlane,
                float                         farPlane,
                float                         fovVerticalRad,
                bool                          resetAccumulation,
                float                         oneGameUnitInMeters,
                bool skipGeneratedFrame ) -> std::optional< FramebufferImageIndex >;

    void CopyDX12OutputToVk( VkCommandBuffer        cmd,
                             uint32_t               frameIndex,
                             const Framebuffers&    framebuffers,
                             const ResolutionState& resolution );

    RgFloat2D GetJitter( const ResolutionState& resolutionState, uint32_t frameId ) const;

    void OnFramebuffersSizeChange( const ResolutionState& resolutionState ) override;

private:
    FfxFsr3Context*        m_context{};
    std::vector< uint8_t > m_scratchBufferSharedResources{};
    std::vector< uint8_t > m_scratchBufferUpscaling{};
    std::vector< uint8_t > m_scratchBufferFrameInterpolation{};

    FfxFrameGenerationConfig* m_framegenConfig{};
};

}
