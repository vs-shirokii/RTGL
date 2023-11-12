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

#ifndef UTILS_H_
#define UTILS_H_



#define M_PI        3.14159265358979323846
#define UINT32_MAX  0xFFFFFFFF
#define UINT16_MAX  65535
#define UINT8_MAX   255



vec4 unpackLittleEndianUintColor(uint c)
{
    return vec4(
         (c & 0x000000FF)        / 255.0,
        ((c & 0x0000FF00) >> 8)  / 255.0,
        ((c & 0x00FF0000) >> 16) / 255.0,
        ((c & 0xFF000000) >> 24) / 255.0
    );
}

uint packLittleEndianUintColor(const vec4 c)
{
    return
        (uint(c.r * 255.0) & 0x000000FF)        |
        (uint(c.g * 255.0) & 0x000000FF) << 8   |
        (uint(c.b * 255.0) & 0x000000FF) << 16  |
        (uint(c.a * 255.0) & 0x000000FF) << 24  ;
}

#define unpackUintColor unpackLittleEndianUintColor

float getLuminance(vec3 c)
{
    return 0.2125 * c.r + 0.7154 * c.g + 0.0721 * c.b;
}

float saturate(float a)
{
    return clamp(a, 0.0, 1.0);
}

float lengthSquared(const vec3 v)
{
    return dot(v, v);
}

float safePositiveRcp(float f)
{
    return f <= 0.0 ? 0.0 : 1.0 / f;
}

float square(float x)
{
    return x * x;
}



struct DirectionAndLength { vec3 dir; float len; };

DirectionAndLength calcDirectionAndLength(const vec3 start, const vec3 end)
{
    DirectionAndLength r;
    r.dir = end - start;
    r.len = length(r.dir);
    r.dir /= r.len;

    return r;
}

DirectionAndLength calcDirectionAndLengthSafe(const vec3 start, const vec3 end)
{
    DirectionAndLength r;
    r.dir = end - start;
    r.len = max(length(r.dir), 0.001);
    r.dir /= r.len;

    return r;
}



/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* https://github.com/godotengine/godot/blob/master/AUTHORS.md            */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

vec2 uint_to_vec2(uint base) {
	uvec2 decode = (uvec2(base) >> uvec2(0, 16)) & uvec2(0xFFFF, 0xFFFF);
	return vec2(decode) / vec2(65535.0, 65535.0) * 2.0 - 1.0;
}

vec3 oct_to_vec3(vec2 oct) {
	vec3 v = vec3(oct.xy, 1.0 - abs(oct.x) - abs(oct.y));
	float t = max(-v.z, 0.0);
	v.xy += t * -sign(v.xy);
	return normalize(v);
}

vec3 decode_uint_oct_to_norm(uint base) {
	return oct_to_vec3(uint_to_vec2(base));
}

vec4 decode_uint_oct_to_tang(uint base) {
	vec2 oct_sign_encoded = uint_to_vec2(base);
	// Binormal sign encoded in y component
	vec2 oct = vec2(oct_sign_encoded.x, abs(oct_sign_encoded.y) * 2.0 - 1.0);
	return vec4(oct_to_vec3(oct), sign(oct_sign_encoded.y));
}

vec2 signNotZero(vec2 v) {
	return mix(vec2(-1.0), vec2(1.0), greaterThanEqual(v.xy, vec2(0.0)));
}

uint vec2_to_uint(vec2 base) {
	uvec2 enc = uvec2(clamp(ivec2(base * vec2(65535, 65535)), ivec2(0), ivec2(0xFFFF, 0xFFFF))) << uvec2(0, 16);
	return enc.x | enc.y;
}

vec2 vec3_to_oct(vec3 e) {
	e /= abs(e.x) + abs(e.y) + abs(e.z);
	vec2 oct = e.z >= 0.0f ? e.xy : (vec2(1.0f) - abs(e.yx)) * signNotZero(e.xy);
	return oct * 0.5f + 0.5f;
}

