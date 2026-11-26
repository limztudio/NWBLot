// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "push_constants.slangi"
#include "effect_records.h"
#include "glyph_coverage.slangi"
#include "glyph_sdf.slangi"
#include "../bindless/bindless_heap.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbUiPixelInput{
    [[vk::location(NWB_UI_RASTER_UV_LOCATION)]] float2 uv : TEXCOORD0;
    [[vk::location(NWB_UI_RASTER_COLOR_LOCATION)]] float4 color : COLOR0;
    [[vk::location(NWB_UI_RASTER_LOGICAL_POS_LOCATION)]] float2 logicalPos : TEXCOORD1;
};

struct NwbUiPixelOutput{
    [[vk::location(NWB_UI_COLOR_TARGET_LOCATION)]] float4 color : SV_Target0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


float nwbUiErf(const float x){
    const float s = abs(x);
    const float t = 1.0 + s * (0.278393 + s * (0.230389 + s * (0.000972 + s * 0.078108)));
    const float t2 = t * t;
    const float res = 1.0 - 1.0 / (t2 * t2);
    if(x < 0.0)
        return -res;
    return res;
}

float nwbUiSdRoundedBox(const float2 p, const float2 halfSize, const float radius){
    const float2 inner = max(halfSize - float2(radius, radius), float2(0.0, 0.0));
    const float2 q = abs(p) - inner;
    return length(max(q, float2(0.0, 0.0))) + min(max(q.x, q.y), 0.0) - radius;
}

float nwbUiApplySpread(const uint spread, const float rawT){
    if(spread == NWB_UI_GRADIENT_SPREAD_REPEAT)
        return frac(rawT);
    if(spread == NWB_UI_GRADIENT_SPREAD_MIRROR){
        const float period = frac(rawT * 0.5) * 2.0;
        if(period > 1.0)
            return 2.0 - period;
        return period;
    }
    return clamp(rawT, 0.0, 1.0);
}

float nwbUiEvaluateAnalyticClipMask(const float2 pos, const uint bufferSlot, const uint headOffset){
    if(headOffset == NWB_UI_INVALID_HEAP_SLOT || bufferSlot == NWB_UI_INVALID_HEAP_SLOT)
        return 1.0;
    const ByteAddressBuffer buf = nwbHeapRawBufferUniform(bufferSlot);
    float mask = 1.0;
    uint offset = headOffset;
    for(uint step = 0u; step < NWB_UI_MAX_ANALYTIC_CLIP_DEPTH; ++step){
        if(offset == NWB_UI_INVALID_HEAP_SLOT)
            break;
        const uint4 meta = buf.Load4(offset);
        const float4 box = asfloat(buf.Load4(offset + 16u));
        const float4 invM = asfloat(buf.Load4(offset + 32u));
        const uint kind = meta.x;
        const float cornerRadius = asfloat(meta.z);
        const float2 delta = pos - box.xy;
        const float2 localPos = float2(invM.x * delta.x + invM.y * delta.y, invM.z * delta.x + invM.w * delta.y);
        float sd = 0.0;
        if(kind == NWB_UI_CLIP_ELLIPSE){
            const float2 r = max(box.zw, float2(1.0e-5, 1.0e-5));
            const float k0 = length(localPos / r);
            const float k1 = length(localPos / (r * r));
            if(k1 > 1.0e-6)
                sd = (k0 * (k0 - 1.0)) / k1;
            else
                sd = -min(r.x, r.y);
        }
        else{
            sd = nwbUiSdRoundedBox(localPos, box.zw, cornerRadius);
        }
        const float aa = max(fwidth(sd), 1.0e-4);
        const float coverage = clamp(0.5 - sd / aa, 0.0, 1.0);
        mask *= coverage;
        offset = meta.y;
    }
    return mask;
}

float4 nwbUiEvaluateGradient(const float2 pos, const uint bufferSlot, const uint effectOffset, const float4 vertexColor){
    if(bufferSlot == NWB_UI_INVALID_HEAP_SLOT || effectOffset == NWB_UI_INVALID_HEAP_SLOT)
        return float4(0.0, 0.0, 0.0, 0.0);
    const ByteAddressBuffer buf = nwbHeapRawBufferUniform(bufferSlot);
    const uint4 hdr0 = buf.Load4(effectOffset);
    const float4 hdr1 = asfloat(buf.Load4(effectOffset + 16u));
    const float4 hdr2 = asfloat(buf.Load4(effectOffset + 32u));
    const uint kind = hdr0.x;
    const uint spread = hdr0.y;
    const uint stopCount = min(hdr0.z, NWB_UI_MAX_GRADIENT_STOPS);
    if(stopCount == 0u)
        return float4(0.0, 0.0, 0.0, 0.0);

    float rawT = 0.0;
    if(kind == NWB_UI_GRADIENT_LINEAR){
        const float2 d = hdr1.zw - hdr1.xy;
        rawT = dot(pos - hdr1.xy, d) * hdr2.z;
    }
    else if(kind == NWB_UI_GRADIENT_CONIC){
        const float2 delta = pos - hdr1.xy;
        const float angle = atan2(delta.y, delta.x) - hdr1.z;
        rawT = frac(angle * 0.15915494309189533577);
    }
    else{
        const float2 d = hdr1.zw - hdr1.xy;
        const float r0 = hdr2.x;
        const float r1 = hdr2.y;
        const float dr = r1 - r0;
        const float2 dp = pos - hdr1.xy;
        const float dLenSq = dot(d, d);
        if(dLenSq <= 1.0e-8){
            const float dist = length(dp);
            if(abs(dr) > 1.0e-6)
                rawT = (dist - r0) / dr;
            else if(dist >= r0)
                rawT = 1.0;
        }
        else{
            const float a = dLenSq - dr * dr;
            const float halfB = -(dot(dp, d) + r0 * dr);
            const float c = dot(dp, dp) - r0 * r0;
            if(abs(a) <= 1.0e-6){
                if(abs(halfB) > 1.0e-6)
                    rawT = 0.5 * c / halfB;
            }
            else{
                const float disc = halfB * halfB - a * c;
                if(disc < 0.0)
                    return float4(0.0, 0.0, 0.0, 0.0);
                const float sqrtDisc = sqrt(disc);
                float signedSqrt = sqrtDisc;
                if(a < 0.0)
                    signedSqrt = -sqrtDisc;
                rawT = (-halfB + signedSqrt) / a;
                if(r0 + rawT * dr < 0.0){
                    const float otherT = (-halfB - signedSqrt) / a;
                    if(r0 + otherT * dr >= 0.0)
                        rawT = otherT;
                    else
                        return float4(0.0, 0.0, 0.0, 0.0);
                }
            }
        }
    }

    const float t = nwbUiApplySpread(spread, rawT);
    const uint stopsBase = effectOffset + NWB_UI_GRADIENT_HEADER_BYTES;
    const uint offsetGroups = (stopCount + 3u) >> 2u;
    const uint colorsBase = stopsBase + offsetGroups * NWB_UI_GRADIENT_OFFSET_GROUP_BYTES;
    float prevOffset = 0.0;
    float nextOffset = 0.0;
    uint hi = stopCount;
    for(uint g = 0u; g < offsetGroups; ++g){
        const uint baseIdx = g * 4u;
        const float4 grp = asfloat(buf.Load4(stopsBase + g * NWB_UI_GRADIENT_OFFSET_GROUP_BYTES));
        if(baseIdx < stopCount){
            if(baseIdx == 0u){
                if(t <= grp.x){
                    hi = 0u;
                    break;
                }
                prevOffset = grp.x;
            }
            else if(t <= grp.x){
                nextOffset = grp.x;
                hi = baseIdx;
                break;
            }
            else
                prevOffset = grp.x;
        }
        if(baseIdx + 1u < stopCount){
            if(t <= grp.y){
                nextOffset = grp.y;
                hi = baseIdx + 1u;
                break;
            }
            prevOffset = grp.y;
        }
        if(baseIdx + 2u < stopCount){
            if(t <= grp.z){
                nextOffset = grp.z;
                hi = baseIdx + 2u;
                break;
            }
            prevOffset = grp.z;
        }
        if(baseIdx + 3u < stopCount){
            if(t <= grp.w){
                nextOffset = grp.w;
                hi = baseIdx + 3u;
                break;
            }
            prevOffset = grp.w;
        }
    }
    if(hi == 0u)
        return asfloat(buf.Load4(colorsBase)) * vertexColor.a;
    if(hi >= stopCount)
        return asfloat(buf.Load4(colorsBase + (stopCount - 1u) * NWB_UI_GRADIENT_COLOR_BYTES)) * vertexColor.a;
    const float4 c0 = asfloat(buf.Load4(colorsBase + (hi - 1u) * NWB_UI_GRADIENT_COLOR_BYTES));
    const float4 c1 = asfloat(buf.Load4(colorsBase + hi * NWB_UI_GRADIENT_COLOR_BYTES));
    const float invSpan = 1.0 / max(nextOffset - prevOffset, 1.0e-6);
    const float factor = clamp((t - prevOffset) * invSpan, 0.0, 1.0);
    return lerp(c0, c1, factor) * vertexColor.a;
}

float4 nwbUiEvaluateDropShadow(const float2 pos, const uint bufferSlot, const uint effectOffset, const float4 vertexColor){
    if(bufferSlot == NWB_UI_INVALID_HEAP_SLOT || effectOffset == NWB_UI_INVALID_HEAP_SLOT)
        return float4(0.0, 0.0, 0.0, 0.0);
    const ByteAddressBuffer buf = nwbHeapRawBufferUniform(bufferSlot);
    const float4 rec0 = asfloat(buf.Load4(effectOffset));
    const float4 rec1 = asfloat(buf.Load4(effectOffset + 16u));
    const float2 p = pos - rec0.xy;
    const float2 halfSize = max(rec0.zw, float2(0.0, 0.0));
    const float corner = min(rec1.x, min(halfSize.x, halfSize.y));
    const float sigma = rec1.y;
    const bool isInset = rec1.z > 0.5;
    const float invSqrt2Sigma = rec1.w;

    float coverage = 0.0;
    if(sigma <= 1.0e-3){
        const float sd = nwbUiSdRoundedBox(p, halfSize, corner);
        coverage = clamp(0.5 - sd / max(fwidth(sd), 1.0e-4), 0.0, 1.0);
    }
    else{
        const float range = 3.5 * sigma;
        const float fullRange = 4.0 * sigma;
        const float absPx = abs(p.x);
        const float absPy = abs(p.y);
        const float innerHalfH = max(halfSize.y - corner, 0.0);
        const float innerHalfW = max(halfSize.x - corner, 0.0);
        if(absPx >= halfSize.x + range || absPy >= halfSize.y + range){
            coverage = 0.0;
        }
        else if(
            absPx + fullRange <= halfSize.x
            && absPy + fullRange <= halfSize.y
            && (corner <= 1.0e-4 || nwbUiSdRoundedBox(p, halfSize, corner) <= -fullRange)
        ){
            coverage = 1.0;
        }
        else{
            const float yMin = max(-halfSize.y - p.y, -range);
            const float yMax = min(halfSize.y - p.y, range);
            if(yMax > yMin){
                if(corner <= 1.0e-4 || absPy + range <= innerHalfH || absPx + fullRange <= innerHalfW){
                    const float yCover = 0.5 * (nwbUiErf(yMax * invSqrt2Sigma) - nwbUiErf(yMin * invSqrt2Sigma));
                    const float xCover = 0.5 * (
                        nwbUiErf((p.x + halfSize.x) * invSqrt2Sigma)
                        - nwbUiErf((p.x - halfSize.x) * invSqrt2Sigma)
                    );
                    coverage = clamp(xCover * yCover, 0.0, 1.0);
                }
                else{
                    const float stepY = (yMax - yMin) * 0.125;
                    float prevErfY = nwbUiErf(yMin * invSqrt2Sigma);
                    for(uint i = 0u; i < 8u; ++i){
                        const float yNextOffset = yMin + float(i + 1u) * stepY;
                        const float yMidRelBox = p.y + (yMin + (float(i) + 0.5) * stepY);
                        const float nextErfY = nwbUiErf(yNextOffset * invSqrt2Sigma);
                        const float weightY = 0.5 * (nextErfY - prevErfY);
                        prevErfY = nextErfY;

                        const float dy = max(abs(yMidRelBox) - innerHalfH, 0.0);
                        float rowHalfWidth = halfSize.x;
                        if(dy > 0.0 && corner > 0.0)
                            rowHalfWidth = innerHalfW + sqrt(max(corner * corner - dy * dy, 0.0));
                        const float xCover = 0.5 * (
                            nwbUiErf((p.x + rowHalfWidth) * invSqrt2Sigma)
                            - nwbUiErf((p.x - rowHalfWidth) * invSqrt2Sigma)
                        );
                        coverage += weightY * xCover;
                    }
                    coverage = clamp(coverage, 0.0, 1.0);
                }
            }
        }
    }
    if(isInset)
        coverage = 1.0 - coverage;
    return vertexColor * coverage;
}

float4 nwbUiEvaluateBlur(const float2 uv, const uint textureSlot, const uint samplerSlot, const uint bufferSlot, const uint effectOffset){
    if(textureSlot == NWB_UI_INVALID_HEAP_SLOT || samplerSlot == NWB_UI_INVALID_HEAP_SLOT || bufferSlot == NWB_UI_INVALID_HEAP_SLOT || effectOffset == NWB_UI_INVALID_HEAP_SLOT)
        return float4(0.0, 0.0, 0.0, 0.0);
    const Texture2D<float4> tex = nwbHeapSampledImage2D(textureSlot);
    const SamplerState samp = nwbHeapSampler(samplerSlot);
    const ByteAddressBuffer buf = nwbHeapRawBufferUniform(bufferSlot);
    const uint4 raw0 = buf.Load4(effectOffset);
    const float2 texelStep = asfloat(raw0.xy);
    const float centerWeight = asfloat(raw0.z);
    const uint pairCount = min(raw0.w, NWB_UI_MAX_BLUR_TAP_PAIRS);
    const float4 uvClamp = asfloat(buf.Load4(effectOffset + 16u));

    float4 accum = tex.SampleLevel(samp, clamp(uv, uvClamp.xy, uvClamp.zw), 0.0) * centerWeight;
    const uint pairsBase = effectOffset + NWB_UI_BLUR_HEADER_BYTES;
    for(uint p = 0u; p < pairCount; p += 2u){
        const float4 packed = asfloat(buf.Load4(pairsBase + (p >> 1u) * NWB_UI_BLUR_PAIR_GROUP_BYTES));
        const float2 delta0 = texelStep * packed.x;
        accum += (
            tex.SampleLevel(samp, clamp(uv + delta0, uvClamp.xy, uvClamp.zw), 0.0)
            + tex.SampleLevel(samp, clamp(uv - delta0, uvClamp.xy, uvClamp.zw), 0.0)
        ) * packed.y;
        if(p + 1u < pairCount){
            const float2 delta1 = texelStep * packed.z;
            accum += (
                tex.SampleLevel(samp, clamp(uv + delta1, uvClamp.xy, uvClamp.zw), 0.0)
                + tex.SampleLevel(samp, clamp(uv - delta1, uvClamp.xy, uvClamp.zw), 0.0)
            ) * packed.w;
        }
    }
    return accum;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NwbUiPixelOutput main(NwbUiPixelInput input){
    NwbUiPixelOutput output;
    if(g_NwbUiPushConstants.material == NWB_UI_MATERIAL_SOLID)
        output.color = input.color;
    else if(g_NwbUiPushConstants.material == NWB_UI_MATERIAL_GRADIENT)
        output.color = nwbUiEvaluateGradient(input.logicalPos, g_NwbUiPushConstants.effectBufferSlot, g_NwbUiPushConstants.effectOffset, input.color);
    else if(g_NwbUiPushConstants.material == NWB_UI_MATERIAL_DROP_SHADOW)
        output.color = nwbUiEvaluateDropShadow(input.logicalPos, g_NwbUiPushConstants.effectBufferSlot, g_NwbUiPushConstants.effectOffset, input.color);
    else if(g_NwbUiPushConstants.material == NWB_UI_MATERIAL_BLUR)
        output.color = nwbUiEvaluateBlur(input.uv, g_NwbUiPushConstants.textureSlot, g_NwbUiPushConstants.samplerSlot, g_NwbUiPushConstants.effectBufferSlot, g_NwbUiPushConstants.effectOffset);
    else{
        const Texture2D<float4> texture = nwbHeapSampledImage2D(g_NwbUiPushConstants.textureSlot);
        const SamplerState sampler = nwbHeapSampler(g_NwbUiPushConstants.samplerSlot);
        const float4 sampled = texture.SampleLevel(sampler, input.uv, 0.0);
        if(g_NwbUiPushConstants.material == NWB_UI_MATERIAL_GLYPH)
            output.color = nwbUiGlyphCoverageColor(input.color, sampled.r);
        else if(g_NwbUiPushConstants.material == NWB_UI_MATERIAL_SDF_GLYPH){
            const float encoded = nwbUiSdfChannel(sampled, g_NwbUiPushConstants.sdfChannel);
            const float distance = nwbUiSdfDistance(encoded, g_NwbUiPushConstants.sdfSpreadPixels);
            output.color = nwbUiGlyphCoverageColor(input.color, nwbUiSdfCoverage(distance, fwidth(distance)));
        }
        else if(g_NwbUiPushConstants.material == NWB_UI_MATERIAL_COLOR_GLYPH){
            const float alpha = sampled.a * input.color.a;
            output.color = float4(sampled.rgb * alpha, alpha);
        }
        else if(g_NwbUiPushConstants.material == NWB_UI_MATERIAL_EFFECT_LAYER_COMPOSITE)
            output.color = sampled * input.color.a;
        else
            output.color = float4(sampled.rgb * sampled.a * input.color.rgb, sampled.a * input.color.a);
    }

    if(g_NwbUiPushConstants.clipMaskOffset != NWB_UI_INVALID_HEAP_SLOT)
        output.color *= nwbUiEvaluateAnalyticClipMask(input.logicalPos, g_NwbUiPushConstants.effectBufferSlot, g_NwbUiPushConstants.clipMaskOffset);
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

