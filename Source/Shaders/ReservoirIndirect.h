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

#ifndef RESERVOIR_INDIRECT_H_
#define RESERVOIR_INDIRECT_H_

struct SampleIndirect
{
    uint positionXY;
    uint positionZ;
    uint normalPacked;
    uint radianceE5;
};

struct ReservoirIndirect
{
    SampleIndirect  selected;
    float           selected_targetPdf; // todo: remove? as it's easily calculated
    float           weightSum;
    uint            M;
};



SampleIndirect emptySampleIndirect()
{
    SampleIndirect s;
    s.positionXY   = 0; // packHalf2x16( vec2( 0, 0 ) );
    s.positionZ    = 0; // packHalf2x16( vec2( 0, 0 ) );
    s.normalPacked = 0;
    s.radianceE5   = 0; // encodeE5B9G9R9( vec3( 0 ) )
    return s;
}

SampleIndirect createSampleIndirect( const vec3 position, const vec3 normal, const vec3 radiance )
{
    SampleIndirect s;
    s.positionXY   = packHalf2x16( position.xy );
    s.positionZ    = packHalf2x16( vec2( position.z, 0 ) );
    s.normalPacked = encodeNormal( normal );
    s.radianceE5   = encodeE5B9G9R9( radiance );
    return s;
}

vec3 unpackSampleIndirectPosition( const SampleIndirect s )
{
    return vec3( unpackHalf2x16( s.positionXY ), //
                 unpackHalf2x16( s.positionZ ).r );
}

ReservoirIndirect emptyReservoirIndirect()
{
    ReservoirIndirect r;
    r.selected.positionXY   = 0; // packHalf2x16( vec2( 0, 0 ) );
    r.selected.positionZ    = 0; // packHalf2x16( vec2( 0, 0 ) );
    r.selected.normalPacked = 0;
    r.selected.radianceE5   = 0; // encodeE5B9G9R9( vec3( 0 ) )

    r.selected_targetPdf = 0.0;
    r.weightSum = 0.0;
    r.M = 0;
    return r;
}

float calcSelectedSampleWeightIndirect(const ReservoirIndirect r)
{
    return safePositiveRcp(r.selected_targetPdf) * (r.weightSum / float(max(1, r.M)));
}

void normalizeReservoirIndirect(inout ReservoirIndirect r, uint maxM)
{
    r.weightSum /= float(max(r.M, 1));

    r.M = clamp(r.M, 0, maxM);
    r.weightSum *= r.M;
}

void updateReservoirIndirect(
    inout ReservoirIndirect r,
    const SampleIndirect newSample, float targetPdf, float oneOverSourcePdf, 
    float rnd)
{
    float weight = targetPdf * oneOverSourcePdf;

    r.weightSum += weight;
    r.M += 1;

    if (rnd * r.weightSum < weight)
    {
        r.selected = newSample;
        r.selected_targetPdf = targetPdf;
    }
}

void initCombinedReservoirIndirect(out ReservoirIndirect combined, const ReservoirIndirect base)
{
    combined.selected = base.selected;
    combined.selected_targetPdf = base.selected_targetPdf;
    combined.weightSum = base.weightSum;
    combined.M = base.M;
}

bool updateCombinedReservoirIndirect(inout ReservoirIndirect combined, const ReservoirIndirect b, float rnd)
{
    float weight = b.weightSum;

    combined.weightSum += weight;
    combined.M += b.M;
    if (rnd * combined.weightSum < weight)
    {
        combined.selected = b.selected;
        combined.selected_targetPdf = b.selected_targetPdf;

        return true;
    }

    return false;
}

void updateCombinedReservoirIndirect_newSurf(inout ReservoirIndirect combined, const ReservoirIndirect b, float targetPdf_b, float rnd)
{
    // targetPdf_b is targetPdf(b.selected) for pixel q
    // but b.selected_targetPdf was calculated for pixel q'
    // so need to renormalize weight
    float weight = targetPdf_b * safePositiveRcp(b.selected_targetPdf) * b.weightSum;

    combined.weightSum += weight;
    combined.M += b.M;
    if (rnd * combined.weightSum < weight)
    {
        combined.selected = b.selected;
        combined.selected_targetPdf = targetPdf_b;
    }
}



#ifdef DESC_SET_GLOBAL_UNIFORM
#ifdef DESC_SET_RESTIR_INDIRECT
bool rgi_TryGetPixOffset(const ivec2 pix, out uint offset)
{
    offset = pix.y * uint(globalUniform.renderWidth) + pix.x;
    return offset >= 0 && offset < uint(globalUniform.renderWidth) * uint(globalUniform.renderHeight);
}

void restirIndirect_StoreInitialSample(const ivec2 pix, const SampleIndirect s, float oneOverSourcePdf)
{
    imageStore( framebufIndirectReservoirsInitial,
                pix,
                uvec4( s.positionXY,
                       packHalf2x16( vec2( unpackHalf2x16( s.positionZ ).x, oneOverSourcePdf ) ),
                       s.normalPacked,
                       s.radianceE5 ) );
}

#define STORAGE_MULT 100.0

