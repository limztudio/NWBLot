// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"
#include "mesh_refresh_parse.h"
#include "mesh_refresh_text.h"
#include "text_write.h"

#include <core/common/log.h>
#include <core/metascript/parser.h>

#include <global/overflow.h>
#include <global/span.h>
#include <global/text_replacements.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_mesh_refresh{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

[[nodiscard]] b8 RemapComponentRefs(const Span<const u32> positions, const Span<const u32> normals, const Span<const u32> tangents, const Span<const u32> uv0, const Span<const u32> colors, const Span<const u32> skin, SourceMeshStreams& inOutMesh){
    for(SourceVertexRef& ref : inOutMesh.vertexRefs){
        if(!RemapSourceVertexRef(positions, normals, tangents, uv0, colors, skin, ref))
            return false;
    }
    return true;
}

[[nodiscard]] Expected<SourceMeshCanonicalizeReport> CanonicalizeSkinnedMeshStreams(const Span<const MeshSkinInfluence> skinInfluences, Core::CpuTaskScheduler& cpuScheduler, Core::Alloc::ScratchArena& scratchArena, SourceMeshStreams& inOutMesh){
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

    const auto normalRemap = DeduplicateStream(Impl::s_VertexRefNormalName, cpuScheduler, scratchArena, inOutMesh.normals);
    if(!normalRemap)
        return MakeUnexpected(Failure{});
    const auto tangentRemap = DeduplicateStream(Impl::s_VertexRefTangentName, cpuScheduler, scratchArena, inOutMesh.tangents);
    if(!tangentRemap)
        return MakeUnexpected(Failure{});
    const auto uv0Remap = DeduplicateStream(Impl::s_MeshUv0FieldNameView, cpuScheduler, scratchArena, inOutMesh.uv0);
    if(!uv0Remap)
        return MakeUnexpected(Failure{});
    const auto colorRemap = DeduplicateStream(Impl::s_MeshColorsFieldNameView, cpuScheduler, scratchArena, inOutMesh.colors);
    if(!colorRemap)
        return MakeUnexpected(Failure{});

    if(!RemapComponentRefs(positionRemap, *normalRemap, *tangentRemap, *uv0Remap, *colorRemap, skinRemap, inOutMesh))
        return MakeUnexpected(Failure{});

    report.after = CountSourceMeshStreams(inOutMesh);
    report.after.skin = skinInfluences.size();
    return report;
}


[[nodiscard]] Expected<ScratchString, ErrorCode> ReadMetascriptSource(const ScratchPath& nwbFilePath, Core::Alloc::ScratchArena& scratchArena){
    ScratchString outText(scratchArena);
    const auto read = ReadTextFile(nwbFilePath, outText);
    if(!read)
        return MakeUnexpected(read.error());

    StripUtf8Bom(outText);
    return outText;
}

