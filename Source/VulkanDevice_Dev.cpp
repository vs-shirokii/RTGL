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

#include "VulkanDevice.h"

#include "Matrix.h"

#include "Generated/ShaderCommonC.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <ranges>

namespace
{

template< typename To, typename From >
To ClampPix( From v )
{
    return std::clamp( To( v ), To( 96 ), To( 3840 ) );
}

struct WholeWindow
{
    explicit WholeWindow( std::string_view name )
    {
#ifdef IMGUI_HAS_VIEWPORT
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos( viewport->WorkPos );
        ImGui::SetNextWindowSize( viewport->WorkSize );
        ImGui::SetNextWindowViewport( viewport->ID );
#else
        ImGui::SetNextWindowPos( ImVec2( 0.0f, 0.0f ) );
        ImGui::SetNextWindowSize( ImGui::GetIO().DisplaySize );
#endif
        ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, 0.0f );

        if( ImGui::Begin( name.data(),
                          nullptr,
                          ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                              ImGuiWindowFlags_NoBackground ) )
        {
            beginSuccess = ImGui::BeginTabBar( "##TabBar", ImGuiTabBarFlags_Reorderable );
        }
    }

    WholeWindow( const WholeWindow& other )                = delete;
    WholeWindow( WholeWindow&& other ) noexcept            = delete;
    WholeWindow& operator=( const WholeWindow& other )     = delete;
    WholeWindow& operator=( WholeWindow&& other ) noexcept = delete;

    explicit operator bool() const { return beginSuccess; }

    ~WholeWindow()
    {
        if( beginSuccess )
        {
            ImGui::EndTabBar();
        }
        ImGui::End();
        ImGui::PopStyleVar( 1 );
    }

private:
    bool beginSuccess{ false };
};

}

bool RTGL1::VulkanDevice::Dev_IsDevmodeInitialized() const
{
    return debugWindows && devmode;
}

namespace
{

template< size_t N >
struct StringLiteral
{
    consteval StringLiteral( const char ( &str )[ N ] ) { std::copy_n( str, N, value ); }
    consteval auto c_str() const { return value; }
    char value[ N ];
};

template< StringLiteral Name >
void imgui_ShowAlwaysOnCheckbox()
{
    ImGui::BeginDisabled( true );
    static bool alwaysOn;
    alwaysOn = true;
    ImGui::Checkbox( Name.c_str(), &alwaysOn );
    ImGui::EndDisabled();
}

}

