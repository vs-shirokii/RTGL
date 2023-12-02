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

#ifdef DESC_SET_GLOBAL_UNIFORM
#ifdef DESC_SET_VERTEX_DATA
layout(
    set = DESC_SET_VERTEX_DATA,
    binding = BINDING_VERTEX_BUFFER_STATIC)
    #ifndef VERTEX_BUFFER_WRITEABLE
    readonly 
    #endif
    buffer VertexBufferStatic_BT
{
    ShVertex g_staticVertices[];
};

layout(
    set = DESC_SET_VERTEX_DATA,
    binding = BINDING_VERTEX_BUFFER_DYNAMIC)
    #ifndef VERTEX_BUFFER_WRITEABLE
    readonly 
    #endif
    buffer VertexBufferDynamic_BT
{
    ShVertex g_dynamicVertices[];
};

layout(
    set = DESC_SET_VERTEX_DATA,
    binding = BINDING_INDEX_BUFFER_STATIC)
    readonly 
    buffer IndexBufferStatic_BT
{
    uint staticIndices[];
};

layout(
    set = DESC_SET_VERTEX_DATA,
    binding = BINDING_INDEX_BUFFER_DYNAMIC)
    readonly 
    buffer IndexBufferDynamic_BT
{
    uint dynamicIndices[];
};

layout(
    set = DESC_SET_VERTEX_DATA,
    binding = BINDING_GEOMETRY_INSTANCES)
    readonly 
    buffer GeometryInstances_BT
{
    ShGeometryInstance geometryInstances[];
};

layout(
    set = DESC_SET_VERTEX_DATA,
    binding = BINDING_GEOMETRY_INSTANCES_MATCH_PREV)
    readonly 
    buffer GeometryIndicesPrevToCur_BT
{
    int geomIndexPrevToCur[];
};

layout(
    set = DESC_SET_VERTEX_DATA,
    binding = BINDING_PREV_POSITIONS_BUFFER_DYNAMIC)
    #ifndef VERTEX_BUFFER_WRITEABLE
    readonly 
    #endif
    buffer PrevPositionsBufferDynamic_BT
{
    ShVertex g_dynamicVertices_Prev[];
};

layout(
    set = DESC_SET_VERTEX_DATA,
    binding = BINDING_PREV_INDEX_BUFFER_DYNAMIC)
    #ifndef VERTEX_BUFFER_WRITEABLE
    readonly 
    #endif
    buffer PrevIndexBufferDynamic_BT
{
    uint prevDynamicIndices[];
};

layout(
    set = DESC_SET_VERTEX_DATA,
    binding = BINDING_STATIC_TEXCOORD_LAYER_1)
    readonly
    buffer StaticTexCoordLayer1_BT
{
    vec2 g_staticTexCoords_Layer1[];
};
layout(
    set = DESC_SET_VERTEX_DATA,
    binding = BINDING_STATIC_TEXCOORD_LAYER_2)
    readonly
    buffer StaticTexCoordLayer2_BT
{
    vec2 g_staticTexCoords_Layer2[];
};
layout(
    set = DESC_SET_VERTEX_DATA,
    binding = BINDING_STATIC_TEXCOORD_LAYER_3)
    readonly
    buffer StaticTexCoordLayer3_BT
{
    vec2 g_staticTexCoords_Layer3[];
};
layout(
    set = DESC_SET_VERTEX_DATA,
    binding = BINDING_DYNAMIC_TEXCOORD_LAYER_1)
    readonly
    buffer DynamicTexCoordLayer1_BT
{
    vec2 g_dynamicTexCoords_Layer1[];
};
layout(
    set = DESC_SET_VERTEX_DATA,
    binding = BINDING_DYNAMIC_TEXCOORD_LAYER_2)
    readonly
    buffer DynamicTexCoordLayer2_BT
{
    vec2 g_dynamicTexCoords_Layer2[];
};
layout(
    set = DESC_SET_VERTEX_DATA,
    binding = BINDING_DYNAMIC_TEXCOORD_LAYER_3)
    readonly
    buffer DynamicTexCoordLayer3_BT
{
    vec2 g_dynamicTexCoords_Layer3[];
};


vec3 getStaticVerticesPositions(uint index)
{
    return g_staticVertices[index].position.xyz;
}

vec3 getStaticVerticesNormals(uint index)
{
    return decodeNormal(g_staticVertices[index].normalPacked);
}

