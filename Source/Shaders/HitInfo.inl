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

#ifdef DESC_SET_VERTEX_DATA
#ifdef DESC_SET_GLOBAL_UNIFORM
#ifdef DESC_SET_TEXTURES



#if defined( HITINFO_INL_PRIM )
vec3 processAlbedoGrad(         
    uint geometryInstanceFlags, 
    const vec2 texCoords[ TEXLAYER_MAX ], const uint layerColorTextures[ TEXLAYER_MAX ], const uint layerColors[ TEXLAYER_MAX ], 
    const vec2 dPdx[ TEXLAYER_MAX ], const vec2 dPdy[ TEXLAYER_MAX ] )

#elif defined( HITINFO_INL_RFL )
vec3 processAlbedoRayConeDeriv( 
    uint geometryInstanceFlags, 
    const vec2 texCoords[ TEXLAYER_MAX ], const uint layerColorTextures[ TEXLAYER_MAX ], const uint layerColors[ TEXLAYER_MAX ], 
    const DerivativeSet derivSet )

#elif defined( HITINFO_INL_INDIR )
vec3 processAlbedo(             
    uint geometryInstanceFlags, 
    const vec2 texCoords[ TEXLAYER_MAX ], const uint layerColorTextures[ TEXLAYER_MAX ], const uint layerColors[ TEXLAYER_MAX ], 
    float lod )
#endif
{
    #if GEOM_INST_FLAG_BLENDING_LAYER_COUNT != 4
        #error
    #endif
    #if MATERIAL_MAX_ALBEDO_LAYERS > GEOM_INST_FLAG_BLENDING_LAYER_COUNT
        #error
    #endif

    vec3 dst                 = vec3( 1.0 );
    bool hasAnyAlbedoTexture = false;

    [[unroll]] for( int i = 0; i < MATERIAL_MAX_ALBEDO_LAYERS; i++ )
    {
        if( i == 1 )
        {
            if( ( geometryInstanceFlags & GEOM_INST_FLAG_EXISTS_LAYER1 ) == 0 )
            {
                continue;
            }
        }
        else if( i == 2 )
        {
            if( ( geometryInstanceFlags & GEOM_INST_FLAG_EXISTS_LAYER2 ) == 0 )
            {
                continue;
            }
        }
        else if( i == 3 )
        {
            if( ( geometryInstanceFlags & GEOM_INST_FLAG_EXISTS_LAYER3 ) == 0 )
            {
                continue;
            }
        }

    #if defined( HITINFO_INL_CLASSIC_SHADING )
        if( i == MATERIAL_LIGHTMAP_LAYER_INDEX )
        {
            // ignore lightmap layer, if using RT illumination
            continue;
        }
    #endif

        if( layerColorTextures[ i ] != MATERIAL_NO_TEXTURE )
        {
            const vec4 src = unpackUintColor( layerColors[ i ] ) *
    #if defined( HITINFO_INL_PRIM )
                getTextureSampleGrad( layerColorTextures[ i ], texCoords[ i ], dPdx[ i ], dPdy[ i ] );
    #elif defined( HITINFO_INL_RFL )
                getTextureSampleDerivSet( layerColorTextures[ i ], texCoords[ i ], derivSet, i );
    #elif defined( HITINFO_INL_INDIR )
                getTextureSampleLod( layerColorTextures[ i ], texCoords[ i ], lod );
    #endif

            uint layerBlendType = 
                ( geometryInstanceFlags >> ( MATERIAL_BLENDING_TYPE_BIT_COUNT * i ) ) 
                    & MATERIAL_BLENDING_TYPE_BIT_MASK;

            bool opq = ( layerBlendType == MATERIAL_BLENDING_TYPE_OPAQUE );
            bool alp = ( layerBlendType == MATERIAL_BLENDING_TYPE_ALPHA );
            bool add = ( layerBlendType == MATERIAL_BLENDING_TYPE_ADD );
            bool shd = ( layerBlendType == MATERIAL_BLENDING_TYPE_SHADE );

            // simple fix for layerColorTextures that have alpha-tested blending for the first layer
            // (just makes "opq" instead of "alp" for that partuicular case);
            // without this fix, alpha-tested geometry will have white color around borders
            opq = opq || ( alp && i == 0 );
            alp = alp && !opq;

            dst = float(opq) * (src.rgb) +
                  float(alp) * (src.rgb * src.a + dst * (1 - src.a)) + 
                  float(add) * (src.rgb + dst) +
                  float(shd) * (src.rgb * dst * 2);

            hasAnyAlbedoTexture = true;
        }
    }

    // if no albedo textures, use primary color
    dst = mix( unpackUintColor( layerColors[ 0 ] ).rgb, dst, float( hasAnyAlbedoTexture ) );

    return clamp( dst, vec3( 0 ), vec3( 1 ) );
}


