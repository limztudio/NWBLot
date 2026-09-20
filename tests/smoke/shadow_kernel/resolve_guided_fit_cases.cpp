// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "resolve_guided_fit_cases.h"

#include <global/algorithm.h>
#include <global/math/convert.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{
namespace ShadowResolveGuidedFit{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] f32 Quantize(const f32 value){
    return ConvertHalfToFloat(ConvertFloatToHalf(value));
}

[[nodiscard]] Float4U Geometry(const Case& testCase, const u32 x, const u32 y){
    Float4U guide{ 0.0f, 0.0f, testCase.baseDistance + testCase.distanceStep * static_cast<f32>(x), 1.0f };
    const u32 phase = (x + y) % 3u;
    if(
        testCase.mask == Mask::AllInvalid
        || (testCase.mask == Mask::Mixed && phase == 0u)
        || (testCase.mask == Mask::SingleGuided && (x != 3u || y != 3u))
    )
        guide.w = 0.0f;
    if(testCase.mask == Mask::AllOpposite || (testCase.mask == Mask::Mixed && phase == 1u)){
        guide.x = 1.0f;
        guide.y = 1.0f;
    }
    return guide;
}

[[nodiscard]] f32 ReceiverDistance(const Case& testCase, const u32 x){
    return testCase.baseDistance + testCase.distanceStep * static_cast<f32>(x) * 0.5f + testCase.receiverOffset;
}

[[nodiscard]] Float4U Color(const Case& testCase, const u32 x, const u32 y, const u32 layer){
    const f32 edge = x >= 3u ? 1.0f : 0.0f;
    const Float4U color = testCase.uniformColor ? Float4U{ 0.25f, 0.5f, 0.75f, 1.0f }
        : Float4U{ edge, 1.0f - edge, 0.125f + 0.0625f * static_cast<f32>(x) + 0.03125f * static_cast<f32>(y), 1.0f };
    const u32 rotation = (layer + 1u) % 3u;
    return { color.raw[rotation], color.raw[(rotation + 1u) % 3u], color.raw[(rotation + 2u) % 3u], 1.0f };
}

[[nodiscard]] Float4U Prior(const u32 y, const u32 height, const u32 layer){
    if(y == height / 2u)
        return { 0.0f, 0.0f, 0.0f, 1.0f };
    return { 0.125f + 0.0625f * static_cast<f32>(layer), 0.5f, 0.75f, 1.0f };
}

// This oracle retains the original weighted moments and covariance equations, independently of coefficient factoring.
[[nodiscard]] Float4U Reference(
    const Case& testCase,
    const u32 width,
    const u32 height,
    const u32 x,
    const u32 y,
    const u32 layer,
    const bool rgb,
    const bool multiply,
    const ReferenceInput& input){
    const Float4U prior = Prior(y, height, layer);
    if(layer < input.slotStart || layer >= Min(input.slotStart + input.slotCount, s_LayerCount))
        return prior;
    if(x == 0u || y == 0u)
        return rgb && multiply ? prior : Float4U{ 1.0f, 1.0f, 1.0f, 1.0f };
    const i32 halfWidth = static_cast<i32>(DivideUp(width, 2u));
    const i32 halfHeight = static_cast<i32>(DivideUp(height, 2u));
    const i32 baseX = static_cast<i32>(x / 2u);
    const i32 baseY = static_cast<i32>(y / 2u);
    // Exact factor-two B-spline phases. Odd pixels interpolate the two adjacent samples; even pixels use 1:6:1.
    const f32 evenWeights[] = { 0.125f, 0.75f, 0.125f };
    const f32 oddWeights[] = { 0.0f, 0.5f, 0.5f };
    const f32* const weightsX = (x & 1u) != 0u ? oddWeights : evenWeights;
    const f32* const weightsY = (y & 1u) != 0u ? oddWeights : evenWeights;
    const f32 centerDistance = Quantize(ReceiverDistance(testCase, x));
    f32 sumG = 0.0f;
    f32 sumG2 = 0.0f;
    f32 guidedWeight = 0.0f;
    f32 validWeight = 0.0f;
    f32 sumP[3]{};
    f32 sumGP[3]{};
    f32 anySum[3]{};
    for(i32 dy = -1; dy <= 1; ++dy){
        for(i32 dx = -1; dx <= 1; ++dx){
            const f32 weight = weightsX[dx + 1] * weightsY[dy + 1];
            if(weight <= 0.0f)
                continue;
            const u32 tapX = static_cast<u32>(Clamp(baseX + dx, 0, halfWidth - 1));
            const u32 tapY = static_cast<u32>(Clamp(baseY + dy, 0, halfHeight - 1));
            const Float4U geometry = Geometry(testCase, tapX, tapY);
            if(geometry.w <= 0.5f)
                continue;
            const Float4U color = Color(testCase, tapX, tapY, layer + input.layerOffset);
            validWeight += weight;
            for(u32 channel = 0u; channel < 3u; ++channel)
                anySum[channel] += weight * Quantize(input.scale * color.raw[rgb ? channel : 0u] + input.bias);
            // The fixture encodes precisely +Z or -Z normals; only +Z passes the production normal gate.
            if(geometry.x == 1.0f)
                continue;
            const f32 g = Quantize(geometry.z) - centerDistance;
            guidedWeight += weight;
            sumG += weight * g;
            sumG2 += weight * g * g;
            for(u32 channel = 0u; channel < 3u; ++channel){
                const f32 p = Quantize(input.scale * color.raw[rgb ? channel : 0u] + input.bias);
                sumP[channel] += weight * p;
                sumGP[channel] += weight * g * p;
            }
        }
    }
    const f32 inverseWeight = guidedWeight > 0.0f ? 1.0f / guidedWeight : 0.0f;
    const f32 meanG = sumG * inverseWeight;
    const f32 varianceG = Max(sumG2 * inverseWeight - meanG * meanG, 0.0f);
    const f32 epsilon = 0.02f * centerDistance;
    Float4U result{ 1.0f, 1.0f, 1.0f, 1.0f };
    for(u32 channel = 0u; channel < 3u; ++channel){
        if(guidedWeight > 0.0f){
            const f32 meanP = sumP[channel] * inverseWeight;
            const f32 covariance = sumGP[channel] * inverseWeight - meanG * meanP;
            const f32 slope = covariance / (varianceG + epsilon * epsilon);
            result.raw[channel] = Clamp(meanP - slope * meanG, 0.0f, 1.0f);
        }
        else if(validWeight > 0.0f)
            result.raw[channel] = anySum[channel] / validWeight;
        if(rgb && multiply)
            result.raw[channel] *= Quantize(prior.raw[channel]);
    }
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

