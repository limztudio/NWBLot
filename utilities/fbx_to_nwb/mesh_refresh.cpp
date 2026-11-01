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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_mesh_refresh{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr usize s_DeduplicateParallelGrainSize = 4096u;
static constexpr usize s_MaxMeshReplacementCount = 8u;

template<typename Value>
struct StreamSortEntry{
    Value value;
    usize sourceIndex = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 LessEmittedF32(const f32 lhs, const f32 rhs)noexcept{
    return FloatingHashBits(TextWrite::CleanFloat(lhs)) < FloatingHashBits(TextWrite::CleanFloat(rhs));
}

[[nodiscard]] b8 SameEmittedF32(const f32 lhs, const f32 rhs)noexcept{
    return FloatingHashBits(TextWrite::CleanFloat(lhs)) == FloatingHashBits(TextWrite::CleanFloat(rhs));
}

[[nodiscard]] b8 LessValue(const Vec2& lhs, const Vec2& rhs)noexcept{
    if(!SameEmittedF32(lhs.x, rhs.x))
        return LessEmittedF32(lhs.x, rhs.x);
    return LessEmittedF32(lhs.y, rhs.y);
}

[[nodiscard]] b8 LessValue(const Vec3& lhs, const Vec3& rhs)noexcept{
    if(!SameEmittedF32(lhs.x, rhs.x))
        return LessEmittedF32(lhs.x, rhs.x);
    if(!SameEmittedF32(lhs.y, rhs.y))
        return LessEmittedF32(lhs.y, rhs.y);
    return LessEmittedF32(lhs.z, rhs.z);
}

[[nodiscard]] b8 LessValue(const Vec4& lhs, const Vec4& rhs)noexcept{
    if(!SameEmittedF32(lhs.x, rhs.x))
        return LessEmittedF32(lhs.x, rhs.x);
    if(!SameEmittedF32(lhs.y, rhs.y))
        return LessEmittedF32(lhs.y, rhs.y);
    if(!SameEmittedF32(lhs.z, rhs.z))
        return LessEmittedF32(lhs.z, rhs.z);
    return LessEmittedF32(lhs.w, rhs.w);
}

[[nodiscard]] b8 LessValue(const MeshSkinInfluence& lhs, const MeshSkinInfluence& rhs)noexcept{
    for(usize i = 0u; i < Impl::s_SkinInfluenceJointCount; ++i){
        if(lhs.joint[i] != rhs.joint[i])
            return lhs.joint[i] < rhs.joint[i];
    }
    for(usize i = 0u; i < Impl::s_SkinInfluenceJointCount; ++i){
        const u32 lhsBits = FloatingHashBits(lhs.weight.raw[i]);
        const u32 rhsBits = FloatingHashBits(rhs.weight.raw[i]);
        if(lhsBits != rhsBits)
            return lhsBits < rhsBits;
    }
    return false;
}

[[nodiscard]] b8 SameValue(const Vec2& lhs, const Vec2& rhs)noexcept{
    return SameEmittedF32(lhs.x, rhs.x) && SameEmittedF32(lhs.y, rhs.y);
}

[[nodiscard]] b8 SameValue(const Vec3& lhs, const Vec3& rhs)noexcept{
    return SameEmittedF32(lhs.x, rhs.x) && SameEmittedF32(lhs.y, rhs.y) && SameEmittedF32(lhs.z, rhs.z);
}

[[nodiscard]] b8 SameValue(const Vec4& lhs, const Vec4& rhs)noexcept{
    return SameEmittedF32(lhs.x, rhs.x) && SameEmittedF32(lhs.y, rhs.y) && SameEmittedF32(lhs.z, rhs.z) && SameEmittedF32(lhs.w, rhs.w);
}


[[nodiscard]] b8 SameValue(const MeshSkinInfluence& lhs, const MeshSkinInfluence& rhs)noexcept{
    MeshSkinInfluenceEqual equal;
    return equal(lhs, rhs);
}

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

    Sort(sortedEntries.begin(), sortedEntries.end(), [](const StreamSortEntry<Value>& lhs, const StreamSortEntry<Value>& rhs)noexcept(noexcept(LessValue(lhs.value, rhs.value))){
        if(LessValue(lhs.value, rhs.value))
            return true;
        if(LessValue(rhs.value, lhs.value))
            return false;
        return lhs.sourceIndex < rhs.sourceIndex;
    });

    usize uniqueCount = 0u;
    for(usize sortedIndex = 0u; sortedIndex < sortedEntries.size(); ++sortedIndex){
        if(sortedIndex == 0u || !SameValue(sortedEntries[sortedIndex].value, sortedEntries[sortedIndex - 1u].value))
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
        if(sortedIndex == 0u || !SameValue(sortedEntries[sortedIndex].value, sortedEntries[sortedIndex - 1u].value)){
            NWB_ASSERT(compact.size() < static_cast<usize>(Impl::s_MeshMissingStreamIndex));
            compactIndex = static_cast<u32>(compact.size());
            compact.push_back(sortedEntries[sortedIndex].value);
        }

        outRemap[sortedEntries[sortedIndex].sourceIndex] = compactIndex;
    }

    inOutStream = Move(compact);
    return outRemap;
}

[[nodiscard]] b8 RemapRequiredIndex(const Span<const u32> remap, const AStringView streamName, u32& inOutIndex){
    if(inOutIndex < remap.size()){
        inOutIndex = remap[inOutIndex];
        return true;
    }

    NWB_LOGGER_ERROR(NWB_TEXT("Failed to canonicalize mesh: vertex_ref {} index is out of range"), ConvertString(streamName));
    return false;
}

[[nodiscard]] b8 RemapOptionalIndex(const Span<const u32> remap, const AStringView streamName, u32& inOutIndex){
    if(inOutIndex == Impl::s_MeshMissingStreamIndex)
        return true;
    return RemapRequiredIndex(remap, streamName, inOutIndex);
}

[[nodiscard]] b8 RemapComponentRefs(const Span<const u32> positions, const Span<const u32> normals, const Span<const u32> tangents, const Span<const u32> uv0, const Span<const u32> colors, const Span<const u32> skin, SourceMeshStreams& inOutMesh){
    for(SourceVertexRef& ref : inOutMesh.vertexRefs){
        if(!RemapRequiredIndex(positions, Impl::s_VertexRefPositionName, ref.position))
            return false;
        if(!RemapRequiredIndex(normals, Impl::s_VertexRefNormalName, ref.normal))
            return false;
        if(!RemapRequiredIndex(tangents, Impl::s_VertexRefTangentName, ref.tangent))
            return false;
        if(!RemapRequiredIndex(uv0, Impl::s_MeshUv0FieldNameView, ref.uv0))
            return false;
        if(!RemapRequiredIndex(colors, Impl::s_VertexRefColorName, ref.color))
            return false;
        if(!RemapOptionalIndex(skin, s_SkinStreamLabel, ref.skin))
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


void AccumulateReport(const SourceMeshCanonicalizeReport& item, SourceMeshCanonicalizeReport& inOutTotal)noexcept{
    inOutTotal.before.positions += item.before.positions;
    inOutTotal.before.normals += item.before.normals;
    inOutTotal.before.tangents += item.before.tangents;
    inOutTotal.before.uv0 += item.before.uv0;
    inOutTotal.before.colors += item.before.colors;
    inOutTotal.before.skin += item.before.skin;
    inOutTotal.before.vertexRefs += item.before.vertexRefs;
    inOutTotal.before.indices += item.before.indices;

    inOutTotal.after.positions += item.after.positions;
    inOutTotal.after.normals += item.after.normals;
    inOutTotal.after.tangents += item.after.tangents;
    inOutTotal.after.uv0 += item.after.uv0;
    inOutTotal.after.colors += item.after.colors;
    inOutTotal.after.skin += item.after.skin;
    inOutTotal.after.vertexRefs += item.after.vertexRefs;
    inOutTotal.after.indices += item.after.indices;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SourceMeshStreamCounts CountSourceMeshStreams(const SourceMeshStreams& mesh)noexcept{
    SourceMeshStreamCounts counts;
    counts.positions = mesh.positions.size();
    counts.normals = mesh.normals.size();
    counts.tangents = mesh.tangents.size();
    counts.uv0 = mesh.uv0.size();
    counts.colors = mesh.colors.size();
    counts.skin = mesh.skin.size();
    counts.vertexRefs = mesh.vertexRefs.size();
    counts.indices = mesh.indices.size();
    return counts;
}

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

    Core::Metascript::MetaArena metaArena(UtilityDetail::s_UtilityArena);
    auto docResult = __hidden_mesh_refresh::ParseMetascriptDocument(inputPath, source, metaArena);
    if(!docResult)
        return MakeUnexpected(Failure{});
    const Core::Metascript::Document& doc = *docResult;

    MeshRefreshTextDetail::TextReplacements replacements(scratchArena);
    const auto replacementCapacity = TryMultiply<usize>(doc.declarations().size(), __hidden_mesh_refresh::s_MaxMeshReplacementCount);
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

        __hidden_mesh_refresh::AccumulateReport(itemReport, outReport);
    }

    if(!sawMesh){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to refresh NWB mesh: '{}' contains no mesh declarations"), PathToString<tchar>(inputPath));
        return MakeUnexpected(Failure{});
    }

    MeshRefreshTextDetail::ApplyTextReplacements(replacements, source);
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