void RTGL1::VulkanDevice::Dev_Draw() const
{
    if( !Dev_IsDevmodeInitialized() )
    {
        return;
    }

    if( debugWindows->IsMinimized() )
    {
        return;
    }

    auto w = WholeWindow( "Main window" );
    if( !w )
    {
        return;
    }

    if( ImGui::BeginTabItem( "General" ) )
    {
        ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.59f, 0.98f, 0.26f, 0.40f ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 0.59f, 0.98f, 0.26f, 1.00f ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonActive, ImVec4( 0.53f, 0.98f, 0.06f, 1.00f ) );
        devmode->reloadShaders = ImGui::Button( "Reload shaders", { -1, 96 } );
        ImGui::PopStyleColor( 3 );

        auto& modifiers = devmode->drawInfoOvrd;

        ImGui::Dummy( ImVec2( 0, 4 ) );
        ImGui::Separator();
        ImGui::Dummy( ImVec2( 0, 4 ) );

        ImGui::Checkbox( "Override", &modifiers.enable );
        ImGui::BeginDisabled( !modifiers.enable );
        if( ImGui::TreeNodeEx( "Present", ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            ImGui::Checkbox( "HDR", &modifiers.hdr );
            
            if( modifiers.frameGeneration != RG_FRAME_GENERATION_MODE_OFF &&
                modifiers.upscaleTechnique == RG_RENDER_UPSCALE_TECHNIQUE_NVIDIA_DLSS)
            {
                imgui_ShowAlwaysOnCheckbox< "Vsync" >();
            }
            else
            {
                ImGui::Checkbox( "Vsync", &modifiers.vsync );
            }
            
            if( modifiers.frameGeneration == RG_FRAME_GENERATION_MODE_OFF )
            {
                ImGui::Checkbox( "Prefer DXGI for Present", &modifiers.preferDxgiPresent );
            }
            else
            {
                imgui_ShowAlwaysOnCheckbox< "Prefer DXGI for Present" >();
            }

            static_assert(
                std::same_as< int, std::underlying_type_t< RgRenderUpscaleTechnique > > );
            static_assert(
                std::same_as< int, std::underlying_type_t< RgRenderSharpenTechnique > > );
            static_assert( std::same_as< int, std::underlying_type_t< RgRenderResolutionMode > > );
            static_assert( std::same_as< int, std::underlying_type_t< RgFrameGenerationMode > > );

            {
                ImGui::Spacing();
                ImGui::TextUnformatted( "Frame Generation:" );
                ImGui::RadioButton( "Off##FG",
                                    reinterpret_cast< int* >( &modifiers.frameGeneration ),
                                    RG_FRAME_GENERATION_MODE_OFF );
                ImGui::SameLine();
                ImGui::BeginDisabled( !IsUpscaleTechniqueAvailable( modifiers.upscaleTechnique, //
                                                                    RG_FRAME_GENERATION_MODE_ON,
                                                                    nullptr ) );
                ImGui::RadioButton( "On##FG",
                                    reinterpret_cast< int* >( &modifiers.frameGeneration ),
                                    RG_FRAME_GENERATION_MODE_ON );
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(
                    !IsUpscaleTechniqueAvailable( modifiers.upscaleTechnique, //
                                                  RG_FRAME_GENERATION_MODE_WITHOUT_GENERATED,
                                                  nullptr ) );
                ImGui::RadioButton( "On, but skip generated frame##FG",
                                    reinterpret_cast< int* >( &modifiers.frameGeneration ),
                                    RG_FRAME_GENERATION_MODE_WITHOUT_GENERATED );
                ImGui::EndDisabled();
            }

            const char* dlssError{};
            const char* fsrError{};

            bool dlssOk = IsUpscaleTechniqueAvailable( RG_RENDER_UPSCALE_TECHNIQUE_NVIDIA_DLSS, //
                                                       modifiers.frameGeneration,
                                                       &dlssError );
            bool fsrOk  = IsUpscaleTechniqueAvailable( RG_RENDER_UPSCALE_TECHNIQUE_AMD_FSR2, //
                                                      modifiers.frameGeneration,
                                                      &fsrError );
            {
                ImGui::Spacing();
                ImGui::TextUnformatted( "Upscaler:" );
                ImGui::RadioButton( "Linear##Upscale",
                                    reinterpret_cast< int* >( &modifiers.upscaleTechnique ),
                                    RG_RENDER_UPSCALE_TECHNIQUE_LINEAR );
                ImGui::SameLine();
                ImGui::RadioButton( "Nearest##Upscale",
                                    reinterpret_cast< int* >( &modifiers.upscaleTechnique ),
                                    RG_RENDER_UPSCALE_TECHNIQUE_NEAREST );
                ImGui::SameLine();
                ImGui::BeginDisabled( !fsrOk );
                ImGui::RadioButton( "AMD FSR##Upscale",
                                    reinterpret_cast< int* >( &modifiers.upscaleTechnique ),
                                    RG_RENDER_UPSCALE_TECHNIQUE_AMD_FSR2 );
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled( !dlssOk );
                ImGui::RadioButton( "NVIDIA DLSS##Upscale",
                                    reinterpret_cast< int* >( &modifiers.upscaleTechnique ),
                                    RG_RENDER_UPSCALE_TECHNIQUE_NVIDIA_DLSS );
                ImGui::EndDisabled();
            }
            if( !Utils::IsCstrEmpty( dlssError ) )
            {
                ImGui::TextUnformatted( dlssError );
            }
            if( !Utils::IsCstrEmpty( fsrError ) )
            {
                ImGui::TextUnformatted( fsrError );
            }

            bool forceCustom =
                modifiers.upscaleTechnique != RG_RENDER_UPSCALE_TECHNIQUE_AMD_FSR2 &&
                modifiers.upscaleTechnique != RG_RENDER_UPSCALE_TECHNIQUE_NVIDIA_DLSS;
            if( forceCustom )
            {
                modifiers.resolutionMode = RG_RENDER_RESOLUTION_MODE_CUSTOM;
            }

            {
                ImGui::RadioButton( "Custom##Resolution",
                                    reinterpret_cast< int* >( &modifiers.resolutionMode ),
                                    RG_RENDER_RESOLUTION_MODE_CUSTOM );
                ImGui::SameLine();
                ImGui::BeginDisabled( forceCustom );
                ImGui::RadioButton( "Ultra Performance##Resolution",
                                    reinterpret_cast< int* >( &modifiers.resolutionMode ),
                                    RG_RENDER_RESOLUTION_MODE_ULTRA_PERFORMANCE );
                ImGui::SameLine();
                ImGui::RadioButton( "Performance##Resolution",
                                    reinterpret_cast< int* >( &modifiers.resolutionMode ),
                                    RG_RENDER_RESOLUTION_MODE_PERFORMANCE );
                ImGui::SameLine();
                ImGui::RadioButton( "Balanced##Resolution",
                                    reinterpret_cast< int* >( &modifiers.resolutionMode ),
                                    RG_RENDER_RESOLUTION_MODE_BALANCED );
                ImGui::SameLine();
                ImGui::RadioButton( "Quality##Resolution",
                                    reinterpret_cast< int* >( &modifiers.resolutionMode ),
                                    RG_RENDER_RESOLUTION_MODE_QUALITY );
                if( modifiers.upscaleTechnique == RG_RENDER_UPSCALE_TECHNIQUE_NVIDIA_DLSS ||
                    modifiers.upscaleTechnique == RG_RENDER_UPSCALE_TECHNIQUE_AMD_FSR2 )
                {
                    ImGui::SameLine();
                    ImGui::RadioButton( "Native AA##Resolution",
                                        reinterpret_cast< int* >( &modifiers.resolutionMode ),
                                        RG_RENDER_RESOLUTION_MODE_NATIVE_AA );
                }
                ImGui::EndDisabled();
            }
            {
                ImGui::BeginDisabled(
                    !( modifiers.resolutionMode == RG_RENDER_RESOLUTION_MODE_CUSTOM ) );

                ImGui::SliderFloat(
                    "Custom render size", &modifiers.customRenderSizeScale, 0.1f, 1.5f );

                ImGui::EndDisabled();
            }
            {
                ImGui::Checkbox( "Downscale to pixelized", &modifiers.pixelizedEnable );
                if( modifiers.pixelizedEnable )
                {
                    ImGui::SliderInt( "Pixelization size", &modifiers.pixelizedHeight, 100, 600 );
                }
            }

            {
                ImGui::Spacing();
                ImGui::TextUnformatted( "Sharpening:" );
                ImGui::RadioButton( "None##Sharp",
                                    reinterpret_cast< int* >( &modifiers.sharpenTechnique ),
                                    RG_RENDER_SHARPEN_TECHNIQUE_NONE );
                ImGui::SameLine();
                ImGui::RadioButton( "Naive##Sharp",
                                    reinterpret_cast< int* >( &modifiers.sharpenTechnique ),
                                    RG_RENDER_SHARPEN_TECHNIQUE_NAIVE );
                ImGui::SameLine();
                ImGui::RadioButton( "AMD CAS##Sharp",
                                    reinterpret_cast< int* >( &modifiers.sharpenTechnique ),
                                    RG_RENDER_SHARPEN_TECHNIQUE_AMD_CAS );
            }

            ImGui::TreePop();
        }
        if( ImGui::TreeNode( "Tonemapping" ) )
        {
            ImGui::Checkbox( "Disable eye adaptation", &modifiers.disableEyeAdaptation );
            ImGui::SliderFloat( "EV100 min", &modifiers.ev100Min, -3, 16, "%.1f" );
            ImGui::SliderFloat( "EV100 max", &modifiers.ev100Max, -3, 16, "%.1f" );
            ImGui::SliderFloat3( "Saturation", modifiers.saturation, -1, 1, "%.1f" );
            ImGui::SliderFloat3( "Crosstalk", modifiers.crosstalk, 0.0f, 1.0f, "%.2f" );
            ImGui::TreePop();
        }
        if( ImGui::TreeNode( "Illumination" ) )
        {
            ImGui::Checkbox( "Anti-firefly", &devmode->antiFirefly );
            ImGui::SliderInt( "Shadow rays max depth",
                              &modifiers.maxBounceShadows,
                              0,
                              2,
                              "%d",
                              ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoInput );
            ImGui::Checkbox( "Second bounce for indirect",
                             &modifiers.enableSecondBounceForIndirect );
            ImGui::SliderFloat( "Sensitivity to change: Diffuse Direct",
                                &modifiers.directDiffuseSensitivityToChange,
                                0.0f,
                                1.0f,
                                "%.2f" );
            ImGui::SliderFloat( "Sensitivity to change: Diffuse Indirect",
                                &modifiers.indirectDiffuseSensitivityToChange,
                                0.0f,
                                1.0f,
                                "%.2f" );
            ImGui::SliderFloat( "Sensitivity to change: Specular",
                                &modifiers.specularSensitivityToChange,
                                0.0f,
                                1.0f,
                                "%.2f" );
            ImGui::TreePop();
        }
        if( ImGui::TreeNode( "Texturing" ) )
        {
            ImGui::SliderFloat( "Normal map Scale", &modifiers.normalMapStrength, 0.f, 1.f );
            ImGui::SliderFloat( "Height map Depth", &modifiers.heightMapDepth, 0.f, 0.05f );
            ImGui::SliderFloat( "Emission map GI Boost", &modifiers.emissionMapBoost, 0.f, 100.f );
            ImGui::SliderFloat( "Emission map Screen Scale", &modifiers.emissionMaxScreenColor, 0.f, 100.f );
            ImGui::TreePop();
        }
        if( ImGui::TreeNode( "Lightmap" ) )
        {
            ImGui::SliderFloat( "Screen coverage", &modifiers.lightmapScreenCoverage, 0.0f, 1.0f );
            ImGui::TreePop();
        }
        if( ImGui::TreeNodeEx( "Fluid", ImGuiTreeNodeFlags_DefaultOpen ) )
        {
            ImGui::Checkbox( "Enable", &modifiers.fluidEnabled );
            ImGui::DragFloat3(
                "Gravity##fluid", modifiers.fluidGravity.data, 0.1f, -100, 100, "%.1f" );
            modifiers.fluidReset = ImGui::Button( "Reset", { -1, 48 } );
            ImGui::Checkbox( "Suppress Fluid Raster", &devmode->fluidStopVisualize );
            if( ImGui::TreeNodeEx( "Debug Spawn##fluidspw", ImGuiTreeNodeFlags_DefaultOpen ) )
            {
                static int       spawnCount                   = 1000;
                static RgFloat3D spawnPosition                = { 0, 3, 0 };
                static RgFloat3D spawnVelocity                = { 0, 2, 0 };
                static float     spawnVelocityDispersion      = 1.0f;
                static float     spawnVelocityDispersionAngle = 180;
                {
                    ImGui::InputInt( "Count##fluidspw", &spawnCount, 1000, 10'000 );
                    spawnCount = std::clamp( spawnCount, 0, 1'000'000 );
                }
                ImGui::DragFloat3( "Position##fluidspw", spawnPosition.data, 0.5f );
                ImGui::DragFloat3( "Velocity##fluidspw", spawnVelocity.data, 0.5f );
                ImGui::DragFloat( "Dispersion##fluidspw", &spawnVelocityDispersion, 0.1f, 0, 1 );
                ImGui::DragFloat(
                    "Dispersion Angle##fluidspw", &spawnVelocityDispersionAngle, 5, 0, 180 );
                if( ImGui::Button( "Spawn", { -1, 48 } ) )
                {
                    auto info = RgSpawnFluidInfo{
                        .sType                  = RG_STRUCTURE_TYPE_SPAWN_FLUID_INFO,
                        .pNext                  = nullptr,
                        .position               = spawnPosition,
                        .radius                 = 0,
                        .velocity               = spawnVelocity,
                        .dispersionVelocity     = spawnVelocityDispersion,
                        .dispersionAngleDegrees = spawnVelocityDispersionAngle,
                        .count                  = static_cast< uint32_t >( spawnCount ),
                    };
                    static_assert( sizeof( RgSpawnFluidInfo ) == 56, "Change here" );
                    // dev const hack
                    const_cast< VulkanDevice* >( this )->SpawnFluid( &info );
                }
                ImGui::TreePop();
            }
            ImGui::TreePop();
        }
        ImGui::EndDisabled();

        ImGui::Dummy( ImVec2( 0, 4 ) );
        ImGui::Separator();
        ImGui::Dummy( ImVec2( 0, 4 ) );

        if( ImGui::TreeNode( "Debug show" ) )
        {
            std::pair< const char*, uint32_t > fs[] = {
                { "Unfiltered diffuse direct", DEBUG_SHOW_FLAG_UNFILTERED_DIFFUSE },
                { "Unfiltered diffuse indirect", DEBUG_SHOW_FLAG_UNFILTERED_INDIRECT },
                { "Unfiltered specular", DEBUG_SHOW_FLAG_UNFILTERED_SPECULAR },
                { "Diffuse direct", DEBUG_SHOW_FLAG_ONLY_DIRECT_DIFFUSE },
                { "Diffuse indirect", DEBUG_SHOW_FLAG_ONLY_INDIRECT_DIFFUSE },
                { "Specular", DEBUG_SHOW_FLAG_ONLY_SPECULAR },
                { "Albedo white", DEBUG_SHOW_FLAG_ALBEDO_WHITE },
                { "Normals", DEBUG_SHOW_FLAG_NORMALS },
                { "Motion vectors", DEBUG_SHOW_FLAG_MOTION_VECTORS },
                { "Gradients", DEBUG_SHOW_FLAG_GRADIENTS },
                { "Light grid", DEBUG_SHOW_FLAG_LIGHT_GRID },
                { "Bloom", DEBUG_SHOW_FLAG_BLOOM },
            };
            for( const auto [ name, f ] : fs )
            {
                ImGui::CheckboxFlags( name, &devmode->debugShowFlags, f );
            }
            ImGui::TreePop();
        }

        ImGui::Dummy( ImVec2( 0, 4 ) );
        ImGui::Separator();
        ImGui::Dummy( ImVec2( 0, 4 ) );

        if( ImGui::TreeNode( "Camera" ) )
        {
            auto& modifiers = devmode->cameraOvrd;

            ImGui::Checkbox( "FOV Override", &modifiers.fovEnable );
            {
                ImGui::BeginDisabled( !modifiers.fovEnable );
                ImGui::SliderFloat( "Vertical FOV", &modifiers.fovDeg, 10, 120, "%.0f degrees" );
                ImGui::EndDisabled();
            }

            ImGui::Checkbox( "Freelook", &modifiers.customEnable );
            ImGui::TextUnformatted(
                "Freelook:\n"
                "    * WASD - to move\n"
                "    * Alt - hold to rotate\n"
                "NOTE: inputs are read only from this window, and not from the game's one" );
            if( modifiers.customEnable )
            {
                if( ImGui::IsKeyPressed( ImGuiKey_LeftAlt ) )
                {
                    if( ImGui::IsMousePosValid() )
                    {
                        modifiers.intr_lastMouse  = { ImGui::GetMousePos().x,
                                                      ImGui::GetMousePos().y };
                        modifiers.intr_lastAngles = modifiers.customAngles;
                    }
                }
                if( ImGui::IsKeyReleased( ImGuiKey_LeftAlt ) )
                {
                    modifiers.intr_lastMouse  = {};
                    modifiers.intr_lastAngles = modifiers.customAngles;
                }

                if( modifiers.intr_lastMouse && ImGui::IsMousePosValid() )
                {
                    modifiers.customAngles = {
                        modifiers.intr_lastAngles.data[ 0 ] -
                            ( ImGui::GetMousePos().x - modifiers.intr_lastMouse->data[ 0 ] ),
                        modifiers.intr_lastAngles.data[ 1 ] -
                            ( ImGui::GetMousePos().y - modifiers.intr_lastMouse->data[ 1 ] ),
                    };
                }
                else
                {
                    modifiers.intr_lastMouse  = {};
                    modifiers.intr_lastAngles = modifiers.customAngles;
                }

                {
                    float speed = 0.1f * sceneImportExport->GetWorldScale();

                    RgFloat3D up, right;
                    Matrix::MakeUpRightFrom( up,
                                             right,
                                             Utils::DegToRad( modifiers.customAngles.data[ 0 ] ),
                                             Utils::DegToRad( modifiers.customAngles.data[ 1 ] ),
                                             sceneImportExport->GetWorldUp(),
                                             sceneImportExport->GetWorldRight() );
                    RgFloat3D fwd = Utils::Cross( up, right );

                    auto fma = []( const RgFloat3D& a, float mult, const RgFloat3D& b ) {
                        return RgFloat3D{ a.data[ 0 ] + mult * b.data[ 0 ],
                                          a.data[ 1 ] + mult * b.data[ 1 ],
                                          a.data[ 2 ] + mult * b.data[ 2 ] };
                    };

                    modifiers.customPos = fma(
                        modifiers.customPos, ImGui::IsKeyDown( ImGuiKey_A ) ? -speed : 0, right );
                    modifiers.customPos = fma(
                        modifiers.customPos, ImGui::IsKeyDown( ImGuiKey_D ) ? +speed : 0, right );
                    modifiers.customPos = fma(
                        modifiers.customPos, ImGui::IsKeyDown( ImGuiKey_W ) ? +speed : 0, fwd );
                    modifiers.customPos = fma(
                        modifiers.customPos, ImGui::IsKeyDown( ImGuiKey_S ) ? -speed : 0, fwd );
                }
            }
            ImGui::TreePop();
        }

        ImGui::Dummy( ImVec2( 0, 4 ) );
        ImGui::Separator();
        ImGui::Dummy( ImVec2( 0, 4 ) );
        devmode->breakOnTexture[ std::size( devmode->breakOnTexture ) - 1 ] = '\0';
        ImGui::TextUnformatted( "Debug break on texture: " );
        ImGui::Checkbox( "Image upload", &devmode->breakOnTextureImage );
        ImGui::Checkbox( "Primitive upload", &devmode->breakOnTexturePrimitive );
        ImGui::InputText( "##Debug break on texture text",
                          devmode->breakOnTexture,
                          std::size( devmode->breakOnTexture ) );

        ImGui::Dummy( ImVec2( 0, 4 ) );
        ImGui::Separator();
        ImGui::Dummy( ImVec2( 0, 4 ) );

        ImGui::Checkbox( "Always on top", &devmode->debugWindowOnTop );
        debugWindows->SetAlwaysOnTop( devmode->debugWindowOnTop );

        ImGui::Text( "%.3f ms/frame (%.1f FPS)",
                     1000.0f / ImGui::GetIO().Framerate,
                     ImGui::GetIO().Framerate );
        ImGui::EndTabItem();

        ImGui::Text( "Chosen volumetric light: %d",
                     uniform->GetData()->volumeLightSourceIndex == LIGHT_INDEX_NONE
                         ? -1
                         : uniform->GetData()->volumeLightSourceIndex );
    }

    if( ImGui::BeginTabItem( "Primitives" ) )
    {
        ImGui::Checkbox( "Ignore external geometry", &devmode->ignoreExternalGeometry );
        ImGui::Dummy( ImVec2( 0, 4 ) );
        ImGui::Separator();
        ImGui::Dummy( ImVec2( 0, 4 ) );

        using PrimMode = Devmode::DebugPrimMode;

        int*     modePtr = reinterpret_cast< int* >( &devmode->primitivesTableMode );
        PrimMode mode    = devmode->primitivesTableMode;

        ImGui::TextUnformatted( "Record: " );
        ImGui::SameLine();
        ImGui::RadioButton( "None", modePtr, static_cast< int >( PrimMode::None ) );
        ImGui::SameLine();
        ImGui::RadioButton( "Ray-traced", modePtr, static_cast< int >( PrimMode::RayTraced ) );
        ImGui::SameLine();
        ImGui::RadioButton( "Rasterized", modePtr, static_cast< int >( PrimMode::Rasterized ) );
        ImGui::SameLine();
        ImGui::RadioButton( "Non-world", modePtr, static_cast< int >( PrimMode::NonWorld ) );
        ImGui::SameLine();
        ImGui::RadioButton( "Decals", modePtr, static_cast< int >( PrimMode::Decal ) );

        ImGui::TextUnformatted(
            "Red    - if exportable, but not found in GLTF, so uploading as dynamic" );
        ImGui::TextUnformatted( "Green  - if exportable was found in GLTF" );

        if( ImGui::BeginTable( "Primitives table",
                               6,
                               ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Resizable |
                                   ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti |
                                   ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                                   ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY ) )
        {
            {
                ImGui::TableSetupColumn( "Call",
                                         ImGuiTableColumnFlags_NoHeaderWidth |
                                             ImGuiTableColumnFlags_DefaultSort );
                ImGui::TableSetupColumn( "Object ID", ImGuiTableColumnFlags_NoHeaderWidth );
                ImGui::TableSetupColumn( "Mesh name", ImGuiTableColumnFlags_NoHeaderWidth );
                ImGui::TableSetupColumn( "Primitive index", ImGuiTableColumnFlags_NoHeaderWidth );
                ImGui::TableSetupColumn( "Primitive name", ImGuiTableColumnFlags_NoHeaderWidth );
                ImGui::TableSetupColumn( "Texture",
                                         ImGuiTableColumnFlags_NoHeaderWidth |
                                             ImGuiTableColumnFlags_WidthStretch );
                ImGui::TableHeadersRow();
                if( ImGui::IsItemHovered() )
                {
                    ImGui::SetTooltip(
                        "Right-click to open menu\nMiddle-click to copy texture name" );
                }
            }

            if( ImGuiTableSortSpecs* sortspecs = ImGui::TableGetSortSpecs() )
            {
                sortspecs->SpecsDirty = true;

                std::ranges::sort(
                    devmode->primitivesTable,
                    [ sortspecs ]( const Devmode::DebugPrim& a,
                                   const Devmode::DebugPrim& b ) -> bool {
                        for( int n = 0; n < sortspecs->SpecsCount; n++ )
                        {
                            const ImGuiTableColumnSortSpecs* srt = &sortspecs->Specs[ n ];

                            std::strong_ordering ord{ 0 };
                            switch( srt->ColumnIndex )
                            {
                                case 0: ord = ( a.callIndex <=> b.callIndex ); break;
                                case 1: ord = ( a.objectId <=> b.objectId ); break;
                                case 2: ord = ( a.meshName <=> b.meshName ); break;
                                case 3: ord = ( a.primitiveIndex <=> b.primitiveIndex ); break;
                                case 4: ord = ( a.primitiveName <=> b.primitiveName ); break;
                                case 5: ord = ( a.textureName <=> b.textureName ); break;
                                default: assert( 0 ); return false;
                            }

                            if( std::is_gt( ord ) )
                            {
                                return srt->SortDirection != ImGuiSortDirection_Ascending;
                            }

                            if( std::is_lt( ord ) )
                            {
                                return srt->SortDirection == ImGuiSortDirection_Ascending;
                            }
                        }

                        return a.callIndex < b.callIndex;
                    } );
            }

            ImGuiListClipper clipper;
            clipper.Begin( int( devmode->primitivesTable.size() ) );
            while( clipper.Step() )
            {
                for( int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++ )
                {
                    const auto& prim = devmode->primitivesTable[ i ];
                    ImGui::TableNextRow();

                    if( prim.result == UploadResult::ExportableStatic )
                    {
                        ImGui::TableSetBgColor( ImGuiTableBgTarget_RowBg0,
                                                IM_COL32( 0, 128, 0, 64 ) );
                        ImGui::TableSetBgColor( ImGuiTableBgTarget_RowBg1,
                                                IM_COL32( 0, 128, 0, 128 ) );
                    }
                    else if( prim.result == UploadResult::ExportableDynamic )
                    {
                        ImGui::TableSetBgColor( ImGuiTableBgTarget_RowBg0,
                                                IM_COL32( 128, 0, 0, 64 ) );
                        ImGui::TableSetBgColor( ImGuiTableBgTarget_RowBg1,
                                                IM_COL32( 128, 0, 0, 128 ) );
                    }
                    else
                    {
                        ImGui::TableSetBgColor( ImGuiTableBgTarget_RowBg0, IM_COL32( 0, 0, 0, 1 ) );
                        ImGui::TableSetBgColor( ImGuiTableBgTarget_RowBg1, IM_COL32( 0, 0, 0, 1 ) );
                    }


                    ImGui::TableNextColumn();
                    if( prim.result != UploadResult::Fail )
                    {
                        ImGui::Text( "%u", prim.callIndex );
                    }
                    else
                    {
                        ImGui::TextUnformatted( "fail" );
                    }

                    ImGui::TableNextColumn();
                    if( mode != PrimMode::Decal && mode != PrimMode::NonWorld )
                    {
                        ImGui::Text( "%llu", prim.objectId );
                    }

                    ImGui::TableNextColumn();
                    if( mode != PrimMode::Decal && mode != PrimMode::NonWorld )
                    {
                        ImGui::TextUnformatted( prim.meshName.c_str() );
                    }

                    ImGui::TableNextColumn();
                    if( mode != PrimMode::Decal )
                    {
                        ImGui::Text( "%u", prim.primitiveIndex );
                    }

                    ImGui::TableNextColumn();
                    if( mode != PrimMode::Decal )
                    {
                        ImGui::TextUnformatted( prim.primitiveName.c_str() );
                    }

                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted( prim.textureName.c_str() );
                    if( ImGui::IsMouseReleased( ImGuiMouseButton_Middle ) &&
                        ImGui::IsItemHovered() )
                    {
                        ImGui::SetClipboardText( prim.textureName.c_str() );
                    }
                    else
                    {
                        if( ImGui::BeginPopupContextItem( std::format( "##popup{}", i ).c_str() ) )
                        {
                            if( ImGui::MenuItem( "Copy texture name" ) )
                            {
                                ImGui::SetClipboardText( prim.textureName.c_str() );
                                ImGui::CloseCurrentPopup();
                            }
                            ImGui::EndPopup();
                        }
                    }
                }
            }

            ImGui::EndTable();
        }
        ImGui::EndTabItem();
    }

    if( ImGui::BeginTabItem( "Log" ) )
    {
        ImGui::Checkbox( "Auto-scroll", &devmode->logAutoScroll );
        ImGui::SameLine();
        if( ImGui::Button( "Clear" ) )
        {
            devmode->logs.clear();
        }
        ImGui::Separator();

        ImGui::CheckboxFlags( "Errors", &devmode->logFlags, RG_MESSAGE_SEVERITY_ERROR );
        ImGui::SameLine();
        ImGui::CheckboxFlags( "Warnings", &devmode->logFlags, RG_MESSAGE_SEVERITY_WARNING );
        ImGui::SameLine();
        ImGui::CheckboxFlags( "Info", &devmode->logFlags, RG_MESSAGE_SEVERITY_INFO );
        ImGui::SameLine();
        ImGui::CheckboxFlags( "Verbose", &devmode->logFlags, RG_MESSAGE_SEVERITY_VERBOSE );
        ImGui::Separator();

        if( ImGui::BeginChild( "##LogScrollingRegion",
                               ImVec2( 0, 0 ),
                               false,
                               ImGuiWindowFlags_HorizontalScrollbar ) )
        {
            for( const auto& [ severity, count, text ] : devmode->logs )
            {
                RgMessageSeverityFlags filtered = severity & devmode->logFlags;

                if( filtered == 0 )
                {
                    continue;
                }

                std::optional< ImU32 > color;
                if( filtered & RG_MESSAGE_SEVERITY_ERROR )
                {
                    color = IM_COL32( 255, 0, 0, 255 );
                }
                else if( filtered & RG_MESSAGE_SEVERITY_WARNING )
                {
                    color = IM_COL32( 255, 255, 0, 255 );
                }

                if( color )
                {
                    ImGui::PushStyleColor( ImGuiCol_Text, *color );
                }

                if( count == 1 )
                {
                    ImGui::TextUnformatted( text.data() );
                }
                else
                {
                    ImGui::Text( "[%u] %s", count, text.data() );
                }

                if( color )
                {
                    ImGui::PopStyleColor();
                }
            }

            if( devmode->logAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() )
            {
                ImGui::SetScrollHereY( 1.0f );
            }
        }
        ImGui::EndChild();
        ImGui::EndTabItem();
    }

    if( ImGui::BeginTabItem( "Import/Export" ) )
    {
        auto& dev = sceneImportExport->dev;
        if( !dev.exportName.enable )
        {
            dev.exportName.SetDefaults( *sceneImportExport );
        }
        if( !dev.importName.enable )
        {
            dev.importName.SetDefaults( *sceneImportExport );
        }
        if( !dev.worldTransform.enable )
        {
            dev.worldTransform.SetDefaults( *sceneImportExport );
        }

        {
            ImGui::Text( "Resource folder: %s",
                         std::filesystem::absolute( ovrdFolder ).string().c_str() );
        }
        ImGui::Separator();
        ImGui::Dummy( ImVec2( 0, 16 ) );
        {
            ImGui::BeginDisabled( dev.buttonRecording );
            if( ImGui::Button( "Reimport replacements GLTF", { -1, 80 } ) )
            {
                sceneImportExport->RequestReplacementsReimport();
            }
            ImGui::Dummy( ImVec2( 0, 8 ) );
            if( ImGui::Button( "Reimport map GLTF", { -1, 80 } ) )
            {
                sceneImportExport->RequestReimport();
            }

            ImGui::Text( "Map import path: %s",
                         sceneImportExport->dev_GetSceneImportGltfPath().c_str() );
            ImGui::BeginDisabled( !dev.importName.enable );
            {
                ImGui::InputText(
                    "Import map name", dev.importName.value, std::size( dev.importName.value ) );
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::Checkbox( "Custom##import", &dev.importName.enable );
            ImGui::EndDisabled();
        }
        ImGui::Dummy( ImVec2( 0, 16 ) );
        ImGui::Separator();
        ImGui::Dummy( ImVec2( 0, 16 ) );
        {
            ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.98f, 0.59f, 0.26f, 0.40f ) );
            ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 0.98f, 0.59f, 0.26f, 1.00f ) );
            ImGui::PushStyleColor( ImGuiCol_ButtonActive, ImVec4( 0.98f, 0.53f, 0.06f, 1.00f ) );
            {
                const auto halfWidth = ( ImGui::GetContentRegionAvail().x -
                                         ImGui::GetCurrentWindow()->WindowPadding.x ) *
                                       0.5f;
                if( dev.buttonRecording )
                {
                    ImGui::BeginDisabled( true );
                    ImGui::Button( "Replacements are being recorded...", { halfWidth, 80 } );
                    ImGui::EndDisabled();
                }
                else
                {
                    if( ImGui::Button( "Export replacements GLTF\n from this frame",
                                       { halfWidth, 80 } ) )
                    {
                        sceneImportExport->RequestReplacementsExport_OneFrame();
                    }
                }
                ImGui::SameLine();
                if( dev.buttonRecording )
                {
                    if( ImGui::Button( "Stop recording\nand Export into GLTF", { halfWidth, 80 } ) )
                    {
                        sceneImportExport->RequestReplacementsExport_RecordEnd();
                        dev.buttonRecording = false;
                    }
                }
                else
                {
                    if( ImGui::Button( "Start recording\nreplacements into GLTF",
                                       { halfWidth, 80 } ) )
                    {
                        sceneImportExport->RequestReplacementsExport_RecordBegin();
                        dev.buttonRecording = true;
                    }
                }
            }
            ImGui::BeginDisabled( dev.buttonRecording );
            ImGui::Checkbox( "Allow export of existing replacements",
                             &devmode->allowExportOfExistingReplacements );
            ImGui::Dummy( ImVec2( 0, 16 ) );
            if( ImGui::Button( "Export map GLTF", { -1, 80 } ) )
            {
                sceneImportExport->RequestExport();
            }
            ImGui::PopStyleColor( 3 );
            ImGui::Checkbox( "Allow auto-export, if scene's GLTF doesn't exist",
                             &devmode->drawInfoOvrd.allowMapAutoExport );
            ImGui::Dummy( ImVec2( 0, 8 ) );
            ImGui::Text( "Export path: %s",
                         sceneImportExport->dev_GetSceneExportGltfPath().c_str() );
            ImGui::BeginDisabled( !dev.exportName.enable );
            {
                ImGui::InputText(
                    "Export map name", dev.exportName.value, std::size( dev.exportName.value ) );
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::Checkbox( "Custom##export", &dev.exportName.enable );
            ImGui::EndDisabled();
        }
        ImGui::Dummy( ImVec2( 0, 16 ) );
        ImGui::Separator();
        ImGui::Dummy( ImVec2( 0, 16 ) );
        {
            ImGui::BeginDisabled( dev.buttonRecording );
            ImGui::Checkbox( "Custom import/export world space", &dev.worldTransform.enable );
            ImGui::BeginDisabled( !dev.worldTransform.enable );
            {
                ImGui::SliderFloat3( "World Up vector", dev.worldTransform.up.data, -1.0f, 1.0f );
                ImGui::SliderFloat3(
                    "World Forward vector", dev.worldTransform.forward.data, -1.0f, 1.0f );
                ImGui::InputFloat(
                    std::format( "1 unit = {} meters", dev.worldTransform.scale ).c_str(),
                    &dev.worldTransform.scale );
            }
            ImGui::EndDisabled();
            ImGui::EndDisabled();
        }
        ImGui::EndTabItem();
    }

    if( ImGui::BeginTabItem( "Textures" ) )
    {
        if( ImGui::Button( "Export original textures", { -1, 80 } ) )
        {
            textureManager->ExportOriginalMaterialTextures( ovrdFolder /
                                                            TEXTURES_FOLDER_ORIGINALS );
        }
        ImGui::Text( "Export path: %s",
                     ( ovrdFolder / TEXTURES_FOLDER_ORIGINALS ).string().c_str() );
        ImGui::Dummy( ImVec2( 0, 16 ) );
        ImGui::Separator();
        ImGui::Dummy( ImVec2( 0, 16 ) );

        enum
        {
            ColumnTextureIndex0,
            ColumnTextureIndex1,
            ColumnTextureIndex2,
            ColumnTextureIndex3,
            ColumnTextureIndex4,
            ColumnMaterialName,
            Column_Count,
        };
        static_assert( std::size( TextureManager::Debug_MaterialInfo{}.textures.indices ) == 5 );

        ImGui::Checkbox( "Record", &devmode->materialsTableEnable );
        ImGui::TextUnformatted( "Blue - if material is non-original (i.e. was loaded from GLTF)" );
        if( ImGui::BeginTable( "Materials table",
                               Column_Count,
                               ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Resizable |
                                   ImGuiTableFlags_Sortable | ImGuiTableFlags_SortMulti |
                                   ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                                   ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY ) )
        {
            auto materialInfos = devmode->materialsTableEnable
                                     ? textureManager->Debug_GetMaterials()
                                     : std::vector< TextureManager::Debug_MaterialInfo >{};
            {
                ImGui::TableSetupColumn( "A", 0, 8 );
                ImGui::TableSetupColumn( "P", 0, 8 );
                ImGui::TableSetupColumn( "N", 0, 8 );
                ImGui::TableSetupColumn( "E", 0, 8 );
                ImGui::TableSetupColumn( "H", 0, 8 );
                ImGui::TableSetupColumn( "Material name",
                                         ImGuiTableColumnFlags_WidthStretch |
                                             ImGuiTableColumnFlags_DefaultSort,
                                         -1 );
                ImGui::TableHeadersRow();
                if( ImGui::IsItemHovered() )
                {
                    ImGui::SetTooltip(
                        "Right-click to open menu\nMiddle-click to copy texture name" );
                }
            }

            if( ImGuiTableSortSpecs* sortspecs = ImGui::TableGetSortSpecs() )
            {
                sortspecs->SpecsDirty = true;

                std::ranges::sort(
                    materialInfos,
                    [ sortspecs ]( const TextureManager::Debug_MaterialInfo& a,
                                   const TextureManager::Debug_MaterialInfo& b ) -> bool {
                        for( int n = 0; n < sortspecs->SpecsCount; n++ )
                        {
                            const ImGuiTableColumnSortSpecs* srt = &sortspecs->Specs[ n ];

                            std::strong_ordering ord{ 0 };
                            switch( srt->ColumnIndex )
                            {
                                case ColumnTextureIndex0:
                                    ord = ( a.textures.indices[ 0 ] <=> b.textures.indices[ 0 ] );
                                    break;
                                case ColumnTextureIndex1:
                                    ord = ( a.textures.indices[ 1 ] <=> b.textures.indices[ 1 ] );
                                    break;
                                case ColumnTextureIndex2:
                                    ord = ( a.textures.indices[ 2 ] <=> b.textures.indices[ 2 ] );
                                    break;
                                case ColumnTextureIndex3:
                                    ord = ( a.textures.indices[ 3 ] <=> b.textures.indices[ 3 ] );
                                    break;
                                case ColumnTextureIndex4:
                                    ord = ( a.textures.indices[ 4 ] <=> b.textures.indices[ 4 ] );
                                    break;
                                case ColumnMaterialName:
                                    ord = ( a.materialName <=> b.materialName );
                                    break;
                                default: continue;
                            }

                            if( std::is_gt( ord ) )
                            {
                                return srt->SortDirection != ImGuiSortDirection_Ascending;
                            }

                            if( std::is_lt( ord ) )
                            {
                                return srt->SortDirection == ImGuiSortDirection_Ascending;
                            }
                        }

                        return a.materialName < b.materialName;
                    } );
            }

            ImGuiListClipper clipper;
            clipper.Begin( int( materialInfos.size() ) );
            while( clipper.Step() )
            {
                for( int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++ )
                {
                    const auto& mat = materialInfos[ i ];
                    ImGui::TableNextRow();
                    ImGui::PushID( i );

                    if( mat.isOriginal )
                    {
                        ImGui::TableSetBgColor( ImGuiTableBgTarget_RowBg0,
                                                IM_COL32( 0, 0, 128, 64 ) );
                        ImGui::TableSetBgColor( ImGuiTableBgTarget_RowBg1,
                                                IM_COL32( 0, 0, 128, 128 ) );
                    }
                    else
                    {
                        ImGui::TableSetBgColor( ImGuiTableBgTarget_RowBg0, IM_COL32( 0, 0, 0, 1 ) );
                        ImGui::TableSetBgColor( ImGuiTableBgTarget_RowBg1, IM_COL32( 0, 0, 0, 1 ) );
                    }

                    auto writeTexIndex = [ &mat ]( int channel ) {
                        assert( channel >= 0 && channel < std::size( mat.textures.indices ) );
                        if( mat.textures.indices[ channel ] != EMPTY_TEXTURE_INDEX )
                        {
                            ImGui::Text( "%u", mat.textures.indices[ channel ] );
                        }
                    };

                    for( auto col = 0; col < Column_Count; col++ )
                    {
                        ImGui::TableNextColumn();

                        switch( col )
                        {
                            case ColumnTextureIndex0:
                                writeTexIndex( 0 );
                                if( ImGui::TableGetColumnFlags( col ) &
                                    ImGuiTableColumnFlags_IsHovered )
                                {
                                    ImGui::SetTooltip( "Image\n[RGB]Albedo\n[A] "
                                                       "Alpha (0.0 - fully transparent)" );
                                }
                                break;

                            case ColumnTextureIndex1:
                                writeTexIndex( 1 );
                                if( ImGui::TableGetColumnFlags( col ) &
                                    ImGuiTableColumnFlags_IsHovered )
                                {
                                    ImGui::SetTooltip(
                                        "Image\n[R]Occlusion (disabled by default)\n[G] "
                                        "Roughness\n[B] Metallic" );
                                }
                                break;

                            case ColumnTextureIndex2:
                                writeTexIndex( 2 );
                                if( ImGui::TableGetColumnFlags( col ) &
                                    ImGuiTableColumnFlags_IsHovered )
                                {
                                    ImGui::SetTooltip(
                                        "Image\n[R] Normal X offset\n[G] Normal Y offset" );
                                }
                                break;

                            case ColumnTextureIndex3:
                                writeTexIndex( 3 );
                                if( ImGui::TableGetColumnFlags( col ) &
                                    ImGuiTableColumnFlags_IsHovered )
                                {
                                    ImGui::SetTooltip( "Image\n[RGB] Emission color" );
                                }
                                break;

                            case ColumnTextureIndex4:
                                writeTexIndex( 4 );
                                if( ImGui::TableGetColumnFlags( col ) &
                                    ImGuiTableColumnFlags_IsHovered )
                                {
                                    ImGui::SetTooltip( "Image\n[R] Height map\n"
                                                       "    0.0 - deepest point\n"
                                                       "    1.0 - surface level" );
                                }
                                break;

                            case ColumnMaterialName:
                                ImGui::TextUnformatted( mat.materialName.c_str() );

                                if( ImGui::IsMouseReleased( ImGuiMouseButton_Middle ) &&
                                    ImGui::IsItemHovered() )
                                {
                                    ImGui::SetClipboardText( mat.materialName.c_str() );
                                }
                                else
                                {
                                    if( ImGui::BeginPopupContextItem(
                                            std::format( "##popup{}", i ).c_str() ) )
                                    {
                                        if( ImGui::MenuItem( "Copy texture name" ) )
                                        {
                                            ImGui::SetClipboardText( mat.materialName.c_str() );
                                            ImGui::CloseCurrentPopup();
                                        }
                                        ImGui::EndPopup();
                                    }
                                }
                                break;

                            default: break;
                        }
                    }

                    ImGui::PopID();
                }
            }

            ImGui::EndTable();
        }
        ImGui::EndTabItem();
    }
}

