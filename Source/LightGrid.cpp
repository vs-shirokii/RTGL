// Copyright (c) 2022 Sultim Tsyrendashiev
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

#include "LightGrid.h"

#include "CmdLabel.h"
#include "Utils.h"
#include "Generated/ShaderCommonC.h"

RTGL1::LightGrid::LightGrid(
    VkDevice _device,
    const std::shared_ptr<ShaderManager> &_shaderManager,
    const std::shared_ptr<GlobalUniform> &_uniform,
    const std::shared_ptr<BlueNoise> &_blueNoise,
    const std::shared_ptr<LightManager> &_lightManager
)
#if LIGHT_GRID_ENABLED_
    : device(_device)
    , pipelineLayout(VK_NULL_HANDLE)
    , gridBuildPipeline(VK_NULL_HANDLE)
#endif
{
#if LIGHT_GRID_ENABLED_
    VkDescriptorSetLayout setLayouts[] =
    {
        _uniform->GetDescSetLayout(),
        _blueNoise->GetDescSetLayout(),
        _lightManager->GetDescSetLayout()
    };

    VkPipelineLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = std::size(setLayouts);
    layoutInfo.pSetLayouts = setLayouts;

    VkResult r = vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout);
    VK_CHECKERROR(r);

    SET_DEBUG_NAME(device, pipelineLayout, VK_OBJECT_TYPE_PIPELINE_LAYOUT, "Light grid pipeline layout");


    CreatePipelines(_shaderManager.get());
#endif
}

RTGL1::LightGrid::~LightGrid()
{
#if LIGHT_GRID_ENABLED_
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    DestroyPipelines();
#endif
}

void RTGL1::LightGrid::Build(
    VkCommandBuffer cmd, uint32_t frameIndex, 
    const std::shared_ptr<GlobalUniform> &uniform,
    const std::shared_ptr<BlueNoise> &blueNoise,
    const std::shared_ptr<LightManager> &lightManager)
{
#if LIGHT_GRID_ENABLED_
    CmdLabel label(cmd, "Light grid build");


    // no barriers here, as lightManager has a AutoBuffer kludge


    VkDescriptorSet sets[] =
    {
        uniform->GetDescSet(frameIndex),
        blueNoise->GetDescSet(),
        lightManager->GetDescSet(frameIndex),
    };
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
        pipelineLayout,
        0, std::size(sets), sets,
        0, nullptr);


    uint32_t lightSamplesCount = LIGHT_GRID_CELL_SIZE * LIGHT_GRID_SIZE_X * LIGHT_GRID_SIZE_Y * LIGHT_GRID_SIZE_Z;
    uint32_t wgCountX = Utils::GetWorkGroupCount(lightSamplesCount, COMPUTE_LIGHT_GRID_GROUP_SIZE_X);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, gridBuildPipeline);
    vkCmdDispatch(cmd, wgCountX, 1, 1);
#endif
}

void RTGL1::LightGrid::OnShaderReload(const ShaderManager* shaderManager)
{
#if LIGHT_GRID_ENABLED_
    DestroyPipelines();
    CreatePipelines(shaderManager);
#endif
}

void RTGL1::LightGrid::CreatePipelines(const ShaderManager* shaderManager)
{
#if LIGHT_GRID_ENABLED_
    VkComputePipelineCreateInfo plInfo = {};
    plInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    plInfo.layout = pipelineLayout;
    plInfo.stage = shaderManager->GetStageInfo("CLightGridBuild");

    VkResult r = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &plInfo, nullptr, &gridBuildPipeline);
    VK_CHECKERROR(r);

    SET_DEBUG_NAME(device, gridBuildPipeline, VK_OBJECT_TYPE_PIPELINE, "Light grid build pipeline");
#endif
}

void RTGL1::LightGrid::DestroyPipelines()
{
#if LIGHT_GRID_ENABLED_
    vkDestroyPipeline(device, gridBuildPipeline, nullptr);
    gridBuildPipeline = VK_NULL_HANDLE;
#endif
}
