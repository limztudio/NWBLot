// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "deformer_gpu_payload.h"

#include <core/alloc/scratch.h>
#include <impl/ecs_deformable/deformable_runtime_helpers.h>
#include <core/common/log.h>


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
    SIMDVector deltaPosition = VectorZero();
    SIMDVector deltaNormal = VectorZero();
    SIMDVector deltaTangent = VectorZero();
    bool active = false;
};

struct MorphDeltaVectors{
    SIMDVector deltaPosition;
    SIMDVector deltaNormal;
    SIMDVector deltaTangent;
};


[[nodiscard]] inline bool BuildResolvedMorphWeightLookup(const DeformableRuntimeMeshInstance& instance, const DeformableMorphWeightsComponent* weights, MorphWeightLookup& outWeights){
    Name failedMorph = NAME_NONE;
    if(DeformableRuntime::BuildMorphWeightSumLookup(instance.morphs, weights, outWeights, failedMorph))
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("DeformerSystem: runtime mesh '{}' morph '{}' weight is invalid")
        , instance.handle.value
        , StringConvert(failedMorph.c_str())
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

[[nodiscard]] inline bool ActiveDeltaVector(const SIMDVector value, const u32 activeMask){
    const SIMDVector invalid = VectorOrInt(VectorIsNaN(value), VectorIsInfinite(value));
    const SIMDVector active = VectorGreater(VectorAbs(value), VectorReplicate(DeformableValidation::s_Epsilon));
    return (VectorMoveMask(VectorOrInt(invalid, active)) & activeMask) != 0u;
}

[[nodiscard]] inline bool ActiveBlendedMorphDelta(const BlendedMorphDeltaAccumulator& delta){
    return
        delta.active
        && (ActiveDeltaVector(delta.deltaPosition, 0x7u)
        || ActiveDeltaVector(delta.deltaNormal, 0x7u)
        || ActiveDeltaVector(delta.deltaTangent, 0xFu))
    ;
}

[[nodiscard]] inline MorphDeltaVectors LoadMorphDelta(const DeformableMorphDelta& delta){
    return MorphDeltaVectors{
        LoadFloat(delta.deltaPosition),
        LoadFloat(delta.deltaNormal),
        LoadFloat(delta.deltaTangent),
    };
}

[[nodiscard]] inline bool ValidMorphDelta(const DeformableMorphDelta& delta, const MorphDeltaVectors& deltaVectors, const usize vertexCount){
    return
        delta.vertexId < vertexCount
        && DeformableValidation::FiniteVector(deltaVectors.deltaPosition, 0x7u)
        && DeformableValidation::FiniteVector(deltaVectors.deltaNormal, 0x7u)
        && DeformableValidation::FiniteVector(deltaVectors.deltaTangent, 0xFu)
    ;
}

inline void AccumulateWeightedMorphDelta(
    BlendedMorphDeltaAccumulator& target,
    const MorphDeltaVectors& source,
    const f32 weight){
    target.active = true;
    const SIMDVector weightVector = VectorReplicate(weight);
    target.deltaPosition = VectorMultiplyAdd(source.deltaPosition, weightVector, target.deltaPosition);
    target.deltaNormal = VectorMultiplyAdd(source.deltaNormal, weightVector, target.deltaNormal);
    target.deltaTangent = VectorMultiplyAdd(source.deltaTangent, weightVector, target.deltaTangent);
}

[[nodiscard]] inline bool ValidateRuntimeMeshVertexCount(const DeformableRuntimeMeshInstance& instance){
    if(instance.restVertices.size() <= static_cast<usize>(Limit<u32>::s_Max))
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("DeformerSystem: runtime mesh '{}' vertex count exceeds u32 limits"), instance.handle.value);
    return false;
}

