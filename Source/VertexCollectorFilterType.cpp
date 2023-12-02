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

#include "VertexCollectorFilterType.h"

#include <cassert>

#include "Const.h"
#include "Generated/ShaderCommonC.h"

using FT = RTGL1::VertexCollectorFilterTypeFlagBits;
using FL = RTGL1::VertexCollectorFilterTypeFlags;

const char* RTGL1::VertexCollectorFilterTypeFlags_GetNameForBLAS( FL flags )
{
    constexpr static std::pair< FL, const char* > FL_NAMES[] = {
        { FT::CF_STATIC_NON_MOVABLE | FT::PT_OPAQUE, "BLAS static opaque" },
        { FT::CF_STATIC_NON_MOVABLE | FT::PT_ALPHA_TESTED, "BLAS static alpha tested" },
        { FT::CF_STATIC_NON_MOVABLE | FT::PT_REFRACT, "BLAS static refract" },

        { FT::CF_REPLACEMENT | FT::PT_OPAQUE, "BLAS c opaque" },
        { FT::CF_REPLACEMENT | FT::PT_ALPHA_TESTED, "BLAS CF_REPLACEMENT alpha tested" },
        { FT::CF_REPLACEMENT | FT::PT_REFRACT, "BLAS CF_REPLACEMENT refract" },

        { FT::CF_DYNAMIC | FT::PT_OPAQUE, "BLAS dynamic opaque" },
        { FT::CF_DYNAMIC | FT::PT_ALPHA_TESTED, "BLAS dynamic alpha tested" },
        { FT::CF_DYNAMIC | FT::PT_REFRACT, "BLAS dynamic refract" },
    };

    for( const auto& [ f, name ] : FL_NAMES )
    {
        if( ( f & flags ) == f )
        {
            return name;
        }
    }

    assert( 0 );
    return nullptr;
}

FL RTGL1::VertexCollectorFilterTypeFlags_GetForGeometry( const RgMeshInfo&          mesh,
                                                         const RgMeshPrimitiveInfo& primitive,
                                                         bool                       isStatic,
                                                         bool                       isReplacement )
{
    FL flags = 0;


    if( isStatic )
    {
        flags |= FL( FT::CF_STATIC_NON_MOVABLE );
    }
    else if( isReplacement )
    {
        flags |= FL( FT::CF_REPLACEMENT );
    }
    else
    {
        flags |= FL( FT::CF_DYNAMIC );
    }


    if( primitive.flags & RG_MESH_PRIMITIVE_ALPHA_TESTED )
    {
        flags |= FL( FT::PT_ALPHA_TESTED );
    }
    else if( ( primitive.flags & RG_MESH_PRIMITIVE_WATER ) ||
             ( mesh.flags & RG_MESH_FORCE_WATER ) ||
             ( primitive.flags & RG_MESH_PRIMITIVE_GLASS ) ||
             ( mesh.flags & RG_MESH_FORCE_GLASS ) ||
             ( primitive.flags & RG_MESH_PRIMITIVE_GLASS_IF_SMOOTH ) ||
             ( primitive.flags & RG_MESH_PRIMITIVE_ACID ) )
    {
        flags |= FL( FT::PT_REFRACT );
    }
    else
    {
        flags |= FL( FT::PT_OPAQUE );
    }


    if( mesh.flags & RG_MESH_FIRST_PERSON )
    {
        flags |= FL( FT::PV_FIRST_PERSON );
    }
    else if( mesh.flags & RG_MESH_FIRST_PERSON_VIEWER )
    {
        flags |= FL( FT::PV_FIRST_PERSON_VIEWER );
    }
    else if( primitive.flags & RG_MESH_PRIMITIVE_SKY_VISIBILITY )
    {
#if RAYCULLMASK_SKY_IS_WORLD2
        flags |= FL( FT::PV_WORLD_2 );
#else
    #error Handle RG_DRAW_FRAME_RAY_CULL_SKY_BIT, if there is no WORLD_2
#endif
    }
    else
    {
        flags |= FL( FT::PV_WORLD_0 );
    }


    return flags;
}