#if defined( HITINFO_INL_PRIM )
// "Ray Traced Reflections in 'Wolfenstein: Youngblood'", Jiho Choi, Jim Kjellin, Patrik Willbo, Dmitry Zhdan
float getBounceLOD(float roughness, float viewDist, float hitDist, float screenWidth, float bounceMipBias)
{    
    const float range = 300.0 * pow((1.0 - roughness) * 0.9 + 0.1, 4.0);

    vec2 f = vec2(viewDist, hitDist);
    f = clamp(f / range, vec2(0.0), vec2(1.0));
    f = sqrt(f);

    float mip = max(log2(3840.0 / screenWidth), 0.0);

    mip += f.x * 10.0;
    mip += f.y * 10.0;

    return mip + bounceMipBias;
}
#endif // HITINFO_INL_PRIM


#if defined(HITINFO_INL_PRIM)
// Fast, Minimum Storage Ray-Triangle Intersection, Moller, Trumbore
vec3 intersectRayTriangle(const mat3 positions, const vec3 orig, const vec3 dir)
{
    const vec3 edge1 = positions[1] - positions[0];
    const vec3 edge2 = positions[2] - positions[0];

    const vec3 pvec = cross(dir, edge2);

    const float det = dot(edge1, pvec);
    const float invDet = 1.0 / det;

    const vec3 tvec = orig - positions[0];
    const vec3 qvec = cross(tvec, edge1);

    const float u = dot(tvec, pvec) * invDet;
    const float v = dot(dir, qvec) * invDet;

    return vec3(1 - u - v, u, v);
}

// 0.0 - deepest point; 1.0 - surface level
float sampleHeightMap( const uint textureIndex, const vec2 texCoords )
{
    return getTextureSampleLod( textureIndex, texCoords, 0 ).r;
}

const int ParallaxLinearSteps       = 10;
const int ParallaxBinarySearchSteps = 4;

vec2 parallaxTexCoords( const uint  heightTextureIndex,
                        const vec2  texCoords,
                        const vec3  viewDir,
                        const float maxDepth )
{
    const float deltaLayerDepth     = maxDepth / ParallaxLinearSteps;
    const vec2  deltaLayerTexCoords = viewDir.xy / viewDir.z * deltaLayerDepth;

    float depth         = 0.0;
    float curLayerDepth = 0.0;

    // linear search for a layer
    for( int i = 0; i < ParallaxLinearSteps; i++ )
    {
        curLayerDepth         = i * deltaLayerDepth;
        vec2 curLayerTexCoord = texCoords - i * deltaLayerTexCoords;

        depth = maxDepth * ( 1.0 - sampleHeightMap( heightTextureIndex, curLayerTexCoord ) );
        if( depth < curLayerDepth )
        {
            break;
        }
    }

    // binary search for more precise hit position in the layer
    float lower  = curLayerDepth - deltaLayerDepth;
    float higher = curLayerDepth;
    for( int i = 0; i < ParallaxBinarySearchSteps; i++ )
    {
        float mid = ( lower + higher ) * 0.5;
        if( depth < mid )
        {
            higher = mid;
        }
        else
        {
            lower = mid;
        }
    }

    float hit = ( lower + higher ) * 0.5;
    return texCoords - viewDir.xy / viewDir.z * hit;
}