template<typename MorphRangeVector, typename MorphDeltaVector>
[[nodiscard]] bool BuildBlendedMorphPayload(
    const DeformableRuntimeMeshInstance& instance,
    const DeformableMorphWeightsComponent* morphWeights,
    MorphRangeVector& outRanges,
    MorphDeltaVector& outDeltas,
    usize& outSignature){
    outRanges.clear();
    outDeltas.clear();
    outSignature = 0;

    if(!DeformableRuntime::HasMorphWeights(morphWeights))
        return true;

    if(!ValidateRuntimeMeshVertexCount(instance))
        return false;

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
    Vector<u32, Core::Alloc::ScratchAllocator<u32>> touchedVertices{ Core::Alloc::ScratchAllocator<u32>(scratchArena) };

    u32 activeMorphCount = 0u;
    usize activeInputDeltaCount = 0u;
    for(const DeformableMorph& morph : instance.morphs){
        const f32 weight = ResolvedMorphWeight(resolvedWeights, morph.name);
        if(!DeformableValidation::ActiveWeight(weight))
            continue;
        if(morph.deltas.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("DeformerSystem: active morph '{}' on runtime mesh '{}' has no deltas")
                , StringConvert(morph.name.c_str())
                , instance.handle.value
            );
            return false;
        }
        if(
            morph.deltas.size() > static_cast<usize>(Limit<u32>::s_Max)
            || activeInputDeltaCount > static_cast<usize>(Limit<u32>::s_Max) - morph.deltas.size()
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("DeformerSystem: morph '{}' on runtime mesh '{}' exceeds u32 delta limits")
                , StringConvert(morph.name.c_str())
                , instance.handle.value
            );
            return false;
        }

        ++activeMorphCount;
        activeInputDeltaCount += morph.deltas.size();
        Core::CoreDetail::HashCombine(outSignature, morph.name);
        Core::CoreDetail::HashCombine(outSignature, weight);
        Core::CoreDetail::HashCombine(outSignature, static_cast<u32>(morph.deltas.size()));

        if(blendedDeltas.empty())
            blendedDeltas.resize(vertexCount);
        touchedVertices.reserve(Min(vertexCount, activeInputDeltaCount));
        for(const DeformableMorphDelta& delta : morph.deltas){
            const MorphDeltaVectors deltaVectors = LoadMorphDelta(delta);
            if(!ValidMorphDelta(delta, deltaVectors, vertexCount)){
                NWB_LOGGER_ERROR(NWB_TEXT("DeformerSystem: morph '{}' on runtime mesh '{}' contains an invalid delta")
                    , StringConvert(morph.name.c_str())
                    , instance.handle.value
                );
                return false;
            }

            BlendedMorphDeltaAccumulator& blendedDelta = blendedDeltas[delta.vertexId];
            if(!blendedDelta.active)
                touchedVertices.push_back(delta.vertexId);
            AccumulateWeightedMorphDelta(blendedDelta, deltaVectors, weight);
        }
    }

    if(activeMorphCount == 0u)
        return true;

    Sort(touchedVertices.begin(), touchedVertices.end());
    outRanges.resize(vertexCount);
    outDeltas.reserve(Min(touchedVertices.size(), activeInputDeltaCount));
    for(const u32 vertexIndex : touchedVertices){
        const BlendedMorphDeltaAccumulator& blendedDelta = blendedDeltas[vertexIndex];
        if(!ActiveBlendedMorphDelta(blendedDelta))
            continue;

        DeformerVertexMorphRangeGpu range;
        range.firstDelta = static_cast<u32>(outDeltas.size());
        range.deltaCount = 1u;
        outRanges[vertexIndex] = range;

        DeformerBlendedMorphDeltaGpu gpuDelta;
        StoreFloat(blendedDelta.deltaPosition, &gpuDelta.deltaPosition);
        StoreFloat(blendedDelta.deltaNormal, &gpuDelta.deltaNormal);
        StoreFloat(blendedDelta.deltaTangent, &gpuDelta.deltaTangent);
        outDeltas.push_back(gpuDelta);
    }

    if(outDeltas.empty())
        outRanges.clear();

    Core::CoreDetail::HashCombine(outSignature, instance.editRevision);
    Core::CoreDetail::HashCombine(outSignature, activeMorphCount);
    Core::CoreDetail::HashCombine(outSignature, static_cast<u32>(outDeltas.size()));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

