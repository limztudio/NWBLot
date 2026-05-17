// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "attribute_transfer.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_GEOMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_geometry_attribute_transfer{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f32 s_WeightEpsilon = 0.000001f;
static constexpr f32 s_SkinWeightSumEpsilon = 0.001f;
static constexpr usize s_MaxSkinBlendSourceCount = 4u;
static constexpr usize s_MaxAccumulatedSkinWeights = s_MaxSkinBlendSourceCount * 4u;
static constexpr usize s_MaxFloat4BlendSourceCount = 4u;

struct SkinWeightSample{
    u16 joint = 0;
    f32 weight = 0.0f;
};

[[nodiscard]] bool ActiveWeight(const f32 weight){
    return weight > s_WeightEpsilon || weight < -s_WeightEpsilon;
}

[[nodiscard]] bool FiniteVector(const SIMDVector value, const u32 activeMask){
    const SIMDVector invalid = VectorOrInt(VectorIsNaN(value), VectorIsInfinite(value));
    return (VectorMoveMask(invalid) & activeMask) == 0u;
}

[[nodiscard]] bool AccumulateSkinWeight(
    SkinWeightSample (&samples)[s_MaxAccumulatedSkinWeights],
    u32& sampleCount,
    const u16 joint,
    const f32 weight
){
    if(!IsFinite(weight) || weight < 0.0f)
        return false;
    if(!ActiveWeight(weight))
        return true;

    for(u32 sampleIndex = 0u; sampleIndex < sampleCount; ++sampleIndex){
        SkinWeightSample& sample = samples[sampleIndex];
        if(sample.joint != joint)
            continue;

        sample.weight += weight;
        return IsFinite(sample.weight);
    }

    if(sampleCount >= LengthOf(samples))
        return false;

    samples[sampleCount].joint = joint;
    samples[sampleCount].weight = weight;
    ++sampleCount;
    return true;
}

[[nodiscard]] bool ExtractStrongestSkinWeight(
    SkinWeightSample (&samples)[s_MaxAccumulatedSkinWeights],
    const u32 sampleCount,
    SkinWeightSample& outSample
){
    u32 bestIndex = Limit<u32>::s_Max;
    for(u32 sampleIndex = 0u; sampleIndex < sampleCount; ++sampleIndex){
        const SkinWeightSample& sample = samples[sampleIndex];
        if(!ActiveWeight(sample.weight))
            continue;
        if(bestIndex == Limit<u32>::s_Max || sample.weight > samples[bestIndex].weight)
            bestIndex = sampleIndex;
    }
    if(bestIndex == Limit<u32>::s_Max)
        return false;

    outSample = samples[bestIndex];
    samples[bestIndex].weight = 0.0f;
    return true;
}

}; // namespace __hidden_geometry_attribute_transfer


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ValidSkinInfluence4(const AttributeTransferSkinInfluence4& influence){
    const SIMDVector weights = VectorSet(
        influence.weight[0],
        influence.weight[1],
        influence.weight[2],
        influence.weight[3]
    );
    if(
        !__hidden_geometry_attribute_transfer::FiniteVector(weights, 0xFu)
        || !Vector4GreaterOrEqual(weights, VectorZero())
    )
        return false;

    const f32 weightSum = VectorGetX(Vector4Dot(weights, s_SIMDOne));
    return Abs(weightSum - 1.0f) <= __hidden_geometry_attribute_transfer::s_SkinWeightSumEpsilon;
}

bool BlendSkinInfluence4(
    const AttributeTransferSkinBlendSource* sources,
    const usize sourceCount,
    AttributeTransferSkinInfluence4& outInfluence
){
    using namespace __hidden_geometry_attribute_transfer;

    outInfluence = AttributeTransferSkinInfluence4{};
    if(!sources || sourceCount == 0u || sourceCount > s_MaxSkinBlendSourceCount)
        return false;

    SkinWeightSample samples[s_MaxAccumulatedSkinWeights] = {};
    u32 sampleCount = 0u;
    for(usize sourceIndex = 0u; sourceIndex < sourceCount; ++sourceIndex){
        const AttributeTransferSkinBlendSource& source = sources[sourceIndex];
        if(!IsFinite(source.weight) || source.weight < 0.0f)
            return false;
        if(!ActiveWeight(source.weight))
            continue;
        if(!ValidSkinInfluence4(source.influence))
            return false;

        for(u32 influenceIndex = 0u; influenceIndex < 4u; ++influenceIndex){
            if(!AccumulateSkinWeight(
                samples,
                sampleCount,
                source.influence.joint[influenceIndex],
                source.influence.weight[influenceIndex] * source.weight
            ))
                return false;
        }
    }

    f32 selectedWeightSum = 0.0f;
    for(u32 influenceIndex = 0u; influenceIndex < 4u; ++influenceIndex){
        SkinWeightSample selectedSample{};
        if(!ExtractStrongestSkinWeight(samples, sampleCount, selectedSample))
            break;

        outInfluence.joint[influenceIndex] = selectedSample.joint;
        outInfluence.weight[influenceIndex] = selectedSample.weight;
        selectedWeightSum += selectedSample.weight;
        if(!IsFinite(selectedWeightSum))
            return false;
    }

    if(!ActiveWeight(selectedWeightSum))
        return false;

    const f32 invSelectedWeightSum = 1.0f / selectedWeightSum;
    for(u32 influenceIndex = 0u; influenceIndex < 4u; ++influenceIndex)
        outInfluence.weight[influenceIndex] *= invSelectedWeightSum;

    return ValidSkinInfluence4(outInfluence);
}

bool BlendFloat4(
    const AttributeTransferFloat4BlendSource* sources,
    const usize sourceCount,
    Float4U& outValue
){
    using namespace __hidden_geometry_attribute_transfer;

    outValue = Float4U(0.0f, 0.0f, 0.0f, 0.0f);
    if(!sources || sourceCount == 0u || sourceCount > s_MaxFloat4BlendSourceCount)
        return false;

    SIMDVector value = VectorZero();
    f32 weightSum = 0.0f;
    for(usize sourceIndex = 0u; sourceIndex < sourceCount; ++sourceIndex){
        const AttributeTransferFloat4BlendSource& source = sources[sourceIndex];
        if(!IsFinite(source.weight) || source.weight < 0.0f)
            return false;
        if(!ActiveWeight(source.weight))
            continue;

        const SIMDVector sourceValue = LoadFloat(source.value);
        if(!FiniteVector(sourceValue, 0xFu))
            return false;

        value = VectorMultiplyAdd(sourceValue, VectorReplicate(source.weight), value);
        weightSum += source.weight;
        if(!FiniteVector(value, 0xFu) || !IsFinite(weightSum))
            return false;
    }

    if(!ActiveWeight(weightSum))
        return false;

    const SIMDVector normalizedValue = VectorScale(value, 1.0f / weightSum);
    if(!FiniteVector(normalizedValue, 0xFu))
        return false;

    StoreFloat(normalizedValue, &outValue);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_GEOMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

