// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "deformable_runtime_helpers.h"
#include "deformer_system.h"

#include <core/alloc/scratch.h>
#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace DeformerMorphPayload{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using MorphWeightLookup = HashMap<
    NameHash,
    f32,
    Hasher<NameHash>,
    EqualTo<NameHash>,
    Core::Alloc::ScratchAllocator<Pair<const NameHash, f32>>
>;


struct BlendedMorphDeltaAccumulator{
    Float4 deltaPosition = Float4(0.0f, 0.0f, 0.0f, 0.0f);
    Float4 deltaNormal = Float4(0.0f, 0.0f, 0.0f, 0.0f);
    Float4 deltaTangent = Float4(0.0f, 0.0f, 0.0f, 0.0f);
    bool active = false;
};


[[nodiscard]] inline bool BuildResolvedMorphWeightLookup(const DeformableRuntimeMeshInstance& instance, const DeformableMorphWeightsComponent* weights, MorphWeightLookup& outWeights){
    Name failedMorph = NAME_NONE;
    if(DeformableRuntime::BuildMorphWeightSumLookup(instance.morphs, weights, outWeights, failedMorph))
        return true;

    NWB_LOGGER_ERROR(
        NWB_TEXT("DeformerSystem: runtime mesh '{}' morph '{}' weight is invalid"),
        instance.handle.value,
        StringConvert(failedMorph.c_str())
    );
    return false;
}

[[nodiscard]] inline f32 ResolvedMorphWeight(const MorphWeightLookup& weights, const Name& morphName){
    if(!morphName)
        return 0.0f;

    const auto iterWeight = weights.find(morphName.hash());
    if(iterWeight == weights.end())
        return 0.0f;

    return iterWeight.value();
}

[[nodiscard]] inline bool ActiveDeltaValue(const f32 value){
    return !IsFinite(value) || DeformableValidation::ActiveWeight(value);
}

[[nodiscard]] inline bool ActiveBlendedMorphDelta(const BlendedMorphDeltaAccumulator& delta){
    return delta.active
        && (ActiveDeltaValue(delta.deltaPosition.x)
            || ActiveDeltaValue(delta.deltaPosition.y)
            || ActiveDeltaValue(delta.deltaPosition.z)
            || ActiveDeltaValue(delta.deltaNormal.x)
            || ActiveDeltaValue(delta.deltaNormal.y)
            || ActiveDeltaValue(delta.deltaNormal.z)
            || ActiveDeltaValue(delta.deltaTangent.x)
            || ActiveDeltaValue(delta.deltaTangent.y)
            || ActiveDeltaValue(delta.deltaTangent.z)
            || ActiveDeltaValue(delta.deltaTangent.w))
    ;
}

inline void AccumulateWeightedMorphDelta(
    BlendedMorphDeltaAccumulator& target,
    const DeformableMorphDelta& source,
    const f32 weight)
{
    target.active = true;
    target.deltaPosition.x += source.deltaPosition.x * weight;
    target.deltaPosition.y += source.deltaPosition.y * weight;
    target.deltaPosition.z += source.deltaPosition.z * weight;
    target.deltaNormal.x += source.deltaNormal.x * weight;
    target.deltaNormal.y += source.deltaNormal.y * weight;
    target.deltaNormal.z += source.deltaNormal.z * weight;
    target.deltaTangent.x += source.deltaTangent.x * weight;
    target.deltaTangent.y += source.deltaTangent.y * weight;
    target.deltaTangent.z += source.deltaTangent.z * weight;
    target.deltaTangent.w += source.deltaTangent.w * weight;
}

