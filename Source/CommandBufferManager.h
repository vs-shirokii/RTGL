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

#include <span>
#include <vector>

#include "Common.h"
#include "Containers.h"
#include "Queues.h"

namespace RTGL1
{

#define SEMAPHORE_IS_BINARY 0

struct ToWait
{
    VkSemaphore semaphore;
    uint64_t    waitvalue;
};

struct ToSignal
{
    VkSemaphore semaphore;
    uint64_t    signalvalue;
};

class CommandBufferManager
{
public:
    explicit CommandBufferManager( VkDevice device, std::shared_ptr< Queues > queues );
    ~CommandBufferManager();

    CommandBufferManager( const CommandBufferManager& other )     = delete;
    CommandBufferManager( CommandBufferManager&& other ) noexcept = delete;
    CommandBufferManager& operator=( const CommandBufferManager& other ) = delete;
    CommandBufferManager& operator=( CommandBufferManager&& other ) noexcept = delete;

    void                  PrepareForFrame( uint32_t frameIndex );

    // Start graphics command buffer for current frame index
    VkCommandBuffer       StartGraphicsCmd();
    // Start compute command buffer for current frame index
    VkCommandBuffer       StartComputeCmd();
    // Start transfer command buffer for current frame index
    VkCommandBuffer       StartTransferCmd();

    void Submit( VkCommandBuffer cmd, VkFence fence = VK_NULL_HANDLE );
    void Submit_Binary( VkCommandBuffer          cmd,
                        std::span< VkSemaphore > waitSemaphores,
                        VkSemaphore              signalSemaphore,
                        VkFence                  fence );
    void Submit_TimelineInternal( VkCommandBuffer          cmd,
                                  std::span< VkSemaphore > waitSemaphores,
                                  std::span< uint64_t >    waitValues,
                                  VkSemaphore              signalSemaphore,
                                  uint64_t                 signalValue,
                                  VkFence                  fence );
    void Submit_Timeline( VkCommandBuffer cmd, VkFence fence, ToWait towait, ToSignal tosignal );
    void Submit_Timeline(
        VkCommandBuffer cmd, VkFence fence, ToWait towait0, ToWait towait1, ToSignal tosignal );

    void                  WaitGraphicsIdle();
    void                  WaitComputeIdle();
    void                  WaitTransferIdle();
    void                  WaitDeviceIdle();

private:
    struct AllocatedCmds
    {
        std::vector< VkCommandBuffer > cmds     = {};
        uint32_t                       curCount = 0;
        VkCommandPool                  pool     = VK_NULL_HANDLE;
    };

private:
    VkCommandBuffer StartCmd( uint32_t frameIndex, AllocatedCmds& cmds, VkQueue queue );

    VkQueue PopQueueOfCmd( VkCommandBuffer cmd );

private:
    VkDevice                                       device;

    uint32_t                                       currentFrameIndex;

    const uint32_t                                 cmdAllocStep = 16;

    // allocated cmds
    AllocatedCmds                                  graphicsCmds[ MAX_FRAMES_IN_FLIGHT ];
    AllocatedCmds                                  computeCmds[ MAX_FRAMES_IN_FLIGHT ];
    AllocatedCmds                                  transferCmds[ MAX_FRAMES_IN_FLIGHT ];

    std::shared_ptr< Queues >                      queues;
    rgl::unordered_map< VkCommandBuffer, VkQueue > cmdQueues[ MAX_FRAMES_IN_FLIGHT ];
};

inline void CommandBufferManager::Submit_Timeline( VkCommandBuffer cmd,
                                                   VkFence         fence,
                                                   ToWait          towait,
                                                   ToSignal        tosignal )
{
    size_t wait_count = 0;

    VkSemaphore wait_semaphores[ 1 ];
    uint64_t    wait_values[ 1 ];
    if( towait.semaphore != nullptr )
    {
        wait_semaphores[ wait_count ] = towait.semaphore;
        wait_values[ wait_count ]     = towait.waitvalue;
        wait_count++;
    }

    Submit_TimelineInternal( cmd,
                             std::span{ wait_semaphores, wait_count },
                             std::span{ wait_values, wait_count },
                             tosignal.semaphore,
                             tosignal.signalvalue,
                             fence );
}

inline void CommandBufferManager::Submit_Timeline(
    VkCommandBuffer cmd, VkFence fence, ToWait towait0, ToWait towait1, ToSignal tosignal )
{
    size_t wait_count = 0;

    VkSemaphore wait_semaphores[ 1 ];
    uint64_t    wait_values[ 1 ];
    if( towait0.semaphore != nullptr )
    {
        wait_semaphores[ wait_count ] = towait0.semaphore;
        wait_values[ wait_count ]     = towait0.waitvalue;
        wait_count++;
    }
    if( towait1.semaphore != nullptr )
    {
        wait_semaphores[ wait_count ] = towait1.semaphore;
        wait_values[ wait_count ]     = towait1.waitvalue;
        wait_count++;
    }

    Submit_TimelineInternal( cmd,
                             std::span{ wait_semaphores, wait_count },
                             std::span{ wait_values, wait_count },
                             tosignal.semaphore,
                             tosignal.signalvalue,
                             fence );
}

}
