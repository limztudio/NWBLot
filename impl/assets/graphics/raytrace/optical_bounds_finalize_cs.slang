// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "optical_bounds_finalize_constants.h"
#include "optical_scene_constants.h"
#include "../mesh/runtime_bounds_constants.h"
#include "../bindless/bindless_heap.slangi"
#include "../common/math.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static const uint NWB_OPTICAL_BOUNDS_RUNTIME_ROW_BYTES = 16u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbOpticalBoundsFinalizePushConstants{
    uint sourceSceneSlot;
    uint runtimeInputsSlot;
    uint outputSceneSlot;
    uint instanceCount;
    uint runtimeCount;
    uint staticBoundsComplete;
};

[[vk::push_constant]] ConstantBuffer<NwbOpticalBoundsFinalizePushConstants> g_NwbOpticalBoundsFinalize;

groupshared float3 g_NwbOpticalMinimum[NWB_OPTICAL_BOUNDS_FINALIZE_GROUP_SIZE];
groupshared float3 g_NwbOpticalMaximum[NWB_OPTICAL_BOUNDS_FINALIZE_GROUP_SIZE];
groupshared uint g_NwbOpticalInvalid[NWB_OPTICAL_BOUNDS_FINALIZE_GROUP_SIZE];


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool nwbOpticalAdmittedInput(const uint bits){
    const uint magnitude = bits & NWB_OPTICAL_BOUNDS_MAGNITUDE_MASK;
    if(magnitude == 0u)
        return true;
    const int exponent = int(magnitude >> NWB_OPTICAL_BOUNDS_MANTISSA_BITS) - NWB_OPTICAL_BOUNDS_EXPONENT_BIAS;
    return exponent >= NWB_OPTICAL_BOUNDS_MIN_INPUT_EXPONENT && exponent <= NWB_OPTICAL_BOUNDS_MAX_INPUT_EXPONENT;
}

uint nwbOpticalOrderedBits(const uint bits){
    if((bits & NWB_OPTICAL_BOUNDS_MAGNITUDE_MASK) == 0u)
        return NWB_OPTICAL_BOUNDS_SIGN_BIT;
    return (bits & NWB_OPTICAL_BOUNDS_SIGN_BIT) != 0u ? ~bits : bits ^ NWB_OPTICAL_BOUNDS_SIGN_BIT;
}

int nwbOpticalCeilingExponent(const uint magnitude){
    const int exponent = int(magnitude >> NWB_OPTICAL_BOUNDS_MANTISSA_BITS) - NWB_OPTICAL_BOUNDS_EXPONENT_BIAS;
    return exponent + ((magnitude & NWB_OPTICAL_BOUNDS_FRACTION_MASK) != 0u ? 1 : 0);
}

bool nwbOpticalMagnitudeExponent(const uint4 row, const uint3 localMagnitude, out int exponent){
    exponent = NWB_OPTICAL_BOUNDS_ZERO_EXPONENT;
    int largest = NWB_OPTICAL_BOUNDS_ZERO_EXPONENT;
    uint count = 0u;
    for(uint lane = 0u; lane < 4u; ++lane){
        if(!nwbOpticalAdmittedInput(row[lane]))
            return false;
        const uint coefficient = row[lane] & NWB_OPTICAL_BOUNDS_MAGNITUDE_MASK;
        uint coordinate = NWB_OPTICAL_BOUNDS_ONE_BITS;
        if(lane < 3u)
            coordinate = localMagnitude[lane];
        if(coefficient != 0u && coordinate != 0u){
            largest = max(largest, nwbOpticalCeilingExponent(coefficient) + nwbOpticalCeilingExponent(coordinate));
            ++count;
        }
    }
    exponent = NWB_OPTICAL_BOUNDS_ZERO_EXPONENT;
    if(count == 0u)
        return true;
    exponent = largest + (count > 2u ? 2 : (count > 1u ? 1 : 0));
    return exponent <= NWB_OPTICAL_BOUNDS_MAX_MAGNITUDE_EXPONENT;
}

uint nwbOpticalFixedBits(const int value, const int exponent){
    if(value == 0)
        return 0u;
    const uint sign = value < 0 ? NWB_OPTICAL_BOUNDS_SIGN_BIT : 0u;
    const uint magnitude = uint(value < 0 ? -value : value);
    const int highest = firstbithigh(magnitude);
    const int resultExponent = exponent - NWB_OPTICAL_BOUNDS_FIXED_BITS + highest;
    if(resultExponent < -126)
        return 0u;
    const uint significand = highest > 23 ? magnitude >> (uint(highest - 23) & 31u) : magnitude << (uint(23 - highest) & 31u);
    return sign | (uint(resultExponent + NWB_OPTICAL_BOUNDS_EXPONENT_BIAS) << NWB_OPTICAL_BOUNDS_MANTISSA_BITS) | (significand & NWB_OPTICAL_BOUNDS_FRACTION_MASK);
}