void restirIndirect_StoreReservoir(const ivec2 pix, ReservoirIndirect r)
{
    uint offset;
    if (!rgi_TryGetPixOffset(pix, offset))
    {
        return;
    }
    
    normalizeReservoirIndirect( r, 20 );

    if (!isinf(r.weightSum) && !isnan(r.weightSum) && r.weightSum >= 0.0)
    {
        g_restirIndirectReservoirs[offset * PACKED_INDIRECT_RESERVOIR_SIZE_IN_WORDS + 0] = r.selected.positionXY;
        g_restirIndirectReservoirs[offset * PACKED_INDIRECT_RESERVOIR_SIZE_IN_WORDS + 1] = packHalf2x16( vec2( unpackHalf2x16( r.selected.positionZ ).x, r.M ) );
        g_restirIndirectReservoirs[offset * PACKED_INDIRECT_RESERVOIR_SIZE_IN_WORDS + 2] = r.selected.normalPacked;
        g_restirIndirectReservoirs[offset * PACKED_INDIRECT_RESERVOIR_SIZE_IN_WORDS + 3] = r.selected.radianceE5;
        g_restirIndirectReservoirs[offset * PACKED_INDIRECT_RESERVOIR_SIZE_IN_WORDS + 4] = packHalf2x16( vec2( r.selected_targetPdf / STORAGE_MULT, r.weightSum / STORAGE_MULT) );
    }
    else
    {
        g_restirIndirectReservoirs[offset * PACKED_INDIRECT_RESERVOIR_SIZE_IN_WORDS + 0] = 0;
        g_restirIndirectReservoirs[offset * PACKED_INDIRECT_RESERVOIR_SIZE_IN_WORDS + 1] = 0;
        g_restirIndirectReservoirs[offset * PACKED_INDIRECT_RESERVOIR_SIZE_IN_WORDS + 2] = 0;
        g_restirIndirectReservoirs[offset * PACKED_INDIRECT_RESERVOIR_SIZE_IN_WORDS + 3] = 0;
        g_restirIndirectReservoirs[offset * PACKED_INDIRECT_RESERVOIR_SIZE_IN_WORDS + 4] = 0;
    }

#if PACKED_INDIRECT_RESERVOIR_SIZE_IN_WORDS != 5
    #error "Size mismatch"
#endif
}

SampleIndirect restirIndirect_LoadInitialSample( const ivec2 pix, out float oneOverSourcePdf )
{
    SampleIndirect s;

    if( pix.x < 0 || pix.y < 0 || //
        pix.x > globalUniform.renderWidth || pix.y > globalUniform.renderHeight )
    {
        s = emptySampleIndirect();
        return s;
    }

    const uvec4 rpacked = imageLoad( framebufIndirectReservoirsInitial, pix );

    s.positionXY     = rpacked[ 0 ];
    s.positionZ      = rpacked[ 1 ];
    oneOverSourcePdf = unpackHalf2x16( rpacked[ 1 ] ).y;
    s.normalPacked   = rpacked[ 2 ];
    s.radianceE5     = rpacked[ 3 ];

    return s;
}

#define INDIR_LOAD_RESERVOIR_T(BUFFER_T) \
    r.selected.positionXY   =               ( BUFFER_T[ offset * PACKED_INDIRECT_RESERVOIR_SIZE_IN_WORDS + 0 ] ); \
    r.selected.positionZ    =               ( BUFFER_T[ offset * PACKED_INDIRECT_RESERVOIR_SIZE_IN_WORDS + 1 ] ); \
    r.selected.normalPacked =               ( BUFFER_T[ offset * PACKED_INDIRECT_RESERVOIR_SIZE_IN_WORDS + 2 ] ); \
    r.selected.radianceE5   =               ( BUFFER_T[ offset * PACKED_INDIRECT_RESERVOIR_SIZE_IN_WORDS + 3 ] ); \
    const vec2 tpws         = unpackHalf2x16( BUFFER_T[ offset * PACKED_INDIRECT_RESERVOIR_SIZE_IN_WORDS + 4 ] ); \
    r.selected_targetPdf    = tpws.x * STORAGE_MULT;                                                              \
    r.weightSum             = tpws.y * STORAGE_MULT;                                                              \
    r.M                     = uint( unpackHalf2x16( r.selected.positionZ ).y );

#if PACKED_INDIRECT_RESERVOIR_SIZE_IN_WORDS != 5
    #error "Size mismatch"
#endif

ReservoirIndirect restirIndirect_LoadReservoir(const ivec2 pix)
{
    ReservoirIndirect r;

    uint offset;
    if (!rgi_TryGetPixOffset(pix, offset))
    {
        r = emptyReservoirIndirect();
        return r;
    }

    INDIR_LOAD_RESERVOIR_T(g_restirIndirectReservoirs);
    return r;
}

ReservoirIndirect restirIndirect_LoadReservoir_Prev(const ivec2 pix)
{
    ReservoirIndirect r;

    uint offset;
    if (!rgi_TryGetPixOffset(pix, offset))
    {
        r = emptyReservoirIndirect();
        return r;
    }

    INDIR_LOAD_RESERVOIR_T(g_restirIndirectReservoirs_Prev);
    return r;
}

#endif // DESC_SET_RESTIR_INDIRECT
#endif // DESC_SET_GLOBAL_UNIFORM

#endif // RESERVOIR_INDIRECT_H_