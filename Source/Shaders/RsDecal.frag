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

#version 460

#define FRAMEBUF_IGNORE_ATTACHMENTS
#define DESC_SET_GLOBAL_UNIFORM 0
#define DESC_SET_FRAMEBUFFERS   1
#define DESC_SET_TEXTURES       2
#include "ShaderCommonGLSLFunc.h"
#include "Random.h"

layout( location = 0 ) in vec4 vertColor;
layout( location = 1 ) in vec2 vertTexCoord;
layout( location = 2 ) in vec3 vertWorldPosition;

layout( location = 0 ) out vec4 out_albedo;
layout( location = 1 ) out uint out_normal;
layout( location = 2 ) out vec3 out_screenEmission;

layout( push_constant ) uniform DecalFrag_BT
{
    layout( offset = 64 ) uint  packedColor;
    layout( offset = 68 ) uint  textureIndex;
    layout( offset = 72 ) uint  emissiveTextureIndex;
    layout( offset = 76 ) float emissiveMult;
    layout( offset = 80 ) uint  normalTextureIndex;
}
decal;

vec4 baseColor()
{
    return vertColor * unpackUintColor( decal.packedColor );
}

void main()
{
    const ivec2 pix = getCheckerboardPix( ivec2( gl_FragCoord.xy ) );

    // omit if too far
    {
        const vec3 underlyingSurfPosition =
            texelFetch( framebufSurfacePosition_Sampler, pix, 0 ).xyz;

        const float DistThreshold = 0.05;
        if( lengthSquared( vertWorldPosition - underlyingSurfPosition ) >
            DistThreshold * DistThreshold )
        {
            discard;
        }
    }

    {
        out_albedo = baseColor() * getTextureSample( decal.textureIndex, vertTexCoord );
    }

    if( decal.normalTextureIndex != MATERIAL_NO_TEXTURE )
    {
        const vec3 underlyingNormal = texelFetchNormal( pix );

        mat3 basis = getONB( underlyingNormal );

        vec2 nmap = getTextureSample( decal.normalTextureIndex, vertTexCoord ).xy;
        nmap.xy   = nmap.xy * 2.0 - vec2( 1.0 );

        out_normal =
            encodeNormal( safeNormalize( basis[ 0 ] * nmap.x + basis[ 1 ] * nmap.y + basis[ 2 ] ) );
    }
    else
    {
        out_normal = texelFetchEncNormal( pix );
    }

    {
        vec3 ldrEmis;
        if( decal.emissiveTextureIndex != MATERIAL_NO_TEXTURE )
        {
            ldrEmis =
                baseColor().rgb * getTextureSample( decal.emissiveTextureIndex, vertTexCoord ).rgb;
        }
        else
        {
            ldrEmis = out_albedo.rgb;
        }
        ldrEmis *= decal.emissiveMult * baseColor().a;

        out_screenEmission = ldrEmis;
    }
}