vec3 getDynamicVerticesPositions(uint index)
{
    return g_dynamicVertices[index].position.xyz;
}

vec3 getDynamicVerticesNormals(uint index)
{
    return decodeNormal(g_dynamicVertices[index].normalPacked);
}

#ifdef VERTEX_BUFFER_WRITEABLE
void setStaticVerticesNormals(uint index, vec3 value)
{
    g_staticVertices[index].normalPacked = encodeNormal(value);
}

void setDynamicVerticesNormals(uint index, vec3 value)
{
    g_dynamicVertices[index].normalPacked = encodeNormal(value);
}
#endif // VERTEX_BUFFER_WRITEABLE

// Get indices in vertex buffer. If geom uses index buffer then it flattens them to vertex buffer indices.
uvec3 getVertIndicesStatic(uint baseVertexIndex, uint baseIndexIndex, uint primitiveId)
{
    // if to use indices
    if (baseIndexIndex != UINT32_MAX)
    {
        return uvec3(
            baseVertexIndex + staticIndices[baseIndexIndex + primitiveId * 3 + 0],
            baseVertexIndex + staticIndices[baseIndexIndex + primitiveId * 3 + 1],
            baseVertexIndex + staticIndices[baseIndexIndex + primitiveId * 3 + 2]);
    }
    else
    {
        return uvec3(
            baseVertexIndex + primitiveId * 3 + 0,
            baseVertexIndex + primitiveId * 3 + 1,
            baseVertexIndex + primitiveId * 3 + 2);
    }
}

uvec3 getVertIndicesDynamic(uint baseVertexIndex, uint baseIndexIndex, uint primitiveId)
{
    // if to use indices
    if (baseIndexIndex != UINT32_MAX)
    {
        return uvec3(
            baseVertexIndex + dynamicIndices[baseIndexIndex + primitiveId * 3 + 0],
            baseVertexIndex + dynamicIndices[baseIndexIndex + primitiveId * 3 + 1],
            baseVertexIndex + dynamicIndices[baseIndexIndex + primitiveId * 3 + 2]);
    }
    else
    {
        return uvec3(
            baseVertexIndex + primitiveId * 3 + 0,
            baseVertexIndex + primitiveId * 3 + 1,
            baseVertexIndex + primitiveId * 3 + 2);
    }
}

// Only for dynamic, static geom vertices are not changed.
uvec3 getPrevVertIndicesDynamic(uint prevBaseVertexIndex, uint prevBaseIndexIndex, uint primitiveId)
{
    // if to use indices
    if (prevBaseIndexIndex != UINT32_MAX)
    {
        return uvec3(
            prevBaseVertexIndex + prevDynamicIndices[prevBaseIndexIndex + primitiveId * 3 + 0],
            prevBaseVertexIndex + prevDynamicIndices[prevBaseIndexIndex + primitiveId * 3 + 1],
            prevBaseVertexIndex + prevDynamicIndices[prevBaseIndexIndex + primitiveId * 3 + 2]);
    }
    else
    {
        return uvec3(
            prevBaseVertexIndex + primitiveId * 3 + 0,
            prevBaseVertexIndex + primitiveId * 3 + 1,
            prevBaseVertexIndex + primitiveId * 3 + 2);
    }
}

vec3 getPrevDynamicVerticesPositions(uint index)
{
    return g_dynamicVertices_Prev[index].position.xyz;
}

void makeTangentBitangent( const mat3   localPos,
                           const mat3x2 texCoord,
                           out vec3     tangent,
                           out vec3     bitangent )
{
    const vec3 e1 = localPos[ 1 ] - localPos[ 0 ];
    const vec3 e2 = localPos[ 2 ] - localPos[ 0 ];

    const vec2 u1 = texCoord[ 1 ] - texCoord[ 0 ];
    const vec2 u2 = texCoord[ 2 ] - texCoord[ 0 ];

    const float invDet = 1.0 / ( u1.x * u2.y - u2.x * u1.y );

    tangent   = normalize( ( e1 * u2.y - e2 * u1.y ) * invDet );
    bitangent = normalize( ( e2 * u1.x - e1 * u2.x ) * invDet );
}

vec4 getTangent(const mat3 localPos, const vec3 normal, const mat3x2 texCoord)
{
    vec3 tangent, bitangent;
    makeTangentBitangent( localPos, texCoord, tangent, bitangent );

    // Don't store bitangent, only store cross(normal, tangent) handedness.
    // If that cross product and bitangent have the same sign,
    // then handedness is 1, otherwise -1
    float handedness = float(dot(cross(normal, tangent), bitangent) > 0.0);
    handedness = handedness * 2.0 - 1.0;

    return vec4(tangent, handedness);
}

