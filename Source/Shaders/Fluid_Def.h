// Copyright (c) 2024 V.Shirokii
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

#ifndef FLUID_DEF_H_
#define FLUID_DEF_H_

#define PARTICLESPUSH_T           \
    ParticlesPush_T               \
    {                             \
        vec3  gravity;            \
        float deltaTime;          \
        uint  activeRingBegin;    \
        uint  activeRingLength;   \
        uint  generateRingBegin;  \
        uint  generateRingLength; \
    }

#ifdef __cplusplus

struct ShParticleDef
{
    float    position[ 3 ];
    uint32_t pad0;
    float    velocity[ 3 ];
    uint32_t pad1;
};

struct ShParticleSourceDef
{
    uint64_t position_dispersionAngle;
    uint64_t velocity_dispersion;
};

#else

struct ShParticleDef
{
    vec3 position;
    uint pad0;
    vec3 velocity;
    uint pad1;
};

struct ShParticleSourceDef
{
    f16vec3   position;
    float16_t velocityDispersionAngle;
    f16vec3   velocity;
    float16_t velocityDispersion;
};

#define PARTICLE_INVALID 0xFFFFFFFF

bool particle_isinvalid_unpacked( const ShParticleDef p )
{
    return isinf( p.position.x ) || isnan( p.position.x );
}

const float TargetDensity   = 630;

#if FLUID_DEF_SPEC_CONST

layout( constant_id = 0 ) const uint g_maxParticleCount = 0;
layout( constant_id = 1 ) const float SmoothingRadius = 0.1;

#endif // FLUID_DEF_SPEC_CONST

#endif // __cplusplus

#endif // FLUID_DEF_H_