// Four 24-bit significand products, represented by low/high 32-bit words.
void nwbOpticalMantissaProducts(const uint4 a, const uint4 b, out uint4 low, out uint4 high){
    const uint4 aLow = a & 0xfffu;
    const uint4 bLow = b & 0xfffu;
    const uint4 first = aLow * bLow;
    const uint4 middle = (first >> 12u) + (a >> 12u) * bLow + aLow * (b >> 12u);
    const uint4 upper = (middle >> 12u) + (a >> 12u) * (b >> 12u);
    low = (first & 0xfffu) | ((middle & 0xfffu) << 12u) | (upper << 24u);
    high = upper >> 8u;
}

uint nwbOpticalEndpoint(const uint4 row, const uint3 localMin, const uint3 localMax, const int exponent, const bool upper){
    uint4 point = uint4(localMin, NWB_OPTICAL_BOUNDS_ONE_BITS);
    for(uint lane = 0u; lane < 3u; ++lane){
        if(upper != ((row[lane] & NWB_OPTICAL_BOUNDS_SIGN_BIT) != 0u))
            point[lane] = localMax[lane];
    }
    uint4 a = uint4(0u);
    uint4 b = uint4(0u);
    for(uint lane = 0u; lane < 4u; ++lane){
        if((row[lane] & NWB_OPTICAL_BOUNDS_MAGNITUDE_MASK) != 0u)
            a[lane] = (row[lane] & NWB_OPTICAL_BOUNDS_FRACTION_MASK) | NWB_OPTICAL_BOUNDS_HIDDEN_BIT;
        if((point[lane] & NWB_OPTICAL_BOUNDS_MAGNITUDE_MASK) != 0u)
            b[lane] = (point[lane] & NWB_OPTICAL_BOUNDS_FRACTION_MASK) | NWB_OPTICAL_BOUNDS_HIDDEN_BIT;
    }
    uint4 low;
    uint4 high;
    nwbOpticalMantissaProducts(a, b, low, high);
    int total = 0;
    for(uint lane = 0u; lane < 4u; ++lane){
        if(a[lane] == 0u || b[lane] == 0u)
            continue;
        const int aExponent = int((row[lane] & NWB_OPTICAL_BOUNDS_MAGNITUDE_MASK) >> NWB_OPTICAL_BOUNDS_MANTISSA_BITS);
        const int bExponent = int((point[lane] & NWB_OPTICAL_BOUNDS_MAGNITUDE_MASK) >> NWB_OPTICAL_BOUNDS_MANTISSA_BITS);
        const int shift = exponent + 2 * (NWB_OPTICAL_BOUNDS_EXPONENT_BIAS + int(NWB_OPTICAL_BOUNDS_MANTISSA_BITS)) - NWB_OPTICAL_BOUNDS_FIXED_BITS - aExponent - bExponent;
        if(shift < 64){
            const uint scaled = shift >= 32 ? high[lane] >> (uint(shift - 32) & 31u) : (low[lane] >> (uint(shift) & 31u)) | (high[lane] << (uint(32 - shift) & 31u));
            total += ((row[lane] ^ point[lane]) & NWB_OPTICAL_BOUNDS_SIGN_BIT) != 0u ? -int(scaled) : int(scaled);
        }
    }
    return nwbOpticalFixedBits(total, exponent);
}

uint nwbOpticalExpandedBits(const uint bits, const int exponent, const bool upper){
    const int marginExponent = max(exponent - NWB_OPTICAL_BOUNDS_RELATIVE_MARGIN_SHIFT, NWB_OPTICAL_BOUNDS_MIN_MARGIN_EXPONENT);
    const uint twiceMargin = uint(marginExponent + 1 + NWB_OPTICAL_BOUNDS_EXPONENT_BIAS) << NWB_OPTICAL_BOUNDS_MANTISSA_BITS;
    uint magnitude = bits & NWB_OPTICAL_BOUNDS_MAGNITUDE_MASK;
    if(magnitude < twiceMargin){
        const uint result = uint(marginExponent + 2 + NWB_OPTICAL_BOUNDS_EXPONENT_BIAS) << NWB_OPTICAL_BOUNDS_MANTISSA_BITS;
        return upper ? result : result | NWB_OPTICAL_BOUNDS_SIGN_BIT;
    }
    const int valueExponent = int(magnitude >> NWB_OPTICAL_BOUNDS_MANTISSA_BITS) - NWB_OPTICAL_BOUNDS_EXPONENT_BIAS;
    const uint step = 1u << uint(max(0, marginExponent - valueExponent + 24));
    magnitude = upper != ((bits & NWB_OPTICAL_BOUNDS_SIGN_BIT) != 0u) ? magnitude + step : magnitude - step;
    return (bits & NWB_OPTICAL_BOUNDS_SIGN_BIT) | magnitude;
}