void RTGL1::VulkanDevice::Dev_Override( RgStartFrameInfo&                   info,
                                        RgStartFrameRenderResolutionParams& resolution,
                                        RgStartFrameFluidParams&            fluid ) const
{
    if( !Dev_IsDevmodeInitialized() )
    {
        return;
    }

    auto& modifiers = devmode->drawInfoOvrd;

    if( modifiers.enable )
    {
        RgStartFrameInfo&                   dst       = info;
        RgStartFrameRenderResolutionParams& dst_resol = resolution;
        RgStartFrameFluidParams&            dst_fluid = fluid;

        // apply modifiers
        {
            dst.vsync                  = modifiers.vsync;
            dst.hdr                    = modifiers.hdr;
            dst.allowMapAutoExport     = modifiers.allowMapAutoExport;
            dst.lightmapScreenCoverage = modifiers.lightmapScreenCoverage;
        }
        {
            dst_fluid.enabled = modifiers.fluidEnabled;
            dst_fluid.reset   = modifiers.fluidReset;
            dst_fluid.gravity = modifiers.fluidGravity;
        }
        {
            float aspect = float( renderResolution.UpscaledWidth() ) /
                           float( renderResolution.UpscaledHeight() );

            dst_resol.upscaleTechnique  = modifiers.upscaleTechnique;
            dst_resol.resolutionMode    = modifiers.resolutionMode;
            dst_resol.frameGeneration   = modifiers.frameGeneration;
            dst_resol.preferDxgiPresent = modifiers.preferDxgiPresent;
            dst_resol.sharpenTechnique  = modifiers.sharpenTechnique;
            dst_resol.customRenderSize  = {
                ClampPix< uint32_t >( modifiers.customRenderSizeScale *
                                      float( renderResolution.UpscaledWidth() ) ),
                ClampPix< uint32_t >( modifiers.customRenderSizeScale *
                                      float( renderResolution.UpscaledHeight() ) ),
            };
            dst_resol.pixelizedRenderSizeEnable = modifiers.pixelizedEnable;
            dst_resol.pixelizedRenderSize       = {
                ClampPix< uint32_t >(
                    static_cast< uint32_t >( aspect * float( modifiers.pixelizedHeight ) ) ),
                ClampPix< uint32_t >( modifiers.pixelizedHeight ),
            };
        }
    }
    else
    {
        const RgStartFrameInfo&                   src       = info;
        const RgStartFrameRenderResolutionParams& src_resol = resolution;
        const RgStartFrameFluidParams&            src_fluid = fluid;

        // reset modifiers
        {
            modifiers.vsync                  = src.vsync;
            modifiers.hdr                    = src.hdr;
            modifiers.allowMapAutoExport     = src.allowMapAutoExport;
            modifiers.lightmapScreenCoverage = src.lightmapScreenCoverage;
        }
        {
            modifiers.fluidEnabled = src_fluid.enabled;
            modifiers.fluidReset   = src_fluid.reset;
            modifiers.fluidGravity = src_fluid.gravity;
        }
        {
            modifiers.upscaleTechnique  = src_resol.upscaleTechnique;
            modifiers.resolutionMode    = src_resol.resolutionMode;
            modifiers.frameGeneration   = src_resol.frameGeneration;
            modifiers.preferDxgiPresent = src_resol.preferDxgiPresent;
            modifiers.sharpenTechnique  = src_resol.sharpenTechnique;

            if( modifiers.resolutionMode == RG_RENDER_RESOLUTION_MODE_CUSTOM )
            {
                modifiers.customRenderSizeScale = float( src_resol.customRenderSize.height ) /
                                                  float( renderResolution.UpscaledHeight() );
            }
            else
            {
                modifiers.customRenderSizeScale = 1.0f;
            }

            modifiers.pixelizedEnable = src_resol.pixelizedRenderSizeEnable;
            modifiers.pixelizedHeight =
                src_resol.pixelizedRenderSizeEnable
                    ? ClampPix< int >( src_resol.pixelizedRenderSize.height )
                    : 0;
        }
    }
}

