// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "mesh_stream_canonicalizer.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_mesh_canonicalizer{


template<typename Value>
struct StreamSortEntry{
    Value value;
    usize sourceIndex = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Value>
[[nodiscard]] Expected<UtilityVector<u32>> DeduplicateStream(const AStringView streamName, Core::CpuTaskScheduler& cpuScheduler, Core::Alloc::ScratchArena& scratchArena, UtilityVector<Value>& inOutStream){
    UtilityVector<u32> outRemap;
    outRemap.reserve(inOutStream.size());
    outRemap.resize(inOutStream.size());

    if(inOutStream.empty())
        return outRemap;

    UtilityScratchVector<StreamSortEntry<Value>> sortedEntries(scratchArena);
    sortedEntries.resize(inOutStream.size());

    auto fillEntry = [&](const usize index)noexcept(noexcept(DeclVal<Value&>() = DeclVal<const Value&>())){
        sortedEntries[index].value = inOutStream[index];
        sortedEntries[index].sourceIndex = index;
    };
    const auto parallelWork = cpuScheduler.parallelFor(static_cast<usize>(0), inOutStream.size(), s_DeduplicateParallelGrainSize, fillEntry);
    if(!parallelWork){
        NWB_LOGGER_ERROR(NWB_TEXT("FBX: stream deduplication preparation rejected CPU work: {}"), static_cast<u32>(parallelWork.error()));
        return MakeUnexpected(Failure{});
    }

    Sort(sortedEntries.begin(), sortedEntries.end(), [](const StreamSortEntry<Value>& lhs, const StreamSortEntry<Value>& rhs)noexcept(noexcept(LessEmittedStreamValue(lhs.value, rhs.value))){
        if(LessEmittedStreamValue(lhs.value, rhs.value))
            return true;
        if(LessEmittedStreamValue(rhs.value, lhs.value))
            return false;
        return lhs.sourceIndex < rhs.sourceIndex;
    });

    usize uniqueCount = 0u;
    for(usize sortedIndex = 0u; sortedIndex < sortedEntries.size(); ++sortedIndex){
        if(sortedIndex == 0u || !SameEmittedStreamValue(sortedEntries[sortedIndex].value, sortedEntries[sortedIndex - 1u].value))
            ++uniqueCount;
    }

    if(uniqueCount == inOutStream.size()){
        for(usize i = 0u; i < outRemap.size(); ++i)
            outRemap[i] = static_cast<u32>(i);
        return outRemap;
    }

    if(uniqueCount >= static_cast<usize>(Impl::s_MeshMissingStreamIndex)){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to canonicalize mesh: {} stream has too many unique values"), ConvertString(streamName));
        return MakeUnexpected(Failure{});
    }

    UtilityVector<Value> compact;
    compact.reserve(uniqueCount);

    u32 compactIndex = 0u;
    for(usize sortedIndex = 0u; sortedIndex < sortedEntries.size(); ++sortedIndex){
        if(sortedIndex == 0u || !SameEmittedStreamValue(sortedEntries[sortedIndex].value, sortedEntries[sortedIndex - 1u].value)){
            NWB_ASSERT(compact.size() < static_cast<usize>(Impl::s_MeshMissingStreamIndex));
            compactIndex = static_cast<u32>(compact.size());
            compact.push_back(sortedEntries[sortedIndex].value);
        }

        outRemap[sortedEntries[sortedIndex].sourceIndex] = compactIndex;
    }

    inOutStream = Move(compact);
    return outRemap;
}


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<SourceMeshCanonicalizeReport> SourceMeshCanonicalizer::CanonicalizeSkinned(const Span<const MeshSkinInfluence> skinInfluences, Core::CpuTaskScheduler& cpuScheduler, Core::Alloc::ScratchArena& scratchArena, SourceMeshStreams& inOutMesh){
    if(inOutMesh.positions.size() != skinInfluences.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to canonicalize skinned mesh: position and skin influence counts must match"));
        return MakeUnexpected(Failure{});
    }

    SourceMeshCanonicalizeReport report;
    report.before = CountSourceMeshStreams(inOutMesh);
    report.before.skin = skinInfluences.size();

    UtilityScratchVector<u32> positionRemap(inOutMesh.positions.size(), scratchArena);
    UtilityScratchVector<u32> skinRemap(skinInfluences.size(), scratchArena);

    for(usize i = 0u; i < positionRemap.size(); ++i)
        positionRemap[i] = static_cast<u32>(i);
    for(usize i = 0u; i < skinRemap.size(); ++i)
        skinRemap[i] = static_cast<u32>(i);

    const auto normalRemap = __hidden_mesh_canonicalizer::DeduplicateStream(Impl::s_VertexRefNormalName, cpuScheduler, scratchArena, inOutMesh.normals);
    if(!normalRemap)
        return MakeUnexpected(Failure{});
    const auto tangentRemap = __hidden_mesh_canonicalizer::DeduplicateStream(Impl::s_VertexRefTangentName, cpuScheduler, scratchArena, inOutMesh.tangents);
    if(!tangentRemap)
        return MakeUnexpected(Failure{});
    const auto uv0Remap = __hidden_mesh_canonicalizer::DeduplicateStream(Impl::s_MeshUv0FieldNameView, cpuScheduler, scratchArena, inOutMesh.uv0);
    if(!uv0Remap)
        return MakeUnexpected(Failure{});
    const auto colorRemap = __hidden_mesh_canonicalizer::DeduplicateStream(Impl::s_MeshColorsFieldNameView, cpuScheduler, scratchArena, inOutMesh.colors);
    if(!colorRemap)
        return MakeUnexpected(Failure{});

    if(!RemapSourceVertexRefs(positionRemap, *normalRemap, *tangentRemap, *uv0Remap, *colorRemap, skinRemap, inOutMesh.vertexRefs))
        return MakeUnexpected(Failure{});

    report.after = CountSourceMeshStreams(inOutMesh);
    report.after.skin = skinInfluences.size();
    return report;
}

Expected<SourceMeshCanonicalizeReport> SourceMeshCanonicalizer::Canonicalize(Core::CpuTaskScheduler& cpuScheduler, Core::Alloc::ScratchArena& scratchArena, SourceMeshStreams& inOutMesh){
    SourceMeshCanonicalizeReport report;
    report.before = CountSourceMeshStreams(inOutMesh);


    const auto positionRemap = __hidden_mesh_canonicalizer::DeduplicateStream(Impl::s_VertexRefPositionName, cpuScheduler, scratchArena, inOutMesh.positions);
    if(!positionRemap)
        return MakeUnexpected(Failure{});
    const auto normalRemap = __hidden_mesh_canonicalizer::DeduplicateStream(Impl::s_VertexRefNormalName, cpuScheduler, scratchArena, inOutMesh.normals);
    if(!normalRemap)
        return MakeUnexpected(Failure{});
    const auto tangentRemap = __hidden_mesh_canonicalizer::DeduplicateStream(Impl::s_VertexRefTangentName, cpuScheduler, scratchArena, inOutMesh.tangents);
    if(!tangentRemap)
        return MakeUnexpected(Failure{});
    const auto uv0Remap = __hidden_mesh_canonicalizer::DeduplicateStream(Impl::s_MeshUv0FieldNameView, cpuScheduler, scratchArena, inOutMesh.uv0);
    if(!uv0Remap)
        return MakeUnexpected(Failure{});
    const auto colorRemap = __hidden_mesh_canonicalizer::DeduplicateStream(Impl::s_VertexRefColorName, cpuScheduler, scratchArena, inOutMesh.colors);
    if(!colorRemap)
        return MakeUnexpected(Failure{});
    const auto skinRemap = __hidden_mesh_canonicalizer::DeduplicateStream(s_SkinStreamLabel, cpuScheduler, scratchArena, inOutMesh.skin);
    if(!skinRemap)
        return MakeUnexpected(Failure{});
    if(!RemapSourceVertexRefs(*positionRemap, *normalRemap, *tangentRemap, *uv0Remap, *colorRemap, *skinRemap, inOutMesh.vertexRefs))
        return MakeUnexpected(Failure{});

    report.after = CountSourceMeshStreams(inOutMesh);
    return report;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

