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

#include "ShaderManager.h"

#include <fstream>
#include <vector>
#include <cstring>
#include "RgException.h"

#include "Generated/ShaderCommonC.h"

using namespace RTGL1;

enum
{
    USES_RAY_QUERY_OR_POSITION_FETCH = 1,
};

struct ShaderModuleDefinition
{
    const char*           name{};
    const char*           filename{};
    uint32_t              flags{ 0 };
    // will be parsed from filename once
    VkShaderStageFlagBits stage{ VK_SHADER_STAGE_ALL };
};

// clang-format off

// Note: set shader stage to VK_SHADER_STAGE_ALL, to identify stage by the file extension
static ShaderModuleDefinition G_SHADERS[] =
{
    { "RGenPrimary",                "RtRaygenPrimary.rgen.spv"              },
    { "RGenReflRefr",               "RtRaygenReflRefr.rgen.spv"             },
    { "RGenDirect",                 "RtRaygenDirect.rgen.spv"               },
    { "RGenIndirectInit",           "RtRaygenIndirectInit.rgen.spv"         },
    { "CmIndirectFinal",            "RtRaygenIndirectFinal.comp.spv"        },
    { "RGenGradients",              "RtGradients.rgen.spv"                  },
    { "RInitialReservoirs",         "RtInitialReservoirs.rgen.spv"          },
    { "RVolumetric",                "RtVolumetric.rgen.spv"                 },
    { "RMiss",                      "RtMiss.rmiss.spv"                      },
    { "RMissShadow",                "RtMissShadowCheck.rmiss.spv"           },
    { "RClsOpaque",                 "RtClsOpaque.rchit.spv"                 },
    { "RAlphaTest",                 "RtAlphaTest.rahit.spv"                 },
#if LIGHT_GRID_ENABLED
    { "CLightGridBuild",            "CmLightGridBuild.comp.spv"             },
#endif
    { "CPrepareFinal",              "CmPrepareFinal.comp.spv"               },
    { "CLuminanceHistogram",        "CmLuminanceHistogram.comp.spv"         },
    { "CLuminanceAvg",              "CmLuminanceAvg.comp.spv"               },
    { "CVolumetricProcess",         "CmVolumetricProcess.comp.spv"          },
    { "ScatterAccum",               "CmScatterAccum.comp.spv"               },
    { "Fluid_Generate",             "Fluid_Generate.comp.spv"               },
    { "Fluid_Particles",            "Fluid_Particles.comp.spv"              , USES_RAY_QUERY_OR_POSITION_FETCH },
    { "Fluid_VisualizeVert",        "Fluid_Visualize.vert.spv"              },
    { "Fluid_VisualizeFrag",        "Fluid_Visualize.frag.spv"              },
    { "Fluid_DepthSmooth",          "Fluid_DepthSmooth.comp.spv"            },
    { "FragWorld",                  "RsWorld_RT.frag.spv"                   },
    { "FragWorldClassic",           "RsWorld_Classic.frag.spv"              },
    { "FragSky",                    "RsSky.frag.spv"                        },
    { "FragSwapchain",              "RsSwapchain.frag.spv"                  },
    { "VertDefault",                "RsRasterizer.vert.spv"                 },
    { "VertDefaultMultiview",       "RsRasterizerMultiview.vert.spv"        },
    { "VertFullscreenQuad",         "RsFullscreenQuad.vert.spv"             },
    { "FragDepthCopying",           "RsDepthCopying.frag.spv"               },
    { "CVertexPreprocess",          "CmVertexPreprocess.comp.spv"           },
    { "CAntiFirefly",               "CmAntiFirefly.comp.spv"                },
    { "CSVGFTemporalAccum",         "CmSVGFTemporalAccumulation.comp.spv"   },
    { "CSVGFVarianceEstim",         "CmSVGFEstimateVariance.comp.spv"       },
    { "CSVGFAtrous",                "CmSVGFAtrous.comp.spv"                 },
    { "CSVGFAtrous_Iter0",          "CmSVGFAtrous_Iter0.comp.spv"           },
    { "CASVGFGradientAtrous",       "CmASVGFGradientAtrous.comp.spv"        },
    { "CBloomDownsample",           "CmBloomDownsample.comp.spv"            },
    { "CBloomUpsample",             "CmBloomUpsample.comp.spv"              },
    { "CBloomApply",                "CmBloomApply.comp.spv"                 },
    { "CBloomPreload",              "CmBloomPreload.comp.spv"               },
    { "CCheckerboard",              "CmCheckerboard.comp.spv"               },
    { "CCas",                       "CmCas.comp.spv"                        },
    { "VertLensFlare",              "RsRasterizerLensFlare.vert.spv"        },
    { "FragLensFlare",              "RsRasterizerLensFlare.frag.spv"        },
    { "CCullLensFlares",            "CmCullLensFlares.comp.spv"             },
    { "VertDecal",                  "RsDecal.vert.spv"                      },
    { "FragDecal",                  "RsDecal.frag.spv"                      },
    { "DecalNormalsCopy",           "CmDecalNormalsCopy.comp.spv"           },
    { "EffectWipe",                 "EfWipe.comp.spv"                       },
    { "EffectRadialBlur",           "EfRadialBlur.comp.spv"                 },
    { "EffectChromaticAberration",  "EfChromaticAberration.comp.spv"        },
    { "EffectInverseBW",            "EfInverseBW.comp.spv"                  },
    { "EffectDistortedSides",       "EfDistortedSides.comp.spv"             },
    { "EffectWaves",                "EfWaves.comp.spv"                      },
    { "EffectColorTint",            "EfColorTint.comp.spv"                  },
    { "EffectTeleport",             "EfTeleport.comp.spv"                   },
    { "EffectHueShift",             "EfHueShift.comp.spv"                   },
    { "EffectNightVision",          "EfNightVision.comp.spv"                },
    { "EffectCrtDemodulateEncode",  "EfCrtDemodulateEncode.comp.spv"        },
    { "EffectCrtDecode",            "EfCrtDecode.comp.spv"                  },
    { "EffectVHS",                  "EfVHS.comp.spv"                        },
    { "EffectDither",               "EfDither.comp.spv"                     },
    { "EffectHDRPrepare",           "EfHDRPrepare.comp.spv"                 },
};