void RTGL1::VulkanDevice::Dev_Override( RgCameraInfo& info ) const
{
    if( !Dev_IsDevmodeInitialized() )
    {
        assert( 0 );
        return;
    }

    auto& modifiers = devmode->cameraOvrd;

    if( modifiers.fovEnable )
    {
        info.fovYRadians = Utils::DegToRad( modifiers.fovDeg );
    }
    else
    {
        modifiers.fovDeg = Utils::RadToDeg( info.fovYRadians );
    }

    if( modifiers.customEnable )
    {
        RgCameraInfo& dst_camera = info;

        dst_camera.position = modifiers.customPos;
        Matrix::MakeUpRightFrom( dst_camera.up,
                                 dst_camera.right,
                                 Utils::DegToRad( modifiers.customAngles.data[ 0 ] ),
                                 Utils::DegToRad( modifiers.customAngles.data[ 1 ] ),
                                 sceneImportExport->GetWorldUp(),
                                 sceneImportExport->GetWorldRight() );
    }
    else
    {
        const RgCameraInfo& src_camera = info;

        modifiers.customPos    = src_camera.position;
        modifiers.customAngles = { 0, 0 };
    }
}

void RTGL1::VulkanDevice::Dev_Override( RgDrawFrameIlluminationParams& illumination,
                                        RgDrawFrameTonemappingParams&  tonemappingp,
                                        RgDrawFrameTexturesParams&     textures ) const
{
    if( !Dev_IsDevmodeInitialized() )
    {
        return;
    }

    auto& modifiers = devmode->drawInfoOvrd;

    if( modifiers.enable )
    {
        RgDrawFrameIlluminationParams& dst_illum = illumination;
        RgDrawFrameTonemappingParams&  dst_tnmp  = tonemappingp;
        RgDrawFrameTexturesParams&     dst_tex   = textures;

        // apply modifiers
        {
            dst_illum.maxBounceShadows                 = modifiers.maxBounceShadows;
            dst_illum.enableSecondBounceForIndirect    = modifiers.enableSecondBounceForIndirect;
            dst_illum.directDiffuseSensitivityToChange = modifiers.directDiffuseSensitivityToChange;
            dst_illum.indirectDiffuseSensitivityToChange =
                modifiers.indirectDiffuseSensitivityToChange;
            dst_illum.specularSensitivityToChange = modifiers.specularSensitivityToChange;
        }
        {
            dst_tnmp.disableEyeAdaptation = modifiers.disableEyeAdaptation;
            dst_tnmp.ev100Min             = modifiers.ev100Min;
            dst_tnmp.ev100Max             = modifiers.ev100Max;
            dst_tnmp.saturation           = { RG_ACCESS_VEC3( modifiers.saturation ) };
            dst_tnmp.crosstalk            = { RG_ACCESS_VEC3( modifiers.crosstalk ) };
        }
        {
            dst_tex.normalMapStrength      = modifiers.normalMapStrength;
            dst_tex.heightMapDepth         = modifiers.heightMapDepth;
            dst_tex.emissionMapBoost       = modifiers.emissionMapBoost;
            dst_tex.emissionMaxScreenColor = modifiers.emissionMaxScreenColor;
        }
    }
    else
    {
        const RgDrawFrameIlluminationParams& src_illum = illumination;
        const RgDrawFrameTonemappingParams&  src_tnmp  = tonemappingp;
        const RgDrawFrameTexturesParams&     src_tex   = textures;

        // reset modifiers
        {
            devmode->antiFirefly = true;
        }
        {
            modifiers.maxBounceShadows                 = int( src_illum.maxBounceShadows );
            modifiers.enableSecondBounceForIndirect    = src_illum.enableSecondBounceForIndirect;
            modifiers.directDiffuseSensitivityToChange = src_illum.directDiffuseSensitivityToChange;
            modifiers.indirectDiffuseSensitivityToChange =
                src_illum.indirectDiffuseSensitivityToChange;
            modifiers.specularSensitivityToChange = src_illum.specularSensitivityToChange;
        }
        {
            modifiers.disableEyeAdaptation = src_tnmp.disableEyeAdaptation;
            modifiers.ev100Min             = src_tnmp.ev100Min;
            modifiers.ev100Max             = src_tnmp.ev100Max;
            RG_SET_VEC3_A( modifiers.saturation, src_tnmp.saturation.data );
            RG_SET_VEC3_A( modifiers.crosstalk, src_tnmp.crosstalk.data );
        }
        {
            modifiers.normalMapStrength      = src_tex.normalMapStrength;
            modifiers.heightMapDepth         = src_tex.heightMapDepth;
            modifiers.emissionMapBoost       = src_tex.emissionMapBoost;
            modifiers.emissionMaxScreenColor = src_tex.emissionMaxScreenColor;
        }
    }
}

