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

#pragma once

#include "Containers.h"
#include "IFileDependency.h"

#include <deque>
#include <filesystem>
#include <future>

namespace RTGL1
{

class FolderObserver
{
public:
    explicit FolderObserver( const std::filesystem::path& ovrdFolder );
    ~FolderObserver();

    FolderObserver( const FolderObserver& other )                = delete;
    FolderObserver( FolderObserver&& other ) noexcept            = delete;
    FolderObserver& operator=( const FolderObserver& other )     = delete;
    FolderObserver& operator=( FolderObserver&& other ) noexcept = delete;

    void RecheckFiles();

    void Subscribe( const std::shared_ptr< IFileDependency >& subscriber )
    {
        m_subscribers.emplace_back( subscriber );
    }

private:
    template< typename Func, typename... Args >
    auto CallSubsbribers( Func f, Args&&... args )
    {
        for( auto& ws : m_subscribers )
        {
            if( auto s = ws.lock() )
            {
                ( ( *s ).*f )( std::forward< Args >( args )... );
            }
        }
    }

private:
    std::future< void > m_asyncChecker;
    std::stop_source    m_asyncStopSource;

    std::mutex                                                  m_mutex;
    std::vector< std::pair< FileType, std::filesystem::path > > m_changedFiles;

    std::vector< std::weak_ptr< IFileDependency > > m_subscribers;
};

}
