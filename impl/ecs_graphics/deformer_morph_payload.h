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

[[nodiscard]] inline bool ActiveDeltaVector(const SIMDVector value, const u32 activeMask){
    const SIMDVector invalid = VectorOrInt(VectorIsNaN(value), VectorIsInfinite(value));
    const SIMDVector active = VectorGreater(VectorAbs(value), VectorReplicate(DeformableValidation::s_Epsilon));
    return (VectorMoveMask(VectorOrInt(invalid, active)) & activeMask) != 0u;
}

[[nodiscard]] inline bool ActiveBlendedMorphDelta(const BlendedMorphDeltaAccumulator& delta){
    return delta.active
        && (ActiveDeltaVector(LoadFloat(delta.deltaPosition), 0x7u)
            || ActiveDeltaVector(LoadFloat(delta.deltaNormal), 0x7u)
            || ActiveDeltaVector(LoadFloat(delta.deltaTangent), 0xFu))
    ;
}

inline void AccumulateWeightedVector(Float4& target, const SIMDVector source, const SIMDVector weight){
    StoreFloat(VectorMultiplyAdd(source, weight, LoadFloat(target)), &target);
}

inline void AccumulateWeightedMorphDelta(
    BlendedMorphDeltaAccumulator& target,
    const DeformableMorphDelta& source,
    const f32 weight)
{
    target.active = true;
    const SIMDVector weightVector = VectorReplicate(weight);
    AccumulateWeightedVector(target.deltaPosition, LoadFloat(source.deltaPosition), weightVector);
    AccumulateWeightedVector(target.deltaNormal, LoadFloat(source.deltaNormal), weightVector);
    AccumulateWeightedVector(target.deltaTangent, LoadFloat(source.deltaTangent), weightVector);
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
    Vector<u32, Core::Alloc::ScratchAllocator<u32>> touchedVertices{
        Core::Alloc::ScratchAllocator<u32>(scratchArena)
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
        if(
            morph.deltas.size() > static_cast<usize>(Limit<u32>::s_Max)
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
        Core::CoreDetail::HashCombine(outSignature, weight);
        Core::CoreDetail::HashCombine(outSignature, static_cast<u32>(morph.deltas.size()));

        if(blendedDeltas.empty()){
            blendedDeltas.resize(vertexCount);
            touchedVertices.reserve(vertexCount);
        }
        for(const DeformableMorphDelta& delta : morph.deltas){
            if(!DeformableValidation::ValidMorphDelta(delta, vertexCount)){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("DeformerSystem: morph '{}' on runtime mesh '{}' contains an invalid delta"),
                    StringConvert(morph.name.c_str()),
                    instance.handle.value
                );
                return false;
            }

            BlendedMorphDeltaAccumulator& blendedDelta = blendedDeltas[delta.vertexId];
            if(!blendedDelta.active)
                touchedVertices.push_back(delta.vertexId);
            AccumulateWeightedMorphDelta(blendedDelta, delta, weight);
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