ShTriangle makeTriangle(const ShVertex a, const ShVertex b, const ShVertex c)
{    
    ShTriangle tr;

    tr.positions[0] = a.position.xyz;
    tr.positions[1] = b.position.xyz;
    tr.positions[2] = c.position.xyz;

    tr.normals[0] = decodeNormal(a.normalPacked);
    tr.normals[1] = decodeNormal(b.normalPacked);
    tr.normals[2] = decodeNormal(c.normalPacked);

    tr.layerTexCoord[0][0] = a.texCoord;
    tr.layerTexCoord[0][1] = b.texCoord;
    tr.layerTexCoord[0][2] = c.texCoord;

    tr.vertexColors[0] = a.color;
    tr.vertexColors[1] = b.color;
    tr.vertexColors[2] = c.color;

    return tr;
}

ShTriangle makeTriangleFromCompact(const ShVertexCompact a, const ShVertexCompact b, const ShVertexCompact c)
{
    ShTriangle tr;

    tr.positions[ 0 ] = a.position.xyz;
    tr.positions[ 1 ] = b.position.xyz;
    tr.positions[ 2 ] = c.position.xyz;

    tr.normals[ 0 ] = decodeNormal( a.normalPacked );
    tr.normals[ 1 ] = decodeNormal( b.normalPacked );
    tr.normals[ 2 ] = decodeNormal( c.normalPacked );

    tr.layerTexCoord[ 0 ][ 0 ] = vec2( 0 );
    tr.layerTexCoord[ 0 ][ 1 ] = vec2( 0 );
    tr.layerTexCoord[ 0 ][ 2 ] = vec2( 0 );

    tr.vertexColors[ 0 ] = packUintColor( 255, 255, 255, 255 );
    tr.vertexColors[ 1 ] = packUintColor( 255, 255, 255, 255 );
    tr.vertexColors[ 2 ] = packUintColor( 255, 255, 255, 255 );

    return tr;
}

bool getCurrentGeometryIndexByPrev(int prevInstanceID, int prevLocalGeometryIndex, out int curFrameGlobalGeomIndex)
{
    // try to find instance index in current frame by it
    curFrameGlobalGeomIndex = geomIndexPrevToCur[prevInstanceID];

    // -1 : no prev to cur exist
    return curFrameGlobalGeomIndex >= 0;
}

// optimizing .rahit
#ifdef ONLY_LAYER0_TEXCOLOR
struct ShTriangleTexInfo
{
    mat3x2 layerTexCoord_0;
    uint   layerColorTextures_0;
    uint   layerColors_0;
};
ShTriangleTexInfo getTriangleTexInfo