// Correct 'candidateNormal' that was produced by a normal map, or vertex interpolation:
// prevent self intersections, i.e. the reflection over 'candidateNormal' would not
// point into the surface (defined by 'triangleNormal'), which would
// produce zero later in a pipeline, e.g. max(0,dot(n,l))
// "The Iray Light Transport Simulation and Rendering System",
// "A.3 Local Shading Normal Adaption"
vec3 sanitizeNormal( const vec3 triangleNormal, //
                     const vec3 candidateNormal,
                     const vec3 fromViewer )
{
    const vec3 candidateRefl = reflect( fromViewer, candidateNormal );

    float nr = dot( candidateRefl, triangleNormal );

    // if reflection doesn't cause self intersection
    if( nr > 0 )
    {
        return candidateNormal;
    }

    const float a = -nr * 2;
    const float b = max( 0.001, dot( candidateNormal, triangleNormal ) );

    const vec3 sanitizedRefl = candidateRefl + a / b * candidateNormal;

    // r = v - 2 * (v.n) * n
    // n ~ -r + v
    return -safeNormalize2( -normalize( sanitizedRefl ) + fromViewer, //
                            triangleNormal );
}
#endif // HITINFO_INL_PRIM


#if defined(HITINFO_INL_PRIM)

ShHitInfo getHitInfoPrimaryRay(
    const ShPayload pl, 
    const vec3 rayOrigin, const vec3 viewDir, const vec3 rayDirAX, const vec3 rayDirAY, 
    out vec2 motion, out float motionDepthLinear, 
    out vec2 gradDepth, out float depthNDC, out float depthLinear,
    out vec3 emission)

#elif defined(HITINFO_INL_RFL)

ShHitInfo getHitInfoWithRayCone_ReflectionRefraction(
    const ShPayload pl, const RayCone rayCone,
    const vec3 rayOrigin, const vec3 rayDir, const vec3 viewDir,
    in out vec3 virtualPosForMotion,
    out float rayLen,
    out vec2 motion, out float motionDepthLinear,
    out vec3 emission,
    out bool hasNormalMap)

#elif defined(HITINFO_INL_INDIR)

ShHitInfo getHitInfoBounce(
    const ShPayload pl, const vec3 rayOrigin, float originRoughness, float bounceMipBias,
    out vec3 emission)

