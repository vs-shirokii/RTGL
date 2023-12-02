// Copyright (c) 2023 V.Shirokii
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

#include "DebugPrint.h"

namespace RTGL1::HDR
{
enum class DisplayHDRState
{
    Undefined,
    Disabled,
    Enabled,
};
}

#ifdef WIN32
    #include <WinUser.h>

namespace RTGL1::HDR
{

namespace detail
{
    inline std::optional< DISPLAYCONFIG_PATH_INFO > FindDisplay( uint32_t displayId )
    {
        constexpr UINT32 flags = QDC_ONLY_ACTIVE_PATHS | QDC_VIRTUAL_MODE_AWARE;

        auto paths  = std::vector< DISPLAYCONFIG_PATH_INFO >{};
        auto modes  = std::vector< DISPLAYCONFIG_MODE_INFO >{};
        LONG result = ERROR_SUCCESS;

        for( int retry = 0; retry < 32; retry++ )
        {
            // Determine how many path and mode structures to allocate
            UINT32 pathCount, modeCount;
            result = GetDisplayConfigBufferSizes( flags, &pathCount, &modeCount );
            if( result != ERROR_SUCCESS )
            {
                auto hr = HRESULT_FROM_WIN32( result );
                return std::nullopt;
            }
            paths.resize( pathCount );
            modes.resize( modeCount );

            result = QueryDisplayConfig(
                flags, &pathCount, paths.data(), &modeCount, modes.data(), nullptr );

            paths.resize( pathCount );
            modes.resize( modeCount );

            if( result != ERROR_INSUFFICIENT_BUFFER )
            {
                break;
            }
        }

        if( result != ERROR_SUCCESS )
        {
            debug::Info( "HDR::detail::FindDisplay fail: QueryDisplayConfig HRESULT={}",
                         HRESULT_FROM_WIN32( result ) );
            return std::nullopt;
        }

        if( displayId < paths.size() )
        {
            debug::Info( "HDR::detail::FindDisplay found a display with Id={}", displayId );
            return paths[ displayId ];
        }

        debug::Warning( "HDR::detail::FindDisplay fail: Can't find a display with Id={}. Available "
                        "Ids: [0, {}]",
                        displayId,
                        paths.size() );
        return std::nullopt;
    }

    inline void WarnIfForceDisabled( const DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO& info )
    {
        if( info.advancedColorForceDisabled )
        {
            debug::Warning(
                "DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO has advancedColorForceDisabled=1" );
        }
    }
}

inline bool IsSupported( uint32_t displayId )
{
    if( auto path = detail::FindDisplay( displayId ) )
    {
        auto getState = DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO{};
        {
            getState.header.id        = path->targetInfo.id;
            getState.header.type      = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
            getState.header.size      = sizeof( DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO );
            getState.header.adapterId = path->targetInfo.adapterId;
        }

        auto result = DisplayConfigGetDeviceInfo( &getState.header );
        if( result != ERROR_SUCCESS )
        {
            debug::Info( "HDR::IsSupported fail: DisplayConfigGetDeviceInfo for "
                         "DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO "
                         "HRESULT={}",
                         HRESULT_FROM_WIN32( result ) );
            return false;
        }
        detail::WarnIfForceDisabled( getState );

        return bool{ getState.advancedColorSupported != 0 };
    }
    return false;
}

inline DisplayHDRState GetState( uint32_t displayId )
{
    if( auto path = detail::FindDisplay( displayId ) )
    {
        auto getState = DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO{};
        {
            getState.header.id        = path->targetInfo.id;
            getState.header.type      = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
            getState.header.size      = sizeof( DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO );
            getState.header.adapterId = path->targetInfo.adapterId;
        }

        auto result = DisplayConfigGetDeviceInfo( &getState.header );
        if( result != ERROR_SUCCESS )
        {
            debug::Info( "HDR::IsSupported fail: DisplayConfigGetDeviceInfo for "
                         "DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO "
                         "HRESULT={}",
                         HRESULT_FROM_WIN32( result ) );
            return DisplayHDRState::Undefined;
        }
        detail::WarnIfForceDisabled( getState );

        return getState.advancedColorEnabled != 0 ? DisplayHDRState::Enabled
                                                  : DisplayHDRState::Disabled;
    }
    return DisplayHDRState::Undefined;
}

inline bool SetEnabled( uint32_t displayId, bool enable )
{
    if( auto path = detail::FindDisplay( displayId ) )
    {
        auto setState = DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE{};
        {
            setState.header.id        = path->targetInfo.id;
            setState.header.type      = DISPLAYCONFIG_DEVICE_INFO_SET_ADVANCED_COLOR_STATE;
            setState.header.size      = sizeof( DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE );
            setState.header.adapterId = path->targetInfo.adapterId;
        }

        setState.enableAdvancedColor = enable ? 1 : 0;

        auto result = DisplayConfigSetDeviceInfo( &setState.header );
        if( result != ERROR_SUCCESS )
        {
            debug::Info( "HDR::IsSupported fail: DisplayConfigSetDeviceInfo for "
                         "DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE "
                         "HRESULT={}",
                         HRESULT_FROM_WIN32( result ) );
            return false;
        }
        return true;
    }
    return false;
}

}

#else

namespace RTGL1::HDR
{
inline bool IsSupported( uint32_t displayId )
{
    return false;
}
inline DisplayHDRState GetState( uint32_t displayId )
{
    return DisplayHDRState::Undefined;
}
inline bool SetEnabled( uint32_t displayId, bool enable )
{
    return false;
}
}

#endif // WIN32