#else
ShTriangle getTriangle
#endif
    ( int instanceID,          // index in TLAS
      int instanceCustomIndex, //
      int localGeometryIndex,  //
      int primitiveId          // index of a triangle
    )
{
    ShTriangle tr;

    const ShGeometryInstance inst = geometryInstances[instanceID];

    const bool isDynamic = ( ( inst.flags & GEOM_INST_FLAG_IS_DYNAMIC ) != 0 );

    if( isDynamic )
    {
        {
            const uvec3 vertIndices = getVertIndicesDynamic(inst.baseVertexIndex, inst.baseIndexIndex, primitiveId);

            tr = makeTriangle(
                g_dynamicVertices[vertIndices[0]],
                g_dynamicVertices[vertIndices[1]],
                g_dynamicVertices[vertIndices[2]]);
        }

#ifndef ONLY_LAYER0_TEXCOLOR

#if !SUPPRESS_TEXLAYERS
        if( ( inst.flags & GEOM_INST_FLAG_EXISTS_LAYER1 ) != 0 )
        {
            const uvec3 vertIndices =
                getVertIndicesDynamic( inst.firstVertex_Layer1, inst.baseIndexIndex, primitiveId );
            tr.layerTexCoord[ 1 ][ 0 ] = g_dynamicTexCoords_Layer1[ vertIndices[ 0 ] ];
            tr.layerTexCoord[ 1 ][ 1 ] = g_dynamicTexCoords_Layer1[ vertIndices[ 1 ] ];
            tr.layerTexCoord[ 1 ][ 2 ] = g_dynamicTexCoords_Layer1[ vertIndices[ 2 ] ];
        }
        if( ( inst.flags & GEOM_INST_FLAG_EXISTS_LAYER2 ) != 0 )
        {
            const uvec3 vertIndices =
                getVertIndicesDynamic( inst.firstVertex_Layer2, inst.baseIndexIndex, primitiveId );
            tr.layerTexCoord[ 2 ][ 0 ] = g_dynamicTexCoords_Layer2[ vertIndices[ 0 ] ];
            tr.layerTexCoord[ 2 ][ 1 ] = g_dynamicTexCoords_Layer2[ vertIndices[ 1 ] ];
            tr.layerTexCoord[ 2 ][ 2 ] = g_dynamicTexCoords_Layer2[ vertIndices[ 2 ] ];
        }
        if( ( inst.flags & GEOM_INST_FLAG_EXISTS_LAYER3 ) != 0 )
        {
            const uvec3 vertIndices =
                getVertIndicesDynamic( inst.firstVertex_Layer3, inst.baseIndexIndex, primitiveId );
            tr.layerTexCoord[ 3 ][ 0 ] = g_dynamicTexCoords_Layer3[ vertIndices[ 0 ] ];
            tr.layerTexCoord[ 3 ][ 1 ] = g_dynamicTexCoords_Layer3[ vertIndices[ 1 ] ];
            tr.layerTexCoord[ 3 ][ 2 ] = g_dynamicTexCoords_Layer3[ vertIndices[ 2 ] ];
        }
#endif // !SUPPRESS_TEXLAYERS

        // to world space
        tr.positions[ 0 ] = transformBy( inst, vec4( tr.positions[ 0 ], 1.0 ) );
        tr.positions[ 1 ] = transformBy( inst, vec4( tr.positions[ 1 ], 1.0 ) );
        tr.positions[ 2 ] = transformBy( inst, vec4( tr.positions[ 2 ], 1.0 ) );

        // dynamic     -- use prev model matrix and prev positions if exist
        const bool hasPrevInfo = inst.prevBaseVertexIndex != UINT32_MAX;

        if( hasPrevInfo )
        {
            const uvec3 prevVertIndices = getPrevVertIndicesDynamic(
                inst.prevBaseVertexIndex, inst.prevBaseIndexIndex, primitiveId );

            tr.prevPositions[ 0 ] = transformBy_prev( inst, vec4( getPrevDynamicVerticesPositions( prevVertIndices[ 0 ] ), 1.0 ) );
            tr.prevPositions[ 1 ] = transformBy_prev( inst, vec4( getPrevDynamicVerticesPositions( prevVertIndices[ 1 ] ), 1.0 ) );
            tr.prevPositions[ 2 ] = transformBy_prev( inst, vec4( getPrevDynamicVerticesPositions( prevVertIndices[ 2 ] ), 1.0 ) );
        }
        else
        {
            tr.prevPositions[ 0 ] = tr.positions[ 0 ];
            tr.prevPositions[ 1 ] = tr.positions[ 1 ];
            tr.prevPositions[ 2 ] = tr.positions[ 2 ];
        }
#endif // !ONLY_LAYER0_TEXCOLOR
    }
    else
    {
        {
            const uvec3 vertIndices = getVertIndicesStatic(inst.baseVertexIndex, inst.baseIndexIndex, primitiveId);
        
            tr = makeTriangle(
                g_staticVertices[vertIndices[0]],
                g_staticVertices[vertIndices[1]],
                g_staticVertices[vertIndices[2]]);
        }

#ifndef ONLY_LAYER0_TEXCOLOR

#if !SUPPRESS_TEXLAYERS
        if( ( inst.flags & GEOM_INST_FLAG_EXISTS_LAYER1 ) != 0 )
        {
            const uvec3 vertIndices =
                getVertIndicesStatic( inst.firstVertex_Layer1, inst.baseIndexIndex, primitiveId );
            tr.layerTexCoord[ 1 ][ 0 ] = g_staticTexCoords_Layer1[ vertIndices[ 0 ] ];
            tr.layerTexCoord[ 1 ][ 1 ] = g_staticTexCoords_Layer1[ vertIndices[ 1 ] ];
            tr.layerTexCoord[ 1 ][ 2 ] = g_staticTexCoords_Layer1[ vertIndices[ 2 ] ];
        }
        if( ( inst.flags & GEOM_INST_FLAG_EXISTS_LAYER2 ) != 0 )
        {
            const uvec3 vertIndices =
                getVertIndicesStatic( inst.firstVertex_Layer2, inst.baseIndexIndex, primitiveId );
            tr.layerTexCoord[ 2 ][ 0 ] = g_staticTexCoords_Layer2[ vertIndices[ 0 ] ];
            tr.layerTexCoord[ 2 ][ 1 ] = g_staticTexCoords_Layer2[ vertIndices[ 1 ] ];
            tr.layerTexCoord[ 2 ][ 2 ] = g_staticTexCoords_Layer2[ vertIndices[ 2 ] ];
        }
        if( ( inst.flags & GEOM_INST_FLAG_EXISTS_LAYER3 ) != 0 )
        {
            const uvec3 vertIndices =
                getVertIndicesStatic( inst.firstVertex_Layer3, inst.baseIndexIndex, primitiveId );
            tr.layerTexCoord[ 3 ][ 0 ] = g_staticTexCoords_Layer3[ vertIndices[ 0 ] ];
            tr.layerTexCoord[ 3 ][ 1 ] = g_staticTexCoords_Layer3[ vertIndices[ 1 ] ];
            tr.layerTexCoord[ 3 ][ 2 ] = g_staticTexCoords_Layer3[ vertIndices[ 2 ] ];
        }
#endif // !SUPPRESS_TEXLAYERS

        const vec3 localPos[] = {
            tr.positions[ 0 ],
            tr.positions[ 1 ],
            tr.positions[ 2 ],
        };

        // to world space
        tr.positions[ 0 ] = transformBy( inst, vec4( localPos[ 0 ], 1.0 ) );
        tr.positions[ 1 ] = transformBy( inst, vec4( localPos[ 1 ], 1.0 ) );
        tr.positions[ 2 ] = transformBy( inst, vec4( localPos[ 2 ], 1.0 ) );

        const bool hasPrevInfo = inst.prevBaseVertexIndex != UINT32_MAX;

        // use prev model matrix if exist
        if( hasPrevInfo )
        {
            // static geoms' local positions are constant,
            // only model matrices are changing
            tr.prevPositions[ 0 ] = transformBy_prev( inst, vec4( localPos[ 0 ], 1.0 ) );
            tr.prevPositions[ 1 ] = transformBy_prev( inst, vec4( localPos[ 1 ], 1.0 ) );
            tr.prevPositions[ 2 ] = transformBy_prev( inst, vec4( localPos[ 2 ], 1.0 ) );
        }
        else
        {
            tr.prevPositions[ 0 ] = tr.positions[ 0 ];
            tr.prevPositions[ 1 ] = tr.positions[ 1 ];
            tr.prevPositions[ 2 ] = tr.positions[ 2 ];
        }
#endif // !ONLY_LAYER0_TEXCOLOR
    }

#ifndef ONLY_LAYER0_TEXCOLOR

    tr.layerColorTextures = uint[](
        inst.texture_base
#if !SUPPRESS_TEXLAYERS
    ,   inst.texture_layer1
    ,   inst.texture_layer2
    ,   inst.texture_layer3
#endif
    );

    tr.layerColors = uint[](
        inst.colorFactor_base
#if !SUPPRESS_TEXLAYERS
    ,   inst.colorFactor_layer1
    ,   inst.colorFactor_layer2
    ,   inst.colorFactor_layer3
#endif
    );

    tr.roughnessDefault = ( ( inst.roughnessDefault_metallicDefault >> 0 ) & 0xFF ) / 255.0;
    tr.metallicDefault  = ( ( inst.roughnessDefault_metallicDefault >> 8 ) & 0xFF ) / 255.0;
    tr.occlusionRougnessMetallicTexture = inst.texture_base_ORM;

    tr.normalTexture = inst.texture_base_N;
    tr.heightTexture = inst.texture_base_D;

    tr.emissiveMult = inst.emissiveMult;
    tr.emissiveTexture = inst.texture_base_E;

    {
        // to world space
        tr.normals[ 0 ] = transformBy( inst, vec4( tr.normals[ 0 ], 0.0 ) );
        tr.normals[ 1 ] = transformBy( inst, vec4( tr.normals[ 1 ], 0.0 ) );
        tr.normals[ 2 ] = transformBy( inst, vec4( tr.normals[ 2 ], 0.0 ) );
    }

    tr.geometryInstanceFlags = inst.flags;
    tr.portalIndex = 0;

    return tr;

#else

    ShTriangleTexInfo trTexInfo;
    trTexInfo.layerTexCoord_0      = tr.layerTexCoord[ 0 ];
    trTexInfo.layerColorTextures_0 = inst.texture_base;
    trTexInfo.layerColors_0        = inst.colorFactor_base;
    return trTexInfo;

#endif // !ONLY_LAYER0_TEXCOLOR
}

