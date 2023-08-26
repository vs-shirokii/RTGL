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

#include "ankerl/unordered_dense.h"

namespace rgl
{

template< typename Key, typename T >
using unordered_map = ankerl::unordered_dense::map< Key, T >;

template< typename T >
using unordered_set = ankerl::unordered_dense::set< T >;

namespace detail
{
    struct transparent_string_hash
    {
        using is_transparent = void; // enable heterogeneous overloads
        using is_avalanching = void; // mark class as high quality avalanching hash

        [[nodiscard]] auto operator()( std::string_view str ) const noexcept -> uint64_t
        {
            return ankerl::unordered_dense::hash< std::string_view >{}( str );
        }
    };
}

// For comparison without casting to std::string

template< typename T >
using string_map = ankerl::unordered_dense::
    map< std::string, T, detail::transparent_string_hash, std::equal_to<> >;

using string_set =
    ankerl::unordered_dense::set< std::string, detail::transparent_string_hash, std::equal_to<> >;

static_assert( ankerl::unordered_dense::detail::is_transparent_v< string_map< int >::hasher,
                                                                  string_map< int >::key_equal > );
static_assert( ankerl::unordered_dense::detail::is_transparent_v< string_set::hasher,
                                                                  string_set::key_equal > );
}