[[nodiscard]] Expected<Core::Metascript::Document> ParseMetascriptDocument(const ScratchPath& nwbFilePath, const AStringView text, Core::Metascript::MetaArena& arena){
    Core::Metascript::Document outDoc(arena);
    if(outDoc.parse(text))
        return outDoc;

    for(const Core::Metascript::ParseError& error : outDoc.errors()){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to refresh NWB mesh: '{}' parse error at {}:{}: {}"), PathToString<tchar>(nwbFilePath), error.line, error.column, ConvertString(error.message));
    }
    return MakeUnexpected(Failure{});
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<SourceMeshCanonicalizeReport> CanonicalizeSourceMeshStreams(Core::CpuTaskScheduler& cpuScheduler, Core::Alloc::ScratchArena& scratchArena, SourceMeshStreams& inOutMesh){
    SourceMeshCanonicalizeReport report;
    report.before = CountSourceMeshStreams(inOutMesh);


    const auto positionRemap = __hidden_mesh_refresh::DeduplicateStream(Impl::s_VertexRefPositionName, cpuScheduler, scratchArena, inOutMesh.positions);
    if(!positionRemap)
        return MakeUnexpected(Failure{});
    const auto normalRemap = __hidden_mesh_refresh::DeduplicateStream(Impl::s_VertexRefNormalName, cpuScheduler, scratchArena, inOutMesh.normals);
    if(!normalRemap)
        return MakeUnexpected(Failure{});
    const auto tangentRemap = __hidden_mesh_refresh::DeduplicateStream(Impl::s_VertexRefTangentName, cpuScheduler, scratchArena, inOutMesh.tangents);
    if(!tangentRemap)
        return MakeUnexpected(Failure{});
    const auto uv0Remap = __hidden_mesh_refresh::DeduplicateStream(Impl::s_MeshUv0FieldNameView, cpuScheduler, scratchArena, inOutMesh.uv0);
    if(!uv0Remap)
        return MakeUnexpected(Failure{});
    const auto colorRemap = __hidden_mesh_refresh::DeduplicateStream(Impl::s_VertexRefColorName, cpuScheduler, scratchArena, inOutMesh.colors);
    if(!colorRemap)
        return MakeUnexpected(Failure{});
    const auto skinRemap = __hidden_mesh_refresh::DeduplicateStream(s_SkinStreamLabel, cpuScheduler, scratchArena, inOutMesh.skin);
    if(!skinRemap)
        return MakeUnexpected(Failure{});
    if(!__hidden_mesh_refresh::RemapComponentRefs(*positionRemap, *normalRemap, *tangentRemap, *uv0Remap, *colorRemap, *skinRemap, inOutMesh))
        return MakeUnexpected(Failure{});

    report.after = CountSourceMeshStreams(inOutMesh);
    return report;
}

Expected<SourceMeshCanonicalizeReport> RefreshNwbMeshAsset(const ScratchPath& inputPath, const ScratchPath& outputPath, Core::CpuTaskScheduler& cpuScheduler, Core::Alloc::ScratchArena& scratchArena){
    SourceMeshCanonicalizeReport outReport;

    auto sourceResult = __hidden_mesh_refresh::ReadMetascriptSource(inputPath, scratchArena);
    if(!sourceResult){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to refresh NWB mesh: failed to read '{}': {}"), PathToString<tchar>(inputPath), ConvertString(sourceResult.error().message()));
        return MakeUnexpected(Failure{});
    }
    ScratchString& source = *sourceResult;
    const b8 useCrlf = HasCrlfLineEndings(AStringView(source));

    Core::Metascript::MetaArena metaArena(s_UtilityArena);
    auto docResult = __hidden_mesh_refresh::ParseMetascriptDocument(inputPath, source, metaArena);
    if(!docResult)
        return MakeUnexpected(Failure{});
    const Core::Metascript::Document& doc = *docResult;

    MeshRefreshTextDetail::TextReplacements replacements(scratchArena);
    const auto replacementCapacity = TryMultiply<usize>(doc.declarations().size(), s_MaxMeshReplacementCount);
    if(!replacementCapacity){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to refresh NWB mesh: replacement capacity overflows"));
        return MakeUnexpected(Failure{});
    }
    replacements.reserve(*replacementCapacity);
    b8 sawMesh = false;

    for(const Core::Metascript::Document::Declaration& declaration : doc.declarations()){
        if(declaration.type != s_MeshAssetTypeText)
            continue;

        sawMesh = true;

        const AStringView meshVariableName(declaration.variable);
        const Core::Metascript::Value* meshValue = doc.findVariable(meshVariableName);
        if(!meshValue){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to refresh NWB mesh: missing mesh variable '{}'"), ConvertString(meshVariableName));
            return MakeUnexpected(Failure{});
        }

        auto meshResult = MeshRefreshParseDetail::ParseMeshValue(inputPath, meshVariableName, *meshValue, scratchArena);
        if(!meshResult)
            return MakeUnexpected(Failure{});
        SourceMeshStreams& mesh = *meshResult;

        SourceMeshCanonicalizeReport itemReport;

        const auto skinReference = MeshRefreshTextDetail::FindSkinForMesh(doc, meshVariableName);
        if(!skinReference)
            return MakeUnexpected(Failure{});
        const AStringView skinVariableName = skinReference->variableName;
        const Core::Metascript::Value* skinValue = skinReference->value;
        if(skinValue){
            auto influences = MeshRefreshParseDetail::ParseSkinInfluences(inputPath, *skinValue, skinVariableName);
            if(!influences)
                return MakeUnexpected(Failure{});
            UtilityVector<MeshSkinInfluence>& skinInfluences = *influences;
            const auto canonicalized = __hidden_mesh_refresh::CanonicalizeSkinnedMeshStreams(skinInfluences, cpuScheduler, scratchArena, mesh);
            if(!canonicalized)
                return MakeUnexpected(Failure{});
            itemReport = *canonicalized;
        }
        else{
            const auto canonicalized = CanonicalizeSourceMeshStreams(cpuScheduler, scratchArena, mesh);
            if(!canonicalized)
                return MakeUnexpected(Failure{});
            itemReport = *canonicalized;
        }
        if(!MeshRefreshTextDetail::AppendMeshReplacements(source, meshVariableName, itemReport.before, mesh, scratchArena, replacements))
            return MakeUnexpected(Failure{});

        AccumulateReport(itemReport, outReport);
    }

    if(!sawMesh){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to refresh NWB mesh: '{}' contains no mesh declarations"), PathToString<tchar>(inputPath));
        return MakeUnexpected(Failure{});
    }

    ApplyTextReplacements(Span<MeshRefreshTextDetail::TextReplacement>(replacements), source);
    NormalizeLineEndingsInPlace(source, useCrlf);

    const auto written = WriteTextFile(outputPath, source);
    if(!written){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to refresh NWB mesh: failed to write output file '{}'"), PathToString<tchar>(outputPath));
        return MakeUnexpected(Failure{});
    }
    return outReport;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