uint encode_norm_to_uint_oct(vec3 base) {
	return vec2_to_uint(vec3_to_oct(base));
}

uint encode_tang_to_uint_oct(vec4 base) {
	vec2 oct = vec3_to_oct(base.xyz);
	// Encode binormal sign in y component
	oct.y = oct.y * 0.5f + 0.5f;
	oct.y = base.w >= 0.0f ? oct.y : 1 - oct.y;
	return vec2_to_uint(oct);
}

/**************************************************************************/
/**************************************************************************/



uint encodeNormal(vec3 n)
{
    return encode_norm_to_uint_oct(n);
}

vec3 decodeNormal(uint _packed)
{
    return decode_uint_oct_to_norm(_packed);
}

vec3 safeNormalize(const vec3 v)
{
    const float len = length(v);
    return len > 0.001 ? v / len : vec3(0, 1, 0);
}



// https://www.khronos.org/registry/OpenGL/extensions/EXT/EXT_texture_shared_exponent.txt

#define ENCODE_E5B9G9R9_EXPONENT_BITS 5
#define ENCODE_E5B9G9R9_MANTISSA_BITS 9
#define ENCODE_E5B9G9R9_MAX_VALID_BIASED_EXP 31
#define ENCODE_E5B9G9R9_EXP_BIAS 15

#define ENCODE_E5B9G9R9_MANTISSA_VALUES (1 << 9)
#define ENCODE_E5B9G9R9_MANTISSA_MASK (ENCODE_E5B9G9R9_MANTISSA_VALUES - 1)
// Equals to (((float)(MANTISSA_VALUES - 1))/MANTISSA_VALUES * (1<<(MAX_VALID_BIASED_EXP-EXP_BIAS)))
#define ENCODE_E5B9G9R9_SHAREDEXP_MAX 65408

uint encodeE5B9G9R9(vec3 unpacked)
{
    const int N = ENCODE_E5B9G9R9_MANTISSA_BITS;
    const int Np2 = 1 << N;
    const int B = ENCODE_E5B9G9R9_EXP_BIAS;

    unpacked = clamp(unpacked, vec3(0.0), vec3(ENCODE_E5B9G9R9_SHAREDEXP_MAX));
    float max_c = max(unpacked.r, max(unpacked.g, unpacked.b));

    // for log2
    if (max_c == 0.0)
    {
        return 0;
    }

    int exp_shared_p = max(-B-1, int(floor(log2(max_c)))) + 1 + B;
    int max_s = int(round(max_c * exp2(-(exp_shared_p - B - N))));

    int exp_shared = max_s != Np2 ? 
        exp_shared_p : 
        exp_shared_p + 1;

    float s = exp2(-(exp_shared - B - N));
    uvec3 rgb_s = uvec3(round(unpacked * s));

    return 
        (exp_shared << (3 * ENCODE_E5B9G9R9_MANTISSA_BITS)) |
        (rgb_s.b    << (2 * ENCODE_E5B9G9R9_MANTISSA_BITS)) |
        (rgb_s.g    << (1 * ENCODE_E5B9G9R9_MANTISSA_BITS)) |
        (rgb_s.r);
}

vec3 decodeE5B9G9R9(const uint _packed)
{
    const int N = ENCODE_E5B9G9R9_MANTISSA_BITS;
    const int B = ENCODE_E5B9G9R9_EXP_BIAS;

    int exp_shared = int(_packed >> (3 * ENCODE_E5B9G9R9_MANTISSA_BITS));
    float s = exp2(exp_shared - B - N);

    return s * vec3(
        (_packed                                       ) & ENCODE_E5B9G9R9_MANTISSA_MASK, 
        (_packed >> (1 * ENCODE_E5B9G9R9_MANTISSA_BITS)) & ENCODE_E5B9G9R9_MANTISSA_MASK,
        (_packed >> (2 * ENCODE_E5B9G9R9_MANTISSA_BITS)) & ENCODE_E5B9G9R9_MANTISSA_MASK
    );
}

#endif // UTILS_H_