template<typename MorphRangeVector, typename MorphDeltaVector>
[[nodiscard]] bool BuildBlendedMorphPayload(
    const DeformableRuntimeMeshInstance& instance,
    const DeformableMorphWeightsComponent* morphWeights,
    MorphRangeVector& outRanges,
    MorphDeltaVector& outDeltas,
    usize& outSignature)
{
    outRanges.clear();
    outDeltas.clear();
    outSignature = 0;

    if(!DeformableRuntime::HasMorphWeights(morphWeights))
        return true;

    if(instance.restVertices.size() > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("DeformerSystem: runtime mesh '{}' vertex count exceeds u32 limits"),
            instance.handle.value
        );
        return false;
    }

    Core::Alloc::ScratchArena<> scratchArena;
    MorphWeightLookup resolvedWeights(
        0,
        Hasher<NameHash>(),
        EqualTo<NameHash>(),
        Core::Alloc::ScratchAllocator<Pair<const NameHash, f32>>(scratchArena)
    );
    if(!BuildResolvedMorphWeightLookup(instance, morphWeights, resolvedWeights))
        return false;

    const usize vertexCount = instance.restVertices.size();
    Vector<BlendedMorphDeltaAccumulator, Core::Alloc::ScratchAllocator<BlendedMorphDeltaAccumulator>> blendedDeltas{
        Core::Alloc::ScratchAllocator<BlendedMorphDeltaAccumulator>(scratchArena)
    };

    u32 activeMorphCount = 0u;
    usize activeInputDeltaCount = 0u;
    for(const DeformableMorph& morph : instance.morphs){
        const f32 weight = ResolvedMorphWeight(resolvedWeights, morph.name);
        if(!DeformableValidation::ActiveWeight(weight))
            continue;
        if(morph.deltas.empty()){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformerSystem: active morph '{}' on runtime mesh '{}' has no deltas"),
                StringConvert(morph.name.c_str()),
                instance.handle.value
            );
            return false;
        }
        if(morph.deltas.size() > static_cast<usize>(Limit<u32>::s_Max)
            || activeInputDeltaCount > static_cast<usize>(Limit<u32>::s_Max) - morph.deltas.size()
        ){
            NWB_LOGGER_ERROR(
                NWB_TEXT("DeformerSystem: morph '{}' on runtime mesh '{}' exceeds u32 delta limits"),
                StringConvert(morph.name.c_str()),
                instance.handle.value
            );
            return false;
        }

        ++activeMorphCount;
        activeInputDeltaCount += morph.deltas.size();
        Core::CoreDetail::HashCombine(outSignature, morph.name);
        Core::CoreDetail::HashCombine(outSignature, static_cast<u32>(morph.deltas.size()));

        for(const DeformableMorphDelta& delta : morph.deltas){
            if(!DeformableValidation::ValidMorphDelta(delta, vertexCount)){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("DeformerSystem: morph '{}' on runtime mesh '{}' contains an invalid delta"),
                    StringConvert(morph.name.c_str()),
                    instance.handle.value
                );
                return false;
            }

            if(blendedDeltas.empty())
                blendedDeltas.resize(vertexCount);
            AccumulateWeightedMorphDelta(blendedDeltas[delta.vertexId], delta, weight);
        }
    }

    if(activeMorphCount == 0u)
        return true;

    outRanges.resize(vertexCount);
    outDeltas.reserve(Min(vertexCount, activeInputDeltaCount));
    for(u32 vertexIndex = 0u; vertexIndex < static_cast<u32>(vertexCount); ++vertexIndex){
        const BlendedMorphDeltaAccumulator& blendedDelta = blendedDeltas[vertexIndex];
        if(!ActiveBlendedMorphDelta(blendedDelta))
            continue;

        DeformerSystem::DeformerVertexMorphRangeGpu range;
        range.firstDelta = static_cast<u32>(outDeltas.size());
        range.deltaCount = 1u;
        outRanges[vertexIndex] = range;

        DeformerSystem::DeformerBlendedMorphDeltaGpu gpuDelta;
        gpuDelta.deltaPosition = blendedDelta.deltaPosition;
        gpuDelta.deltaNormal = blendedDelta.deltaNormal;
        gpuDelta.deltaTangent = blendedDelta.deltaTangent;
        outDeltas.push_back(gpuDelta);
    }

    if(outDeltas.empty())
        outRanges.clear();

    Core::CoreDetail::HashCombine(outSignature, activeMorphCount);
    Core::CoreDetail::HashCombine(outSignature, static_cast<u32>(outDeltas.size()));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