mat3 getOnlyCurPositions(int globalGeometryIndex, int primitiveId)
{
    mat3 positions;

    const ShGeometryInstance inst = geometryInstances[globalGeometryIndex];

    const bool isDynamic = ( ( inst.flags & GEOM_INST_FLAG_IS_DYNAMIC ) != 0 );

    if (isDynamic)
    {
        const uvec3 vertIndices = getVertIndicesDynamic(inst.baseVertexIndex, inst.baseIndexIndex, primitiveId);

        // to world space
        positions[0] = transformBy(inst, vec4(getDynamicVerticesPositions(vertIndices[0]), 1.0));
        positions[1] = transformBy(inst, vec4(getDynamicVerticesPositions(vertIndices[1]), 1.0));
        positions[2] = transformBy(inst, vec4(getDynamicVerticesPositions(vertIndices[2]), 1.0));
    }
    else
    {
        const uvec3 vertIndices = getVertIndicesStatic(inst.baseVertexIndex, inst.baseIndexIndex, primitiveId);

        // to world space
        positions[0] = transformBy(inst, vec4(getStaticVerticesPositions(vertIndices[0]), 1.0));
        positions[1] = transformBy(inst, vec4(getStaticVerticesPositions(vertIndices[1]), 1.0));
        positions[2] = transformBy(inst, vec4(getStaticVerticesPositions(vertIndices[2]), 1.0));
    }
    
    return positions;
}

