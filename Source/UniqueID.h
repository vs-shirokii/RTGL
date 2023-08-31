// Copyright (c) 2021 Sultim Tsyrendashiev
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

#include <RTGL1/RTGL1.h>

namespace RTGL1
{
struct PrimitiveUniqueID
{
    PrimitiveUniqueID( const RgMeshInfo& mesh, const RgMeshPrimitiveInfo& primitive )
        : objectId( mesh.uniqueObjectID ), primitiveIndex( primitive.primitiveIndexInMesh )
    {
    }

    bool operator==( const PrimitiveUniqueID& other ) const
    {
        return objectId == other.objectId && primitiveIndex == other.primitiveIndex;
    }

    uint64_t objectId;
    uint64_t primitiveIndex;
};
}

namespace std
{
template<>
struct hash< RTGL1::PrimitiveUniqueID >
{
    using is_avalanching = void; // mark class as high quality avalanching hash

    size_t operator()( const RTGL1::PrimitiveUniqueID& id ) const noexcept
    {
        static auto hashCombine = []( size_t seed, size_t v ) {
            seed ^= v + 0x9e3779b9 + ( seed << 6 ) + ( seed >> 2 );
            return seed;
        };

        size_t h = 0;

        h = hashCombine( h, id.objectId );
        h = hashCombine( h, id.primitiveIndex );

        return h;
    }
};
}

namespace RTGL1
{
using UniqueIDToTlasID = rgl::unordered_map< PrimitiveUniqueID, uint32_t >;
}