#endif
{
    ShHitInfo h;

    int instanceId, instCustomIndex;
    int geomIndex, primIndex;

    unpackInstanceIdAndCustomIndex(pl.instIdAndIndex, instanceId, instCustomIndex);
    unpackGeometryAndPrimitiveIndex(pl.geomAndPrimIndex, geomIndex, primIndex);

    const ShTriangle tr = getTriangle(instanceId, instCustomIndex, geomIndex, primIndex);



    // GEOMETRY

    const vec2 inBaryCoords = pl.baryCoords;
    const vec3 baryCoords = vec3(1.0f - inBaryCoords.x - inBaryCoords.y, inBaryCoords.x, inBaryCoords.y);

     vec2 texCoords[] = {
        tr.layerTexCoord[ 0 ] * baryCoords,
#if !SUPPRESS_TEXLAYERS
        tr.layerTexCoord[ 1 ] * baryCoords,
        tr.layerTexCoord[ 2 ] * baryCoords,
        tr.layerTexCoord[ 3 ] * baryCoords,
#endif
    };

    h.hitPosition = tr.positions * baryCoords;

    vec3 exactNormal = safeNormalize3(        //
        cross( tr.positions[ 1 ] - tr.positions[ 0 ], //
               tr.positions[ 2 ] - tr.positions[ 0 ] ),
        0.00000001,
        vec3( 0, 1, 0 ) );
    {
        if( ( tr.geometryInstanceFlags & GEOM_INST_FLAG_EXACT_NORMALS ) == 0 )
        {
            h.normal = normalize( tr.normals * baryCoords );
        }
        else
        {
            h.normal = exactNormal;
        }

        // always face ray origin
        const vec3 toViewer = safeNormalize3( rayOrigin - h.hitPosition, 0.00000001, vec3( 0 ) );
        if( dot( exactNormal, toViewer ) < 0 )
        {
            exactNormal *= -1;
            h.normal *= -1;
        }

        h.normal = sanitizeNormal( exactNormal, h.normal, -toViewer );
    }



    // CLIP SPACE

#if defined(HITINFO_INL_PRIM)
    // Tracing Ray Differentials, Igehy
    // instead of casting new rays, check intersections on the same triangle
    const vec3 baryCoordsAX = intersectRayTriangle(tr.positions, rayOrigin, rayDirAX);
    const vec3 baryCoordsAY = intersectRayTriangle(tr.positions, rayOrigin, rayDirAY);

    const vec4 viewSpacePosCur   = globalUniform.view     * vec4(h.hitPosition, 1.0);
    const vec4 viewSpacePosPrev  = globalUniform.viewPrev * vec4(tr.prevPositions * baryCoords, 1.0);
    const vec4 viewSpacePosAX    = globalUniform.view     * vec4(tr.positions     * baryCoordsAX, 1.0);
    const vec4 viewSpacePosAY    = globalUniform.view     * vec4(tr.positions     * baryCoordsAY, 1.0);

    const vec4 clipSpacePosCur   = globalUniform.projection     * viewSpacePosCur;
    const vec4 clipSpacePosPrev  = globalUniform.projectionPrev * viewSpacePosPrev;

    const float clipSpaceDepth   = clipSpacePosCur[2];
    const float clipSpaceDepthAX = dot(globalUniform.projection[2], viewSpacePosAX);
    const float clipSpaceDepthAY = dot(globalUniform.projection[2], viewSpacePosAY);

    const vec3 ndcCur            = clipSpacePosCur.xyz  / clipSpacePosCur.w;
    const vec3 ndcPrev           = clipSpacePosPrev.xyz / clipSpacePosPrev.w;

    const vec2 screenSpaceCur    = ndcCur.xy  * 0.5 + 0.5;
    const vec2 screenSpacePrev   = ndcPrev.xy * 0.5 + 0.5;
#endif // HITINFO_INL_PRIM

#if defined(HITINFO_INL_RFL) 
    rayLen = length(h.hitPosition - rayOrigin);
    virtualPosForMotion += viewDir * rayLen;

    const vec4 viewSpacePosCur   = globalUniform.view     * vec4(virtualPosForMotion, 1.0);
    const vec4 viewSpacePosPrev  = globalUniform.viewPrev * vec4(virtualPosForMotion, 1.0);
    const vec4 clipSpacePosCur   = globalUniform.projection     * viewSpacePosCur;
    const vec4 clipSpacePosPrev  = globalUniform.projectionPrev * viewSpacePosPrev;
    const vec3 ndcCur            = clipSpacePosCur.xyz  / clipSpacePosCur.w;
    const vec3 ndcPrev           = clipSpacePosPrev.xyz / clipSpacePosPrev.w;
    const vec2 screenSpaceCur    = ndcCur.xy  * 0.5 + 0.5;
    const vec2 screenSpacePrev   = ndcPrev.xy * 0.5 + 0.5;

    const float clipSpaceDepth   = clipSpacePosCur[2];
#endif // HITINFO_INL_RFL

#if defined(HITINFO_INL_PRIM)
    depthNDC = ndcCur.z;
    depthLinear = length(viewSpacePosCur.xyz);
#endif



    // MOTION

#if defined(HITINFO_INL_PRIM) || defined(HITINFO_INL_RFL)
    // difference in screen-space
    motion = (screenSpacePrev - screenSpaceCur);
#endif

#if defined(HITINFO_INL_PRIM)
    motionDepthLinear = length(viewSpacePosPrev.xyz) - depthLinear;
#elif defined(HITINFO_INL_RFL)
    motionDepthLinear = length(viewSpacePosPrev.xyz) - length(viewSpacePosCur.xyz);
#endif 

#if defined(HITINFO_INL_PRIM) 
    // gradient of clip-space depth with respect to clip-space coordinates
    gradDepth = vec2(clipSpaceDepthAX - clipSpaceDepth, clipSpaceDepthAY - clipSpaceDepth);
#elif defined(HITINFO_INL_RFL)
    // don't touch gradDepth for reflections / refractions
#endif



    // TEXTURE SAMPLING

#if defined( HITINFO_INL_PRIM )
    // pixel's footprint in texture space
    const vec2 dTdx[] = {
        ( tr.layerTexCoord[ 0 ] * baryCoordsAX - texCoords[ 0 ] ),
#if !SUPPRESS_TEXLAYERS
        ( tr.layerTexCoord[ 1 ] * baryCoordsAX - texCoords[ 1 ] ),
        ( tr.layerTexCoord[ 2 ] * baryCoordsAX - texCoords[ 2 ] ),
        ( tr.layerTexCoord[ 3 ] * baryCoordsAX - texCoords[ 3 ] ),
#endif
    };

    const vec2 dTdy[] = {
        ( tr.layerTexCoord[ 0 ] * baryCoordsAY - texCoords[ 0 ] ),
#if !SUPPRESS_TEXLAYERS
        ( tr.layerTexCoord[ 1 ] * baryCoordsAY - texCoords[ 1 ] ),
        ( tr.layerTexCoord[ 2 ] * baryCoordsAY - texCoords[ 2 ] ),
        ( tr.layerTexCoord[ 3 ] * baryCoordsAY - texCoords[ 3 ] ),
#endif
    };
#elif defined(HITINFO_INL_RFL)
    const DerivativeSet derivSet = getTriangleUVDerivativesFromRayCone(tr, h.normal, rayCone, rayDir);
#endif



    // HEIGHT / NORMAL MAP (ignored for indirect)

#if defined( HITINFO_INL_PRIM ) || defined( HITINFO_INL_RFL )
    vec3 tangent, bitangent;
    if( tr.heightTexture != MATERIAL_NO_TEXTURE || tr.normalTexture != MATERIAL_NO_TEXTURE )
    {
        makeTangentBitangent( tr.positions, tr.layerTexCoord[ 0 ], tangent, bitangent );
    }

    if( globalUniform.parallaxMaxDepth > 0.0001 && tr.heightTexture != MATERIAL_NO_TEXTURE )
    {
        vec3 viewDirInTextureSpace;
        {
            mat3 worldToTbn       = transpose( mat3( tangent, bitangent, h.normal ) );
            viewDirInTextureSpace = normalize( worldToTbn * viewDir );
        }

        texCoords[ 0 ] = parallaxTexCoords( tr.heightTexture,
                                            texCoords[ 0 ],
                                            viewDirInTextureSpace,
                                            globalUniform.parallaxMaxDepth );
    }


    if (tr.normalTexture != MATERIAL_NO_TEXTURE)
    {
        vec2 nrm =
    #if defined( HITINFO_INL_PRIM )
            getTextureSampleGrad( tr.normalTexture, texCoords[ 0 ], dTdx[ 0 ], dTdy[ 0 ] )
    #elif defined( HITINFO_INL_RFL )
            getTextureSampleDerivSet( tr.normalTexture, texCoords[ 0 ], derivSet, 0 )
    #endif
            .xy;
        nrm.xy = nrm.xy * 2.0 - vec2( 1.0 );

        const vec3 newNormal = safeNormalize2( tangent * nrm.x + bitangent * nrm.y + h.normal, //
                                               h.normal );
        h.normal = safeNormalize2( mix( h.normal, newNormal, globalUniform.normalMapStrength ), //
                                   h.normal );

        const vec3 toViewer = safeNormalize2( rayOrigin - h.hitPosition, vec3( 0 ) );
        h.normal            = sanitizeNormal( exactNormal, h.normal, -toViewer );
    }

#if defined( HITINFO_INL_RFL )
    hasNormalMap = ( tr.normalTexture != MATERIAL_NO_TEXTURE );
#endif

#endif // HITINFO_INL_PRIM || HITINFO_INL_RFL



    // ALBEDO

#if defined( HITINFO_INL_PRIM )
    h.albedo = processAlbedoGrad( tr.geometryInstanceFlags, 
                                  texCoords,
                                  tr.layerColorTextures, 
                                  tr.layerColors, 
                                  dTdx, dTdy );
#elif defined(HITINFO_INL_RFL)
    h.albedo = processAlbedoRayConeDeriv( tr.geometryInstanceFlags,
                                          texCoords,
                                          tr.layerColorTextures,
                                          tr.layerColors,
                                          derivSet );
#elif defined(HITINFO_INL_INDIR)
    const float viewDist = length(h.hitPosition - globalUniform.cameraPosition.xyz);
    const float hitDistance = length(h.hitPosition - rayOrigin);

    const float lod = getBounceLOD(originRoughness, viewDist, hitDistance, globalUniform.renderWidth, bounceMipBias);

    h.albedo = processAlbedo(tr.geometryInstanceFlags, texCoords, tr.layerColorTextures, tr.layerColors, lod);
#endif



    // METALLIC-ROUGHNESS

    if( tr.occlusionRougnessMetallicTexture != MATERIAL_NO_TEXTURE )
    {
        const vec3 orm =
    #if defined( HITINFO_INL_PRIM )
            getTextureSampleGrad( tr.occlusionRougnessMetallicTexture, texCoords[ 0 ], dTdx[ 0 ], dTdy[ 0 ] ).xyz;
    #elif defined( HITINFO_INL_RFL )
            getTextureSampleDerivSet( tr.occlusionRougnessMetallicTexture, texCoords[ 0 ], derivSet, 0 ).xyz;
    #elif defined( HITINFO_INL_INDIR )
            getTextureSampleLod( tr.occlusionRougnessMetallicTexture, texCoords[ 0 ], lod ).xyz;
    #endif

        h.roughness = orm[ 1 ];
        h.metallic  = orm[ 2 ];
    }
    else
    {
        h.roughness = tr.roughnessDefault;
        h.metallic  = tr.metallicDefault;
    }
    h.roughness = max( h.roughness, max( globalUniform.minRoughness, MIN_GGX_ROUGHNESS ) );



    // EMISSIVE

    if( tr.emissiveTexture != MATERIAL_NO_TEXTURE )
    {
        emission =
    #if defined( HITINFO_INL_PRIM )
            getTextureSampleGrad( tr.emissiveTexture, texCoords[ 0 ], dTdx[ 0 ], dTdy[ 0 ] ).rgb;
    #elif defined( HITINFO_INL_RFL )
            getTextureSampleDerivSet( tr.emissiveTexture, texCoords[ 0 ], derivSet, 0 ).rgb;
    #elif defined( HITINFO_INL_INDIR )
            getTextureSampleLod( tr.emissiveTexture, texCoords[ 0 ], lod ).rgb;
    #endif
    }
    else
    {
        emission = h.albedo * tr.emissiveMult;
    }



    // MISC

    h.instCustomIndex = instCustomIndex;
    h.geometryInstanceFlags = tr.geometryInstanceFlags;
    h.portalIndex = tr.portalIndex;

    return h;
}

#endif // DESC_SET_TEXTURES
#endif // DESC_SET_GLOBAL_UNIFORM
#endif // DESC_SET_VERTEX_DATA