vec4 packVisibilityBuffer(const ShPayload p)
{
    return vec4(uintBitsToFloat(p.instIdAndIndex), uintBitsToFloat(p.geomAndPrimIndex), p.baryCoords);
}

vec4 packVisibilityBuffer_Invalid()
{
    return vec4( uintBitsToFloat( UINT32_MAX ), uintBitsToFloat( UINT32_MAX ), vec2( 0 ) );
}

int unpackInstCustomIndexFromVisibilityBuffer(const vec4 v)
{
    int instanceID, instCustomIndex;
    unpackInstanceIdAndCustomIndex(floatBitsToUint(v[0]), instanceID, instCustomIndex);

    return instCustomIndex;
}

void unpackVisibilityBuffer(
    const vec4 v,
    out int instanceID, out int instCustomIndex,
    out int localGeomIndex, out int primIndex,
    out vec2 bary)
{
    unpackInstanceIdAndCustomIndex(floatBitsToUint(v[0]), instanceID, instCustomIndex);
    unpackGeometryAndPrimitiveIndex(floatBitsToUint(v[1]), localGeomIndex, primIndex);
    bary = vec2(v[2], v[3]);
}

// v must be fetched from framebufVisibilityBuffer_Prev_Sampler
bool unpackPrevVisibilityBuffer(const vec4 v, out vec3 prevPos)
{
    int prevInstanceID, instCustomIndex;
    int prevLocalGeomIndex, primIndex;

    {
        const uvec2 ii = uvec2( floatBitsToUint( v[ 0 ] ), floatBitsToUint( v[ 1 ] ) );

        if( ii.x == UINT32_MAX || ii.y == UINT32_MAX )
        {
            return false;
        }

        unpackInstanceIdAndCustomIndex( ii[ 0 ], prevInstanceID, instCustomIndex );
        unpackGeometryAndPrimitiveIndex( ii[ 1 ], prevLocalGeomIndex, primIndex );
    }

    int curFrameGlobalGeomIndex;
    const bool matched = getCurrentGeometryIndexByPrev(prevInstanceID, prevLocalGeomIndex, curFrameGlobalGeomIndex);

    if (!matched)
    {
        return false;
    }

    const mat3 verts = getOnlyCurPositions(curFrameGlobalGeomIndex, primIndex);
    const vec3 baryCoords = vec3(1.0 - v[2] - v[3], v[2], v[3]);

    prevPos = verts * baryCoords;

    return true;
}
#endif // DESC_SET_VERTEX_DATA
#endif // DESC_SET_GLOBAL_UNIFORM