void RTGL1::VulkanDevice::Dev_TryBreak( const char* pTextureName, bool isImageUpload )
{
#ifdef _MSC_VER
    if( !devmode )
    {
        return;
    }

    if( isImageUpload )
    {
        if( !devmode->breakOnTextureImage )
        {
            return;
        }
    }
    else
    {
        if( !devmode->breakOnTexturePrimitive )
        {
            return;
        }
    }

    if( Utils::IsCstrEmpty( devmode->breakOnTexture ) || Utils::IsCstrEmpty( pTextureName ) )
    {
        return;
    }

    devmode->breakOnTexture[ std::size( devmode->breakOnTexture ) - 1 ] = '\0';
    if( std::strcmp( devmode->breakOnTexture, Utils::SafeCstr( pTextureName ) ) == 0 )
    {
        __debugbreak();
        devmode->breakOnTextureImage     = false;
        devmode->breakOnTexturePrimitive = false;
    }
#endif
}

namespace RTGL1
{
extern bool g_showAutoExportPlaque;
}

void RTGL1::VulkanDevice::DrawEndUserWarnings()
{
    constexpr int   OverallDurationInSeconds = 7;
    constexpr float FadingInSeconds          = 3;

    using clock        = std::chrono::high_resolution_clock;
    static auto stopAt = clock::time_point{};
    static auto ratio  = 0.0f;

    if( g_showAutoExportPlaque )
    {
        stopAt                 = clock::now() + std::chrono::seconds{ OverallDurationInSeconds };
        ratio                  = 1.0f;
        g_showAutoExportPlaque = false;
    }

    if( ratio <= 0 )
    {
        return;
    }

    {
        float diffStop = std::chrono::duration< float >{ stopAt - clock::now() }.count();
        ratio          = std::clamp( diffStop / FadingInSeconds, 0.f, 1.f );
        if( ratio <= 0 )
        {
            return;
        }
    }

    static constexpr RgColor4DPacked32 white       = Utils::PackColor( 255, 255, 255, 255 );
    static constexpr RgPrimitiveVertex quadVerts[] = {
        RgPrimitiveVertex{ .position = { -1, -1, 0 }, .texCoord = { 0, 0 }, .color = white },
        RgPrimitiveVertex{ .position = { -1, +1, 0 }, .texCoord = { 0, 1 }, .color = white },
        RgPrimitiveVertex{ .position = { +1, -1, 0 }, .texCoord = { 1, 0 }, .color = white },
        RgPrimitiveVertex{ .position = { +1, -1, 0 }, .texCoord = { 1, 0 }, .color = white },
        RgPrimitiveVertex{ .position = { -1, +1, 0 }, .texCoord = { 0, 1 }, .color = white },
        RgPrimitiveVertex{ .position = { +1, +1, 0 }, .texCoord = { 1, 1 }, .color = white },
    };

    const float screen[] = {
        static_cast< float >( renderResolution.GetResolutionState().upscaledWidth ),
        static_cast< float >( renderResolution.GetResolutionState().upscaledHeight ),
    };
    // size of MATERIAL_NAME_SCENEBUILDINGWARNING texture
    constexpr float plaque[] = {
        1024,
        256,
    };
    if( screen[ 0 ] < 1 || screen[ 1 ] < 1 )
    {
        return;
    }

    constexpr float safeZoneAt1080 = 96;
    constexpr float heightAt1080   = 128;

    const float safeZone  = safeZoneAt1080 / 1080 * screen[ 1 ];
    const float pixHeight = heightAt1080 / 1080 * screen[ 1 ];
    const float pixWidth  = pixHeight / plaque[ 1 ] * plaque[ 0 ];

    auto vp = RgViewport{
        .x        = screen[ 0 ] / 2 - pixWidth / 2, // at center
        .y        = safeZone,
        .width    = pixWidth,
        .height   = pixHeight,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    static constexpr float identity[] = {
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
    };
    auto sw = RgMeshPrimitiveSwapchainedEXT{
        .sType           = RG_STRUCTURE_TYPE_MESH_PRIMITIVE_SWAPCHAINED_EXT,
        .pViewport       = &vp,
        .pViewProjection = identity,
    };
    auto prim = RgMeshPrimitiveInfo{
        .sType                = RG_STRUCTURE_TYPE_MESH_PRIMITIVE_INFO,
        .pNext                = &sw,
        .flags                = RG_MESH_PRIMITIVE_TRANSLUCENT,
        .primitiveIndexInMesh = 0,
        .pVertices            = quadVerts,
        .vertexCount          = std::size( quadVerts ),
        .pTextureName         = MATERIAL_NAME_SCENEBUILDINGWARNING,
        .color                = Utils::PackColorFromFloat( 1, 1, 1, ratio ),
    };

    auto warnPlaque = RgMeshInfo{
        .sType          = RG_STRUCTURE_TYPE_MESH_INFO,
        .uniqueObjectID = 0,
        .pMeshName      = nullptr,
        .transform      = RG_TRANSFORM_IDENTITY,
    };

    UploadMeshPrimitive( &warnPlaque, &prim );
}
