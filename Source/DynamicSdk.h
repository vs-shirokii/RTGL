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

#include "DebugPrint.h"

#include <Windows.h>

#include <type_traits>

template< typename T >
    requires std::is_trivial_v< T > && requires( T t ) { t.SdkName(); }
struct DynamicSdk final : T
{
    DynamicSdk() = default;
    ~DynamicSdk() { Free(); }

    HMODULE Add( HMODULE dll )
    {
        ++m_requested;
        if( !dll )
        {
            Free();
            return nullptr;
        }
        m_dlls.push_back( dll );
        return dll;
    }

    void Free()
    {
        for( HMODULE dll : m_dlls )
        {
            FreeLibrary( dll );
        }
        T::operator=( T{} );
        m_dlls.clear();
        m_requested = 0;
    }

    bool Valid() const { return !m_dlls.empty() && m_dlls.size() == m_requested; }

    template< typename Func >
    static Func LoadFunction( HMODULE dll, const char* name )
    {
        auto f = reinterpret_cast< Func >( // NOLINT(clang-diagnostic-cast-function-type-strict)
            GetProcAddress( dll, name ) );
        if( !f )
        {
            RTGL1::debug::Error( "[{}] Failed to load DLL function: \'{}\'", T::SdkName(), name );
            return nullptr;
        }
        return f;
    }

    DynamicSdk( DynamicSdk&& ) noexcept = default;
    DynamicSdk& operator=( DynamicSdk&& other ) noexcept
    {
        if( this != &other )
        {
            this->Free();
            T::operator=( std::move( other ) );
            std::swap( this->m_dlls, other.m_dlls ); // NOLINT(bugprone-use-after-move)
            std::swap( this->m_requested, other.m_requested );
        }
        return *this;
    }

    DynamicSdk( const DynamicSdk& )            = delete;
    DynamicSdk& operator=( const DynamicSdk& ) = delete;

private:
    std::vector< HMODULE > m_dlls{};
    uint32_t               m_requested{ 0 };
};


template< typename T >
DynamicSdk< T > OnlyFullyLoaded( DynamicSdk< T >&& other )
{
    if( !other.Valid() )
    {
        return DynamicSdk< T >{};
    }
    return DynamicSdk< T >{ std::move( other ) };
}


#define DYNAMICSDK_DECLARE( func ) decltype( &( func ) ) func

#define DYNAMICSDK_FETCH( dll, func )                                  \
    sdk.func = sdk.LoadFunction< decltype( sdk.func ) >( dll, #func ); \
    if( !sdk.func )                                                    \
        return {}