bool nwbOpticalNormalBounds(const float3 value){
    const uint3 magnitude = asuint(value) & NWB_OPTICAL_BOUNDS_MAGNITUDE_MASK;
    return all(magnitude <= NWB_FLOAT_MAX_BITS) && all((magnitude == uint3(0u)) | (magnitude >= uint3(NWB_FLOAT_MIN_NORMAL_BITS)));
}

bool nwbOpticalTransformRuntimeBounds(const uint inputOffset, out float3 boundsMin, out float3 boundsMax){
    boundsMin = float3(0.0);
    boundsMax = float3(0.0);
    const ByteAddressBuffer inputs = nwbHeapRawBuffer(g_NwbOpticalBoundsFinalize.runtimeInputsSlot);
    const uint localSlot = inputs.Load(inputOffset + NWB_OPTICAL_BOUNDS_RUNTIME_SLOT_OFFSET);
    if(localSlot == NWB_BINDLESS_HEAP_INVALID_SLOT)
        return false;
    const ByteAddressBuffer localBounds = nwbHeapRawBuffer(localSlot);
    if((localBounds.Load(NWB_RUNTIME_MESH_BOUNDS_FLAGS_OFFSET) & NWB_RUNTIME_MESH_BOUNDS_FLAG_VALID) == 0u)
        return false;
    const uint3 localMin = localBounds.Load3(NWB_RUNTIME_MESH_BOUNDS_MIN_OFFSET);
    const uint3 localMax = localBounds.Load3(NWB_RUNTIME_MESH_BOUNDS_MAX_OFFSET);
    for(uint axis = 0u; axis < 3u; ++axis){
        if(!nwbOpticalAdmittedInput(localMin[axis]) || !nwbOpticalAdmittedInput(localMax[axis]) || nwbOpticalOrderedBits(localMin[axis]) > nwbOpticalOrderedBits(localMax[axis]))
            return false;
    }
    const uint3 magnitude = max(localMin & NWB_OPTICAL_BOUNDS_MAGNITUDE_MASK, localMax & NWB_OPTICAL_BOUNDS_MAGNITUDE_MASK);
    for(uint rowIndex = 0u; rowIndex < 3u; ++rowIndex){
        const uint4 row = inputs.Load4(inputOffset + rowIndex * NWB_OPTICAL_BOUNDS_RUNTIME_ROW_BYTES);
        int exponent = NWB_OPTICAL_BOUNDS_ZERO_EXPONENT;
        if(!nwbOpticalMagnitudeExponent(row, magnitude, exponent))
            return false;
        boundsMin[rowIndex] = asfloat(nwbOpticalExpandedBits(nwbOpticalEndpoint(row, localMin, localMax, exponent, false), exponent, false));
        boundsMax[rowIndex] = asfloat(nwbOpticalExpandedBits(nwbOpticalEndpoint(row, localMin, localMax, exponent, true), exponent, true));
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_OPTICAL_BOUNDS_FINALIZE_GROUP_SIZE, 1, 1)]
void main(uint3 groupThreadId : SV_GroupThreadID){
    const uint lane = groupThreadId.x;
    const ByteAddressBuffer source = nwbHeapRawBuffer(g_NwbOpticalBoundsFinalize.sourceSceneSlot);
    const RWByteAddressBuffer destination = g_NwbHeapStorageBuffers[g_NwbOpticalBoundsFinalize.outputSceneSlot];
    for(uint index = lane; index < g_NwbOpticalBoundsFinalize.instanceCount; index += NWB_OPTICAL_BOUNDS_FINALIZE_GROUP_SIZE){
        const uint offset = NWB_RT_OPTICAL_SCENE_HEADER_BYTES + index * NWB_RT_OPTICAL_INSTANCE_BYTES;
        destination.Store4(offset, source.Load4(offset));
    }
    DeviceMemoryBarrierWithGroupSync();

    const float largest = NWB_FLOAT_MAX;
    float3 minimum = float3(largest);
    float3 maximum = float3(-largest);
    uint invalid = g_NwbOpticalBoundsFinalize.staticBoundsComplete == 0u ? 1u : 0u;
    const uint transparentCount = source.Load(NWB_RT_OPTICAL_SCENE_TRANSPARENT_COUNT_OFFSET);
    if(g_NwbOpticalBoundsFinalize.runtimeCount > transparentCount || transparentCount > g_NwbOpticalBoundsFinalize.instanceCount)
        invalid = 1u;
    if(lane == 0u && transparentCount > g_NwbOpticalBoundsFinalize.runtimeCount){
        minimum = asfloat(source.Load3(NWB_RT_OPTICAL_SCENE_BOUNDS_MIN_OFFSET));
        maximum = asfloat(source.Load3(NWB_RT_OPTICAL_SCENE_BOUNDS_MAX_OFFSET));
        if(!nwbOpticalNormalBounds(minimum) || !nwbOpticalNormalBounds(maximum) || any(minimum > maximum))
            invalid = 1u;
    }
    const ByteAddressBuffer inputs = nwbHeapRawBuffer(g_NwbOpticalBoundsFinalize.runtimeInputsSlot);
    for(uint index = lane; index < g_NwbOpticalBoundsFinalize.runtimeCount; index += NWB_OPTICAL_BOUNDS_FINALIZE_GROUP_SIZE){
        const uint inputOffset = index * NWB_OPTICAL_BOUNDS_RUNTIME_INPUT_BYTES;
        const uint instanceIndex = inputs.Load(inputOffset + NWB_OPTICAL_BOUNDS_RUNTIME_INSTANCE_OFFSET);
        if(instanceIndex >= g_NwbOpticalBoundsFinalize.instanceCount){
            invalid = 1u;
            continue;
        }
        const uint flagOffset = NWB_RT_OPTICAL_SCENE_HEADER_BYTES + instanceIndex * NWB_RT_OPTICAL_INSTANCE_BYTES + NWB_RT_OPTICAL_INSTANCE_FLAGS_OFFSET;
        uint flags = source.Load(flagOffset) & ~NWB_RT_OPTICAL_INSTANCE_FLAG_BOUNDS_VALID;
        float3 boundsMin = float3(0.0);
        float3 boundsMax = float3(0.0);
        const bool valid = (flags & NWB_RT_OPTICAL_INSTANCE_FLAG_TRANSPARENT) != 0u && nwbOpticalTransformRuntimeBounds(inputOffset, boundsMin, boundsMax);
        if(valid){
            minimum = min(minimum, boundsMin);
            maximum = max(maximum, boundsMax);
            flags |= NWB_RT_OPTICAL_INSTANCE_FLAG_BOUNDS_VALID;
        }
        else
            invalid = 1u;
        destination.Store(flagOffset, flags);
    }
    g_NwbOpticalMinimum[lane] = minimum;
    g_NwbOpticalMaximum[lane] = maximum;
    g_NwbOpticalInvalid[lane] = invalid;
    GroupMemoryBarrierWithGroupSync();
    for(uint stride = NWB_OPTICAL_BOUNDS_FINALIZE_GROUP_SIZE / 2u; stride != 0u; stride >>= 1u){
        if(lane < stride){
            g_NwbOpticalMinimum[lane] = min(g_NwbOpticalMinimum[lane], g_NwbOpticalMinimum[lane + stride]);
            g_NwbOpticalMaximum[lane] = max(g_NwbOpticalMaximum[lane], g_NwbOpticalMaximum[lane + stride]);
            g_NwbOpticalInvalid[lane] |= g_NwbOpticalInvalid[lane + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }
    if(lane == 0u){
        const bool complete = g_NwbOpticalInvalid[0] == 0u;
        const float3 finalMin = complete && transparentCount != 0u ? g_NwbOpticalMinimum[0] : float3(0.0);
        const float3 finalMax = complete && transparentCount != 0u ? g_NwbOpticalMaximum[0] : float3(0.0);
        const uint flags = (source.Load(NWB_RT_OPTICAL_SCENE_FLAGS_OFFSET) & ~NWB_RT_OPTICAL_SCENE_FLAG_BOUNDS_VALID) | (complete ? NWB_RT_OPTICAL_SCENE_FLAG_BOUNDS_VALID : 0u);
        destination.Store4(NWB_RT_OPTICAL_SCENE_BOUNDS_MIN_OFFSET, uint4(asuint(finalMin), transparentCount));
        destination.Store4(NWB_RT_OPTICAL_SCENE_BOUNDS_MAX_OFFSET, uint4(asuint(finalMax), flags));
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

