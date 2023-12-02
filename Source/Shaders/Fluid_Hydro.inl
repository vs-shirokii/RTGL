/*

MIT License

Copyright (c) 2024 V.Shirokii
Copyright (c) 2019 Sebastian Lague

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

#define FLT  float
#define FLT3 vec3
#define FLT4 vec4

// Smoothed-particle hydrodynamics

#define FLUID_SPH 0
#if FLUID_SPH

const FLT pressureMultiplier     = FLT( 2.88 );
const FLT nearPressureMultiplier = FLT( 2.25 );


FLT SmoothingKernelPoly6(FLT dst, FLT radius)
{
    if (dst < radius)
    {
		FLT scale = FLT(315) / (FLT(64) * FLT(M_PI) * pow(abs(radius), FLT(9)));
		FLT v = radius * radius - dst * dst;
		return v * v * v * scale;
	}
    return FLT( 0 );
}


FLT SpikyKernelPow3(FLT dst, FLT radius)
{
    if (dst < radius)
	{
		FLT scale = FLT(15) / (FLT(M_PI) * pow(radius, FLT(6)));
		FLT v = radius - dst;
		return v * v * v * scale;
	}
    return FLT( 0 );
}


//Integrate[(h-r)^2 r^2 Sin[θ], {r, 0, h}, {θ, 0, π}, {φ, 0, 2*π}]
FLT SpikyKernelPow2(FLT dst, FLT radius)
{
    if (dst < radius)
	{
		FLT scale = FLT(15) / (FLT(2) * FLT(M_PI) * pow(radius, FLT(5)));
		FLT v = radius - dst;
		return v * v * scale;
	}
    return FLT( 0 );
}


FLT DerivativeSpikyPow3(FLT dst, FLT radius)
{
    if (dst < radius)
	{
		FLT scale = FLT(45) / (pow(radius, FLT(6)) * FLT(M_PI));
		FLT v = radius - dst;
		return -v * v * scale;
	}
    return FLT( 0 );
}


FLT DerivativeSpikyPow2(FLT dst, FLT radius)
{
    if (dst < radius)
	{
		FLT scale = FLT(15) / (pow(radius, FLT(5)) * FLT(M_PI));
		FLT v = radius - dst;
		return -v * scale;
    }
    return FLT( 0 );
}

FLT DensityKernel(FLT dst, FLT radius)
{
	//return SmoothingKernelPoly6(dst, radius);
	return SpikyKernelPow2(dst, radius);
}

FLT NearDensityKernel(FLT dst, FLT radius)
{
	return SpikyKernelPow3(dst, radius);
}

FLT DensityDerivative(FLT dst, FLT radius)
{
	return DerivativeSpikyPow2(dst, radius);
}

FLT NearDensityDerivative(FLT dst, FLT radius)
{
	return DerivativeSpikyPow3(dst, radius);
}

FLT PressureFromDensity(FLT density)
{
	return (density - FLT(TargetDensity)) * pressureMultiplier;
}

FLT NearPressureFromDensity(FLT nearDensity)
{
	return nearDensity * nearPressureMultiplier;
}

uint hashCombine( uint seed, uint v )
{
    seed ^= v + 0x9e3779b9 + ( seed << 6 ) + ( seed >> 2 );
    return seed;
}

#define foreach_neighbour_particle( cur_id )                                             \
    for( uint rsample = 0; rsample < 8; rsample++ )                                      \
    {                                                                                    \
        /* NOTE: rnd01 is a float that must have precision 1/g_particleCount, */         \
        /*       otherwise, need a better sampling based on particle position region; */ \
        /*       at the moment, it's very bad 1/60000, and it would sample uniformly */  \
        const float rnd01 = rnd16( hashCombine( gl_GlobalInvocationID.x, rsample ),      \
                                   uint( push.deltaTime * 1000000 ) );                   \
                                                                                         \
        const uint other_globalId = uint( rnd01 * g_particleCount );                     \
        if( other_globalId == cur_id )                                                   \
        {                                                                                \
            continue;                                                                    \
        }                                                                                \
        const ShParticleDef other = g_particlesArray[ other_globalId ];                  \
        if( particle_isinvalid_unpacked( other ) )                                       \
        {                                                                                \
            continue;                                                                    \
        }

#define foreach_end }

void SPH_calcVelocityAndDensityFromOtherParticles( const uint cur_id,
                                                   const FLT3 cur_position,
                                                   inout FLT3 cur_velocity,
                                                   out FLT    cur_density,
                                                   out FLT    cur_nearDensity )
{
    // Density
    cur_density     = DensityKernel( 0, SmoothingRadius );
    cur_nearDensity = NearDensityKernel( 0, SmoothingRadius );

    foreach_neighbour_particle( cur_id ) // -> other
    {
        const FLT3 offsetToNeighbour = other.position - cur_position;
        const FLT  dst               = length( offsetToNeighbour );

        cur_density += DensityKernel( dst, SmoothingRadius );
        cur_nearDensity += NearDensityKernel( dst, SmoothingRadius );
    }
    foreach_end;


    // Pressure / Viscosity
    FLT3 pressureForce  = FLT3( 0 );
    FLT3 viscosityForce = FLT3( 0 );
    {
        const FLT cur_pressure     = PressureFromDensity( cur_density );
        const FLT cur_nearPressure = NearPressureFromDensity( cur_nearDensity );

        foreach_neighbour_particle( cur_id ) // -> other
        {
            const FLT sharedPressure =
                ( cur_pressure + PressureFromDensity( FLT( other.density ) ) ) * FLT( 0.5 );

            const FLT sharedNearPressure =
                ( cur_nearPressure + NearPressureFromDensity( FLT( other.nearDensity ) ) ) *
                FLT( 0.5 );

            const FLT3 offsetToNeighbour = other.position - cur_position;

            const FLT  dst = length( offsetToNeighbour );
            const FLT3 dir = dst > FLT( 0 ) ? offsetToNeighbour / dst : FLT3( 0, 1, 0 );

            pressureForce += dir * DensityDerivative( dst, SmoothingRadius ) * //
                             sharedPressure / FLT( other.density );

            pressureForce += dir * NearDensityDerivative( dst, SmoothingRadius ) *
                             sharedNearPressure / FLT( other.nearDensity );

            viscosityForce += ( other.velocity - cur_velocity ) * //
                              SmoothingKernelPoly6( dst, SmoothingRadius );
        }
        foreach_end;
    }

    FLT3 acceleration = cur_density > 0.00001 ? pressureForce / cur_density : FLT3( 0 );
    acceleration += viscosityForce * ViscosityStrength;

    cur_velocity += acceleration * deltaTime();
}

#undef foreach_end
#undef foreach_neighbour_particle

#endif // FLUID_SPH
