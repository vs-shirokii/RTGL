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

#include "FolderObserver.h"

#include <future>

#include "Const.h"

namespace fs = std::filesystem;

namespace RTGL1
{
namespace
{
    constexpr auto CHECK_FREQUENCY = std::chrono::milliseconds( 500 );

    using Clock = std::filesystem::file_time_type::clock;


    struct DependentFile
    {
        FileType              type;
        std::filesystem::path path;
        uint64_t              pathHash;
        Clock::time_point     lastWriteTime;

        friend std::strong_ordering operator<=>( const DependentFile& a,
                                                 const DependentFile& b ) noexcept
        {
            // only path
            return a.path <=> b.path;
        }
    };


    void InsertAllFolderFiles( std::deque< DependentFile >& dst, const fs::path& folder )
    {
        if( !fs::exists( folder ) )
        {
            return;
        }

        for( const fs::directory_entry& entry : fs::directory_iterator( folder ) )
        {
            if( entry.is_regular_file() )
            {
                FileType type = MakeFileType( entry.path() );

                if( type != FileType::Unknown )
                {
                    dst.push_back( DependentFile{
                        .type          = type,
                        .path          = entry.path(),
                        .pathHash      = std::hash< fs::path >{}( entry.path() ),
                        .lastWriteTime = entry.last_write_time(),
                    } );
                }
            }
            else if( entry.is_directory() )
            {
                // ignore
                if( entry.path().filename() == TEXTURES_FOLDER_JUNCTION )
                {
                    continue;
                }

                InsertAllFolderFiles( dst, entry.path() );
            }
        }
    }
}
}

RTGL1::FolderObserver::FolderObserver( const fs::path& ovrdFolder )
{
    auto folders = std::vector{
        ovrdFolder / DATABASE_FOLDER,     //
        ovrdFolder / SCENES_FOLDER,       //
        ovrdFolder / SHADERS_FOLDER,      //
        ovrdFolder / TEXTURES_FOLDER,     //
        ovrdFolder / TEXTURES_FOLDER_DEV, //
        ovrdFolder / REPLACEMENTS_FOLDER, //
    };

    m_asyncChecker = std::async(
        std::launch::async,
        [ this ](                                                      //
            const std::vector< std::filesystem::path > foldersToCheck, //
            std::stop_token                            token           //
        ) {
            auto s_lastCheck    = std::optional< Clock::time_point >{};
            auto s_prevAllFiles = std::deque< DependentFile >{};

            while( !token.stop_requested() )
            {
                std::this_thread::sleep_for( CHECK_FREQUENCY );

                auto curAllFiles = std::deque< DependentFile >{};
                auto changed     = std::vector< std::pair< FileType, std::filesystem::path > >{};
                {
                    for( const fs::path& f : foldersToCheck )
                    {
                        InsertAllFolderFiles( curAllFiles, f );
                    }

                    if( s_lastCheck )
                    {
                        for( const auto& cur : curAllFiles )
                        {
                            bool foundInPrev = false;

                            for( const auto& prev : s_prevAllFiles )
                            {
                                // if file previously existed
                                if( cur.pathHash == prev.pathHash && cur.path == prev.path )
                                {
                                    // if was changed
                                    if( cur.lastWriteTime != prev.lastWriteTime )
                                    {
                                        changed.emplace_back( cur.type, cur.path );
                                    }

                                    foundInPrev = true;
                                    break;
                                }
                            }

                            // if new file
                            if( !foundInPrev )
                            {
                                changed.emplace_back( cur.type, cur.path );
                            }
                        }
                    }
                }

                {
                    auto l = std::lock_guard{ this->m_mutex };

                    for( auto& f : changed )
                    {
                        bool alreadyContains =
                            std::ranges::find_if( this->m_changedFiles, [ &f ]( const auto& o ) {
                                return o.second == f.second;
                            } ) != this->m_changedFiles.end();

                        if( !alreadyContains )
                        {
                            this->m_changedFiles.emplace_back( std::move( f ) );
                        }
                    }
                }

                s_prevAllFiles = std::move( curAllFiles );
                s_lastCheck    = Clock::now();
            }
        },
        std::move( folders ),
        m_asyncStopSource.get_token() );
}

RTGL1::FolderObserver::~FolderObserver()
{
    m_asyncStopSource.request_stop();
}

void RTGL1::FolderObserver::RecheckFiles()
{
    auto l = std::lock_guard{ this->m_mutex };

    for( const auto& [ type, path ] : this->m_changedFiles )
    {
        CallSubsbribers( &IFileDependency::OnFileChanged, type, path );
    }
    this->m_changedFiles.clear();
}
