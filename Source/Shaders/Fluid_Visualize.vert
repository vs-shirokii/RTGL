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
#include "Random.h"

layout( location = 0 ) out vec2 out_uv;
layout( location = 1 ) out vec3 out_center;

#define DESC_SET_FLUID 0

layout( set     = DESC_SET_FLUID,
        binding = BINDING_FLUID_PARTICLES_ARRAY ) readonly buffer ParticlesArray_T
{
    ShParticleDef g_particlesArray[];
};

layout( push_constant ) uniform VisualizePush_T
{
    layout( offset = 0 ) mat4  proj;
    layout( offset = 64 ) mat4 view;
}
push;

vec3 getQuadCornerPosition()
{
    // 0: -1 -1
    // 1: -1  1
    // 2:  1 -1
    // 3:  1  1

    return vec3( float( gl_VertexIndex / 2 ) * 2 - 1, //
                 float( gl_VertexIndex % 2 ) * 2 - 1,
                 0 );
}

const float BoxCorrection = 1.5;

void main()
{
    const uint          id       = gl_InstanceIndex % g_maxParticleCount;
    const ShParticleDef particle = g_particlesArray[ id ];

    const vec3 q = getQuadCornerPosition();

    vec3 quadVertex = vec3( particle.position );
    quadVertex += transpose( mat3( push.view ) ) * q * SmoothingRadius * BoxCorrection;

    gl_Position = push.proj * push.view * vec4( quadVertex, 1.0 );
    out_uv      = q.xy * BoxCorrection;
    out_center  = vec3( particle.position );
}