// clang-format on

ShaderManager::ShaderManager( VkDevice              _device,
                              std::filesystem::path _shaderFolderPath,
                              bool                  _supportsRayQueryAndPositionFetch )
    : device( _device )
    , shaderFolderPath( std::move( _shaderFolderPath ) )
    , supportsRayQueryAndPositionFetch( _supportsRayQueryAndPositionFetch )
{
    LoadShaderModules();
}

ShaderManager::~ShaderManager()
{
    UnloadShaderModules();
}

void ShaderManager::ReloadShaders()
{
    vkDeviceWaitIdle( device );

    UnloadShaderModules();
    LoadShaderModules();

    NotifySubscribersAboutReload();

    vkDeviceWaitIdle( device );
}

void ShaderManager::LoadShaderModules()
{
    for( auto& s : G_SHADERS )
    {
        assert( strlen( s.filename ) > 0 );
        assert( strlen( s.name ) > 0 );

        if( s.flags & USES_RAY_QUERY_OR_POSITION_FETCH )
        {
            if( !supportsRayQueryAndPositionFetch )
            {
                debug::Warning(
                    "Skipping \'{}\' shader, as ray query or position fetch is not supported",
                    s.filename );
                continue;
            }
        }

        if( s.stage == VK_SHADER_STAGE_ALL )
        {
            // parse stage if needed, it's done only once, as names won't be changing
            s.stage = GetStageByExtension( s.filename );
        }

        auto path = shaderFolderPath / s.filename;

        VkShaderModule m = LoadModuleFromFile( path.c_str() );
        SET_DEBUG_NAME( device, m, VK_OBJECT_TYPE_SHADER_MODULE, s.name );

        modules[ s.name ] = { m, s.stage };
    }
}

void ShaderManager::UnloadShaderModules()
{
    for( auto& s : modules )
    {
        vkDestroyShaderModule( device, s.second.module, nullptr );
    }

    modules.clear();
}

