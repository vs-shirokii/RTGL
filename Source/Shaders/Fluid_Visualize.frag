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

#version 460

#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types : require

#include "ShaderCommonGLSLFunc.h"
#define FLUID_DEF_SPEC_CONST 1
#include "Fluid_Def.h"

layout( location = 0 ) in vec2 in_uv;
layout( location = 1 ) in vec3 in_center;

layout( location = 0 ) out uint32_t out_normal;

layout( push_constant ) uniform VisualizePush_T
{
    layout( offset = 0 ) mat4  proj;
    layout( offset = 64 ) mat4 view;
}
push;


// https://paroj.github.io/gltut/Illumination/Tut13%20Correct%20Chicanery.html
// https://github.com/paroj/gltut/tree/master/Tut%2013%20Impostors
void sphereImpostor( const vec3  viewSpaceSphereCenter,
                     const float sphereRadius,
                     out vec3    out_viewSpacePos,
                     out vec3    out_viewSpaceNormal )
{
    // clang-format off
	vec3 cameraPlanePos = vec3(in_uv * sphereRadius, 0.0) + viewSpaceSphereCenter;
	vec3 rayDirection = normalize(cameraPlanePos);
	
	float B = 2.0 * dot(rayDirection, -viewSpaceSphereCenter);
	float C = dot(viewSpaceSphereCenter, viewSpaceSphereCenter) - (sphereRadius * sphereRadius);
	
	float det = (B * B) - (4 * C);
	if(det < 0.0)
		discard;
		
	float sqrtDet = sqrt(det);
	float posT = (-B + sqrtDet)/2;
	float negT = (-B - sqrtDet)/2;
	
	float intersectT = min(posT, negT);
	out_viewSpacePos = rayDirection * intersectT;
	out_viewSpaceNormal = normalize(out_viewSpacePos - viewSpaceSphereCenter);
    // clang-format on
}

void main()
{
    vec3 viewSpacePos, viewSpaceNormal;
    {
        vec3 viewSpaceSphereCenter = ( push.view * vec4( in_center, 1 ) ).xyz;

        sphereImpostor( viewSpaceSphereCenter, //
                        SmoothingRadius,
                        viewSpacePos,
                        viewSpaceNormal );
    }

    {
        vec4 clip    = push.proj * vec4( viewSpacePos, 1.0 );
        gl_FragDepth = clip.z / clip.w;
    }
    {
        vec3 worldNormal = transpose( mat3( push.view ) ) * viewSpaceNormal;
        out_normal       = encodeNormal( worldNormal );
    }
}