VkShaderModule ShaderManager::GetShaderModule( std::string_view name ) const
{
    const auto& m = modules.find( name );
    return m != modules.end() ? m->second.module : VK_NULL_HANDLE;
}

VkShaderStageFlagBits ShaderManager::GetModuleStage( std::string_view name ) const
{
    const auto& m = modules.find( name );
    return m != modules.end() ? m->second.shaderStage : static_cast< VkShaderStageFlagBits >( 0 );
}

VkPipelineShaderStageCreateInfo ShaderManager::GetStageInfo( std::string_view name ) const
{
    const auto& m = modules.find( name );

    if( m == modules.end() )
    {
        using namespace std::string_literals;

        throw RgException( RG_RESULT_ERROR_CANT_FIND_HARDCODED_RESOURCES,
                           std::format( "Can't find loaded shader with name \"{}\"", name ) );
    }

    return VkPipelineShaderStageCreateInfo{
        .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage  = m->second.shaderStage,
        .module = m->second.module,
        .pName  = "main",
    };
}

VkShaderModule ShaderManager::LoadModuleFromFile( const std::filesystem::path& path )
{
    std::ifstream          shaderFile( path, std::ios::binary );
    std::vector< uint8_t > shaderSource( std::istreambuf_iterator( shaderFile ), {} );

    if( shaderSource.empty() )
    {
        throw RgException( RG_RESULT_ERROR_CANT_FIND_HARDCODED_RESOURCES,
                           "Can't find shader file: \"" + path.string() + "\"" );
    }

    return LoadModuleFromMemory( reinterpret_cast< const uint32_t* >( shaderSource.data() ),
                                 uint32_t( shaderSource.size() ) );
}

VkShaderModule ShaderManager::LoadModuleFromMemory( const uint32_t* pCode, uint32_t codeSize )
{
    VkShaderModule shaderModule;

    VkShaderModuleCreateInfo moduleInfo = {
        .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = codeSize,
        .pCode    = pCode,
    };

    VkResult r = vkCreateShaderModule( device, &moduleInfo, nullptr, &shaderModule );
    VK_CHECKERROR( r );

    return shaderModule;
}

VkShaderStageFlagBits ShaderManager::GetStageByExtension( std::string_view name )
{
    // assume that file names end with ".spv"

    constexpr std::pair< std::string_view, VkShaderStageFlagBits > endingToType[] = {
        { ".vert.spv", VK_SHADER_STAGE_VERTEX_BIT },
        { ".frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT },
        { ".comp.spv", VK_SHADER_STAGE_COMPUTE_BIT },
        { ".rgen.spv", VK_SHADER_STAGE_RAYGEN_BIT_KHR },
        { ".rahit.spv", VK_SHADER_STAGE_ANY_HIT_BIT_KHR },
        { ".rchit.spv", VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR },
        { ".rmiss.spv", VK_SHADER_STAGE_MISS_BIT_KHR },
        { ".rcall.spv", VK_SHADER_STAGE_CALLABLE_BIT_KHR },
        { ".rint.spv", VK_SHADER_STAGE_INTERSECTION_BIT_KHR },
        { ".tesc.spv", VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT },
        { ".tese.spv", VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT },
        { ".mesh.spv", VK_SHADER_STAGE_MESH_BIT_NV },
        { ".task.spv", VK_SHADER_STAGE_TASK_BIT_NV },
    };

    for( const auto& [ ending, type ] : endingToType )
    {
        if( name.ends_with( ending ) )
        {
            return type;
        }
    }

    throw RgException( RG_RESULT_INTERNAL_ERROR,
                       "Can't find shader stage type for " + std::string( name ) );
}

void ShaderManager::Subscribe( std::shared_ptr< IShaderDependency > subscriber )
{
    if( subscriber )
    {
        subscribers.emplace_back( subscriber );
    }
}

void ShaderManager::NotifySubscribersAboutReload()
{
    for( auto& ws : subscribers )
    {
        if( auto s = ws.lock() )
        {
            s->OnShaderReload( this );
        }
    }

    auto ifExpired = []( const std::weak_ptr< IShaderDependency >& ws ) {
        return ws.expired();
    };
    erase_if( subscribers, ifExpired );
}
